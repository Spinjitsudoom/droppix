#include "aoa_known_store.h"
#include <QDir>
#include <QFile>
#include <QTextStream>

namespace droppix {

AoaKnownStore::AoaKnownStore(QString dir) : dir_(std::move(dir)) {
  QFile f(path());
  if (f.open(QIODevice::ReadOnly)) {
    QTextStream in(&f);
    while (!in.atEnd()) {
      const QString line = in.readLine().trimmed();
      if (!line.isEmpty()) serials_.insert(line);
    }
  }
}

QString AoaKnownStore::path() const { return dir_ + "/known_aoa"; }

bool AoaKnownStore::contains(const QString& serial) const { return serials_.contains(serial); }

void AoaKnownStore::add(const QString& serial) {
  if (serial.isEmpty() || serials_.contains(serial)) return;
  serials_.insert(serial);
  QDir().mkpath(dir_);
  QFile f(path());
  if (f.open(QIODevice::Append)) f.write((serial + "\n").toUtf8());
}

QStringList AoaKnownStore::all() const {
  QStringList out(serials_.begin(), serials_.end());
  out.sort();
  return out;
}

}  // namespace droppix
