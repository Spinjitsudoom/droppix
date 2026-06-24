#pragma once
#include <QObject>
#include <QString>

namespace droppix {
class AdbManager : public QObject {
  Q_OBJECT
 public:
  explicit AdbManager(QObject* parent = nullptr);
  void refresh();             // async: emits deviceStatus
  void setupReverse(int port);
 signals:
  void deviceStatus(const QString& status);
};
}  // namespace droppix
