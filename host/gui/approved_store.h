#pragma once
#include <QSet>
#include <QString>

namespace droppix {
// Remembers device ids the user has approved, persisted to <dir>/approved (one id per line).
class ApprovedStore {
 public:
  explicit ApprovedStore(QString dir);
  bool isApproved(const QString& id) const;
  void approve(const QString& id);
  void clear();
 private:
  QString path() const;  // <dir>/approved
  QString dir_;
  QSet<QString> ids_;
};
}  // namespace droppix
