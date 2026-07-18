# droppix_gui restyle: dark, modern accent

**Status:** Shipped on master.
panel — no behavior changes, so existing GUI logic/tests are untouched.

## Goal
Make `droppix_gui` look like a polished control panel instead of a default-themed
form, via a self-contained dark theme (independent of the user's KDE theme) plus minor
layout/structure improvements.

## Palette (teal accent; kept as constants for easy retinting)
- bg `#1b1f24`, card `#22272e`, border `#323a45`
- text `#e6e9ef`, muted `#8a93a3`
- accent `#14b8a6`, accent-hover `#2dd4bf`, danger `#ef4444`
- status dot: connected `#22c55e`, waiting `#f59e0b`, stopped `#5b6573`

## Components
- **`host/gui/style.h` (new):** `kStyleSheet` (one QSS string) + the accent color
  constants. QSS targets QWidget, QGroupBox (rounded card), QPushButton (default +
  `#startButton` accent, with a `running` dynamic property for the red Stop state),
  QComboBox/QSpinBox/QRadioButton/QCheckBox, QPlainTextEdit (monospace log), and label
  roles (`#header`, `#tagline`, `#caption`, `#statusDot`, `#statusText`).
- **`main.cpp`:** `QApplication::setStyle("Fusion")` (consistent base for the custom
  QSS regardless of system theme) then `app.setStyleSheet(kStyleSheet)`.
- **`main_window.cpp/.h`:**
  - Header: accent square + "droppix" wordmark + tagline.
  - Settings `QGroupBox` rendered as a card; Refresh + Orientation share one row.
  - Status row: a round color **dot** (helper sets its color) + state text + a compact
    stats string (`fps 30 · 14 KB · enc 1.2 ms`) replacing the three plain labels.
  - Start button: `objectName("startButton")`, full-width/taller; `setRunningUi`
    toggles the `running` property (+ unpolish/polish) for the red Stop state.
  - Log: monospace, a small "Log" caption, consistent margins; larger default window.

## Non-goals
- No new dependencies, no functional changes, no new settings.
- Light/system-theme variants (the user chose dark).

## Testing
- Builds in `droppix-dev`; existing GUI unit tests still pass (logic unchanged).
- Manual: launch `droppix_gui`, confirm the dark theme renders, the Start button
  toggles accent↔red, and the status dot changes color with connection state.
