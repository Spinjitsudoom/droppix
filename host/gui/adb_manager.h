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
  // USB connect a specific device: `adb -s <serial> reverse tcp:port tcp:port`
  // then launch the app on the tablet pointed at 127.0.0.1:<port> (no tap needed).
  void usbConnect(const QString& serial, int port);
 signals:
  void deviceStatus(const QString& status);
};
}  // namespace droppix
