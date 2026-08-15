#pragma once
#include <openssl/ssl.h>
#include "byte_channel.h"
#include "socket_pending.h"

namespace droppix {

// ByteChannel over an accepted TCP socket, optionally wrapped in TLS. Owns the fd and
// (when TLS) the SSL*; close() tears both down. Byte-for-byte the behavior TransportServer
// had inline before the ByteChannel refactor.
class SocketChannel : public ByteChannel {
 public:
  SocketChannel(int fd, SSL* ssl) : fd_(fd), ssl_(ssl) {}
  ~SocketChannel() override { close(); }
  ssize_t recv(void* buf, size_t n) override;
  bool send_all(const unsigned char* p, size_t n) override;
  bool wait_readable(int timeout_ms) override;
  bool connected() const override { return fd_ >= 0; }
  size_t pending_bytes() const override { return socket_pending_bytes(fd_); }
  void close() override;

 private:
  int fd_ = -1;
  SSL* ssl_ = nullptr;  // null => plaintext
};

}  // namespace droppix
