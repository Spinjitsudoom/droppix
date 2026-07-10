#pragma once
#include <QString>

namespace droppix {

// Same dark theme as the host GUI (host/gui/style.h) for visual consistency across
// droppix's apps — copied rather than shared via the build (client/ intentionally
// doesn't link against host/gui code; see client/CMakeLists.txt's reuse notes).
constexpr const char* kAccent       = "#14b8a6";
constexpr const char* kDotConnected = "#22c55e";
constexpr const char* kDotWaiting   = "#f59e0b";
constexpr const char* kDotStopped   = "#5b6573";

inline QString styleSheet() {
  return QStringLiteral(R"QSS(
QWidget { background: #1b1f24; color: #e6e9ef; font-size: 13px; }
QLabel { background: transparent; }
QLabel#statusText  { font-weight: 600; }
QLabel#statusStats { color: #8a93a3; }
QPushButton {
  background: #2b313a; border: 1px solid #3a424e; border-radius: 6px;
  padding: 6px 12px;
}
QPushButton:hover   { background: #333b45; border-color: #14b8a6; }
QPushButton:pressed { background: #262b33; }
QListWidget {
  background: #14171c; border: 1px solid #323a45; border-radius: 8px;
}
QLineEdit {
  background: #1b1f24; border: 1px solid #323a45; border-radius: 6px;
  padding: 5px 8px;
}
QLineEdit:focus { border-color: #14b8a6; }
)QSS");
}

}  // namespace droppix
