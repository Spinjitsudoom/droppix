# Host GUI Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reorganize the host GUI (`host/gui/`) into a spacedesk-style sectioned app — a top nav switching a `QStackedWidget` between Status · Connections · Interfaces · Settings · About, with a Status hero — and add a persisted light theme beside the existing dark one, surfacing settings that have no UI today.

**Architecture:** A **view + theming reorganization over the unchanged controllers.** The theme system (a `Theme` enum, a token-driven `styleSheet(Theme)`, and a pure `theme_pref` persistence helper) is the tested core. The layout is migrated **progressively**: first the entire current central widget is wrapped as page 0 of a new `QStackedWidget` behind a 5-button nav (app keeps working), then each section's widgets are **peeled out** into a thin `gui/pages/*` container class one task at a time. `MainWindow` keeps ownership of the interactive widgets and **all existing signal/slot wiring** (lowest regression risk); the page classes are layout containers. No protocol, streamer, discovery, session, encoder, evdi, or packaging change.

> **Realization note (deliberate):** the spec calls for "dedicated page classes." This plan realizes that as **thin container classes** that `MainWindow` populates, rather than pages that own their widgets and re-emit every signal. The user-facing result (five sections, Status hero, dual theme, surfaced settings, split files) is identical; wiring stays centralized to avoid a large, error-prone re-connect of ~30 signal sites in one step.

**Tech Stack:** C++17, Qt6 Widgets/Test, QSS theming, GoogleTest, CMake. Build in the `droppix-dev` distrobox.

## Global Constraints

- **Build/test in distrobox:** `distrobox enter droppix-dev -- bash -lc '<cmd>'`; build dir `~/droppix-build` (the CIFS source mount is no-exec). Configure once: `cmake -S "/var/mnt/nas/Projects/Spacedesk for linux/host" -B ~/droppix-build`.
- **GUI tests are offscreen:** run with `QT_QPA_PLATFORM=offscreen`. Targets: `droppix_tests` (pure, no Qt), `droppix_gui_tests` (Qt Widgets/Test), `droppix_gui` (the app, build-only in CI).
- **NEVER timeout-kill a `droppix_gui` launch.** It regenerates its TLS cert every launch and a killed launch wipes the real cert; XDG isolation does not work through distrobox. Verify the app by building it, not by launching-and-killing. On-device manual runs are the user's, closed cleanly.
- **No behavior change** to protocol/wire, streamer, discovery (`mdns_*`/`tether_scanner`/`aoa_scanner`), `SessionManager`/`StreamController`, encoders, evdi, TLS/pairing, tray, polkit, `--minimized`, or packaging. This is view + theming only.
- **Accent teal `#14b8a6`** in both themes. **Dark** = today's palette; **light** = new, contrast-checked.
- **Keep every existing test green.** `droppix_tests` and `droppix_gui_tests` must pass unchanged except where a task adds cases.
- Web PWA client redesign is **out of scope** (separate spec).

---

### Task 1: Theme enum + token-driven `styleSheet(Theme)` (dark + light)

**Files:**
- Create: `host/gui/theme.h` (Qt-free enum, shared by the pure helper and the QSS builder)
- Modify: `host/gui/style.h` (token table + `styleSheet(Theme)`; add nav/switch/hero rules)
- Test: `host/gui/tests/test_style_theme.cpp`
- Modify: `host/CMakeLists.txt` (add the test to `droppix_gui_tests`)

**Interfaces:**
- Produces: `enum class droppix::Theme { Dark, Light };` (in `theme.h`); `QString droppix::styleSheet(Theme);` (in `style.h`). The old zero-arg `styleSheet()` is removed — every caller passes a `Theme`.

- [ ] **Step 1: Write the failing test**

`host/gui/tests/test_style_theme.cpp`:
```cpp
#include <gtest/gtest.h>
#include "style.h"
using droppix::Theme; using droppix::styleSheet;

TEST(StyleTheme, DarkAndLightDiffer) {
  EXPECT_NE(styleSheet(Theme::Dark), styleSheet(Theme::Light));
}
TEST(StyleTheme, BothCarryAccentAndBaseSelectors) {
  for (auto t : {Theme::Dark, Theme::Light}) {
    const QString q = styleSheet(t);
    EXPECT_TRUE(q.contains("#14b8a6"));   // teal accent in both
    EXPECT_TRUE(q.contains("QWidget"));   // real stylesheet, not empty
    EXPECT_GT(q.size(), 400);
  }
}
TEST(StyleTheme, GroundsMatchTheme) {
  EXPECT_TRUE(styleSheet(Theme::Dark).contains("#1b1f24"));   // dark ground
  EXPECT_TRUE(styleSheet(Theme::Light).contains("#f"));        // a light (#f..) ground
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake -S "/var/mnt/nas/Projects/Spacedesk for linux/host" -B ~/droppix-build >/dev/null && cmake --build ~/droppix-build --target droppix_gui_tests -j'`
Expected: FAIL — compile error, `styleSheet` takes no `Theme` / `theme.h` missing.

- [ ] **Step 3: Create `host/gui/theme.h`**

```cpp
#pragma once
namespace droppix {
// Qt-free so the pure theme_pref helper (Task 2) and the QSS builder both use it.
enum class Theme { Dark, Light };
}  // namespace droppix
```

- [ ] **Step 4: Rewrite `host/gui/style.h` as a token-driven builder**

