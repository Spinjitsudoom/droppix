#include "cert_manager.h"
#include "lan_ifaces.h"
#include "../src/pairing_code.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSslCertificate>
#include <QSysInfo>
#include <sys/stat.h>

namespace droppix {

CertManager::CertManager(QString dir) : dir_(std::move(dir)) {}

QString CertManager::certPath() const { return dir_ + "/cert.pem"; }
QString CertManager::keyPath() const { return dir_ + "/key.pem"; }
QString CertManager::caPath() const { return dir_ + "/ca.pem"; }
QString CertManager::caKeyPath() const { return dir_ + "/ca-key.pem"; }

bool CertManager::ensureCa() {
  QDir().mkpath(dir_);
  const QString ca = caPath();
  const QString caKey = caKeyPath();
  if (QFileInfo::exists(ca) && QFileInfo::exists(caKey)) return true;
  // Long-lived (20y): every client that installs this CA stays trusted for the CA's
  // lifetime, so it must outlive any realistic deployment — unlike the leaf below, this
  // is never rotated by regenerate().
  QProcess::execute("openssl", {
      "req", "-x509", "-newkey", "rsa:2048", "-nodes",
      "-keyout", caKey, "-out", ca,
      "-days", "7300", "-subj", "/CN=droppix local CA",
      "-addext", "basicConstraints=critical,CA:true",
      "-addext", "keyUsage=critical,keyCertSign,cRLSign"});
  ::chmod(caKey.toLocal8Bit().constData(), 0600);
  return QFileInfo::exists(ca) && QFileInfo::exists(caKey);
}

bool CertManager::ensure() {
  QDir().mkpath(dir_);
  if (!ensureCa()) return false;
  const QString cert = certPath();
  const QString key = keyPath();
  if (!QFileInfo::exists(cert) || !QFileInfo::exists(key)) {
    // SAN must cover every address a browser might actually connect to: all current LAN
    // IPv4s (the QR/pairing URL uses the "primary" one, but Wi-Fi adapters can reorder
    // across restarts) plus the mDNS hostname, so a client that bookmarks either keeps
    // working without a certificate mismatch warning.
    QStringList san{"IP:127.0.0.1"};
    for (const LanIface& i : lan_ipv4_ifaces()) san << ("IP:" + i.ip);
    const QString host = QSysInfo::machineHostName();
    if (!host.isEmpty()) san << ("DNS:" + host + ".local");

    const QString csr = dir_ + "/leaf.csr.tmp";
    QProcess::execute("openssl", {
        "req", "-new", "-newkey", "rsa:2048", "-nodes",
        "-keyout", key, "-out", csr,
        "-subj", "/CN=droppix",
        "-addext", "subjectAltName=" + san.join(','),
        "-addext", "extendedKeyUsage=serverAuth"});
    // -copy_extensions carries the CSR's requested SAN/EKU onto the signed cert; openssl
    // x509 -req has no -addext of its own for a CA-signed (non-self-signed) leaf.
    QProcess::execute("openssl", {
        "x509", "-req", "-in", csr,
        "-CA", caPath(), "-CAkey", caKeyPath(), "-CAcreateserial",
        "-out", cert, "-days", "825", "-copy_extensions", "copy"});
    QFile::remove(csr);
    ::chmod(key.toLocal8Bit().constData(), 0600);
  }
  return QFileInfo::exists(cert) && QFileInfo::exists(key);
}

bool CertManager::regenerate() {
  QFile::remove(certPath());
  QFile::remove(keyPath());
  codeComputed_ = false;   // force pairingCode() to recompute from the new cert
  code_.clear();
  return ensure();
}

QString CertManager::pairingCode() const {
  if (codeComputed_) return code_;
  codeComputed_ = true;

  QFile f(certPath());
  if (!f.open(QIODevice::ReadOnly)) {
    code_ = "unavailable";
    return code_;
  }
  const QByteArray pem = f.readAll();
  QSslCertificate cert(pem, QSsl::Pem);
  if (cert.isNull()) {
    code_ = "unavailable";
    return code_;
  }
  const QByteArray der = cert.toDer();
  std::vector<unsigned char> bytes(der.begin(), der.end());
  code_ = QString::fromStdString(derive_pairing_code(bytes));
  return code_;
}
}  // namespace droppix
