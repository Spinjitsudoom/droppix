#pragma once
#include <openssl/ssl.h>
#include <memory>
#include <string>
#include <vector>
#include "byte_channel.h"

namespace droppix {

// Client-side counterpart to host/src/socket_channel.h: connects OUT to a host:port
// instead of accepting. Same ByteChannel behavior (TLS optional, blocking recv/send,
// wait_readable via poll() + SSL_pending()) so TransportClient's protocol loop is
// transport-agnostic exactly like the host's TransportServer is.
class ClientSocketChannel : public ByteChannel {
 public:
  ClientSocketChannel(int fd, SSL_CTX* ctx, SSL* ssl) : fd_(fd), ctx_(ctx), ssl_(ssl) {}
  ~ClientSocketChannel() override { close(); }
  ssize_t recv(void* buf, size_t n) override;
  bool send_all(const unsigned char* p, size_t n) override;
  bool wait_readable(int timeout_ms) override;
  bool connected() const override { return fd_ >= 0; }
  void close() override;

  // The certificate the server presented, valid only right after a TLS connect()
  // succeeds (nullptr for a plaintext channel, e.g. localhost's TLS-optional path if
  // ever used that way, or AOA — not applicable to this client, but kept symmetric).
  X509* peer_certificate() const { return peer_cert_; }

  // Connect to host:port. When use_tls is true: no certificate verification is
  // performed here (SSL_CTX has no CA store, no verify callback) — the handshake
  // "succeeds" against any certificate, exactly like Android's TlsTrust; the CALLER
  // decides trust afterward by comparing peer_certificate()'s fingerprint against a
  // pin store. timeout_ms bounds the TCP connect only. Returns nullptr on any failure.
  static std::unique_ptr<ClientSocketChannel> connect(const std::string& host, uint16_t port,
                                                       bool use_tls, int timeout_ms);

 private:
  int fd_ = -1;
  SSL_CTX* ctx_ = nullptr;   // owned; null for plaintext
  SSL* ssl_ = nullptr;       // owned; null for plaintext
  X509* peer_cert_ = nullptr;  // owned (X509_dup'd from SSL_get_peer_certificate)
};

}  // namespace droppix
