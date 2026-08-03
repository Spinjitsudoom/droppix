#pragma once
#include <QWidget>

class QListWidget;
class QPushButton;

namespace droppix {

// "Connections" section: arranges MainWindow-owned discovered-devices and
// active-monitors widgets into two cards. This is a layout container only —
// it does not create, own, or wire any signals; MainWindow keeps all
// existing connect()s (onConnectToSelectedDevice/stopSelectedMonitor/
// toggleSelectedMonitorMirror) and refresh logic (rebuildClientList()/
// addMonitorRow()) against the same widget pointers.
class ConnectionsPage : public QWidget {
  Q_OBJECT
 public:
  ConnectionsPage(QListWidget* devices, QPushButton* connectBtn,
                  QListWidget* monitors, QPushButton* stopBtn, QPushButton* mirrorBtn,
                  QWidget* parent = nullptr);
};

}  // namespace droppix
