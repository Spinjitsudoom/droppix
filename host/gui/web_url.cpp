#include "web_url.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QStandardPaths>
#include "qrcodegen.hpp"
#include "web_root.h"
#include "lan_ifaces.h"

namespace droppix {

std::string resolve_web_root_for_gui() {
  // Prefer the relocated runtime copy (AppImage) — readable by pkexec/root.
  const QString runtimeWeb =
      QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/droppix/runtime/web";
  if (QFileInfo(runtimeWeb + "/index.html").exists()) return runtimeWeb.toStdString();

  // Flatpak host-staged runtime (same path shape after staging).
  const QString appDir = QCoreApplication::applicationDirPath();
  QStringList candidates = {
      appDir + "/../share/droppix/web",
      appDir + "/web",
      appDir + "/../../web/dist",
      appDir + "/../../../web/dist",
  };
#ifdef DROPPIX_SOURCE_WEB_DIR
  candidates << QString::fromUtf8(DROPPIX_SOURCE_WEB_DIR);   // dev build: source-tree web/dist
#endif
  for (const QString& c : candidates) {
    const QString abs = QDir(c).absolutePath();
    if (QFileInfo(abs + "/index.html").exists()) return abs.toStdString();
  }

  const QString appdir = qEnvironmentVariable("APPDIR");
  if (!appdir.isEmpty()) {
    const QString w = appdir + "/usr/share/droppix/web";
    if (QFileInfo(w + "/index.html").exists()) return w.toStdString();
  }

  return droppix::resolve_web_root();
}

QString primary_lan_ipv4() {
  const QList<LanIface> ifaces = lan_ipv4_ifaces();
  return ifaces.isEmpty() ? QStringLiteral("127.0.0.1") : ifaces.first().ip;
}

QString session_web_url(const QString& ip, int port) {
  return QStringLiteral("https://%1:%2/").arg(ip).arg(port);
}

QString session_web_url(int port) {
  return session_web_url(primary_lan_ipv4(), port);
}

QImage make_qr_image(const QString& text, int scale) {
  using qrcodegen::QrCode;
  try {
    const QrCode qr = QrCode::encodeText(text.toUtf8().constData(), QrCode::Ecc::MEDIUM);
    const int size = qr.getSize();
    if (scale < 1) scale = 1;
    QImage img(size * scale, size * scale, QImage::Format_RGB32);
    img.fill(Qt::white);
    for (int y = 0; y < size; ++y) {
      for (int x = 0; x < size; ++x) {
        if (!qr.getModule(x, y)) continue;
        for (int dy = 0; dy < scale; ++dy)
          for (int dx = 0; dx < scale; ++dx)
            img.setPixel(x * scale + dx, y * scale + dy, qRgb(0, 0, 0));
      }
    }
    return img;
  } catch (...) {
    return {};
  }
}

}  // namespace droppix
