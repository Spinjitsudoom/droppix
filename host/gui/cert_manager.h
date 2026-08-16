#pragma once
#include <QString>

namespace droppix {
// Generates (on first run) and locates the PC's TLS cert/key used by the streamer's
// --tls mode, and derives the human-facing pairing code from the leaf cert.
//
// The leaf cert is signed by a persistent local CA (also generated on first run, in the
// same directory) instead of being self-signed: a browser can never fully trust a bare
// self-signed cert, but once a client installs this CA's public cert once (served at
// /ca.crt — see web_frontend.cpp), every future leaf cert signed by it is auto-trusted,
// including across the per-launch leaf rotation below. The CA itself is never rotated —
// doing so would silently untrust every device that already installed it.
class CertManager {
 public:
  explicit CertManager(QString dir);

  // Generates <dir>/ca.pem + ca-key.pem (if missing) then <dir>/cert.pem + key.pem
  // (if missing), the latter signed by the former with SAN entries for every current
  // LAN IPv4 address + the local mDNS hostname. Returns true if the leaf cert/key exist
  // afterward (whether freshly generated or pre-existing).
  bool ensure();

  // Deletes the existing LEAF cert/key (never the CA) and generates a fresh signed pair,
  // so the derived pairing code changes. Resets the cached code. Called once per launch
  // for a per-restart code, and picks up any LAN IP change since the CA was last used.
  bool regenerate();

  QString certPath() const;    // <dir>/cert.pem (leaf, CA-signed)
  QString keyPath() const;     // <dir>/key.pem (leaf, private)
  QString caPath() const;      // <dir>/ca.pem (CA public cert — safe to serve to clients)
  QString caKeyPath() const;   // <dir>/ca-key.pem (CA private key — never served)

  // Reads cert.pem, derives the 6-digit pairing code from its DER encoding.
  // Returns "" or "unavailable" if the cert can't be read. Cached after first call.
  QString pairingCode() const;

 private:
  bool ensureCa();

  QString dir_;
  mutable QString code_;
  mutable bool codeComputed_ = false;
};
}  // namespace droppix
