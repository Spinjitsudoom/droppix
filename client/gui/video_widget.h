#pragma once
#include <QVideoWidget>
#include <functional>
#include <map>
#include <vector>
#include "protocol.h"   // droppix::TouchContact
#include "touch_normalize.h"

namespace droppix {

// Renders decoded video (via its inherited QVideoSink) and captures touch/mouse input,
// normalizing it into the same TouchContact wire shape the Android app sends — see
// android/.../ui/DisplaySurfaceView.kt for the algorithm this mirrors:
//   - a real touchscreen's QTouchEvent points map 1:1 to contacts (multi-touch works)
//   - without one, left-click+drag synthesizes a single contact (down/move/up)
//   - right-click synthesizes a brief two-contact tap at the same point, to trigger the
//     HOST's existing two-finger-tap-to-right-click gesture (host/src/tap_gesture.cpp) —
//     the wire protocol has no dedicated "right click" message, so this reuses the
//     mechanism the host already has rather than inventing a new one.
//   - MOVE is throttled to ~12ms (~83Hz) so a drag can't flood the host; DOWN/UP/CANCEL
//     are never throttled (a dropped "up" would leave a phantom finger stuck down).
//   - coordinates normalize to the widget's current pixel size, into 0..65535; pressure
//     (real touch pressure if reported, else full-scale 1023 for mice/plain touchpads)
//     into 0..1023.
class VideoWidget : public QVideoWidget {
  Q_OBJECT
 public:
  explicit VideoWidget(QWidget* parent = nullptr);

  using TouchCallback = std::function<void(const std::vector<TouchContact>&)>;
  void setTouchCallback(TouchCallback cb) { onTouch_ = std::move(cb); }

 protected:
  bool event(QEvent* e) override;         // QTouchEvent path
  void mousePressEvent(QMouseEvent* e) override;
  void mouseMoveEvent(QMouseEvent* e) override;
  void mouseReleaseEvent(QMouseEvent* e) override;

 private:
  void emitContacts(const std::vector<TouchContact>& contacts);
  TouchContact normalize(qreal x, qreal y, qreal pressure, uint8_t id) const {
    return normalize_touch(x, y, width(), height(), pressure, id);
  }

  TouchCallback onTouch_;
  qint64 lastMoveSentMs_ = 0;
  bool mouseDown_ = false;
  static constexpr qint64 kMoveMinIntervalMs = 12;
};

}  // namespace droppix
