# G-2026-08-03-qss-property-repolish: QSS `[prop="true"]` selectors don't re-apply after `setProperty()` alone

- **ID:** `G-2026-08-03-qss-property-repolish`
- **Tags:** `gui`, `gotcha`
- **Date:** 2026-08-03
- **Related:** none

## Symptom

Two spots in the host GUI drive a QSS attribute-selector style off a dynamic property set
at runtime: the nav row's active button (`QPushButton#navButton[current="true"]`,
`MainWindow::selectSection()`) and the Status hero's server toggle
(`QPushButton#serverSwitch[on="true"]`, `MainWindow::updateServerButton()`). In both cases,
calling `widget->setProperty("current"/"on", true)` alone left the widget rendered in its
*previous* visual state — the active nav button didn't tint teal, the server switch didn't
flip to its "on" look — even though `property(...)` correctly returned the new value and
the underlying bool (`serverEnabled_`, the selected index) was correct.

## Root cause

Qt's style sheet engine evaluates dynamic-property selectors (`[prop="value"]`) as part of
polishing a widget, not on every property write. `QObject::setProperty()` just stores the
value and emits no repaint/restyle request by itself — the widget keeps whatever QSS match
was computed the last time it was polished. Without an explicit re-polish, the new property
value sits there unused by the active style until something else (a resize, a theme
change, an unrelated `polish()`) happens to trigger one.

## Fix

Force Qt to re-evaluate the widget's style rules immediately after the property write:

```cpp
sw->setProperty("on", serverEnabled_);
sw->style()->unpolish(sw);
sw->style()->polish(sw);   // re-apply [on="true"] / [current="true"] QSS now, not on the next repaint
```

`MainWindow::selectSection()` already had this pattern for the nav buttons' `current`
property; `MainWindow::updateServerButton()` (host GUI redesign, Task 9) needed the same
pair for the Status hero's `serverSwitch()` `on` property, or the switch would silently
lag one toggle behind its QSS `[on="true"]` styling.

## How to detect this in the future

Anti-pattern signature: any `widget->setProperty(name, value)` immediately followed by a
QSS rule keyed on `[name="value"]` in `style.h`, with no adjacent `style()->unpolish(w);
style()->polish(w);` pair. Grep for `setProperty(` in `host/gui/*.cpp` and check each hit
has the unpolish/polish pair nearby (or another guaranteed repolish path, e.g. a theme
reload) before trusting the QSS to reflect the new state.
