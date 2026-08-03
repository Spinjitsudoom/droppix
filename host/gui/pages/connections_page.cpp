#include "connections_page.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace droppix {

ConnectionsPage::ConnectionsPage(QListWidget* devices, QPushButton* connectBtn,
                                  QListWidget* monitors, QPushButton* stopBtn,
                                  QPushButton* mirrorBtn, QWidget* parent)
    : QWidget(parent) {
  // --- Discovered devices card: header + list + connect button ---
  auto* devicesCard = new QFrame(this);
  devicesCard->setObjectName("card");
  auto* devicesHeader = new QLabel("Discovered devices", devicesCard);
  devicesHeader->setObjectName("header");
  auto* devicesCardLayout = new QVBoxLayout;
  devicesCardLayout->addWidget(devicesHeader);
  devicesCardLayout->addWidget(devices);
  devicesCardLayout->addWidget(connectBtn);
  devicesCard->setLayout(devicesCardLayout);

  // --- Active monitors card: header + list + stop/mirror button row ---
  auto* monitorsCard = new QFrame(this);
  monitorsCard->setObjectName("card");
  auto* monitorsHeader = new QLabel("Active monitors", monitorsCard);
  monitorsHeader->setObjectName("header");
  auto* monitorsBtnRow = new QHBoxLayout;
  monitorsBtnRow->addWidget(stopBtn);
  monitorsBtnRow->addWidget(mirrorBtn);
  auto* monitorsCardLayout = new QVBoxLayout;
  monitorsCardLayout->addWidget(monitorsHeader);
  monitorsCardLayout->addWidget(monitors);
  monitorsCardLayout->addLayout(monitorsBtnRow);
  monitorsCard->setLayout(monitorsCardLayout);

  auto* root = new QVBoxLayout(this);
  root->addWidget(devicesCard);
  root->addWidget(monitorsCard);
  root->addStretch();
}

}  // namespace droppix
