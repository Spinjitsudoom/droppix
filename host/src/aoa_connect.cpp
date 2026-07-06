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

}  // namespace

std::unique_ptr<AoaChannel> aoa_connect(const std::string& serial) {
  libusb_context* ctx = nullptr;
  if (libusb_init(&ctx)) return nullptr;

  // 0) If a previous attempt left a device stuck in accessory mode, reset it back to normal
  // so we can run the handshake fresh (the non-accessory search below then finds it).
  {
    libusb_device_handle* acc = libusb_open_device_with_vid_pid(ctx, GOOGLE_VID, ACC_PID1);
    if (!acc) acc = libusb_open_device_with_vid_pid(ctx, GOOGLE_VID, ACC_PID0);
    if (acc) {
      libusb_reset_device(acc);
      libusb_close(acc);
      timespec ts{1, 500 * 1000 * 1000};   // give it time to re-enumerate to the default config
      nanosleep(&ts, nullptr);
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

  // 3) Wait for the device to re-enumerate in accessory mode (VID 0x18d1, PID 0x2d0x).
  libusb_device_handle* a = nullptr;
  for (int tries = 0; tries < 50 && !a; ++tries) {
    timespec ts{0, 100 * 1000 * 1000};
    nanosleep(&ts, nullptr);
    a = libusb_open_device_with_vid_pid(ctx, GOOGLE_VID, ACC_PID0);
    if (!a) a = libusb_open_device_with_vid_pid(ctx, GOOGLE_VID, ACC_PID1);
  }
  if (!a) { libusb_exit(ctx); return nullptr; }
  if (libusb_kernel_driver_active(a, 0) == 1) libusb_detach_kernel_driver(a, 0);
  if (libusb_claim_interface(a, 0)) { libusb_close(a); libusb_exit(ctx); return nullptr; }

  // 4) Locate the bulk IN/OUT endpoints on interface 0.
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
    libusb_exit(ctx);
    return nullptr;
  }

  std::fprintf(stderr, "aoa: connected (proto %d, IN=0x%02x OUT=0x%02x)\n", proto, ep_in, ep_out);
  return std::make_unique<AoaChannel>(ctx, a, ep_in, ep_out);  // AoaChannel now owns ctx + a
}

}  // namespace droppix
