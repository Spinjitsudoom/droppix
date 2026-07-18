#pragma once
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>
#include <openssl/ssl.h>
#include "byte_channel.h"

namespace droppix {

// ByteChannel over a TLS WebSocket. TransportServer speaks length-prefixed bytes;
// this adapter maps each complete encode_message blob to one WS binary frame
// ([type][body]) and reconstructs length prefixes on recv.
class WsChannel : public ByteChannel {
 public:
  // Takes ownership of fd and ssl (already past HTTP Upgrade).
  WsChannel(int fd, SSL* ssl);
  ~WsChannel() override { close(); }

  ssize_t recv(void* buf, size_t n) override;
  bool send_all(const unsigned char* p, size_t n) override;
  bool wait_readable(int timeout_ms) override;
  bool connected() const override { return fd_ >= 0 && !closed_; }
  void close() override;

 private:
  bool read_ws_frame_into_inbuf();
  bool send_ws_binary(const unsigned char* payload, size_t n);
  ssize_t ssl_read_some(void* buf, size_t n);
  bool ssl_write_all(const unsigned char* p, size_t n);

  int fd_ = -1;
  SSL* ssl_ = nullptr;
  bool closed_ = false;

  // Outbound: accumulate until a full length-prefixed message is present.
  std::vector<unsigned char> out_acc_;

  // Inbound: length-prefixed bytes ready for TransportServer.
  std::vector<unsigned char> inbuf_;
  size_t inpos_ = 0;

  std::mutex mu_;
};

}  // namespace droppix
