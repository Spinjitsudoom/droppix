#include "idle_page.h"

#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace droppix {

IdlePage::IdlePage(QWidget* parent) : QWidget(parent) {
  auto* card = new QFrame(this);
  card->setObjectName("card");          // styled by the shared host stylesheet
  card->setMinimumWidth(360);

  state_ = new QLabel("NOT CONNECTED", card);
  state_->setObjectName("stateWord");
  state_->setAlignment(Qt::AlignCenter);

  caption_ = new QLabel("Pick a PC to view its screen", card);
  caption_->setObjectName("caption");
  caption_->setAlignment(Qt::AlignCenter);
  caption_->setWordWrap(true);

  connectBtn_ = new QPushButton("Connect", card);
  connectBtn_->setCursor(Qt::PointingHandCursor);
  connect(connectBtn_, &QPushButton::clicked, this, &IdlePage::connectRequested);

  auto* inner = new QVBoxLayout(card);
  inner->setContentsMargins(28, 26, 28, 26);
  inner->setSpacing(12);
  inner->addWidget(state_);
  inner->addWidget(caption_);
  inner->addSpacing(4);
  inner->addWidget(connectBtn_, 0, Qt::AlignCenter);

  auto* outer = new QVBoxLayout(this);
  outer->addStretch(1);
  outer->addWidget(card, 0, Qt::AlignCenter);
  outer->addStretch(1);
}

void IdlePage::setMessage(const QString& text) {
  caption_->setText(text.isEmpty() ? "Pick a PC to view its screen" : text);
}

}  // namespace droppix
