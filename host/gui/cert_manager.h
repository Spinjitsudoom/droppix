#pragma once
#include <QString>

namespace droppix {
// Generates (on first run) and locates the PC's self-signed TLS cert/key used by
// the streamer's --tls mode, and derives the human-facing pairing code from the cert.
class CertManager {
 public:
  explicit CertManager(QString dir);

  // Generates <dir>/cert.pem + key.pem via openssl if either is missing.
  // Returns true if both files exist afterward (whether freshly generated or pre-existing).
  bool ensure();

  QString certPath() const;  // <dir>/cert.pem
  QString keyPath() const;   // <dir>/key.pem

  // Reads cert.pem, derives the 6-digit pairing code from its DER encoding.
  // Returns "" or "unavailable" if the cert can't be read. Cached after first call.
  QString pairingCode() const;

 private:
  QString dir_;
  mutable QString code_;
  mutable bool codeComputed_ = false;
};
}  // namespace droppix
