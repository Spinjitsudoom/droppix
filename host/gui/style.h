#pragma once
#include <QString>

namespace droppix {

// Theme colors kept here so the whole look is easy to retint from one place.
constexpr const char* kAccent       = "#14b8a6";
constexpr const char* kDotConnected = "#22c55e";
constexpr const char* kDotWaiting   = "#f59e0b";
constexpr const char* kDotStopped   = "#5b6573";

// Self-contained dark theme. Applied app-wide over the Fusion base style so it looks
// the same regardless of the user's system (KDE) theme.
inline QString styleSheet() {
  return QStringLiteral(R"QSS(
QWidget { background: #1b1f24; color: #e6e9ef; font-size: 13px; }

QLabel { background: transparent; }
QLabel#header  { font-size: 20px; font-weight: 700; }
QLabel#tagline { color: #8a93a3; }
QLabel#caption { color: #8a93a3; font-size: 12px; }
QLabel#logo {
  background: #14b8a6; border-radius: 6px;
  min-width: 22px; max-width: 22px; min-height: 22px; max-height: 22px;
}
QLabel#statusText  { font-weight: 600; }
QLabel#statusStats { color: #8a93a3; }

QGroupBox {
  background: #22272e; border: 1px solid #323a45; border-radius: 10px;
  margin-top: 14px; padding: 12px; font-weight: 600;
}
QGroupBox::title {
  subcontrol-origin: margin; subcontrol-position: top left;
  left: 12px; padding: 0 4px; color: #8a93a3;
}

QComboBox, QSpinBox {
  background: #1b1f24; border: 1px solid #323a45; border-radius: 6px;
  padding: 5px 8px; min-height: 20px;
}
QComboBox:hover, QSpinBox:hover { border-color: #14b8a6; }
QComboBox::drop-down { border: none; width: 18px; }
QComboBox QAbstractItemView {
  background: #22272e; border: 1px solid #323a45;
  selection-background-color: #14b8a6; selection-color: #06231f; outline: none;
}
QSpinBox::up-button, QSpinBox::down-button { width: 16px; background: #2b313a; border: none; }

QRadioButton, QCheckBox { spacing: 7px; background: transparent; }
QRadioButton::indicator, QCheckBox::indicator { width: 16px; height: 16px; }
QCheckBox::indicator   { border: 1px solid #4a5360; border-radius: 4px; background: #1b1f24; }
QRadioButton::indicator{ border: 1px solid #4a5360; border-radius: 8px; background: #1b1f24; }
QCheckBox::indicator:checked, QRadioButton::indicator:checked {
  background: #14b8a6; border-color: #14b8a6;
}

QPushButton {
  background: #2b313a; border: 1px solid #3a424e; border-radius: 6px;
  padding: 6px 12px;
}
QPushButton:hover   { background: #333b45; border-color: #14b8a6; }
QPushButton:pressed { background: #262b33; }

QPushButton#startButton {
  background: #14b8a6; border: none; border-radius: 8px; padding: 12px;
  color: #06231f; font-size: 15px; font-weight: 700;
}
QPushButton#startButton:hover { background: #2dd4bf; }
QPushButton#startButton[running="true"]        { background: #ef4444; color: #ffffff; }
QPushButton#startButton[running="true"]:hover  { background: #f87171; }

QPlainTextEdit {
  background: #14171c; border: 1px solid #323a45; border-radius: 8px;
  color: #c7cdd6; padding: 6px;
}

QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: #3a424e; border-radius: 5px; min-height: 24px; }
QScrollBar::handle:vertical:hover { background: #4a5360; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
)QSS");
}

}  // namespace droppix
