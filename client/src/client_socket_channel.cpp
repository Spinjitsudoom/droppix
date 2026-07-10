#include "client_socket_channel.h"
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <cstring>

// POSIX sockets only (Linux/macOS). A Windows port needs Winsock2 (WSAStartup,
// SOCKET/closesocket, non-blocking connect via ioctlsocket) in place of this file —
// the ByteChannel interface and TransportClient above it are unaffected.

namespace droppix {
namespace {

int connect_tcp(const std::string& host, uint16_t port, int timeout_ms) {
  addrinfo hints{}; hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
  addrinfo* res = nullptr;
  if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0) return -1;
  int fd = -1;
  for (addrinfo* p = res; p; p = p->ai_next) {
    fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (fd < 0) continue;
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    int r = ::connect(fd, p->ai_addr, p->ai_addrlen);
    if (r == 0) break;   // connected immediately (e.g. localhost)
    if (errno != EINPROGRESS) { ::close(fd); fd = -1; continue; }
    pollfd pfd{fd, POLLOUT, 0};
    if (::poll(&pfd, 1, timeout_ms) <= 0) { ::close(fd); fd = -1; continue; }
    int soerr = 0; socklen_t len = sizeof(soerr);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &len) != 0 || soerr != 0) {
      ::close(fd); fd = -1; continue;
    }
    break;  // connected within the timeout
  }
  freeaddrinfo(res);
  if (fd >= 0) fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) & ~O_NONBLOCK);  // back to blocking
  return fd;
}

}  // namespace

std::unique_ptr<ClientSocketChannel> ClientSocketChannel::connect(
    const std::string& host, uint16_t port, bool use_tls, int timeout_ms) {
  int fd = connect_tcp(host, port, timeout_ms);
  if (fd < 0) return nullptr;

  if (!use_tls) return std::make_unique<ClientSocketChannel>(fd, nullptr, nullptr);

  SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
  // No CA store, no verify callback: the handshake succeeds against ANY certificate.
  // Trust is decided by the CALLER afterward via peer_certificate()'s fingerprint —
  // same TOFU model as the Android app's TlsTrust (a never-throwing TrustManager).
  SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
  SSL* ssl = SSL_new(ctx);
  SSL_set_fd(ssl, fd);
  SSL_set_tlsext_host_name(ssl, host.c_str());  // SNI; harmless even though unverified
  if (SSL_connect(ssl) != 1) {
    SSL_free(ssl); SSL_CTX_free(ctx); ::close(fd);
    return nullptr;
  }
  auto ch = std::make_unique<ClientSocketChannel>(fd, ctx, ssl);
  ch->peer_cert_ = SSL_get_peer_certificate(ssl);  // already up-ref'd; we own this reference
  return ch;
}

ssize_t ClientSocketChannel::recv(void* buf, size_t n) {
  return ssl_ ? static_cast<ssize_t>(SSL_read(ssl_, buf, static_cast<int>(n)))
              : ::recv(fd_, buf, n, 0);
}

bool ClientSocketChannel::send_all(const unsigned char* p, size_t n) {
  while (n) {
    ssize_t w = ssl_ ? static_cast<ssize_t>(SSL_write(ssl_, p, static_cast<int>(n)))
                     : ::send(fd_, p, n, MSG_NOSIGNAL);
    if (w <= 0) return false;
    p += w;
    n -= static_cast<size_t>(w);
  }
  return true;
}

bool ClientSocketChannel::wait_readable(int timeout_ms) {
  if (fd_ < 0) return false;
  if (ssl_ && SSL_pending(ssl_) > 0) return true;  // TLS already holds decrypted bytes
  pollfd pfd{fd_, POLLIN, 0};
  return ::poll(&pfd, 1, timeout_ms) > 0 && (pfd.revents & POLLIN);
}

void ClientSocketChannel::close() {
  if (peer_cert_) { X509_free(peer_cert_); peer_cert_ = nullptr; }
  if (ssl_) { SSL_shutdown(ssl_); SSL_free(ssl_); ssl_ = nullptr; }
  if (ctx_) { SSL_CTX_free(ctx_); ctx_ = nullptr; }
  if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
}

}  // namespace droppix
