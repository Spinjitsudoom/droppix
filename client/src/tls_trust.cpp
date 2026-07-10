#include "tls_trust.h"
#include <openssl/sha.h>
#include <QSettings>
#include <QString>
#include <cstdio>

namespace droppix {

std::vector<unsigned char> cert_der(X509* cert) {
  if (!cert) return {};
  unsigned char* buf = nullptr;
  int len = i2d_X509(cert, &buf);
  if (len <= 0) return {};
  std::vector<unsigned char> out(buf, buf + len);
  OPENSSL_free(buf);
  return out;
}

std::string cert_fingerprint(X509* cert) {
  auto der = cert_der(cert);
  if (der.empty()) return {};
  unsigned char h[SHA256_DIGEST_LENGTH];
  SHA256(der.data(), der.size(), h);
  char hex[SHA256_DIGEST_LENGTH * 2 + 1];
  for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) std::snprintf(hex + i * 2, 3, "%02x", h[i]);
  return std::string(hex, SHA256_DIGEST_LENGTH * 2);
}

namespace {
QSettings& settings() {
  static QSettings s("droppix", "droppix_client_pins");
  return s;
}
QString key(const std::string& host) { return QString("pins/") + QString::fromStdString(host); }
}  // namespace

bool TlsTrust::isPaired(const std::string& host) const {
  return settings().contains(key(host));
}

std::optional<std::string> TlsTrust::pinnedFingerprint(const std::string& host) const {
  if (!settings().contains(key(host))) return std::nullopt;
  return settings().value(key(host)).toString().toStdString();
}

void TlsTrust::pin(const std::string& host, const std::string& fingerprint) {
  settings().setValue(key(host), QString::fromStdString(fingerprint));
}

void TlsTrust::clear(const std::string& host) {
  settings().remove(key(host));
}

}  // namespace droppix
