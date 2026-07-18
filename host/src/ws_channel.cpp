#include "ws_channel.h"
#include <poll.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>

namespace droppix {

WsChannel::WsChannel(int fd, SSL* ssl) : fd_(fd), ssl_(ssl) {}

ssize_t WsChannel::ssl_read_some(void* buf, size_t n) {
  if (!ssl_ || fd_ < 0) return -1;
  int r = SSL_read(ssl_, buf, static_cast<int>(n));
  return r;
}

bool WsChannel::ssl_write_all(const unsigned char* p, size_t n) {
  while (n) {
    int w = SSL_write(ssl_, p, static_cast<int>(n));
    if (w <= 0) return false;
    p += w;
    n -= static_cast<size_t>(w);
  }
  return true;
}

bool WsChannel::send_ws_binary(const unsigned char* payload, size_t n) {
  std::vector<unsigned char> hdr;
  hdr.push_back(0x82);
  if (n < 126) {
    hdr.push_back(static_cast<unsigned char>(n));
  } else if (n <= 0xffff) {
    hdr.push_back(126);
    hdr.push_back(static_cast<unsigned char>((n >> 8) & 0xff));
    hdr.push_back(static_cast<unsigned char>(n & 0xff));
  } else {
    hdr.push_back(127);
    for (int i = 7; i >= 0; --i)
      hdr.push_back(static_cast<unsigned char>((static_cast<uint64_t>(n) >> (8 * i)) & 0xff));
  }
  if (!ssl_write_all(hdr.data(), hdr.size())) return false;
  return n == 0 || ssl_write_all(payload, n);
}

bool WsChannel::send_all(const unsigned char* p, size_t n) {
  std::lock_guard<std::mutex> lock(mu_);
  if (closed_ || fd_ < 0) return false;
  out_acc_.insert(out_acc_.end(), p, p + n);
  while (out_acc_.size() >= 4) {
    uint32_t len = (uint32_t(out_acc_[0]) << 24) | (uint32_t(out_acc_[1]) << 16) |
                   (uint32_t(out_acc_[2]) << 8) | uint32_t(out_acc_[3]);
    if (len == 0 || len > 16u * 1024u * 1024u) {
      closed_ = true;
      return false;
    }
    if (out_acc_.size() < 4u + len) break;
    const unsigned char* payload = out_acc_.data() + 4;
    if (!send_ws_binary(payload, len)) {
      closed_ = true;
      return false;
    }
    out_acc_.erase(out_acc_.begin(), out_acc_.begin() + static_cast<std::ptrdiff_t>(4 + len));
  }
  return true;
}

bool WsChannel::read_ws_frame_into_inbuf() {
  unsigned char h[2];
  if (ssl_read_some(h, 2) != 2) return false;
  const int opcode = h[0] & 0x0f;
  const bool fin = (h[0] & 0x80) != 0;
  const bool masked = (h[1] & 0x80) != 0;
  uint64_t plen = h[1] & 0x7f;
  if (plen == 126) {
    unsigned char e[2];
    if (ssl_read_some(e, 2) != 2) return false;
    plen = (uint64_t(e[0]) << 8) | e[1];
  } else if (plen == 127) {
    unsigned char e[8];
    if (ssl_read_some(e, 8) != 8) return false;
    plen = 0;
    for (int i = 0; i < 8; ++i) plen = (plen << 8) | e[i];
  }
  if (plen > 16ull * 1024ull * 1024ull) return false;
  unsigned char mask[4] = {0, 0, 0, 0};
  if (masked) {
    if (ssl_read_some(mask, 4) != 4) return false;
  }
  std::vector<unsigned char> payload(static_cast<size_t>(plen));
  size_t got = 0;
  while (got < payload.size()) {
    ssize_t r = ssl_read_some(payload.data() + got, payload.size() - got);
    if (r <= 0) return false;
    got += static_cast<size_t>(r);
  }
  if (masked) {
    for (size_t i = 0; i < payload.size(); ++i) payload[i] ^= mask[i % 4];
  }
  if (opcode == 0x8) {
    closed_ = true;
    return false;
  }
  if (opcode == 0x9) {
    if (payload.size() < 126) {
      unsigned char pong_hdr[2] = {0x8a, static_cast<unsigned char>(payload.size())};
      ssl_write_all(pong_hdr, 2);
      if (!payload.empty()) ssl_write_all(payload.data(), payload.size());
    }
    return true;
  }
  if (opcode == 0xA) return true;
  if (opcode != 0x2 && opcode != 0x0) return true;
  if (!fin) {
    std::fprintf(stderr, "ws: fragmented binary not supported\n");
    return false;
  }
  if (payload.empty()) return true;
  uint32_t len = static_cast<uint32_t>(payload.size());
  inbuf_.push_back(static_cast<unsigned char>((len >> 24) & 0xff));
  inbuf_.push_back(static_cast<unsigned char>((len >> 16) & 0xff));
  inbuf_.push_back(static_cast<unsigned char>((len >> 8) & 0xff));
  inbuf_.push_back(static_cast<unsigned char>(len & 0xff));
  inbuf_.insert(inbuf_.end(), payload.begin(), payload.end());
  return true;
}

bool WsChannel::wait_readable(int timeout_ms) {
  std::lock_guard<std::mutex> lock(mu_);
  if (closed_ || fd_ < 0) return false;
  if (inpos_ < inbuf_.size()) return true;

  // Drain any already-decrypted control frames; stop when app data is buffered.
  while (ssl_ && SSL_pending(ssl_) > 0) {
    if (!read_ws_frame_into_inbuf()) return false;
    if (inpos_ < inbuf_.size() || !inbuf_.empty()) {
      inpos_ = 0;
      return !inbuf_.empty();
    }
  }
  if (!inbuf_.empty()) return true;

  pollfd pfd{fd_, POLLIN, 0};
  if (::poll(&pfd, 1, timeout_ms) <= 0 || !(pfd.revents & POLLIN)) return false;

  // Read frames until we get application data or the socket has no more pending bytes.
  for (;;) {
    if (!read_ws_frame_into_inbuf()) return false;
    if (!inbuf_.empty()) return true;
    if (!(ssl_ && SSL_pending(ssl_) > 0)) {
      // Control-only wake: not readable for the protocol layer.
      return false;
    }
  }
}

ssize_t WsChannel::recv(void* buf, size_t n) {
  std::lock_guard<std::mutex> lock(mu_);
  if (closed_ || fd_ < 0) return -1;
  if (inpos_ >= inbuf_.size()) {
    inbuf_.clear();
    inpos_ = 0;
    // Caller should have used wait_readable; pull at least one app frame.
    while (inbuf_.empty()) {
      if (!read_ws_frame_into_inbuf()) return -1;
      if (inbuf_.empty() && !(ssl_ && SSL_pending(ssl_) > 0)) {
        // No app data available without blocking — should be rare after wait_readable.
        return -1;
      }
    }
  }
  size_t avail = inbuf_.size() - inpos_;
  size_t take = avail < n ? avail : n;
  std::memcpy(buf, inbuf_.data() + inpos_, take);
  inpos_ += take;
  if (inpos_ >= inbuf_.size()) {
    inbuf_.clear();
    inpos_ = 0;
  }
  return static_cast<ssize_t>(take);
}

void WsChannel::close() {
  std::lock_guard<std::mutex> lock(mu_);
  closed_ = true;
  if (ssl_) {
    SSL_shutdown(ssl_);
    SSL_free(ssl_);
    ssl_ = nullptr;
  }
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

}  // namespace droppix
