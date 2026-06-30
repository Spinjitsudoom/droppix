#include "profile_store.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace droppix {

ProfileStore::ProfileStore(QString dir) : dir_(std::move(dir)) {}

QString ProfileStore::path() const { return dir_ + "/profiles.json"; }
QString ProfileStore::lastUsedPath() const { return dir_ + "/last_profile"; }

static QJsonObject readAll(const QString& p) {
  QFile f(p);
  if (!f.open(QIODevice::ReadOnly)) return {};
  auto doc = QJsonDocument::fromJson(f.readAll());
  return doc.isObject() ? doc.object() : QJsonObject{};
}
static bool writeAll(const QString& dir, const QString& p, const QJsonObject& o) {
  QDir().mkpath(dir);
  QFile f(p);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
  f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
  return true;
}
static QJsonObject toJson(const Settings& s) {
  QJsonObject o;
  o["source"] = (s.source == Settings::Source::Evdi) ? "evdi" : "test";
  o["width"] = s.width; o["height"] = s.height;
  o["fps"] = s.fps; o["bitrate_kbps"] = s.bitrate_kbps; o["port"] = s.port;
  o["refresh_hz"] = s.refresh_hz;
  o["auto_adb_reverse"] = s.auto_adb_reverse;
  o["touch"] = s.touch;
  o["audio"] = s.audio;
  o["overlay"] = s.overlay;
  o["orientation"] = s.orientation;
  return o;
}
static Settings fromJson(const QJsonObject& o) {
  Settings s;
  s.source = (o["source"].toString() == "evdi") ? Settings::Source::Evdi
                                                : Settings::Source::TestPattern;
  s.width = o["width"].toInt(s.width);
  s.height = o["height"].toInt(s.height);
  s.fps = o["fps"].toInt(s.fps);
  s.bitrate_kbps = o["bitrate_kbps"].toInt(s.bitrate_kbps);
  s.port = o["port"].toInt(s.port);
  s.refresh_hz = o["refresh_hz"].toInt(s.refresh_hz);
  s.auto_adb_reverse = o["auto_adb_reverse"].toBool(s.auto_adb_reverse);
  s.touch = o["touch"].toBool(s.touch);
  s.audio = o["audio"].toBool(s.audio);
  s.overlay = o["overlay"].toBool(s.overlay);
  s.orientation = o["orientation"].toInt(s.orientation);
  return s;
}

QStringList ProfileStore::names() const { return readAll(path()).keys(); }

bool ProfileStore::save(const QString& name, const Settings& s) {
  QJsonObject all = readAll(path());
  all[name] = toJson(s);
  return writeAll(dir_, path(), all);
}

bool ProfileStore::load(const QString& name, Settings& out) const {
  QJsonObject all = readAll(path());
  if (!all.contains(name)) return false;
  out = fromJson(all[name].toObject());
  return true;
}

bool ProfileStore::remove(const QString& name) {
  QJsonObject all = readAll(path());
  if (!all.contains(name)) return false;
  all.remove(name);
  return writeAll(dir_, path(), all);
}

void ProfileStore::setLastUsed(const QString& name) {
  QDir().mkpath(dir_);
  QFile f(lastUsedPath());
  if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(name.toUtf8());
}

QString ProfileStore::lastUsed() const {
  QFile f(lastUsedPath());
  if (!f.open(QIODevice::ReadOnly)) return {};
  return QString::fromUtf8(f.readAll()).trimmed();
}
}  // namespace droppix
