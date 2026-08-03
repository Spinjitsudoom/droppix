#include "status_page.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace droppix {

namespace {
// One "number over caption" column of the metrics row. Returns the layout (added to the
// row by the caller) and hands back the number QLabel via numOut so setMetrics() can
// update it later.
QVBoxLayout* metricColumn(QWidget* parent, const char* caption, QLabel** numOut) {
  auto* num = new QLabel("0", parent);
  num->setObjectName("metricNum");
  num->setAlignment(Qt::AlignCenter);
  auto* cap = new QLabel(caption, parent);
  cap->setObjectName("caption");
  cap->setAlignment(Qt::AlignCenter);
  auto* col = new QVBoxLayout;
  col->addWidget(num);
  col->addWidget(cap);
  *numOut = num;
  return col;
}
}  // namespace

StatusPage::StatusPage(QComboBox* profile, QPushButton* save, QPushButton* saveAs, QPushButton* del,
                       QLabel* dot, QLabel* stateText, QLabel* stats,
                       QLabel* scanCaption, QLabel* scanQr, QWidget* parent)
    : QWidget(parent) {
  // --- Hero card: beacon + state word + server switch, and a sub-line underneath ---
  auto* hero = new QFrame(this);
  hero->setObjectName("card");
  stateText->setObjectName("stateWord");   // was "statusText" (compact row); hero wants it large

  serverSwitch_ = new QPushButton("Server OFF", hero);
  serverSwitch_->setObjectName("serverSwitch");
  serverSwitch_->setCheckable(true);
  serverSwitch_->setCursor(Qt::PointingHandCursor);

  auto* heroTopRow = new QHBoxLayout;
  heroTopRow->addWidget(dot);
  heroTopRow->addSpacing(8);
  heroTopRow->addWidget(stateText);
  heroTopRow->addStretch();
  heroTopRow->addWidget(serverSwitch_);

  auto* heroLayout = new QVBoxLayout;
  heroLayout->addLayout(heroTopRow);
  heroLayout->addWidget(stats);   // sub-line: compact stats ("—" / connection summary)
  hero->setLayout(heroLayout);

  // --- Metrics row: three live number+caption columns, driven by setMetrics() ---
  auto* metricsRow = new QHBoxLayout;
  metricsRow->addLayout(metricColumn(this, "Active monitors", &monitorsNum_));
  metricsRow->addLayout(metricColumn(this, "Clients", &clientsNum_));
  metricsRow->addLayout(metricColumn(this, "Interfaces up", &ifacesNum_));

  // --- Profile row ---
  auto* profRow = new QHBoxLayout;
  profRow->addWidget(new QLabel("Profile:", this));
  profRow->addWidget(profile, 1);
  profRow->addWidget(save);
  profRow->addWidget(saveAs);
  profRow->addWidget(del);

  // --- Scan-to-pair card ---
  auto* scanCard = new QFrame(this);
  scanCard->setObjectName("card");
  auto* scanLayout = new QVBoxLayout;
  scanLayout->addWidget(scanCaption);
  scanLayout->addWidget(scanQr);
  scanCard->setLayout(scanLayout);

  auto* root = new QVBoxLayout(this);
  root->addWidget(hero);
  root->addLayout(metricsRow);
  root->addLayout(profRow);
  root->addWidget(scanCard);
  root->addStretch();
}

QPushButton* StatusPage::serverSwitch() { return serverSwitch_; }

void StatusPage::setMetrics(int monitors, int clients, int ifaces) {
  monitorsNum_->setText(QString::number(monitors));
  clientsNum_->setText(QString::number(clients));
  ifacesNum_->setText(QString::number(ifaces));
}

}  // namespace droppix
