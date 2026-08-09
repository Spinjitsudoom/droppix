#include "toggle_switch.h"
#include <QPainter>
#include <QPropertyAnimation>

namespace droppix {

ToggleSwitch::ToggleSwitch(QWidget* parent) : QAbstractButton(parent) {
  setCheckable(true);
  setCursor(Qt::PointingHandCursor);
}

QSize ToggleSwitch::sizeHint() const { return QSize(46, 26); }

void ToggleSwitch::checkStateSet() {
  auto* anim = new QPropertyAnimation(this, "knobPos", this);
  anim->setDuration(150);
  anim->setStartValue(knobPos_);
  anim->setEndValue(isChecked() ? 1.0 : 0.0);
  anim->setEasingCurve(QEasingCurve::OutCubic);
  anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void ToggleSwitch::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QRectF track = QRectF(rect()).adjusted(1, 1, -1, -1);
  const qreal r = track.height() / 2.0;
  p.setPen(Qt::NoPen);
  p.setBrush(isChecked() ? trackOnColor_ : trackOffColor_);
  p.drawRoundedRect(track, r, r);
  const qreal knobD = track.height() - 4;
  const qreal knobX = track.left() + 2 + knobPos_ * (track.width() - knobD - 2);
  p.setBrush(isChecked() ? knobOnColor_ : knobOffColor_);
  p.drawEllipse(QRectF(knobX, track.top() + 2, knobD, knobD));
}

}  // namespace droppix
