#include "connections_page.h"

#include <QAbstractItemModel>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace droppix {

namespace {
// Self-managed empty state: shows `empty` (and optionally hides `list`) whenever `list`
// has zero rows, watching the list's model directly (rowsInserted/rowsRemoved/
// modelReset) rather than going through MainWindow. Deliberately does NOT insert a
// placeholder ROW into the QListWidget itself — MainWindow::updateStatus() uses
// monitorsList_->count() as the real monitor tally, so a placeholder row there would
// silently inflate that count and desync the Status hero's metrics.
// `context` is the empty-state QLabel's owner (ConnectionsPage) so the connection is torn
// down if the page ever outlives the (MainWindow-owned) list — never the reverse.
void wireEmptyState(QListWidget* list, QLabel* empty, QObject* context) {
  auto sync = [list, empty] {
    const bool isEmpty = list->count() == 0;
    empty->setVisible(isEmpty);
    list->setVisible(!isEmpty);
  };
  sync();   // initial state from the list's count() at construction time
  QObject::connect(list->model(), &QAbstractItemModel::rowsInserted, context, sync);
  QObject::connect(list->model(), &QAbstractItemModel::rowsRemoved, context, sync);
  QObject::connect(list->model(), &QAbstractItemModel::modelReset, context, sync);
}
}  // namespace

ConnectionsPage::ConnectionsPage(QListWidget* devices, QPushButton* connectBtn,
                                  QListWidget* monitors, QPushButton* stopBtn,
                                  QPushButton* mirrorBtn, QWidget* parent)
    : QWidget(parent) {
  // --- Discovered devices card: header + list + empty state + connect button ---
  auto* devicesCard = new QFrame(this);
  devicesCard->setObjectName("card");
  auto* devicesHeader = new QLabel("Discovered devices", devicesCard);
  devicesHeader->setObjectName("header");
  auto* devicesEmpty = new QLabel("Searching for tablets…", devicesCard);
  devicesEmpty->setObjectName("caption");
  devicesEmpty->setAlignment(Qt::AlignCenter);
  auto* devicesCardLayout = new QVBoxLayout;
  devicesCardLayout->addWidget(devicesHeader);
  devicesCardLayout->addWidget(devices);
  devicesCardLayout->addWidget(devicesEmpty);
  devicesCardLayout->addWidget(connectBtn);
  devicesCard->setLayout(devicesCardLayout);
  wireEmptyState(devices, devicesEmpty, this);

  // --- Active monitors card: header + list + empty state + stop/mirror button row ---
  auto* monitorsCard = new QFrame(this);
  monitorsCard->setObjectName("card");
  auto* monitorsHeader = new QLabel("Active monitors", monitorsCard);
  monitorsHeader->setObjectName("header");
  auto* monitorsEmpty = new QLabel(
      "No active monitors — connect a device to extend your desktop", monitorsCard);
  monitorsEmpty->setObjectName("caption");
  monitorsEmpty->setAlignment(Qt::AlignCenter);
  monitorsEmpty->setWordWrap(true);
  auto* monitorsBtnRow = new QHBoxLayout;
  monitorsBtnRow->addWidget(stopBtn);
  monitorsBtnRow->addWidget(mirrorBtn);
  auto* monitorsCardLayout = new QVBoxLayout;
  monitorsCardLayout->addWidget(monitorsHeader);
  monitorsCardLayout->addWidget(monitors);
  monitorsCardLayout->addWidget(monitorsEmpty);
  monitorsCardLayout->addLayout(monitorsBtnRow);
  monitorsCard->setLayout(monitorsCardLayout);
  wireEmptyState(monitors, monitorsEmpty, this);

  auto* root = new QVBoxLayout(this);
  root->addWidget(devicesCard);
  root->addWidget(monitorsCard);
  root->addStretch();
}

}  // namespace droppix