Replace the file body with a `Palette` token table + a QSS template that interpolates it, plus dark/light palettes. Keep every selector the current `style.h` has (QWidget/QLabel#header/#caption/#logo/#statusText/#statusStats, QGroupBox, QComboBox/QSpinBox, QRadioButton/QCheckBox, QPushButton/#startButton, QPlainTextEdit, QScrollBar) and **add** the new redesign selectors: `QPushButton#navButton` (+ `[current="true"]`), `QPushButton#serverSwitch` (+ `[on="true"]`), `QLabel#stateWord`, `QLabel#metricNum`, `QFrame#card`.

```cpp
#pragma once
#include <QString>
#include "theme.h"

namespace droppix {

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

QPushButton#startButton, QPushButton#serverSwitch {
  background: %ACCENT%; border: none; border-radius: 8px; padding: 12px; color: %ACCENTINK%; font-size: 15px; font-weight: 700;
}
QPushButton#startButton:hover, QPushButton#serverSwitch[on="true"]:hover { background: %ACCENT2%; }
QPushButton#startButton[running="true"], QPushButton#serverSwitch:!checked { background: %IDLE%; color: %TEXT%; }

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
```
(Order the `.replace` calls so longer tokens like `%BORDERSTRONG%`/`%ACCENTINK%`/`%ACCENT2%` run before their prefixes `%BORDER%`/`%ACCENT%`.)

- [ ] **Step 5: Wire the test into CMake**

In `host/CMakeLists.txt`, inside the `add_executable(droppix_gui_tests …)` list (after `gui/tests/test_interface_filter.cpp` / `gui/lan_ifaces.cpp`), add:
```cmake
    gui/tests/test_style_theme.cpp
```
`style.h`/`theme.h` are header-only — no extra `.cpp` needed. `droppix_gui_tests` already links `Qt6::Widgets`.

- [ ] **Step 6: Run tests to verify they pass**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake -S "/var/mnt/nas/Projects/Spacedesk for linux/host" -B ~/droppix-build >/dev/null && cmake --build ~/droppix-build --target droppix_gui_tests -j && QT_QPA_PLATFORM=offscreen ctest --test-dir ~/droppix-build -R StyleTheme --output-on-failure'`
Expected: PASS (3 StyleTheme tests).

- [ ] **Step 7: Fix the now-broken `styleSheet()` caller**

`host/gui/main.cpp:10` calls `droppix::styleSheet()` (no arg). Change it to `droppix::styleSheet(droppix::Theme::Dark)` for now (Task 3 makes it read the persisted pref). Build `droppix_gui` to confirm it compiles:
Run: `distrobox enter droppix-dev -- bash -lc 'cmake --build ~/droppix-build --target droppix_gui -j'`
Expected: links clean.

- [ ] **Step 8: Commit**

```bash
git add host/gui/theme.h host/gui/style.h host/gui/tests/test_style_theme.cpp host/gui/main.cpp host/CMakeLists.txt
git commit -m "feat(gui): token-driven dual-theme styleSheet(Theme)"
```

---

### Task 2: Pure theme-preference persistence (`theme_pref`)

**Files:**
- Create: `host/gui/theme_pref.h`, `host/gui/theme_pref.cpp`
- Test: `host/tests/test_theme_pref.cpp`
- Modify: `host/CMakeLists.txt` (add `theme_pref.cpp` to `droppix_gui` sources, and the test + `theme_pref.cpp` to `droppix_tests`)

**Interfaces:**
- Consumes: `droppix::Theme` (Task 1, `theme.h`).
- Produces: `Theme droppix::loadThemePref(const std::string& configDir);` (default `Dark` when missing/garbage) and `void droppix::saveThemePref(const std::string& configDir, Theme);` — a `<configDir>/theme` marker holding `"dark"`/`"light"`. Pure std (mirrors `autostart.{h,cpp}`), no Qt.

- [ ] **Step 1: Write the failing test**

`host/tests/test_theme_pref.cpp`:
```cpp
#include <gtest/gtest.h>
#include <cstdio>
#include <filesystem>
#include "theme_pref.h"
using droppix::Theme; using droppix::loadThemePref; using droppix::saveThemePref;

static std::string tmpdir() {
  auto d = std::filesystem::temp_directory_path() / ("droppix-theme-" + std::to_string(::getpid()));
  std::filesystem::create_directories(d);
  return d.string();
}
TEST(ThemePref, DefaultsToDarkWhenAbsent) {
  EXPECT_EQ(loadThemePref(tmpdir()), Theme::Dark);
}
TEST(ThemePref, RoundTripsLight) {
  auto d = tmpdir(); saveThemePref(d, Theme::Light);
  EXPECT_EQ(loadThemePref(d), Theme::Light);
}
TEST(ThemePref, RoundTripsDark) {
  auto d = tmpdir(); saveThemePref(d, Theme::Dark);
  EXPECT_EQ(loadThemePref(d), Theme::Dark);
}
TEST(ThemePref, GarbageFallsBackToDark) {
  auto d = tmpdir(); FILE* f = std::fopen((d + "/theme").c_str(), "w"); std::fputs("purple", f); std::fclose(f);
  EXPECT_EQ(loadThemePref(d), Theme::Dark);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake -S "/var/mnt/nas/Projects/Spacedesk for linux/host" -B ~/droppix-build >/dev/null && cmake --build ~/droppix-build --target droppix_tests -j'`
Expected: FAIL — `theme_pref.h` missing.

- [ ] **Step 3: Implement `theme_pref.{h,cpp}`**

`host/gui/theme_pref.h`:
```cpp
#pragma once
#include <string>
#include "theme.h"
namespace droppix {
Theme loadThemePref(const std::string& configDir);           // default Dark
void  saveThemePref(const std::string& configDir, Theme t);  // writes <dir>/theme
}  // namespace droppix
```
`host/gui/theme_pref.cpp`:
```cpp
#include "theme_pref.h"
#include <fstream>
namespace droppix {
Theme loadThemePref(const std::string& configDir) {
  std::ifstream in(configDir + "/theme");
  std::string v; std::getline(in, v);
  return v == "light" ? Theme::Light : Theme::Dark;   // anything else -> Dark
}
void saveThemePref(const std::string& configDir, Theme t) {
  std::ofstream out(configDir + "/theme", std::ios::trunc);
  out << (t == Theme::Light ? "light" : "dark");
}
}  // namespace droppix
```

- [ ] **Step 4: Wire CMake**

In `host/CMakeLists.txt`: add `gui/theme_pref.cpp` to the `target_sources(droppix_gui …)` list (near `gui/autostart.cpp`); and add both `tests/test_theme_pref.cpp` and `gui/theme_pref.cpp` to the `add_executable(droppix_tests …)` list (near `tests/test_autostart.cpp` / `gui/autostart.cpp`). `droppix_tests` already `target_include_directories(... PRIVATE gui)`.

- [ ] **Step 5: Run tests to verify they pass**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake -S "/var/mnt/nas/Projects/Spacedesk for linux/host" -B ~/droppix-build >/dev/null && cmake --build ~/droppix-build --target droppix_tests -j && ctest --test-dir ~/droppix-build -R ThemePref --output-on-failure'`
Expected: PASS (4 ThemePref tests).

- [ ] **Step 6: Commit**

```bash
git add host/gui/theme_pref.h host/gui/theme_pref.cpp host/tests/test_theme_pref.cpp host/CMakeLists.txt
git commit -m "feat(gui): pure theme-preference persistence (theme_pref)"
```

---

### Task 3: Apply persisted theme at startup + `MainWindow::setTheme`

**Files:**
- Modify: `host/gui/main_window.h` (declare `setTheme` + `currentTheme_`), `host/gui/main_window.cpp` (apply pref in ctor; implement `setTheme`)
- No test (app wiring; persistence covered by Task 2). Verification = build-green.

**Interfaces:**
- Consumes: `styleSheet(Theme)` (Task 1), `loadThemePref`/`saveThemePref` (Task 2), the existing free function `configDir()` used in the `MainWindow` ctor initializer list.
- Produces: `void MainWindow::setTheme(Theme);` (applies `qApp->setStyleSheet` + persists) and `Theme currentTheme_;` — consumed by the theme toggle in Task 4.

- [ ] **Step 1: Declare in `main_window.h`**

Add `#include "theme.h"`. In the private methods add `void setTheme(Theme t);`, and in the members add `Theme currentTheme_ = Theme::Dark;`.

- [ ] **Step 2: Implement `setTheme` and apply the pref in the ctor**

In `main_window.cpp` add includes `#include <QApplication>`, `#include "theme_pref.h"`, `#include "style.h"` (if not present). Add:
```cpp
void MainWindow::setTheme(Theme t) {
  currentTheme_ = t;
  qApp->setStyleSheet(styleSheet(t));
  saveThemePref(configDir(), t);
}
```
Near the **end** of the constructor (after widgets exist, before `setupTray()`), add:
```cpp
  currentTheme_ = loadThemePref(configDir());
  qApp->setStyleSheet(styleSheet(currentTheme_));   // honor the saved choice on launch
```

- [ ] **Step 3: Build to verify it compiles**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake --build ~/droppix-build --target droppix_gui -j'`
Expected: links clean.

- [ ] **Step 4: Commit**

```bash
git add host/gui/main_window.h host/gui/main_window.cpp
git commit -m "feat(gui): apply persisted theme at startup + setTheme"
```

---

### Task 4: Transitional shell — nav + `QStackedWidget` + header theme toggle

Wrap **today's entire central layout as page 0** of a new `QStackedWidget`, add a 5-button nav that selects stack pages (pages 1–4 are empty placeholders for now), and put the light/dark toggle in the header. The app keeps working exactly as before under the first tab; later tasks peel content out of page 0.

**Files:**
- Modify: `host/gui/main_window.h` (members: `QStackedWidget* stack_`, `QList<QPushButton*> navButtons_`, `void selectSection(int)`), `host/gui/main_window.cpp` (ctor central-widget assembly at `main_window.cpp:288-322`)
- No unit test (view). Verification = build-green + the app still shows all controls under "Status".

**Interfaces:**
- Consumes: `setTheme`/`currentTheme_` (Task 3).
- Produces: `QStackedWidget* stack_` with 5 pages (index 0 = current content, 1..4 empty), a `navButtons_` row, and `void selectSection(int i)` — later tasks add widgets to `stack_->widget(i)`.

- [ ] **Step 1: Build the nav + stack in the ctor**

Replace the tail of the ctor that builds `root`/`central` (`main_window.cpp:288-322`). Keep every existing `addLayout/addWidget` call, but add them to a **page-0 layout** instead of the window root:
```cpp
  // Page 0 holds today's full layout verbatim (peeled apart in later tasks).
  auto* page0 = new QWidget;
  auto* p0 = new QVBoxLayout(page0);
  p0->setContentsMargins(0,0,0,0); p0->setSpacing(12);
  p0->addLayout(profRow);
  p0->addLayout(statusRow);
  p0->addWidget(deviceLabel_);
  p0->addLayout(serverBtnRow);
  p0->addWidget(commBox_);
  p0->addWidget(monitorsBox_);
  p0->addWidget(devicesBox_, 1);

  stack_ = new QStackedWidget;
  stack_->addWidget(page0);                       // 0 Status
  for (int i = 1; i < 5; ++i) stack_->addWidget(new QWidget);   // 1..4 placeholders

  static const char* kNav[5] = {"Status","Connections","Interfaces","Settings","About"};
  auto* navRow = new QHBoxLayout; navRow->setSpacing(6);
  for (int i = 0; i < 5; ++i) {
    auto* b = new QPushButton(kNav[i]); b->setObjectName("navButton");
    b->setCheckable(true); b->setCursor(Qt::PointingHandCursor);
    connect(b, &QPushButton::clicked, this, [this, i]{ selectSection(i); });
    navButtons_ << b; navRow->addWidget(b);
  }

  // Header: logo + wordmark + stretch + theme toggle (replaces the gear/about icon btns).
  auto* themeBtn = new QPushButton("Theme"); themeBtn->setObjectName("iconButton");
  themeBtn->setCursor(Qt::PointingHandCursor);
  connect(themeBtn, &QPushButton::clicked, this, [this]{
    setTheme(currentTheme_ == Theme::Dark ? Theme::Light : Theme::Dark);
  });
  headerRow->addWidget(themeBtn);   // headerRow already has logo+title+stretch

  auto* root = new QVBoxLayout;
  root->setContentsMargins(16,16,16,16); root->setSpacing(12);
  root->addLayout(headerRow);
  root->addLayout(navRow);
  root->addWidget(stack_, 1);
  auto* central = new QWidget; central->setLayout(root);
  setCentralWidget(central);
  selectSection(0);
  resize(720, 640);
```
Delete the now-removed `settingsBtn`/`aboutBtn` creation (`main_window.cpp:182-191, 206`) and the `settingsDialog_->exec()` gear connection — the gear is replaced by the Settings tab (Task 8). Keep the `settingsDialog_` member and its `rememberAuth`/`manageDevices`/`overlayToggled` connections for now (Task 8 relocates them); it just isn't opened from a button.

- [ ] **Step 2: Implement `selectSection`**

```cpp
void MainWindow::selectSection(int i) {
  stack_->setCurrentIndex(i);
  for (int k = 0; k < navButtons_.size(); ++k) {
    navButtons_[k]->setChecked(k == i);
    navButtons_[k]->setProperty("current", k == i);
    navButtons_[k]->style()->unpolish(navButtons_[k]);
    navButtons_[k]->style()->polish(navButtons_[k]);   // re-evaluate [current="true"] QSS
  }
}
```
Add `#include <QStackedWidget>`, `#include <QStyle>` and declare `stack_`, `navButtons_`, `selectSection` in `main_window.h` (`class QStackedWidget;` fwd-decl already present at line 26; add `#include <QList>` and `QPushButton*` list member).

- [ ] **Step 3: Build + verify no regression**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake --build ~/droppix-build --target droppix_gui -j'`
Expected: links clean. (Manual, user: launching shows the nav row; "Status" tab holds all the old controls; theme button flips dark/light and persists.)

- [ ] **Step 4: Commit**

```bash
git add host/gui/main_window.h host/gui/main_window.cpp
git commit -m "feat(gui): section nav + QStackedWidget shell + header theme toggle"
```

---

### Task 5: Extract `AboutPage` (establishes the page pattern)

**Files:**
- Create: `host/gui/pages/about_page.h`, `host/gui/pages/about_page.cpp`
- Test: `host/gui/tests/test_about_page.cpp`
- Modify: `host/CMakeLists.txt` (add `gui/pages/about_page.cpp` to `droppix_gui`; add the test + `gui/pages/about_page.cpp` to `droppix_gui_tests`; add `gui/pages` include dir), `host/gui/main_window.cpp` (mount into stack page 4)

**Interfaces:**
- Produces: `class droppix::AboutPage : public QWidget { public: explicit AboutPage(QWidget* parent=nullptr); };` — self-contained static content (app name, version `0.1.0`, "HELLO v6", "evdi · KWin/X11", "NVENC · VAAPI · x264", a "Project page" and "Report an issue" button opening URLs via `QDesktopServices`). A `QLabel` with `objectName("aboutVersion")` shows the version.

- [ ] **Step 1: Write the failing test**

`host/gui/tests/test_about_page.cpp`:
```cpp
#include <gtest/gtest.h>
#include <QLabel>
#include "pages/about_page.h"
TEST(AboutPage, ShowsVersion) {
  droppix::AboutPage page;
  auto* v = page.findChild<QLabel*>("aboutVersion");
  ASSERT_NE(v, nullptr);
  EXPECT_TRUE(v->text().contains("0.1.0"));
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake -S "/var/mnt/nas/Projects/Spacedesk for linux/host" -B ~/droppix-build >/dev/null && cmake --build ~/droppix-build --target droppix_gui_tests -j'`
Expected: FAIL — `pages/about_page.h` missing.

- [ ] **Step 3: Implement `AboutPage`**

`host/gui/pages/about_page.h`:
```cpp
#pragma once
#include <QWidget>
namespace droppix { class AboutPage : public QWidget { Q_OBJECT public: explicit AboutPage(QWidget* parent=nullptr); }; }
```
`host/gui/pages/about_page.cpp`: build a `QFrame#card` with a `QLabel#header` "droppix", a caption, a version `QLabel` (`setObjectName("aboutVersion")`, text `"Version 0.1.0"`), a small key/value block (protocol/backend/encoders), and two `QPushButton`s wired to `QDesktopServices::openUrl(QUrl(...))` for the repo + issues. Arrange with a `QVBoxLayout`.

- [ ] **Step 4: Wire CMake**

`host/CMakeLists.txt`: to `droppix_gui`'s `target_sources`, add `gui/pages/about_page.cpp`; to `target_include_directories(droppix_gui …)` append `gui/pages`. To `droppix_gui_tests`: add `gui/tests/test_about_page.cpp` and `gui/pages/about_page.cpp`, and add `gui/pages` to its `target_include_directories`.

- [ ] **Step 5: Mount into the stack**

`main_window.cpp` (in the ctor, after `stack_` is built): replace placeholder page 4:
```cpp
  auto* aboutPage = new AboutPage;
  stack_->removeWidget(stack_->widget(4));
  stack_->insertWidget(4, aboutPage);
```
Add `#include "pages/about_page.h"`.

- [ ] **Step 6: Build + test**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake -S "/var/mnt/nas/Projects/Spacedesk for linux/host" -B ~/droppix-build >/dev/null && cmake --build ~/droppix-build --target droppix_gui droppix_gui_tests -j && QT_QPA_PLATFORM=offscreen ctest --test-dir ~/droppix-build -R AboutPage --output-on-failure'`
Expected: PASS + `droppix_gui` links.

- [ ] **Step 7: Commit**

```bash
git add host/gui/pages/about_page.h host/gui/pages/about_page.cpp host/gui/tests/test_about_page.cpp host/CMakeLists.txt host/gui/main_window.cpp
git commit -m "feat(gui): About section page"
```

---

### Task 6: Extract `InterfacesPage` (Communication Interfaces)

Move the "Communication interfaces" group (LAN/USB toggles + adapter rows) and the web-client URL/QR/copy widgets out of page 0 into an `InterfacesPage` **container** that lays out `MainWindow`-owned widgets (wiring stays in `MainWindow`).

**Files:**
- Create: `host/gui/pages/interfaces_page.h`, `host/gui/pages/interfaces_page.cpp`
- Modify: `host/CMakeLists.txt` (add source to `droppix_gui`), `host/gui/main_window.cpp` (build the page, mount at stack index 2, stop adding these to page 0)

**Interfaces:**
- Consumes: `MainWindow`-owned `lanToggle_`, `usbToggle_`, `adapterRows_` (a `QVBoxLayout`), `webUrlLabel_`, `webQrLabel_`, `webCopyBtn_`.
- Produces: `class InterfacesPage : public QWidget { public: InterfacesPage(QCheckBox* lan, QVBoxLayout* adapters, QCheckBox* usb, QLabel* webUrl, QLabel* webQr, QPushButton* webCopy, QWidget* parent=nullptr); };` — arranges them into a LAN `QFrame#card` (toggle + adapters) and a Web-client `QFrame#card` (url + copy + qr).

- [ ] **Step 1: Implement `InterfacesPage`**

Header declares the ctor above. `.cpp` reparents/arranges the passed widgets: LAN card = a `QFrame#card` with a `QVBoxLayout` holding `lan`, then `adapters` (already a layout — add via `addLayout`), then `usb`; Web card = a `QFrame#card` with `webUrl`, a row with `webCopy`, and `webQr`. A page `QVBoxLayout` holds both cards + a stretch. (No signals — `MainWindow` keeps its existing `connect(lanToggle_, …)` / `connect(usbToggle_, …)` / `webCopyBtn_` connections.)

- [ ] **Step 2: Build the page in `MainWindow` and mount it; remove from page 0**

In `main_window.cpp`: the widgets `lanToggle_`, `usbToggle_`, `adapterRows_`, `commBox_`, `webUrlLabel_`, `webQrLabel_`, `webCopyBtn_` are created earlier in the ctor. **Stop** wrapping them in `commBox_`/`monLayout` where they belong to Interfaces: keep creating `lanToggle_`/`usbToggle_`/`adapterRows_` and the web widgets, but do **not** add `commBox_` to page 0, and remove `webUrlLabel_`/`webQrLabel_`/`webCopyBtn_` from `monLayout`. Then:
```cpp
  auto* ifacesPage = new InterfacesPage(lanToggle_, adapterRows_, usbToggle_, webUrlLabel_, webQrLabel_, webCopyBtn_);
  stack_->removeWidget(stack_->widget(2)); stack_->insertWidget(2, ifacesPage);
```
Retire the `commBox_` `QGroupBox` (the card now provides the frame); keep the `commBox_` member only if other code references it — otherwise delete the member and its uses. `refreshInterfaces()` still repopulates `adapterRows_` unchanged.

- [ ] **Step 3: Build + verify**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake -S "/var/mnt/nas/Projects/Spacedesk for linux/host" -B ~/droppix-build >/dev/null && cmake --build ~/droppix-build --target droppix_gui -j'`
Expected: links clean. (Manual: Interfaces tab shows LAN/USB + adapter IPs + web URL/QR; toggles still work.)

- [ ] **Step 4: Commit**

```bash
git add host/gui/pages/interfaces_page.h host/gui/pages/interfaces_page.cpp host/CMakeLists.txt host/gui/main_window.cpp
git commit -m "feat(gui): Interfaces (communication) section page"
```

---

### Task 7: Extract `ConnectionsPage` (devices + active monitors)

**Files:**
- Create: `host/gui/pages/connections_page.h`, `host/gui/pages/connections_page.cpp`
- Modify: `host/CMakeLists.txt` (source into `droppix_gui`), `host/gui/main_window.cpp` (mount at stack index 1; remove `devicesBox_`/`monitorsBox_` from page 0)

**Interfaces:**
- Consumes: `MainWindow`-owned `devicesList_`, `connectBtn_`, `monitorsList_`, and the two monitor buttons (currently local `stopMonBtn`/`toggleMirrorBtn` at `main_window.cpp:246-247` — **promote them to members** `stopMonBtn_`, `mirrorBtn_` so the page can receive them; their `connect(...)` stays in `MainWindow`), plus `pairingScanCaption_`, `pairingScanQr_` (move these to the Status page in Task 9 — for now leave them in `monitorsBox_`'s old spot by passing them here temporarily, or park them on page 0; simplest: pass them to ConnectionsPage now and relocate in Task 9).
- Produces: `class ConnectionsPage : public QWidget { public: ConnectionsPage(QListWidget* devices, QPushButton* connectBtn, QListWidget* monitors, QPushButton* stopBtn, QPushButton* mirrorBtn, QWidget* parent=nullptr); };` — a "Discovered devices" `QFrame#card` (list + connect) and an "Active monitors" `QFrame#card` (list + stop + mirror).

- [ ] **Step 1: Promote the two monitor buttons to members**

In `main_window.h` add `QPushButton* stopMonBtn_ = nullptr; QPushButton* mirrorBtn_ = nullptr;`. In `main_window.cpp` change the local `auto* stopMonBtn`/`auto* toggleMirrorBtn` (lines 246-247) to assign the members; keep their `connect(...)` to `stopSelectedMonitor`/`toggleSelectedMonitorMirror`.

- [ ] **Step 2: Implement `ConnectionsPage`** — header ctor above; `.cpp` arranges the two cards. No signals (wiring stays in `MainWindow`).

- [ ] **Step 3: Mount + remove from page 0**

Remove the `monitorsBox_`/`devicesBox_` `QGroupBox` wrapping and their page-0 `addWidget`s. Build:
```cpp
  auto* connPage = new ConnectionsPage(devicesList_, connectBtn_, monitorsList_, stopMonBtn_, mirrorBtn_);
  stack_->removeWidget(stack_->widget(1)); stack_->insertWidget(1, connPage);
```
`monitorsBox_->hide()`/`show()` logic (visibility when sessions exist) — replace references to `monitorsBox_` with `connPage`'s active-monitors card, or keep the monitors list always visible with an empty-state row (see Task 9). For this task, keep the list always visible; drop the `monitorsBox_->hide()` calls (note them for Task 9's empty-state).

- [ ] **Step 4: Build + verify** — `cmake --build ~/droppix-build --target droppix_gui -j`; links clean. (Manual: Connections tab lists devices + monitors; Connect/Stop/Mirror work.)

- [ ] **Step 5: Commit**

```bash
git add host/gui/pages/connections_page.h host/gui/pages/connections_page.cpp host/CMakeLists.txt host/gui/main_window.h host/gui/main_window.cpp
git commit -m "feat(gui): Connections section page"
```

---

### Task 8: Extract `SettingsPage` (form + hidden fields), retire `SettingsDialog`

Move the settings form into a page, **surface the hidden `Settings` fields** (resolution, fps, audio, orientation), and move the `load()/store()` round-trip out of `SettingsDialog` into `SettingsPage`. The dialog is deleted.

**Files:**
- Create: `host/gui/pages/settings_page.h`, `host/gui/pages/settings_page.cpp`
- Delete: `host/gui/settings_dialog.h`, `host/gui/settings_dialog.cpp`
- Test: `host/gui/tests/test_settings_page.cpp`
- Modify: `host/CMakeLists.txt` (swap `settings_dialog.cpp`→`settings_page.cpp` in `droppix_gui`; add page + test to `droppix_gui_tests`), `host/gui/main_window.cpp` + `.h` (use `SettingsPage` instead of `SettingsDialog`), `host/gui/main_window.cpp` overlay/remember/manage connections

**Interfaces:**
- Consumes: `Settings` struct (`settings.h`); `Theme`/`setTheme` (Task 3).
- Produces:
```cpp
class SettingsPage : public QWidget {
  Q_OBJECT
 public:
  explicit SettingsPage(QWidget* parent=nullptr);
  void load(const Settings& s);   // widgets <- settings (same contract as old dialog)
  void store(Settings& s) const;  // widgets -> settings
  void setTheme(Theme t);         // reflect current theme in the toggle
 signals:
  void rememberAuthRequested();
  void manageDevicesRequested();
  void overlayToggled(bool show);
  void themeChangeRequested(Theme t);
};
```
Surfaces new widgets for `width`/`height` (a resolution `QComboBox`: "1280×800", "1920×1080", "Match client"), `fps` (`QComboBox` 30/60), `audio` (`QCheckBox`), `orientation` (`QComboBox` 0/90/180/270) — `store()` writes them into the `Settings` fields left unset today (`settings_dialog.cpp:128`).

- [ ] **Step 1: Write the failing round-trip test**

`host/gui/tests/test_settings_page.cpp`:
```cpp
#include <gtest/gtest.h>
#include "pages/settings_page.h"
#include "settings.h"
using namespace droppix;
TEST(SettingsPage, RoundTripsIncludingSurfacedFields) {
  SettingsPage page;
  Settings in; in.bitrate_kbps = 16000; in.port = 34000; in.touch = true;
  in.audio = true; in.orientation = 90; in.fps = 60; in.width = 1920; in.height = 1080;
  page.load(in);
  Settings out; page.store(out);
  EXPECT_EQ(out.bitrate_kbps, 16000);
  EXPECT_EQ(out.port, 34000);
  EXPECT_TRUE(out.touch);
  EXPECT_TRUE(out.audio);
  EXPECT_EQ(out.orientation, 90);
  EXPECT_EQ(out.fps, 60);
  EXPECT_EQ(out.width, 1920);
  EXPECT_EQ(out.height, 1080);
}
```

- [ ] **Step 2: Run to verify it fails** — build `droppix_gui_tests`; FAIL (`pages/settings_page.h` missing).

- [ ] **Step 3: Implement `SettingsPage`**

Port the widget set from `settings_dialog.cpp:49-107` (source radios, bitrate/port spins, refresh combo, touch/overlay/autoConnect/webClient checks, launchAtLogin/minimizeOnClose checks, remember-auth/manage-devices buttons) into three `QFrame#card` groups — **Stream** / **Session & input** / **Application** — and **add** the resolution/fps/audio/orientation widgets. Port `load()`/`store()` verbatim from `settings_dialog.cpp` and extend `store()` to also set `s.width/s.height` (from the resolution combo; "Match client" → leave 0), `s.fps`, `s.audio`, `s.orientation`. In the Application card add a theme control (two radios or a combo Dark/Light) that emits `themeChangeRequested(Theme)`; `setTheme(Theme)` sets it without emitting. Emit `rememberAuthRequested`/`manageDevicesRequested`/`overlayToggled` from the same widgets the dialog did.

- [ ] **Step 4: Swap dialog → page in `MainWindow`**

In `main_window.h`: replace `class SettingsDialog;`/`SettingsDialog* settingsDialog_;` with `class SettingsPage;`/`SettingsPage* settingsPage_;`. In `main_window.cpp`: replace `settingsDialog_ = new SettingsDialog(this)` with `settingsPage_ = new SettingsPage`; keep the three `connect(settingsPage_, &SettingsPage::{rememberAuthRequested,manageDevicesRequested,overlayToggled}, …)`; add `connect(settingsPage_, &SettingsPage::themeChangeRequested, this, &MainWindow::setTheme);` and after applying the startup theme call `settingsPage_->setTheme(currentTheme_);`. Everywhere `collectSettings()`/`applySettings()` call `settingsDialog_->store/load`, call `settingsPage_->store/load`. Mount: `stack_->removeWidget(stack_->widget(3)); stack_->insertWidget(3, settingsPage_);`. Remove the header theme button added in Task 4 **or** keep it as a shortcut that calls `setTheme` and `settingsPage_->setTheme` — keep it; just ensure both stay in sync (have `setTheme` also call `settingsPage_->setTheme(t)`).

- [ ] **Step 5: Delete `SettingsDialog` + CMake swap**

Remove `gui/settings_dialog.cpp` from `droppix_gui` sources; add `gui/pages/settings_page.cpp`. Add `gui/tests/test_settings_page.cpp` + `gui/pages/settings_page.cpp` to `droppix_gui_tests`. `git rm host/gui/settings_dialog.h host/gui/settings_dialog.cpp`.

- [ ] **Step 6: Build + test**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake -S "/var/mnt/nas/Projects/Spacedesk for linux/host" -B ~/droppix-build >/dev/null && cmake --build ~/droppix-build --target droppix_gui droppix_gui_tests -j && QT_QPA_PLATFORM=offscreen ctest --test-dir ~/droppix-build -R SettingsPage --output-on-failure'`
Expected: PASS + `droppix_gui` links.

- [ ] **Step 7: Commit**

```bash
git add host/gui/pages/settings_page.* host/gui/tests/test_settings_page.cpp host/CMakeLists.txt host/gui/main_window.* 
git rm host/gui/settings_dialog.h host/gui/settings_dialog.cpp
git commit -m "feat(gui): Settings section page (surfaces hidden fields), retire SettingsDialog"
```

---

### Task 9: Finalize Status hero, empty states, docs

Turn page-0's remainder into the real Status page (server **toggle switch** replacing the two Start/Stop buttons, live metrics, scan-to-pair card), add empty states, and update living docs.

**Files:**
- Create: `host/gui/pages/status_page.h`, `host/gui/pages/status_page.cpp`
- Modify: `host/gui/main_window.h` + `.cpp` (server toggle instead of `serverStartBtn_`/`serverStopBtn_`; `updateServerButton()`/`updateStatus()` drive the hero; metrics), `host/CMakeLists.txt`
- Docs: `docs/STATUS.md`, `docs/ARCHITECTURE.md`, `docs/lessons/` (new entry), spec Status header

**Interfaces:**
- Consumes: `profileBox_` (+ save/saveAs/delete buttons — promote to members `profSaveBtn_`/`profSaveAsBtn_`/`profDeleteBtn_`), `statusDot_`, `streamLabel_`, `statsLabel_`, `pairingScanCaption_`, `pairingScanQr_`, `onServerToggled(bool)`.
- Produces: `class StatusPage : public QWidget { public: StatusPage(QComboBox* profile, QPushButton* save, QPushButton* saveAs, QPushButton* del, QLabel* dot, QLabel* stateText, QLabel* stats, QLabel* scanCaption, QLabel* scanQr, QWidget* parent=nullptr); QPushButton* serverSwitch(); void setMetrics(int monitors,int clients,int ifaces); };` The `serverSwitch()` is a checkable `QPushButton#serverSwitch` created by the page.

- [ ] **Step 1: Replace the two server buttons with one toggle**

In `main_window.h` remove `serverStartBtn_`/`serverStopBtn_`; the toggle lives on `StatusPage` (`serverSwitch()`). In `main_window.cpp` delete the `serverStartBtn_`/`serverStopBtn_` creation (`main_window.cpp:235-239`) and the `serverBtnRow`. `updateServerButton()` now sets the switch: `auto* sw = statusPage_->serverSwitch(); sw->setChecked(serverEnabled_); sw->setProperty("on", serverEnabled_); sw->setText(serverEnabled_ ? "Server ON" : "Server OFF"); sw->style()->unpolish(sw); sw->style()->polish(sw);`. Wire `connect(statusPage_->serverSwitch(), &QPushButton::toggled, this, &MainWindow::onServerToggled);` (guard re-entrancy if `onServerToggled` calls `updateServerButton` — block signals around the programmatic `setChecked`).

- [ ] **Step 2: Implement `StatusPage`** — a hero `QFrame#card` (beacon `statusDot_` + `stateText` `QLabel#stateWord` + the `serverSwitch()` + a sub-line), a metrics row of three `QLabel#metricNum` + captions (`setMetrics` updates them), the profile row (`profile` + 3 buttons), and a scan-to-pair `QFrame#card` (`scanCaption` + `scanQr`). Add `#include`s; promote the three profile buttons to members in Step 1.

- [ ] **Step 3: Mount + drive metrics**

Build `statusPage_` (member `StatusPage* statusPage_`), `stack_->removeWidget(stack_->widget(0)); stack_->insertWidget(0, statusPage_);`. In `updateStatus()` compute counts and call `statusPage_->setMetrics(activeMonitors, connectedClients, interfacesUp)`; keep the existing `setStatusDot`/`streamLabel_`/`statsLabel_` updates (the labels now live in the hero via the passed pointers).

- [ ] **Step 4: Empty states**

Connections: when `monitorsList_->count()==0` show a muted "No active monitors — connect a device to extend your desktop" placeholder row (add/remove a single item). Devices: when empty, a muted "Searching for tablets…" item. Status hero when server off: metrics show 0 and sub-line "Server stopped".

- [ ] **Step 5: Build + full suites**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake -S "/var/mnt/nas/Projects/Spacedesk for linux/host" -B ~/droppix-build >/dev/null && cmake --build ~/droppix-build -j && QT_QPA_PLATFORM=offscreen ctest --test-dir ~/droppix-build --output-on-failure'`
Expected: `droppix_gui` links; full `droppix_tests` + `droppix_gui_tests` green (incl StyleTheme, ThemePref, AboutPage, SettingsPage).

- [ ] **Step 6: Docs**

- `docs/STATUS.md`: add a "Host GUI" row — redesigned into spacedesk-style sections (Status/Connections/Interfaces/Settings/About), Status hero, dark+light themes with a persisted toggle, surfaced resolution/fps/audio/orientation settings; bump Last verified.
- `docs/ARCHITECTURE.md`: note the GUI is now a `QStackedWidget` of `gui/pages/*` section widgets themed via `styleSheet(Theme)`.
- `docs/lessons/`: add a gotcha entry — QSS `[property="…"]` selectors need `style()->unpolish/polish()` after `setProperty` to re-apply (nav "current" + server switch "on"); add the row to `docs/lessons/INDEX.md`.
- Spec `docs/superpowers/specs/2026-08-03-host-gui-redesign-design.md`: flip Status header to `Shipped on master (<date>)`.

- [ ] **Step 7: Commit**

```bash
git add host/gui/pages/status_page.* host/gui/main_window.* host/CMakeLists.txt docs/STATUS.md docs/ARCHITECTURE.md docs/lessons/ docs/superpowers/specs/2026-08-03-host-gui-redesign-design.md
git commit -m "feat(gui): Status hero + metrics + empty states; host GUI redesign complete"
```

---

## Self-Review

**Spec coverage:** Reference/direction → Tasks 1,4 (nav + teal + flat cards). 5-section nav + stacked → Tasks 4–9. Status hero (toggle+metrics+scan) → Task 9. Interfaces-as-cards with IPs → Task 6. Settings inline + surfaced hidden fields + retire dialog → Task 8. Dual theme + persisted toggle → Tasks 1–3 (+ toggle in header Task 4 / Settings Task 8). File split into `gui/pages/*` → Tasks 5–9. Testing (theme slivers + keep green + offscreen) → Tasks 1,2,8 + build/ctest gates. Preserved behavior (tray/polkit/auto-connect/pairing) → wiring kept in `MainWindow` throughout. Empty states → Task 9. No gaps.

**Placeholder scan:** No TBD/TODO; every code step has real code or exact move anchors (`main_window.cpp` line ranges) for relocations.

**Type consistency:** `Theme` (Qt-free, `theme.h`) used identically by `styleSheet(Theme)`, `theme_pref`, `MainWindow::setTheme`, `SettingsPage`. `styleSheet(Theme)` single-arg everywhere (old zero-arg removed in Task 1, caller fixed same task). Page ctor signatures in each task's Interfaces match their mount call. `serverSwitch()`/`setMetrics()`/`load()`/`store()`/`setTheme()` names consistent across Tasks 8–9.

**Note on realization:** thin container pages (wiring centralized in `MainWindow`) rather than signal-owning pages — deliberate, flagged in the header; identical user-facing result.
