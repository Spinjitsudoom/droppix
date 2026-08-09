#pragma once
#include <QString>
#include "theme.h"

namespace droppix {

// Kept for existing callers (e.g. main_window.cpp's setStatusDot()) that reference
// these directly and aren't theme-aware yet. Values match the dark palette below.
constexpr const char* kAccent       = "#14b8a6";
constexpr const char* kDotConnected = "#22c55e";
constexpr const char* kDotWaiting   = "#f59e0b";
constexpr const char* kDotStopped   = "#5b6573";

struct Palette {
  const char *bg, *surface, *panel, *border, *borderStrong,
             *text, *muted, *accent, *accent2, *good, *warn, *bad, *idle,
             *accentInk;   // text drawn on an accent fill
};

inline const Palette& palette(Theme t) {
  static const Palette dark{
    "#14181d","#1b1f24","#22272e","#2e343d","#3a424e",
    "#e6e9ef","#8a93a3","#14b8a6","#2dd4bf","#22c55e","#f59e0b","#ef4444","#5b6573","#06231f"};
  static const Palette light{
    "#eaeef2","#ffffff","#ffffff","#dde3e9","#c6cfd8",
    "#131820","#5b6674","#14b8a6","#0f9e8e","#16a34a","#d97706","#dc2626","#94a1af","#ffffff"};
  return t == Theme::Light ? light : dark;
}

inline QString styleSheet(Theme theme) {
  const Palette& p = palette(theme);
  auto c = [](const char* s){ return QString::fromLatin1(s); };
  return QString(R"QSS(
QWidget { background: %BG%; color: %TEXT%; font-size: 13px; }
QLabel { background: transparent; }
QLabel#header  { font-size: 20px; font-weight: 800; }
QLabel#caption { color: %MUTED%; font-size: 12px; }
QLabel#logo { min-width: 34px; max-width: 34px; min-height: 34px; max-height: 34px; }
QLabel#statusText  { font-weight: 600; }
QLabel#statusStats { color: %MUTED%; }
QLabel#stateWord   { font-size: 30px; font-weight: 800; }
QLabel#metricNum   { font-size: 22px; font-weight: 700; }

QFrame#card, QGroupBox {
  background: %PANEL%; border: 1px solid %BORDER%; border-radius: 12px;
  margin-top: 14px; padding: 12px; font-weight: 600;
}
QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 12px; padding: 0 4px; color: %MUTED%; }

QComboBox, QSpinBox {
  background: %BG%; border: 1px solid %BORDER%; border-radius: 6px; padding: 5px 8px; min-height: 20px;
}
QComboBox:hover, QSpinBox:hover { border-color: %ACCENT%; }
QComboBox::drop-down { border: none; width: 18px; }
QComboBox QAbstractItemView { background: %PANEL%; border: 1px solid %BORDER%; selection-background-color: %ACCENT%; selection-color: %ACCENTINK%; outline: none; }
QSpinBox::up-button, QSpinBox::down-button { width: 16px; background: %SURFACE%; border: none; }

QRadioButton, QCheckBox { spacing: 7px; background: transparent; }
QRadioButton::indicator, QCheckBox::indicator { width: 16px; height: 16px; }
QCheckBox::indicator   { border: 1px solid %BORDERSTRONG%; border-radius: 4px; background: %BG%; }
QRadioButton::indicator{ border: 1px solid %BORDERSTRONG%; border-radius: 8px; background: %BG%; }
QCheckBox::indicator:checked, QRadioButton::indicator:checked { background: %ACCENT%; border-color: %ACCENT%; }

QPushButton { background: %SURFACE%; border: 1px solid %BORDERSTRONG%; border-radius: 6px; padding: 6px 12px; }
QPushButton:hover   { border-color: %ACCENT%; }
QPushButton:pressed { background: %PANEL%; }

QPushButton#navButton { background: transparent; border: none; border-radius: 9px; padding: 11px 16px; color: %MUTED%; font-weight: 600; }
QPushButton#navButton:hover { background: %PANEL%; color: %TEXT%; }
QPushButton#navButton[current="true"] { background: %ACCENT%; color: %ACCENTINK%; }

ToggleSwitch#serverSwitch {
  qproperty-trackOnColor: %ACCENT%; qproperty-trackOffColor: %IDLE%;
  qproperty-knobOnColor: %ACCENTINK%; qproperty-knobOffColor: %TEXT%;
}

QPlainTextEdit { background: %BG%; border: 1px solid %BORDER%; border-radius: 8px; color: %TEXT%; padding: 6px; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: %BORDERSTRONG%; border-radius: 5px; min-height: 24px; }
QScrollBar::handle:vertical:hover { background: %MUTED%; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
)QSS")
    .replace("%BG%", c(p.bg)).replace("%SURFACE%", c(p.surface)).replace("%PANEL%", c(p.panel))
    .replace("%BORDERSTRONG%", c(p.borderStrong)).replace("%BORDER%", c(p.border))
    .replace("%TEXT%", c(p.text)).replace("%MUTED%", c(p.muted))
    .replace("%ACCENTINK%", c(p.accentInk)).replace("%ACCENT2%", c(p.accent2)).replace("%ACCENT%", c(p.accent))
    .replace("%GOOD%", c(p.good)).replace("%WARN%", c(p.warn)).replace("%BAD%", c(p.bad)).replace("%IDLE%", c(p.idle));
}

}  // namespace droppix
