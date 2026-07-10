#pragma once
#include <openssl/x509.h>
#include <optional>
#include <string>
#include <vector>

namespace droppix {

// Cert DER bytes, for fingerprinting and pairing-code derivation (pairing_code.h).
std::vector<unsigned char> cert_der(X509* cert);

// SHA-256 over the cert's DER encoding, lowercase hex — same fingerprint scheme as the
// Android app's TlsTrust.certFingerprint() and the host's own cert identity.
std::string cert_fingerprint(X509* cert);

// TOFU pin store + pairing flow, mirroring android/.../net/TlsTrust.kt. Persisted via
// QSettings (one fingerprint string per host key) so a paired host reconnects silently;
// a mismatched fingerprint on a later connect means "the PC's identity changed."
class TlsTrust {
 public:
  bool isPaired(const std::string& host) const;
  std::optional<std::string> pinnedFingerprint(const std::string& host) const;
  void pin(const std::string& host, const std::string& fingerprint);
  void clear(const std::string& host);
};

}  // namespace droppix
