#include "transport_server.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <openssl/err.h>

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

void TransportServer::enable_tls(const std::string& cert, const std::string& key) {
  cert_ = cert; key_ = key; tls_ = true;
  ctx_ = SSL_CTX_new(TLS_server_method());
  if (!ctx_) {
    std::fprintf(stderr, "tls: SSL_CTX_new failed\n");
    ERR_print_errors_fp(stderr);
    return;
  }
  if (SSL_CTX_use_certificate_file(ctx_, cert_.c_str(), SSL_FILETYPE_PEM) <= 0) {
    std::fprintf(stderr, "tls: failed to load cert %s\n", cert_.c_str());
    ERR_print_errors_fp(stderr);
  }
  if (SSL_CTX_use_PrivateKey_file(ctx_, key_.c_str(), SSL_FILETYPE_PEM) <= 0) {
    std::fprintf(stderr, "tls: failed to load key %s\n", key_.c_str());
    ERR_print_errors_fp(stderr);
  }
}

ssize_t TransportServer::conn_recv(void* buf, size_t n) {
  return tls_ ? static_cast<ssize_t>(SSL_read(ssl_, buf, static_cast<int>(n)))
              : ::recv(client_fd_, buf, n, 0);
}

bool TransportServer::conn_send_all(const unsigned char* p, size_t n) {
  while (n) {
    ssize_t w = tls_ ? static_cast<ssize_t>(SSL_write(ssl_, p, static_cast<int>(n)))
                      : ::send(client_fd_, p, n, MSG_NOSIGNAL);
    if (w <= 0) return false;
    p += w;
    n -= static_cast<size_t>(w);
  }
  return true;
}

bool TransportServer::accept_client(int timeout_ms) {
  close_all();  // drop any prior client so its fd can't leak on a new accept
  if (!wait_readable(listen_fd_, timeout_ms)) return false;
  sockaddr_in cli{};
  socklen_t cli_len = sizeof(cli);
  client_fd_ = ::accept(listen_fd_, (sockaddr*)&cli, &cli_len);
  if (client_fd_ < 0) return false;
  char buf[INET_ADDRSTRLEN] = {0};
  if (inet_ntop(AF_INET, &cli.sin_addr, buf, sizeof(buf))) {
    peer_ip_ = buf;
  } else {
    peer_ip_.clear();
  }
  int yes = 1;
  setsockopt(client_fd_, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));

  if (tls_) {
    ssl_ = SSL_new(ctx_);
    if (!ssl_) {
      std::fprintf(stderr, "tls: SSL_new failed\n");
      ERR_print_errors_fp(stderr);
      ::close(client_fd_); client_fd_ = -1;
      return false;
    }
    SSL_set_fd(ssl_, client_fd_);
    if (SSL_accept(ssl_) <= 0) {
      std::fprintf(stderr, "tls: SSL_accept failed\n");
      ERR_print_errors_fp(stderr);
      SSL_free(ssl_); ssl_ = nullptr;
      ::close(client_fd_); client_fd_ = -1;
      return false;
    }
  }
  return true;
}

bool TransportServer::read_hello(uint32_t& version, uint32_t& w, uint32_t& h, uint32_t& density,
                                 std::string& name, std::string& id, int timeout_ms) {
  unsigned char buf[1024];
  ParsedMessage m;
  for (;;) {
    if (parser_.next(m)) {
      if (m.type != MsgType::Hello) continue;
      return decode_hello(m.body, version, w, h, density, name, id);
    }
    if (!wait_readable(client_fd_, timeout_ms)) return false;
    ssize_t n = conn_recv(buf, sizeof(buf));
    if (n <= 0) { close_all(); return false; }
    parser_.feed(buf, static_cast<size_t>(n));
  }
}

bool TransportServer::send_all(const std::vector<unsigned char>& bytes) {
  if (client_fd_ < 0) return false;
  if (!conn_send_all(bytes.data(), bytes.size())) { close_all(); return false; }
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

bool TransportServer::send_audio(const std::vector<unsigned char>& pcm) {
  return send_all(encode_message(MsgType::Audio, pcm));
}

void TransportServer::poll_control() {
  if (client_fd_ < 0) return;
  bool tls_buffered = tls_ && ssl_ && SSL_pending(ssl_) > 0;
  if (!tls_buffered && !wait_readable(client_fd_, 0)) return;
  unsigned char buf[1024];
  ssize_t n = conn_recv(buf, sizeof(buf));
  if (n <= 0) { close_all(); return; }
  parser_.feed(buf, static_cast<size_t>(n));
  ParsedMessage m;
  while (parser_.next(m)) {
    if (m.type == MsgType::Ping) {
      send_all(encode_message(MsgType::Pong, m.body));
    } else if (m.type == MsgType::Input && input_handler_) {
      uint8_t a; uint16_t x, y;
      if (decode_input(m.body, a, x, y)) input_handler_(a, x, y);
    } else if (m.type == MsgType::Orientation && orientation_handler_) {
      uint8_t code;
      if (decode_orientation(m.body, code)) orientation_handler_(code);
    }
  }
}

void TransportServer::close_all() {
  if (ssl_) { SSL_shutdown(ssl_); SSL_free(ssl_); ssl_ = nullptr; }
  if (client_fd_ >= 0) { ::close(client_fd_); client_fd_ = -1; }
}

TransportServer::~TransportServer() {
  close_all();
  if (listen_fd_ >= 0) ::close(listen_fd_);
  if (ctx_) { SSL_CTX_free(ctx_); ctx_ = nullptr; }
}

}  // namespace droppix
