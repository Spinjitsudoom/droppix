#include "video_widget.h"
#include <QMouseEvent>
#include <QTouchEvent>
#include <QDateTime>
#include <algorithm>

namespace droppix {

VideoWidget::VideoWidget(QWidget* parent) : QVideoWidget(parent) {
  setAttribute(Qt::WA_AcceptTouchEvents, true);
}

void VideoWidget::emitContacts(const std::vector<TouchContact>& contacts) {
  if (onTouch_) onTouch_(contacts);
}

bool VideoWidget::event(QEvent* e) {
  switch (e->type()) {
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
    case QEvent::TouchCancel: {
      auto* te = static_cast<QTouchEvent*>(e);
      if (e->type() == QEvent::TouchCancel) { emitContacts({}); return true; }

      const auto& points = te->points();
      const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
      const bool isUpdate = e->type() == QEvent::TouchUpdate;
      bool anyNonMove = false;
      for (const auto& p : points)
        if (p.state() != QEventPoint::Updated) anyNonMove = true;
      // Coalesce a pure-move burst (mirrors DisplaySurfaceView.kt's ~12ms MOVE throttle);
      // any press/release is always sent immediately regardless of timing.
      if (isUpdate && !anyNonMove) {
        if (nowMs - lastMoveSentMs_ < kMoveMinIntervalMs) return true;
        lastMoveSentMs_ = nowMs;
      }
      std::vector<TouchContact> contacts;
      for (const auto& p : points) {
        if (p.state() == QEventPoint::Released) continue;   // exclude the lifting finger
        contacts.push_back(normalize(p.position().x(), p.position().y(),
                                     p.pressure() > 0 ? p.pressure() : 1.0, p.id() & 0xFF));
      }
      emitContacts(contacts);
      return true;
    }
    default:
      return QVideoWidget::event(e);
  }
}

void VideoWidget::mousePressEvent(QMouseEvent* e) {
  if (e->button() == Qt::RightButton) {
    // Synthesize a brief two-contact tap at the same point (immediately followed by
    // release) to trigger the host's existing two-finger-tap-to-right-click detector
    // (host/src/tap_gesture.cpp) — there is no dedicated "right click" wire message.
    auto a = normalize(e->position().x(), e->position().y(), 1.0, 0);
    auto b = normalize(e->position().x(), e->position().y(), 1.0, 1);
    emitContacts({a, b});
    emitContacts({});
    return;
  }
  if (e->button() == Qt::LeftButton) {
    mouseDown_ = true;
    emitContacts({normalize(e->position().x(), e->position().y(), 1.0, 0)});
  }
}

void VideoWidget::mouseMoveEvent(QMouseEvent* e) {
  if (!mouseDown_) return;
  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  if (nowMs - lastMoveSentMs_ < kMoveMinIntervalMs) return;
  lastMoveSentMs_ = nowMs;
  emitContacts({normalize(e->position().x(), e->position().y(), 1.0, 0)});
}

void VideoWidget::mouseReleaseEvent(QMouseEvent* e) {
  if (e->button() != Qt::LeftButton) return;
  mouseDown_ = false;
  emitContacts({});
}

}  // namespace droppix
