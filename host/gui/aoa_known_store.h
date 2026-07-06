#pragma once
#include <QSet>
#include <QString>
#include <QStringList>

namespace droppix {
// Remembers USB serials of tablets we've successfully streamed to over AOA,
// persisted to <dir>/known_aoa (one serial per line). Drives auto-start on plug.
class AoaKnownStore {
 public:
  explicit AoaKnownStore(QString dir);
  bool contains(const QString& serial) const;
  void add(const QString& serial);
  QStringList all() const;
 private:
  QString path() const;   // <dir>/known_aoa
  QString dir_;
  QSet<QString> serials_;
};
}  // namespace droppix
