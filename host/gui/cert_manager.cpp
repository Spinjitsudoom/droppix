#include "cert_manager.h"
#include "../src/pairing_code.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSslCertificate>
#include <sys/stat.h>

namespace droppix {

CertManager::CertManager(QString dir) : dir_(std::move(dir)) {}

QString CertManager::certPath() const { return dir_ + "/cert.pem"; }
QString CertManager::keyPath() const { return dir_ + "/key.pem"; }

bool CertManager::ensure() {
  QDir().mkpath(dir_);
  const QString cert = certPath();
  const QString key = keyPath();
  if (!QFileInfo::exists(cert) || !QFileInfo::exists(key)) {
    QProcess::execute("openssl", {
        "req", "-x509", "-newkey", "rsa:2048", "-nodes",
        "-keyout", key, "-out", cert,
        "-days", "3650", "-subj", "/CN=droppix"});
    ::chmod(key.toLocal8Bit().constData(), 0600);
  }
  return QFileInfo::exists(cert) && QFileInfo::exists(key);
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
