#pragma once
#include <QtGlobal>     // qreal
#include <algorithm>
#include <cstdint>
#include "protocol.h"   // droppix::TouchContact

namespace droppix {

// Pure math behind VideoWidget's touch/mouse capture: maps a point in widget-local
// pixels (x,y within [0,w)x[0,h)) plus a 0..1 pressure into the wire's normalized
// TouchContact (x/y 0..65535, pressure 0..1023) — see video_widget.h for the full
// touch-capture algorithm this is one piece of (throttling, contact-set semantics, the
// right-click two-tap synth), all of which live in VideoWidget since they need real
// QTouchEvent/QMouseEvent state; this function is the widget-independent part, kept
// separate so it's unit-testable without a live QWidget/QApplication.
inline TouchContact normalize_touch(qreal x, qreal y, qreal w, qreal h,
                                    qreal pressure, uint8_t id) {
  w = std::max<qreal>(1, w);
  h = std::max<qreal>(1, h);
  TouchContact c;
  c.id = id;
  c.x = static_cast<uint16_t>(std::clamp(x / w, 0.0, 1.0) * 65535.0);
  c.y = static_cast<uint16_t>(std::clamp(y / h, 0.0, 1.0) * 65535.0);
  c.pressure = static_cast<uint16_t>(std::clamp(pressure, 0.0, 1.0) * 1023.0);
  return c;
}

}  // namespace droppix
