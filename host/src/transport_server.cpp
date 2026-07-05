#include "transport_server.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <openssl/err.h>
#include "socket_channel.h"

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

void TransportServer::adopt_channel(std::unique_ptr<ByteChannel> ch, std::string peer) {
  channel_ = std::move(ch);
  peer_ip_ = std::move(peer);
}

bool TransportServer::accept_client(int timeout_ms) {
  close_all();  // drop any prior client so its fd can't leak on a new accept
  if (listen_fd_ < 0) return false;
  pollfd pfd{listen_fd_, POLLIN, 0};
  if (::poll(&pfd, 1, timeout_ms) <= 0 || !(pfd.revents & POLLIN)) return false;

  sockaddr_in cli{};
  socklen_t cli_len = sizeof(cli);
  int fd = ::accept(listen_fd_, (sockaddr*)&cli, &cli_len);
  if (fd < 0) return false;
  char buf[INET_ADDRSTRLEN] = {0};
  std::string peer = inet_ntop(AF_INET, &cli.sin_addr, buf, sizeof(buf)) ? buf : "";
  int yes = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));

  SSL* ssl = nullptr;
  if (tls_) {
    ssl = SSL_new(ctx_);
    if (!ssl) {
      std::fprintf(stderr, "tls: SSL_new failed\n"); ERR_print_errors_fp(stderr);
      ::close(fd); return false;
    }
    SSL_set_fd(ssl, fd);
    if (SSL_accept(ssl) <= 0) {
      std::fprintf(stderr, "tls: SSL_accept failed\n"); ERR_print_errors_fp(stderr);
      SSL_free(ssl); ::close(fd); return false;
    }
  }
  adopt_channel(std::make_unique<SocketChannel>(fd, ssl), std::move(peer));
  return true;
}

bool TransportServer::read_hello(uint32_t& version, uint32_t& w, uint32_t& h, uint32_t& density,
                                 std::string& name, std::string& id, int timeout_ms) {
  if (!channel_) return false;
  unsigned char buf[1024];
  ParsedMessage m;
  for (;;) {
    if (parser_.next(m)) {
      if (m.type != MsgType::Hello) continue;
      return decode_hello(m.body, version, w, h, density, name, id);
    }
    if (!channel_->wait_readable(timeout_ms)) return false;
    ssize_t n = channel_->recv(buf, sizeof(buf));
    if (n <= 0) { close_all(); return false; }
    parser_.feed(buf, static_cast<size_t>(n));
  }
}

bool TransportServer::send_all(const std::vector<unsigned char>& bytes) {
  if (!channel_ || !channel_->connected()) return false;
  if (!channel_->send_all(bytes.data(), bytes.size())) { close_all(); return false; }
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

bool TransportServer::send_overlay(uint8_t show) {
  return send_all(encode_message(MsgType::Overlay, encode_overlay(show)));
}

void TransportServer::poll_control() {
  if (!channel_ || !channel_->wait_readable(0)) return;
  unsigned char buf[1024];
  ssize_t n = channel_->recv(buf, sizeof(buf));
  if (n <= 0) { close_all(); return; }
  parser_.feed(buf, static_cast<size_t>(n));
  ParsedMessage m;
  while (parser_.next(m)) {
    if (m.type == MsgType::Ping) {
      send_all(encode_message(MsgType::Pong, m.body));
    } else if (m.type == MsgType::Touch && touch_handler_) {
      std::vector<TouchContact> contacts;
      if (decode_touch(m.body, contacts)) touch_handler_(contacts);
    } else if (m.type == MsgType::Input && touch_handler_) {
      // Legacy single-pointer client: translate to a full-set touch (up => no contacts).
      uint8_t a; uint16_t x, y, p;
      if (decode_input(m.body, a, x, y, p)) {
        if (a == 2) touch_handler_({});
        else touch_handler_({TouchContact{0, x, y, p}});
      }
    } else if (m.type == MsgType::Orientation && orientation_handler_) {
      uint8_t code;
      if (decode_orientation(m.body, code)) orientation_handler_(code);
    }
  }
}

void TransportServer::close_all() {
  if (channel_) { channel_->close(); channel_.reset(); }
}

TransportServer::~TransportServer() {
  close_all();
  if (listen_fd_ >= 0) ::close(listen_fd_);
  if (ctx_) { SSL_CTX_free(ctx_); ctx_ = nullptr; }
}

}  // namespace droppix
