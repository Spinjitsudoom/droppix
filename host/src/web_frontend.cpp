#include "web_frontend.h"
#include "socket_channel.h"
#include "ws_channel.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/sha.h>
#include <openssl/x509.h>

namespace droppix {
namespace {

constexpr const char* kWsGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

std::string b64(const unsigned char* data, size_t n) {
  BIO* b64 = BIO_new(BIO_f_base64());
  BIO* mem = BIO_new(BIO_s_mem());
  BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
  BIO_push(b64, mem);
  BIO_write(b64, data, static_cast<int>(n));
  BIO_flush(b64);
  BUF_MEM* ptr = nullptr;
  BIO_get_mem_ptr(mem, &ptr);
  std::string out(ptr ? ptr->data : "", ptr ? ptr->length : 0);
  BIO_free_all(b64);
  return out;
}

std::string ws_accept_key(const std::string& client_key) {
  std::string cat = client_key + kWsGuid;
  unsigned char dig[SHA_DIGEST_LENGTH];
  SHA1(reinterpret_cast<const unsigned char*>(cat.data()), cat.size(), dig);
  return b64(dig, SHA_DIGEST_LENGTH);
}

bool ssl_write_str(SSL* ssl, const std::string& s) {
  size_t off = 0;
  while (off < s.size()) {
    int w = SSL_write(ssl, s.data() + off, static_cast<int>(s.size() - off));
    if (w <= 0) return false;
    off += static_cast<size_t>(w);
  }
  return true;
}

bool ssl_read_http_headers(SSL* ssl, std::string& out, int timeout_ms) {
  out.clear();
  char buf[1];
  const int fd = SSL_get_fd(ssl);
  while (out.size() < 65536) {
    if (SSL_pending(ssl) <= 0) {
      pollfd pfd{fd, POLLIN, 0};
      if (::poll(&pfd, 1, timeout_ms) <= 0) return false;
    }
    int r = SSL_read(ssl, buf, 1);
    if (r <= 0) return false;
    out.push_back(buf[0]);
    if (out.size() >= 4 && out.compare(out.size() - 4, 4, "\r\n\r\n") == 0) return true;
  }
  return false;
}

std::string header_value(const std::string& headers, const char* name) {
  std::string lower = headers;
  for (char& c : lower) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  std::string lkey = std::string(name) + ":";
  for (char& c : lkey) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  auto pos = lower.find(lkey);
  if (pos == std::string::npos) return "";
  auto start = headers.find(':', pos);
  if (start == std::string::npos) return "";
  ++start;
  while (start < headers.size() && (headers[start] == ' ' || headers[start] == '\t')) ++start;
  auto end = headers.find("\r\n", start);
  if (end == std::string::npos) end = headers.size();
  return headers.substr(start, end - start);
}

std::string request_path(const std::string& headers) {
  // "GET /path HTTP/1.1\r\n..."
  auto sp1 = headers.find(' ');
  if (sp1 == std::string::npos) return "";
  auto sp2 = headers.find(' ', sp1 + 1);
  if (sp2 == std::string::npos) return "";
  std::string path = headers.substr(sp1 + 1, sp2 - sp1 - 1);
  auto q = path.find('?');
  if (q != std::string::npos) path.resize(q);
  return path;
}

bool is_safe_relpath(const std::string& p) {
  if (p.empty() || p[0] == '/' || p.find("..") != std::string::npos) return false;
  return true;
}

std::string mime_for(const std::string& path) {
  if (path.size() >= 5 && path.substr(path.size() - 5) == ".html") return "text/html; charset=utf-8";
  if (path.size() >= 3 && path.substr(path.size() - 3) == ".js") return "text/javascript; charset=utf-8";
  if (path.size() >= 4 && path.substr(path.size() - 4) == ".css") return "text/css; charset=utf-8";
  if (path.size() >= 5 && path.substr(path.size() - 5) == ".json") return "application/json";
  if (path.size() >= 5 && path.substr(path.size() - 5) == ".wasm") return "application/wasm";
  if (path.size() >= 4 && path.substr(path.size() - 4) == ".png") return "image/png";
  if (path.size() >= 4 && path.substr(path.size() - 4) == ".svg") return "image/svg+xml";
  if (path.size() >= 5 && path.substr(path.size() - 5) == ".webmanifest")
    return "application/manifest+json";
  return "application/octet-stream";
}

bool read_file(const std::string& path, std::string& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::ostringstream ss;
  ss << in.rdbuf();
  out = ss.str();
  return true;
}

void http_respond(SSL* ssl, int code, const char* status, const std::string& ctype,
                  const std::string& body, bool no_cache = false) {
  std::ostringstream oss;
  oss << "HTTP/1.1 " << code << " " << status << "\r\n"
      << "Content-Type: " << ctype << "\r\n"
      << "Content-Length: " << body.size() << "\r\n"
      << "Connection: close\r\n";
  if (no_cache) oss << "Cache-Control: no-store\r\n";
  oss << "\r\n" << body;
  ssl_write_str(ssl, oss.str());
}

void serve_static(SSL* ssl, const std::string& web_root, const std::string& url_path) {
  std::string rel = url_path;
  if (rel.empty() || rel == "/") rel = "/index.html";
  if (!rel.empty() && rel[0] == '/') rel = rel.substr(1);
  if (!is_safe_relpath(rel)) {
    http_respond(ssl, 400, "Bad Request", "text/plain", "bad path");
    return;
  }
  std::string full = web_root;
  if (!full.empty() && full.back() != '/') full.push_back('/');
  full += rel;
  std::string body;
  if (!read_file(full, body)) {
    http_respond(ssl, 404, "Not Found", "text/plain", "not found");
    return;
  }
  http_respond(ssl, 200, "OK", mime_for(rel), body);
}

bool looks_like_http(const unsigned char* peek, size_t n) {
  if (n < 3) return false;
  // GET, HEAD, POST, PUT, OPTIONS
  if (n >= 3 && std::memcmp(peek, "GET", 3) == 0) return true;
  if (n >= 4 && std::memcmp(peek, "HEAD", 4) == 0) return true;
  if (n >= 4 && std::memcmp(peek, "POST", 4) == 0) return true;
  return false;
}

}  // namespace

std::vector<unsigned char> load_cert_der_pem(const std::string& cert_path) {
  FILE* f = std::fopen(cert_path.c_str(), "r");
  if (!f) return {};
  X509* x = PEM_read_X509(f, nullptr, nullptr, nullptr);
  std::fclose(f);
  if (!x) return {};
  int len = i2d_X509(x, nullptr);
  if (len <= 0) { X509_free(x); return {}; }
  std::vector<unsigned char> der(static_cast<size_t>(len));
  unsigned char* p = der.data();
  i2d_X509(x, &p);
  X509_free(x);
  return der;
}

bool WebFrontend::serve_until_stream(TransportServer& tx,
                                     const std::string& web_root,
                                     const std::string& pairing_code,
                                     std::unique_ptr<ByteChannel>& out_channel,
                                     std::string& out_peer,
                                     volatile std::sig_atomic_t& stop) {
  out_channel.reset();
  out_peer.clear();
  if (!tx.ssl_ctx() || tx.listen_fd() < 0) {
    std::fprintf(stderr, "web: TLS listen required for --web\n");
    return false;
  }

  while (!stop) {
    pollfd pfd{tx.listen_fd(), POLLIN, 0};
    if (::poll(&pfd, 1, 500) <= 0) continue;
    if (!(pfd.revents & POLLIN)) continue;

    sockaddr_in cli{};
    socklen_t cli_len = sizeof(cli);
    int fd = ::accept(tx.listen_fd(), (sockaddr*)&cli, &cli_len);
    if (fd < 0) continue;
    char ibuf[INET_ADDRSTRLEN] = {0};
    std::string peer = inet_ntop(AF_INET, &cli.sin_addr, ibuf, sizeof(ibuf)) ? ibuf : "";
    int yes = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));

