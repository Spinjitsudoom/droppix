#include "transport_server.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>

namespace droppix {

bool TransportServer::listen(uint16_t port) {
  if (listen_fd_ >= 0) { ::close(listen_fd_); listen_fd_ = -1; }  // re-listen safe
  listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd_ < 0) return false;
  int yes = 1;
  setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);
  if (::bind(listen_fd_, (sockaddr*)&addr, sizeof(addr)) != 0) {
    ::close(listen_fd_); listen_fd_ = -1; return false;
  }
  if (::listen(listen_fd_, 1) != 0) {
    ::close(listen_fd_); listen_fd_ = -1; return false;
  }
  socklen_t len = sizeof(addr);
  if (getsockname(listen_fd_, (sockaddr*)&addr, &len) == 0) {
    port_ = ntohs(addr.sin_port);
  }
  return true;
}

bool TransportServer::wait_readable(int fd, int timeout_ms) {
  if (fd < 0) return false;
  pollfd pfd{fd, POLLIN, 0};
  return ::poll(&pfd, 1, timeout_ms) > 0 && (pfd.revents & POLLIN);
}

bool TransportServer::accept_client(int timeout_ms) {
  close_all();  // drop any prior client so its fd can't leak on a new accept
  if (!wait_readable(listen_fd_, timeout_ms)) return false;
  client_fd_ = ::accept(listen_fd_, nullptr, nullptr);
  if (client_fd_ < 0) return false;
  int yes = 1;
  setsockopt(client_fd_, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
  return true;
}

bool TransportServer::read_hello(uint32_t& version, uint32_t& w, uint32_t& h, uint32_t& density,
                                 int timeout_ms) {
  unsigned char buf[1024];
  ParsedMessage m;
  for (;;) {
    if (parser_.next(m)) {
      if (m.type != MsgType::Hello) continue;
      return decode_hello(m.body, version, w, h, density);
    }
    if (!wait_readable(client_fd_, timeout_ms)) return false;
    ssize_t n = ::recv(client_fd_, buf, sizeof(buf), 0);
    if (n <= 0) { close_all(); return false; }
    parser_.feed(buf, static_cast<size_t>(n));
  }
}

bool TransportServer::send_all(const std::vector<unsigned char>& bytes) {
  if (client_fd_ < 0) return false;
  size_t off = 0;
  while (off < bytes.size()) {
    ssize_t n = ::send(client_fd_, bytes.data() + off, bytes.size() - off,
                       MSG_NOSIGNAL);
    if (n <= 0) { close_all(); return false; }
    off += static_cast<size_t>(n);
  }
  return true;
}

bool TransportServer::send_config(uint32_t w, uint32_t h, uint32_t fps,
                                  const std::vector<unsigned char>& ed) {
  return send_all(encode_message(MsgType::Config, encode_config(w, h, fps, ed)));
}

bool TransportServer::send_video(uint64_t pts_us, bool key,
                                 const std::vector<unsigned char>& nal) {
  return send_all(encode_message(MsgType::Video, encode_video(pts_us, key, nal)));
}

void TransportServer::poll_control() {
  if (client_fd_ < 0) return;
  if (!wait_readable(client_fd_, 0)) return;
  unsigned char buf[1024];
  ssize_t n = ::recv(client_fd_, buf, sizeof(buf), 0);
  if (n <= 0) { close_all(); return; }
  parser_.feed(buf, static_cast<size_t>(n));
  ParsedMessage m;
  while (parser_.next(m)) {
    if (m.type == MsgType::Ping) {
      send_all(encode_message(MsgType::Pong, m.body));
    } else if (m.type == MsgType::Input && input_handler_) {
      uint8_t a; uint16_t x, y;
      if (decode_input(m.body, a, x, y)) input_handler_(a, x, y);
    }
  }
}

void TransportServer::close_all() {
  if (client_fd_ >= 0) { ::close(client_fd_); client_fd_ = -1; }
}

TransportServer::~TransportServer() {
  close_all();
  if (listen_fd_ >= 0) ::close(listen_fd_);
}

}  // namespace droppix
