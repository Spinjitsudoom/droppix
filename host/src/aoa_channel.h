#pragma once
#include <libusb-1.0/libusb.h>
#include <cstddef>
#include <vector>
#include "byte_channel.h"

namespace droppix {

// ByteChannel over an Android Open Accessory (AOA) USB link: bulk IN/OUT endpoints on a
// claimed libusb interface. Owns the libusb handle + context; close() releases the
// interface, closes the handle, and exits the context.
//
// USB bulk endpoints have no pollable fd, so recv()/wait_readable() keep an internal read
// buffer refilled by timed bulk-IN transfers — mapping TransportServer's poll+recv pattern
// (wait_readable(timeout) then recv()) onto USB.
class AoaChannel : public ByteChannel {
 public:
  AoaChannel(libusb_context* ctx, libusb_device_handle* handle,
             unsigned char ep_in, unsigned char ep_out);
  ~AoaChannel() override;

  ssize_t recv(void* buf, size_t n) override;
  bool send_all(const unsigned char* p, size_t n) override;
  bool wait_readable(int timeout_ms) override;
  bool connected() const override { return handle_ != nullptr; }
  void close() override;

 private:
  size_t available() const { return rxbuf_.size() - rxpos_; }
  bool fill(int timeout_ms);   // buffer one bulk-IN chunk; false on timeout/error (error closes)

  libusb_context* ctx_ = nullptr;
  libusb_device_handle* handle_ = nullptr;
  unsigned char ep_in_ = 0, ep_out_ = 0;
  std::vector<unsigned char> rxbuf_;
  size_t rxpos_ = 0;
};

}  // namespace droppix