    SSL* ssl = SSL_new(tx.ssl_ctx());
    if (!ssl) { ::close(fd); continue; }
    SSL_set_fd(ssl, fd);
    if (SSL_accept(ssl) <= 0) {
      SSL_free(ssl);
      ::close(fd);
      continue;
    }

    // Peek to distinguish HTTP (browser) vs native droppix TLS client.
    unsigned char peek[8] = {};
    int peeked = SSL_peek(ssl, peek, sizeof(peek));
    if (peeked <= 0) {
      SSL_shutdown(ssl); SSL_free(ssl); ::close(fd);
      continue;
    }

    if (!looks_like_http(peek, static_cast<size_t>(peeked))) {
      // Native Android/Qt client on the same port.
      out_channel = std::make_unique<SocketChannel>(fd, ssl);
      out_peer = peer;
      std::fprintf(stderr, "web: native client from %s\n", peer.c_str());
      return true;
    }

    std::string headers;
    if (!ssl_read_http_headers(ssl, headers, 10000)) {
      SSL_shutdown(ssl); SSL_free(ssl); ::close(fd);
      continue;
    }
    std::string path = request_path(headers);
    std::string upgrade = header_value(headers, "Upgrade");
    for (char& c : upgrade) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');

    if (path == "/ws" && upgrade.find("websocket") != std::string::npos) {
      std::string key = header_value(headers, "Sec-WebSocket-Key");
      if (key.empty()) {
        http_respond(ssl, 400, "Bad Request", "text/plain", "missing key");
        SSL_shutdown(ssl); SSL_free(ssl); ::close(fd);
        continue;
      }
      std::string accept = ws_accept_key(key);
      std::ostringstream resp;
      resp << "HTTP/1.1 101 Switching Protocols\r\n"
           << "Upgrade: websocket\r\n"
           << "Connection: Upgrade\r\n"
           << "Sec-WebSocket-Accept: " << accept << "\r\n\r\n";
      if (!ssl_write_str(ssl, resp.str())) {
        SSL_shutdown(ssl); SSL_free(ssl); ::close(fd);
        continue;
      }
      out_channel = std::make_unique<WsChannel>(fd, ssl);
      out_peer = peer;
      std::fprintf(stderr, "web: websocket client from %s\n", peer.c_str());
      return true;
    }

    if (path == "/config.json") {
      std::string body = std::string("{\"pairingCode\":\"") + pairing_code + "\"}\n";
      http_respond(ssl, 200, "OK", "application/json", body, true);
      SSL_shutdown(ssl); SSL_free(ssl); ::close(fd);
      continue;
    }

    serve_static(ssl, web_root, path);
    SSL_shutdown(ssl); SSL_free(ssl); ::close(fd);
  }
  return false;
}

}  // namespace droppix
