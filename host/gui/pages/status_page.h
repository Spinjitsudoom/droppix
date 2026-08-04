#pragma once
#include <QWidget>

class QComboBox;
class QPushButton;
class QLabel;

namespace droppix {

// "Status" section (stack_ index 0): the redesigned landing page — a hero card
// (beacon dot + state word + the server ON/OFF switch + a sub-line), a live
// metrics row (active monitors / clients / interfaces up), the profile row,
// and a scan-to-pair card. This is a thin layout container, same spirit as
// ConnectionsPage/InterfacesPage: the profile combo/buttons and the dot/state/
// stats/scan labels are MainWindow-owned — StatusPage only arranges them, it
// does not recreate or take over their existing signal wiring. The one
// widget StatusPage DOES create and own is the server toggle switch
// (serverSwitch()): MainWindow wires it once, at construction, with `clicked`
// (see main_window.cpp) so `updateServerButton()`'s programmatic
// `setChecked()` (which only emits `toggled`) can never re-enter
// `onServerToggled()`.
class StatusPage : public QWidget {
  Q_OBJECT
 public:
  StatusPage(QComboBox* profile, QPushButton* save, QPushButton* saveAs, QPushButton* del,
             QLabel* dot, QLabel* stateText, QLabel* stats,
             QLabel* scanCaption, QLabel* scanQr, QPushButton* copyUrl, QWidget* parent = nullptr);

  QPushButton* serverSwitch();                       // checkable QPushButton#serverSwitch
  void setMetrics(int monitors, int clients, int ifaces);   // updates the 3 metric numbers

 private:
  QPushButton* serverSwitch_ = nullptr;
  QLabel* monitorsNum_ = nullptr;
  QLabel* clientsNum_ = nullptr;
  QLabel* ifacesNum_ = nullptr;
};

}  // namespace droppix
