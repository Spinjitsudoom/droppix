#include "interfaces_page.h"

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace droppix {

InterfacesPage::InterfacesPage(QCheckBox* lan, QVBoxLayout* adapters, QCheckBox* usb,
                                QLabel* webUrl, QLabel* webQr, QPushButton* webCopy,
                                QWidget* parent)
    : QWidget(parent) {
  // --- LAN card: toggle + indented per-adapter rows + USB toggle ---
  auto* lanCard = new QFrame(this);
  lanCard->setObjectName("card");
  auto* lanCardLayout = new QVBoxLayout;
  lanCardLayout->addWidget(lan);
  lanCardLayout->addLayout(adapters);
  lanCardLayout->addWidget(usb);
  lanCard->setLayout(lanCardLayout);

  // --- Web client card: URL, copy button row, QR ---
  auto* webCard = new QFrame(this);
  webCard->setObjectName("card");
  auto* webBtnRow = new QHBoxLayout;
  webBtnRow->addWidget(webCopy);
  webBtnRow->addStretch();
  auto* webCardLayout = new QVBoxLayout;
  webCardLayout->addWidget(webUrl);
  webCardLayout->addLayout(webBtnRow);
  webCardLayout->addWidget(webQr);
  webCard->setLayout(webCardLayout);

  auto* root = new QVBoxLayout(this);
  root->addWidget(lanCard);
  root->addWidget(webCard);
  root->addStretch();
}

}  // namespace droppix
