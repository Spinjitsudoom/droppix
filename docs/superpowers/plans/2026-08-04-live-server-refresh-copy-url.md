# Live Server Refresh + Prominent Copy URL Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** When the host Server is ON, restart the server listener (debounced) whenever a streamer-affecting setting or Interfaces toggle changes so it applies immediately; and surface the full web-client URL + a Copy-URL button on Status + Interfaces whenever the server + web client are on.

**Architecture:** `SettingsPage` gains a `settingsChanged()` signal wired only to streamer controls (signal-blocked during `load()`). `MainWindow` debounces that signal + the Interfaces toggles into `refreshServer()`, which restarts the server listener when running. A `currentWebUrl()` helper feeds the existing Interfaces web-client card and a new StatusPage Copy-URL button. Host GUI only.

**Tech Stack:** C++17, Qt6 Widgets/Test, GoogleTest, CMake (build in the `droppix-dev` distrobox).

## Global Constraints

- **Build/test in distrobox:** `distrobox enter droppix-dev -- bash -lc '<cmd>'`; build dir `~/droppix-build`; configure once: `cmake -S "/var/mnt/nas/Projects/Spacedesk for linux/host" -B ~/droppix-build`. GUI tests run with `QT_QPA_PLATFORM=offscreen`.
- **NEVER timeout-kill a `droppix_gui` launch** (it regenerates its TLS cert each launch; a killed launch wipes it; XDG isolation doesn't work through distrobox). Verify by building, not launching.
- **Streamer-trigger set** (controls that emit `settingsChanged` / trigger a restart): source (`srcTest_`/`srcEvdi_`), `resolution_`, `fps_`, `bitrate_`, `port_`, `refresh_`, `touch_`, `audio_`, `orientation_`, `webClient_`. **NOT** `overlay_` (applied live via stdin — its own `overlayToggled` path), `autoConnect_` (discovery behavior), `themeDark_`/`themeLight_`, `launchAtLogin_`, `minimizeOnClose_`.
- **`SettingsPage::load()` must NOT emit `settingsChanged`** (else a profile switch triggers a refresh storm) — block signals on the streamer controls during `load()`.
- **Debounce** = single-shot 600 ms, coalescing rapid edits into one restart.
- **Restart scope** = the server listener session only, and only when `serverEnabled_`; server OFF → no-op.
- **Copy target** = the full `https://ip:port` URL.
- **Host GUI only** — no change to `host/src/**` (protocol/streamer/wire), Android, client, or packaging. Keep `droppix_tests` + `droppix_gui_tests` green.

---

### Task 1: `SettingsPage::settingsChanged()` signal + signal-blocked `load()`

**Files:**
- Modify: `host/gui/pages/settings_page.h` (signal decl; objectNames noted below), `host/gui/pages/settings_page.cpp` (connect streamer controls; block during `load`)
- Test: `host/gui/tests/test_settings_page.cpp` (extend — already in `droppix_gui_tests`)

**Interfaces:**
- Produces: `void SettingsPage::settingsChanged();` (Qt signal) — emitted on any streamer-control change, **not** during `load()`, **not** by app-pref controls. Consumed by `MainWindow` in Task 2.

- [ ] **Step 1: Write the failing test** — append to `host/gui/tests/test_settings_page.cpp` (reuse the file's existing `ensureQApplication()` helper):
```cpp
#include <QSignalSpy>
#include <QSpinBox>
#include <QRadioButton>
// ... inside the test translation unit:
TEST(SettingsPage, EmitsSettingsChangedOnStreamerControl) {
  ensureQApplication();
  droppix::SettingsPage page;
  QSignalSpy spy(&page, &droppix::SettingsPage::settingsChanged);
  auto* bitrate = page.findChild<QSpinBox*>("bitrateSpin");
  ASSERT_NE(bitrate, nullptr);
  bitrate->setValue(bitrate->value() + 1000);
  EXPECT_GE(spy.count(), 1);
}
TEST(SettingsPage, AppPrefControlsDoNotEmitSettingsChanged) {
  ensureQApplication();
  droppix::SettingsPage page;
  QSignalSpy spy(&page, &droppix::SettingsPage::settingsChanged);
  auto* dark = page.findChild<QRadioButton*>("themeDark");
  ASSERT_NE(dark, nullptr);
  dark->setChecked(true);
  EXPECT_EQ(spy.count(), 0);
}
TEST(SettingsPage, LoadDoesNotEmitSettingsChanged) {
  ensureQApplication();
  droppix::SettingsPage page;
  QSignalSpy spy(&page, &droppix::SettingsPage::settingsChanged);
  droppix::Settings s; s.bitrate_kbps = 16000; s.port = 34000; s.webClient = true;
  page.load(s);
  EXPECT_EQ(spy.count(), 0);
}
```

- [ ] **Step 2: Run it — FAIL**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake -S "/var/mnt/nas/Projects/Spacedesk for linux/host" -B ~/droppix-build >/dev/null && cmake --build ~/droppix-build --target droppix_gui_tests -j'`
Expected: compile FAIL — `settingsChanged` not a member / `findChild` targets have no objectName.

- [ ] **Step 3: Declare the signal + objectNames**

`settings_page.h`: under `signals:`, add `void settingsChanged();`.
`settings_page.cpp` ctor: give the two test-poked controls stable objectNames — `bitrate_->setObjectName("bitrateSpin");` (after `bitrate_` is created, ~line 62) and `themeDark_->setObjectName("themeDark");` (after `themeDark_` is created, ~line 118).

- [ ] **Step 4: Connect the streamer controls to emit `settingsChanged`**

`settings_page.cpp` ctor, after the streamer controls exist (and after the existing `overlay_`/theme connects), add — bind ONLY the streamer-trigger set:
```cpp
  for (QRadioButton* r : {srcTest_, srcEvdi_})
    connect(r, &QRadioButton::toggled, this, &SettingsPage::settingsChanged);
  for (QComboBox* c : {resolution_, fps_, refresh_, orientation_})
    connect(c, &QComboBox::currentIndexChanged, this, &SettingsPage::settingsChanged);
  for (QSpinBox* s : {bitrate_, port_})
    connect(s, QOverload<int>::of(&QSpinBox::valueChanged), this, &SettingsPage::settingsChanged);
  for (QCheckBox* cb : {touch_, audio_, webClient_})
    connect(cb, &QCheckBox::toggled, this, &SettingsPage::settingsChanged);
```
(Do NOT connect `overlay_`, `autoConnect_`, `themeDark_`, `themeLight_`, `launchAtLogin_`, `minimizeOnClose_`.) Add `#include <QRadioButton>`/`<QSpinBox>`/`<QComboBox>`/`<QCheckBox>` if not already present.

- [ ] **Step 5: Block signals during `load()`**

In `SettingsPage::load(const Settings& s)` (~line 164), before assigning the streamer controls, add signal blockers so programmatic loads don't emit (mirrors the existing theme `QSignalBlocker` at ~line 207):
```cpp
  const QSignalBlocker bSrcT(srcTest_), bSrcE(srcEvdi_), bRes(resolution_), bFps(fps_),
      bBit(bitrate_), bPort(port_), bRef(refresh_), bTouch(touch_), bAudio(audio_),
      bOri(orientation_), bWeb(webClient_);
```
(These blockers live for the whole function scope, covering all the control assignments in `load()`.)

- [ ] **Step 6: Run tests — PASS**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake --build ~/droppix-build --target droppix_gui_tests -j && QT_QPA_PLATFORM=offscreen ctest --test-dir ~/droppix-build -R SettingsPage --output-on-failure'`
Expected: PASS (existing `SettingsPage.*` + the 3 new).

- [ ] **Step 7: Commit**
```bash
git add host/gui/pages/settings_page.h host/gui/pages/settings_page.cpp host/gui/tests/test_settings_page.cpp
git commit -m "feat(gui): SettingsPage.settingsChanged signal (streamer controls, signal-blocked load)"
```

---

### Task 2: `MainWindow` debounced `refreshServer()` + trigger wiring

**Files:**
- Modify: `host/gui/main_window.h` (members + method decls), `host/gui/main_window.cpp` (ctor timer/connect; `onLanToggled`/`onUsbToggled`; adapter lambda; `refreshServer`/`scheduleServerRefresh`)
- No unit test (timer + live session — not unit-testable); verification = build-green.

**Interfaces:**
- Consumes: `SettingsPage::settingsChanged()` (Task 1); existing `serverEnabled_`, `startServerSession()`, `stopServerSession()`, `refreshWebClientUi()`, `updateStatus()`.
- Produces: `void MainWindow::scheduleServerRefresh();` and `void MainWindow::refreshServer();` — consumed by Task 3's UI updates (they call `refreshWebClientUi()`).

- [ ] **Step 1: Declare members + methods**

`main_window.h`: add `#include <QTimer>` (likely already present). In members add `QTimer serverRefreshTimer_;`. In private methods add `void scheduleServerRefresh(); void refreshServer();`.

- [ ] **Step 2: Set up the debounce timer + wire the signal in the ctor**

`main_window.cpp` ctor (near the other timer setup, e.g. after `autoConnectTimer_` config): 
```cpp
  serverRefreshTimer_.setSingleShot(true);
  serverRefreshTimer_.setInterval(600);
  connect(&serverRefreshTimer_, &QTimer::timeout, this, &MainWindow::refreshServer);
```
Where `settingsPage_`'s other signals are connected (~lines 190-200), add:
```cpp
  connect(settingsPage_, &SettingsPage::settingsChanged, this, &MainWindow::scheduleServerRefresh);
```

- [ ] **Step 3: Implement `scheduleServerRefresh` + `refreshServer`**

Add (near `updateServerButton`/`startServerSession`):
```cpp
void MainWindow::scheduleServerRefresh() {
  serverRefreshTimer_.start();   // (re)start the debounce; coalesces rapid edits
}
void MainWindow::refreshServer() {
  if (!serverEnabled_) return;   // nothing to restart; settings apply on next Start
  stopServerSession();
  startServerSession();          // reads fresh collectSettings() -> new --web/port/args
  refreshWebClientUi();
  updateStatus();
}
```

- [ ] **Step 4: Fire it from the Interfaces toggles**

`onLanToggled(bool)` and `onUsbToggled(bool)`: add `scheduleServerRefresh();` as the last line of each.
In `refreshInterfaces()`, the per-adapter checkbox `toggled` lambda (currently updates `excludedAdapters_` + `saveInterfacePrefs()`): append `refreshWebClientUi(); scheduleServerRefresh();` inside that lambda so excluding/including an adapter re-serves + updates the URL.

- [ ] **Step 5: Build — verify it compiles + links**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake --build ~/droppix-build --target droppix_gui -j'`
Expected: links clean. (Manual, user: Server ON + a tablet connected → toggle web client / change bitrate → ~0.6 s later the server restarts and the tablet reconnects; Server OFF → no restart.)

- [ ] **Step 6: Commit**
```bash
git add host/gui/main_window.h host/gui/main_window.cpp
git commit -m "feat(gui): debounced server refresh on streamer-setting + interface changes"
```

---

### Task 3: `currentWebUrl()` + StatusPage Copy-URL button + docs

**Files:**
- Modify: `host/gui/main_window.h` + `.cpp` (`currentWebUrl()`, `statusCopyUrlBtn_`, refactor `refreshWebClientUi`), `host/gui/pages/status_page.h` + `.cpp` (ctor gains a copy button)
- Docs: `docs/STATUS.md`, `docs/superpowers/specs/2026-08-04-live-server-refresh-copy-url-design.md`

**Interfaces:**
- Consumes: existing `refreshWebClientUi()`, `included_ifaces`/`lan_ipv4_ifaces`/`session_web_url` (in `web_url.*`/`lan_ifaces.*`), `sessions_`, `excludedAdapters_`, `lanEnabled_`.
- Produces: `QString MainWindow::currentWebUrl() const` — the `https://ip:port` for the newest non-AOA session when `webClient && lanEnabled_ && sessions_.count() > 0`, else `""`.

- [ ] **Step 1: Extract `currentWebUrl()`**

`main_window.h`: declare `QString currentWebUrl() const;`.
`main_window.cpp`: implement by lifting the compute out of `refreshWebClientUi()`:
```cpp
QString MainWindow::currentWebUrl() const {
  Settings s = collectSettings();
  if (!s.webClient || !lanEnabled_ || sessions_.count() == 0) return {};
  const Session& sess = sessions_.list().last();
  if (sess.transport == "usb-aoa") return {};       // web client unavailable over AOA
  const auto inc = included_ifaces(lan_ipv4_ifaces(), excludedAdapters_);
  const QString ip = inc.isEmpty() ? QStringLiteral("127.0.0.1") : inc.first().ip;
  return session_web_url(ip, sess.port);
}
```
(`collectSettings()` is non-const-friendly? it's already `const` per `Settings MainWindow::collectSettings() const`. If a const-correctness issue arises, keep `currentWebUrl()` non-const — adjust the header decl to match.)

- [ ] **Step 2: Refactor `refreshWebClientUi()` to use it**

Replace the inline compute in `refreshWebClientUi()` with `const QString url = currentWebUrl();`. Keep the existing branch structure: the AOA "Web client unavailable for USB/AOA sessions" label (check `sess.transport == "usb-aoa"` there as today); when `url.isEmpty()` for other reasons → hide `webUrlLabel_`/`webQrLabel_`/`webCopyBtn_`; else set `webUrlLabel_->setText(url)` + QR + show. Change the `webCopyBtn_` click lambda (~line 244) to copy `currentWebUrl()` instead of `webUrlLabel_->text()` (equivalent, but authoritative).

- [ ] **Step 3: Add the Copy-URL button to StatusPage**

`status_page.h`: extend the ctor signature with a trailing `QPushButton* copyUrl` param (before `parent`):
```cpp
StatusPage(QComboBox* profile, QPushButton* save, QPushButton* saveAs, QPushButton* del,
           QLabel* dot, QLabel* stateText, QLabel* stats,
           QLabel* scanCaption, QLabel* scanQr, QPushButton* copyUrl,
           QWidget* parent = nullptr);
```
`status_page.cpp`: place `copyUrl` in the scan-to-pair card (e.g. below `scanQr`/`scanCaption`) so the web URL copy sits with the pairing affordances. It's a normal button; visibility is driven by `MainWindow`.

- [ ] **Step 4: Create + wire the button in `MainWindow`**

`main_window.h`: add member `QPushButton* statusCopyUrlBtn_ = nullptr;`.
`main_window.cpp`: where the profile/status widgets are created for the StatusPage (before the `new StatusPage(...)` call), add:
```cpp
  statusCopyUrlBtn_ = new QPushButton("Copy web URL");
  statusCopyUrlBtn_->hide();
  connect(statusCopyUrlBtn_, &QPushButton::clicked, this, [this]{
    const QString u = currentWebUrl();
    if (!u.isEmpty()) QGuiApplication::clipboard()->setText(u);
  });
```
Pass `statusCopyUrlBtn_` as the new ctor arg in the `new StatusPage(...)` construction. In `refreshWebClientUi()` (which already runs on session/setting changes), toggle it: `statusCopyUrlBtn_->setVisible(!currentWebUrl().isEmpty());`.

- [ ] **Step 5: Build + full suites**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake -S "/var/mnt/nas/Projects/Spacedesk for linux/host" -B ~/droppix-build >/dev/null && cmake --build ~/droppix-build -j && QT_QPA_PLATFORM=offscreen ctest --test-dir ~/droppix-build --output-on-failure'`
Expected: `droppix_gui` links; full `droppix_tests` + `droppix_gui_tests` green (incl. Task 1's SettingsPage tests). (Manual: Server ON + web client on → the Status "Copy web URL" button + the Interfaces card appear without a tablet connected; clicking copies `https://ip:port`.)

- [ ] **Step 6: Docs**

- `docs/STATUS.md`: add a short note (in the Host GUI row or a new feature-matrix row) — "Live server refresh: changing a streamer setting or interface toggle while the Server is ON restarts the listener (debounced) to apply immediately; a prominent Copy-web-URL button on Status + Interfaces." Bump Last verified to 2026-08-04.
- Flip the spec `docs/superpowers/specs/2026-08-04-live-server-refresh-copy-url-design.md` Status header to `**Status:** Shipped on master (2026-08-04).`

- [ ] **Step 7: Commit**
```bash
git add host/gui/main_window.h host/gui/main_window.cpp host/gui/pages/status_page.h host/gui/pages/status_page.cpp docs/STATUS.md docs/superpowers/specs/2026-08-04-live-server-refresh-copy-url-design.md
git commit -m "feat(gui): currentWebUrl() + prominent Copy web URL on Status & Interfaces"
```

---

## Self-Review

**Spec coverage:** Live refresh on any streamer setting → Task 1 (signal) + Task 2 (debounce/restart). Interfaces toggles trigger it → Task 2 (onLanToggled/onUsbToggled/adapter lambda). App-prefs excluded → Task 1 (only streamer controls connected; test asserts theme doesn't emit). `load()` no-emit → Task 1 (signal-blocked; tested). Debounce 600 ms + server-off no-op → Task 2. Restart = server listener only → Task 2 (`refreshServer` uses stop/startServerSession). Copy full URL, prominent on Status + Interfaces → Task 3 (`currentWebUrl` + StatusPage button + refactored `refreshWebClientUi`). Copy target = full URL → Task 3. Docs → Task 3. No gaps.

**Placeholder scan:** No TBD/TODO; full code for the signal wiring, blockers, `refreshServer`, `currentWebUrl`, and the button. Line anchors (`~62`, `~118`, `~164`, `~190-200`, `~244`) are approximate — the implementer confirms exact lines by reading the file.

**Type consistency:** `settingsChanged()` (Task 1) is the exact signal `MainWindow` connects in Task 2. `scheduleServerRefresh()`/`refreshServer()` names consistent Task 2↔plan. `currentWebUrl()` returns `QString` ("" when unavailable) and is used identically by `refreshWebClientUi`, the Interfaces copy lambda, and the Status button (Task 3). The `StatusPage` ctor's new trailing `QPushButton* copyUrl` param matches the `new StatusPage(...)` call updated in Task 3 Step 4. `statusCopyUrlBtn_` created before it's passed to the ctor.
