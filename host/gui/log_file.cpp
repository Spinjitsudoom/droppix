#include "log_file.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace droppix {

LogFile::LogFile(QString path, qint64 maxBytes) : path_(std::move(path)), maxBytes_(maxBytes) {}

QString LogFile::format(const LogEntry& e) {
  return QDateTime::fromMSecsSinceEpoch(e.epochMs).toString(Qt::ISODate) + ' ' +
         (e.session.isEmpty() ? QString() : "[" + e.session + "]") +
         (e.source.isEmpty() ? QString() : "[" + e.source + "]") + ' ' + e.text;
}

QString LogFile::defaultPath() {
  QString base = qEnvironmentVariable("XDG_STATE_HOME");
  if (base.isEmpty()) base = QDir::homePath() + "/.local/state";
  return base + "/droppix/droppix.log";
}

void LogFile::write(const LogEntry& e) {
  if (!dirReady_) dirReady_ = QDir().mkpath(QFileInfo(path_).absolutePath());
  if (maxBytes_ > 0 && QFileInfo(path_).size() >= maxBytes_) {
    QFile::remove(path_ + ".1");
    QFile::rename(path_, path_ + ".1");
  }
  QFile f(path_);
  if (!f.open(QIODevice::Append | QIODevice::Text)) return;
  f.write((format(e) + '\n').toUtf8());
}

}  // namespace droppix
