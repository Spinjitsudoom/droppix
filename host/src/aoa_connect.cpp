#include "aoa_connect.h"
#include <libusb-1.0/libusb.h>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace droppix {
namespace {

constexpr int AOA_GET_PROTOCOL = 51, AOA_SEND_STRING = 52, AOA_START = 53;
constexpr uint16_t GOOGLE_VID = 0x18d1, ACC_PID0 = 0x2d00, ACC_PID1 = 0x2d01;

void send_string(libusb_device_handle* h, int idx, const char* s) {
  libusb_control_transfer(h, 0x40, AOA_SEND_STRING, 0, idx,
                          (unsigned char*)s, (uint16_t)(std::strlen(s) + 1), 1000);
}

// Match the device's USB serial (iSerialNumber). Empty `want` matches only Google VID.
bool device_matches(libusb_device_handle* h, const libusb_device_descriptor& d,
                    const std::string& want) {
  if (want.empty()) return d.idVendor == GOOGLE_VID;
  if (!d.iSerialNumber) return false;
  unsigned char s[256] = {0};
  int n = libusb_get_string_descriptor_ascii(h, d.iSerialNumber, s, sizeof(s));
  return n > 0 && want == std::string(reinterpret_cast<char*>(s), static_cast<size_t>(n));
}

// Claim interface 0 of an accessory-mode device and locate its bulk endpoints. On
// success the returned AoaChannel owns ctx + a; on failure `a` is closed and nullptr
// returned (ctx stays the caller's).
std::unique_ptr<AoaChannel> claim_accessory(libusb_context* ctx, libusb_device_handle* a,
                                            int proto) {
  if (libusb_kernel_driver_active(a, 0) == 1) libusb_detach_kernel_driver(a, 0);
  if (libusb_claim_interface(a, 0)) { libusb_close(a); return nullptr; }

  libusb_config_descriptor* cfg = nullptr;
  libusb_get_active_config_descriptor(libusb_get_device(a), &cfg);
  unsigned char ep_in = 0, ep_out = 0;
  if (cfg) {
    const libusb_interface_descriptor* id = &cfg->interface[0].altsetting[0];
    for (int e = 0; e < id->bNumEndpoints; ++e) {
      const libusb_endpoint_descriptor* ep = &id->endpoint[e];
      if ((ep->bmAttributes & 0x3) == LIBUSB_TRANSFER_TYPE_BULK) {
        if (ep->bEndpointAddress & 0x80) ep_in = ep->bEndpointAddress;
        else ep_out = ep->bEndpointAddress;
      }
    }
    libusb_free_config_descriptor(cfg);
  }
  if (!ep_in || !ep_out) {
    libusb_release_interface(a, 0);
    libusb_close(a);
    return nullptr;
  }
  std::fprintf(stderr, "aoa: connected (proto %d, IN=0x%02x OUT=0x%02x)\n", proto, ep_in, ep_out);
  return std::make_unique<AoaChannel>(ctx, a, ep_in, ep_out);  // AoaChannel owns ctx + a
}

libusb_device_handle* open_accessory(libusb_context* ctx) {
  libusb_device_handle* a = libusb_open_device_with_vid_pid(ctx, GOOGLE_VID, ACC_PID1);
  if (!a) a = libusb_open_device_with_vid_pid(ctx, GOOGLE_VID, ACC_PID0);
  return a;
}

}  // namespace

std::unique_ptr<AoaChannel> aoa_connect(const std::string& serial) {
  libusb_context* ctx = nullptr;
  if (libusb_init(&ctx)) return nullptr;

  // 0) A device already in accessory mode (previous session ended, cable stayed in) is
  // ADOPTED, not reset. Resetting on every reconnect forces a full re-enumeration each
  // time, which relaunches the tablet app and — on older hardware — can drop the device
  // off the bus entirely ("Device not responding to setup address", error -71). Only if
  // the claim fails (a genuinely wedged session) do we reset and redo the handshake.
  if (libusb_device_handle* acc = open_accessory(ctx)) {
    libusb_device_descriptor d{};
    libusb_get_device_descriptor(libusb_get_device(acc), &d);
    if (device_matches(acc, d, serial)) {
      std::fprintf(stderr, "aoa: adopting device already in accessory mode\n");
      if (auto ch = claim_accessory(ctx, acc, 0)) return ch;   // closes acc on failure
      // OUR device, but wedged (claim/endpoints failed): reset it back to normal mode
      // so the handshake below can run fresh.
      if (libusb_device_handle* stuck = open_accessory(ctx)) {
        std::fprintf(stderr, "aoa: accessory claim failed; resetting for a fresh handshake\n");
        libusb_reset_device(stuck);
        libusb_close(stuck);
        timespec ts{1, 500 * 1000 * 1000};  // give it time to re-enumerate to the default config
        nanosleep(&ts, nullptr);
      }
    } else {
      libusb_close(acc);   // another session's accessory (multi-monitor): leave it alone
    }
  }

  // 1) Find the target Android (still in non-accessory mode) and open it.
  libusb_device_handle* h = nullptr;
  libusb_device** list;
  ssize_t cnt = libusb_get_device_list(ctx, &list);
  for (ssize_t i = 0; i < cnt && !h; ++i) {
    libusb_device_descriptor d;
    if (libusb_get_device_descriptor(list[i], &d)) continue;
    if (d.idProduct == ACC_PID0 || d.idProduct == ACC_PID1) continue;  // already accessory
    libusb_device_handle* hh = nullptr;
    if (libusb_open(list[i], &hh) != 0) continue;
    if (device_matches(hh, d, serial)) h = hh; else libusb_close(hh);
  }
  libusb_free_device_list(list, 1);
  if (!h) { libusb_exit(ctx); return nullptr; }

  // 2) Query AOA support, send our identification strings, start accessory mode.
  unsigned char buf[2] = {0, 0};
  int r = libusb_control_transfer(h, 0xC0, AOA_GET_PROTOCOL, 0, 0, buf, 2, 1000);
  int proto = (r >= 2) ? (buf[0] | (buf[1] << 8)) : 0;
  if (proto < 1) { libusb_close(h); libusb_exit(ctx); return nullptr; }
  send_string(h, 0, "droppix");         // manufacturer  (must match accessory_filter.xml)
  send_string(h, 1, "droppix");         // model
  send_string(h, 2, "droppix USB");     // description
  send_string(h, 3, "1.0");             // version
  send_string(h, 4, "https://droppix"); // uri
  send_string(h, 5, "0000");            // serial
  libusb_control_transfer(h, 0x40, AOA_START, 0, 0, nullptr, 0, 1000);
  libusb_close(h);

  // 3) Wait for the device to re-enumerate in accessory mode (VID 0x18d1, PID 0x2d0x),
  // then claim its interface + bulk endpoints.
  libusb_device_handle* a = nullptr;
  for (int tries = 0; tries < 50 && !a; ++tries) {
    timespec ts{0, 100 * 1000 * 1000};
    nanosleep(&ts, nullptr);
    a = open_accessory(ctx);
  }
  if (!a) { libusb_exit(ctx); return nullptr; }
  auto ch = claim_accessory(ctx, a, proto);
  if (!ch) libusb_exit(ctx);
  return ch;
}

}  // namespace droppix
