#include "header_bar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

#include "style.h"   // ../host/gui — shared palette + dot colours

namespace droppix {

HeaderBar::HeaderBar(QWidget* parent) : QFrame(parent) {
  setObjectName("headerBar");

  logo_ = new QLabel(this);
  logo_->setObjectName("logo");
  logo_->setPixmap(QPixmap(":/logo.png").scaled(34, 34, Qt::KeepAspectRatio,
                                                Qt::SmoothTransformation));

  title_ = new QLabel("Droppix Client", this);
  title_->setObjectName("header");

  dot_ = new QLabel(this);
  dot_->setFixedSize(10, 10);
  status_ = new QLabel("Not connected", this);
  status_->setObjectName("statusText");
  detail_ = new QLabel(this);
  detail_->setObjectName("caption");
  detail_->hide();

  // ONE primary action rather than a Connect/Disconnect pair. The shared stylesheet has no
  // :disabled rule, so a greyed-out Disconnect rendered identically to a live button — a
  // dead control that looked clickable. Flipping one button removes the dead state entirely
  // instead of styling around it.
  actionBtn_ = new QPushButton("Connect", this);
  settingsBtn_ = new QPushButton("Settings", this);
  themeBtn_ = new QPushButton("Theme", this);
  for (QPushButton* b : {actionBtn_, settingsBtn_, themeBtn_})
    b->setCursor(Qt::PointingHandCursor);
  themeBtn_->setObjectName("iconButton");

  auto* statusCol = new QVBoxLayout;
  statusCol->setSpacing(0);
  auto* statusRow = new QHBoxLayout;
  statusRow->setSpacing(7);
  statusRow->addWidget(dot_);
  statusRow->addWidget(status_);
  statusCol->addLayout(statusRow);
  statusCol->addWidget(detail_);

  auto* row = new QHBoxLayout(this);
  row->setContentsMargins(14, 10, 14, 10);
  row->setSpacing(12);
  row->addWidget(logo_);
  row->addWidget(title_);
  row->addSpacing(8);
  row->addLayout(statusCol);
  row->addStretch(1);
  row->addWidget(actionBtn_);
  row->addWidget(settingsBtn_);
  row->addWidget(themeBtn_);

  connect(actionBtn_, &QPushButton::clicked, this, [this]{
    if (connected_) emit disconnectRequested();
    else emit connectRequested();
  });
  connect(settingsBtn_, &QPushButton::clicked, this, &HeaderBar::settingsRequested);
  connect(themeBtn_, &QPushButton::clicked, this, &HeaderBar::themeToggled);

  setConnected(false);
}

void HeaderBar::paintDot(const char* color) {
  dot_->setStyleSheet(QString("background:%1; border-radius:5px;").arg(color));
}

void HeaderBar::setConnected(bool connected) {
  connected_ = connected;
  actionBtn_->setText(connected ? "Disconnect" : "Connect");
  paintDot(connected ? kDotConnected : kDotStopped);
  if (!connected) detail_->hide();
}

void HeaderBar::setStatus(const QString& text) { status_->setText(text); }

void HeaderBar::setDetail(const QString& text) {
  detail_->setText(text);
  detail_->setVisible(!text.isEmpty());
}

void HeaderBar::setTheme(Theme) {
  // The palette arrives via the app-wide stylesheet; the dot is painted inline (a
  // per-widget stylesheet), so re-apply it for the new theme.
  paintDot(connected_ ? kDotConnected : kDotStopped);
}

}  // namespace droppix
