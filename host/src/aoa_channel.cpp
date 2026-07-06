#include "aoa_channel.h"
#include <algorithm>
#include <cstring>

namespace droppix {

static constexpr int kChunk = 16384;
static constexpr int kSendTimeoutMs = 5000;

AoaChannel::AoaChannel(libusb_context* ctx, libusb_device_handle* handle,
                       unsigned char ep_in, unsigned char ep_out)
    : ctx_(ctx), handle_(handle), ep_in_(ep_in), ep_out_(ep_out) {}

AoaChannel::~AoaChannel() { close(); }

// Read one bulk-IN chunk into rxbuf_. Returns true iff bytes were buffered. A timeout with
// no data returns false (not an error). A hard USB error disconnects the channel (close()).
bool AoaChannel::fill(int timeout_ms) {
  if (!handle_) return false;
  if (rxpos_ >= rxbuf_.size()) { rxbuf_.clear(); rxpos_ = 0; }  // recycle a drained buffer
  unsigned char tmp[kChunk];
  int got = 0;
  int r = libusb_bulk_transfer(handle_, ep_in_, tmp, kChunk, &got,
                               timeout_ms < 0 ? 0 : timeout_ms);
  if (r == 0 || (r == LIBUSB_ERROR_TIMEOUT && got > 0)) {
    if (got > 0) { rxbuf_.insert(rxbuf_.end(), tmp, tmp + got); return true; }
    return false;  // completed with zero bytes -> treat as no data
  }
  if (r == LIBUSB_ERROR_TIMEOUT) return false;  // timed out, nothing read (channel still up)
  close();  // NO_DEVICE / pipe / other -> disconnected
  return false;
}

ssize_t AoaChannel::recv(void* buf, size_t n) {
  if (available() == 0) {
    if (!fill(1000)) return handle_ ? 0 : -1;  // 0 = no data yet; -1 = disconnected
  }
  size_t k = std::min(n, available());
  std::memcpy(buf, rxbuf_.data() + rxpos_, k);
  rxpos_ += k;
  return static_cast<ssize_t>(k);
}

bool AoaChannel::send_all(const unsigned char* p, size_t n) {
  if (!handle_) return false;
  while (n) {
    int sent = 0;
    int chunk = static_cast<int>(std::min(n, static_cast<size_t>(kChunk)));
    int r = libusb_bulk_transfer(handle_, ep_out_, const_cast<unsigned char*>(p), chunk,
                                 &sent, kSendTimeoutMs);
    if (r != 0 && r != LIBUSB_ERROR_TIMEOUT) { close(); return false; }
    if (sent <= 0) return false;
    p += sent;
    n -= static_cast<size_t>(sent);
  }
  return true;
}

bool AoaChannel::wait_readable(int timeout_ms) {
  if (available() > 0) return true;   // already buffered
  return fill(timeout_ms);
}

void AoaChannel::close() {
  if (handle_) {
    libusb_release_interface(handle_, 0);
    libusb_close(handle_);
    handle_ = nullptr;
  }
  if (ctx_) { libusb_exit(ctx_); ctx_ = nullptr; }
}

}  // namespace droppix
