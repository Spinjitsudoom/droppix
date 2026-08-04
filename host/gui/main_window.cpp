#include "main_window.h"
#include "args_builder.h"
#include "web_url.h"
#include "qr_generator.h"
#include "log_forwarder.h"
#include "log_classify.h"
#include "server_control.h"
#include "lan_ifaces.h"
#include "pages/about_page.h"
#include "pages/interfaces_page.h"
#include "pages/connections_page.h"
#include "pages/settings_page.h"
#include "pages/status_page.h"
#include "style.h"
#include "theme_pref.h"
#include <QApplication>
#include <QTextStream>
#include <QtWidgets>
#include <QStackedWidget>
#include <QStyle>
#include <QClipboard>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QProcess>
#include <QTemporaryFile>
#include <QFile>
#include <QUdpSocket>
#include <QHostAddress>
#include <QDateTime>
#include <QTimer>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QProcess>
#include <algorithm>
#include "wake.h"
#include "auto_connect.h"

namespace droppix {

static QString configDir() {
  return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
}

// Copy src -> dst, overwriting; returns success. (QFile::copy won't overwrite.)
static bool copyOver(const QString& src, const QString& dst) {
  QFile::remove(dst);
  return QFile::copy(src, dst);
}

// --- Flatpak host bridge -------------------------------------------------------------
// In a Flatpak the sandbox can't run droppix's root/evdi streamer or reach host KWin/
// polkit/PipeWire/adb. The GUI reaches the host via `flatpak-spawn --host` (PATH shims
// forward pkexec/adb/avahi; here we stage the streamer onto the host and mirror the cert).
static bool inFlatpak() { return QFileInfo::exists("/.flatpak-info"); }

static QString hostSpawnCapture(const QStringList& args) {   // `flatpak-spawn --host <args>` -> stdout
  QProcess p;
  p.start("flatpak-spawn", QStringList{"--host"} + args);
  if (!p.waitForFinished(15000)) return {};
  return QString::fromUtf8(p.readAllStandardOutput());
}

static QString shq(const QString& s) {   // single-quote for a host `sh -c`
  QString q = s; q.replace("'", "'\\''"); return "'" + q + "'";
}

static void copyFileToHost(const QString& src, const QString& hostDst) {   // bytes over the spawn pipe
  QFile f(src);
  if (!f.open(QIODevice::ReadOnly)) return;
  QProcess p;
  p.start("flatpak-spawn", {"--host", "sh", "-c", "cat > " + shq(hostDst)});
  if (p.waitForStarted(5000)) { p.write(f.readAll()); p.closeWriteChannel(); p.waitForFinished(10000); }
}

// When running from an AppImage, the bundled droppix_stream sits on a per-run FUSE mount
// that root (pkexec) can't read and whose path changes each launch (breaking the permanent
// polkit rule). Relocate it + its bundled libs to a stable, real path and use that. The
// bundled binary's RPATH is $ORIGIN/../lib, so from runtime/bin it finds runtime/lib —
// no LD_LIBRARY_PATH or wrapper needed, and it works when pkexec'd as root.
std::string MainWindow::resolveStreamBin() {
  const QString dev = QCoreApplication::applicationDirPath() + "/droppix_stream";

  if (inFlatpak()) {
    // Stage the bundled streamer runtime (droppix_stream + libs + LD_LIBRARY_PATH wrapper)
    // onto the HOST, then run it there via the pkexec shim (flatpak-spawn --host). The host
    // can't see /app, so we transfer the tarball's bytes over the flatpak-spawn stdio pipe.
    const QString hostHome = hostSpawnCapture({"sh", "-c", "printf %s \"$HOME\""}).trimmed();
    if (hostHome.isEmpty()) return dev.toStdString();
    const QString rt = hostHome + "/.local/share/droppix/runtime";
    QProcess p;
    p.start("flatpak-spawn", {"--host", "sh", "-c",
            "mkdir -p " + shq(rt) + " && tar xzf - -C " + shq(rt)});
    if (p.waitForStarted(5000)) {
      QFile tar("/app/share/droppix/droppix-runtime.tar.gz");
      if (tar.open(QIODevice::ReadOnly)) { p.write(tar.readAll()); tar.close(); }
      p.closeWriteChannel();
      p.waitForFinished(30000);
    }
    flatpakHostRuntime_ = rt;
    flatpakHostWeb_ = rt + "/web";
    return (rt + "/bin/droppix_stream_host").toStdString();
  }

  const QString appdir = qEnvironmentVariable("APPDIR");
  if (appdir.isEmpty()) return dev.toStdString();   // not an AppImage: use the sibling binary

  const QString srcBin = appdir + "/usr/bin/droppix_stream";
  const QString srcLib = appdir + "/usr/lib";
  if (!QFileInfo::exists(srcBin)) return dev.toStdString();   // nothing bundled -> fall back

  const QString runtime = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                          + "/droppix/runtime";
  const QString dstBin = runtime + "/bin/droppix_stream";
  const QString dstLib = runtime + "/lib";
  const QString srcWeb = appdir + "/usr/share/droppix/web";
  const QString dstWeb = runtime + "/web";

  // (Re)extract only when the destination is missing or older than the bundled copy.
  if (!QFileInfo::exists(dstBin) ||
      QFileInfo(dstBin).lastModified() < QFileInfo(srcBin).lastModified()) {
    QDir().mkpath(runtime + "/bin");
    QDir().mkpath(dstLib);
    copyOver(srcBin, dstBin);
    QFile::setPermissions(dstBin, QFileDevice::ReadOwner  | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
                                   QFileDevice::ReadGroup  | QFileDevice::ExeGroup |
                                   QFileDevice::ReadOther  | QFileDevice::ExeOther);
    const auto libs = QDir(srcLib).entryInfoList({"*.so*"}, QDir::Files);
    for (const auto& fi : libs) copyOver(fi.absoluteFilePath(), dstLib + "/" + fi.fileName());
  }
  // Stage web PWA next to the relocated streamer so pkexec/root can read --web-root.
  if (QFileInfo::exists(srcWeb + "/index.html")) {
    QDir(dstWeb).removeRecursively();
    QDir().mkpath(dstWeb);
    const auto files = QDir(srcWeb).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    // QDir::mkpath + recursive copy via process (portable enough for AppImage staging)
    QProcess::execute("cp", {"-a", srcWeb + "/.", dstWeb + "/"});
  }
  return QFileInfo::exists(dstBin) ? dstBin.toStdString() : dev.toStdString();
}

// Dev/local builds: the AppImage/Flatpak paths stage web/ next to the relocated streamer,
// but a plain build runs the sibling streamer with no staging — so the GUI can't hand it a
// --web-root and silently omits --web. Copy the source-tree web/dist (baked at build time)
// into the root-readable runtime dir so the pkexec streamer can serve it. No-op when the
// source dir isn't present (packaged builds) or the staged copy is already current.
void MainWindow::stageWebAssets() {
#ifdef DROPPIX_SOURCE_WEB_DIR
  const QString src = QDir(QString::fromUtf8(DROPPIX_SOURCE_WEB_DIR)).absolutePath();
  if (!QFileInfo::exists(src + "/index.html")) return;
  const QString dstWeb = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                         + "/droppix/runtime/web";
  const QFileInfo dstIdx(dstWeb + "/index.html");
  const QFileInfo srcIdx(src + "/index.html");
  if (dstIdx.exists() && dstIdx.lastModified() >= srcIdx.lastModified()) return;   // already current
  QDir().mkpath(dstWeb);
  QProcess::execute("cp", {"-a", src + "/.", dstWeb + "/"});
  qInfo("staged web client assets -> %s", qUtf8Printable(dstWeb));
#endif
}

// Flatpak: mirror the freshly generated cert/key onto the host so the host-run streamer
// (--cert/--key) can read them. No-op outside Flatpak (flatpakHostRuntime_ empty).
void MainWindow::stageCertsToHost() {
  if (flatpakHostRuntime_.isEmpty()) return;
  flatpakHostCert_ = flatpakHostRuntime_ + "/cert.pem";
  flatpakHostKey_  = flatpakHostRuntime_ + "/key.pem";
  copyFileToHost(cert_.certPath(), flatpakHostCert_);
  copyFileToHost(cert_.keyPath(),  flatpakHostKey_);
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      store_(configDir()),
      approved_(configDir()),
      knownAoa_(configDir()),
      cert_(configDir()) {
  streamBin_ = resolveStreamBin();
  cert_.regenerate();   // fresh cert => new pairing code every launch (per-restart rotation)
  stageCertsToHost();   // Flatpak: mirror cert/key to the host for the streamer (else no-op)
  setWindowTitle("Droppix");
  setWindowIcon(QIcon(":/icon.png"));
  settingsPage_ = new SettingsPage;   // Settings section (stack_ index 3)

  // --- Header: logo + wordmark (theme toggle button added lower, after headerRow exists) ---
  auto* logo = new QLabel; logo->setObjectName("logo");
  logo->setPixmap(QPixmap(":/logo.png").scaled(36, 36, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  auto* title = new QLabel("Droppix"); title->setObjectName("header");

  connect(settingsPage_, &SettingsPage::rememberAuthRequested, this, &MainWindow::setupAuth);
  connect(settingsPage_, &SettingsPage::manageDevicesRequested, this, &MainWindow::manageDevices);
  // Perf-overlay checkbox applies live: if a stream is running, push "overlay N" to the
  // streamer's stdin so the tablet shows/hides it without a restart. store()/load() still
  // persist the setting for the next launch.
  connect(settingsPage_, &SettingsPage::overlayToggled, this, [this](bool on){
    for (auto& s : sessions_.list())
      if (s.controller && s.controller->running())
        s.controller->writeLine(QString("overlay %1").arg(on ? 1 : 0));
  });
  connect(settingsPage_, &SettingsPage::themeChangeRequested, this, &MainWindow::setTheme);
  // Streamer-setting edits (source/resolution/bitrate/port/...) debounce into a live
  // server restart so a running listener picks them up without a manual Stop/Start.
  connect(settingsPage_, &SettingsPage::settingsChanged, this, &MainWindow::scheduleServerRefresh);

  auto* headerRow = new QHBoxLayout;
  headerRow->addWidget(logo); headerRow->addSpacing(10);
  headerRow->addWidget(title); headerRow->addStretch();

  // --- Profile row (widgets only; StatusPage lays them out into the Status hero) ---
  profileBox_ = new QComboBox;
  profSaveBtn_ = new QPushButton("Save");
  profSaveAsBtn_ = new QPushButton("Save As");
  profDeleteBtn_ = new QPushButton("Delete");

  // (ALL stream/session/application options — including resolution/fps/audio/
  // orientation, which are otherwise client-driven per session via HELLO — now live
  // in the Settings section (SettingsPage, stack_ index 3); see settingsPage_ below.)

  // --- Status hero widgets: beacon dot + state word + compact stats sub-line ---
  // Laid out by StatusPage (stack_ index 0), which also retags streamLabel_'s
  // objectName to "stateWord" (style.h's large hero-word selector) when it arranges
  // these into the hero card — see StatusPage's constructor.
  statusDot_   = new QLabel; statusDot_->setObjectName("statusDot");
  streamLabel_ = new QLabel("Stopped");
  statsLabel_  = new QLabel("—");       statsLabel_->setObjectName("statusStats");
  setStatusDot(kDotStopped);
  deviceLabel_ = new QLabel; deviceLabel_->setObjectName("caption");
  deviceLabel_->hide();   // reserved for a future discovery-status hint; unused — kept as a
                          // member but deliberately not added to any layout (Task 9 cleanup)

  // (auth setup moved to the Settings menu → "Remember authentication"; the old
  // Start/Stop button pair is gone — StatusPage creates a single server toggle switch
  // instead, see the statusPage_ construction + updateServerButton() below.)

  // --- Active monitors (one row per live streaming session) ---
  monitorsList_ = new QListWidget;
  monitorsList_->setMaximumHeight(96);
  stopMonBtn_ = new QPushButton("Stop selected");
  mirrorBtn_ = new QPushButton("Toggle mirror");
  webUrlLabel_ = new QLabel; webUrlLabel_->setObjectName("caption");
  webUrlLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  webUrlLabel_->hide();
  webQrLabel_ = new QLabel; webQrLabel_->setAlignment(Qt::AlignCenter); webQrLabel_->hide();
  webCopyBtn_ = new QPushButton("Copy web URL"); webCopyBtn_->hide();
  connect(webCopyBtn_, &QPushButton::clicked, this, [this]{
    if (!webUrlLabel_->text().isEmpty())
      QGuiApplication::clipboard()->setText(webUrlLabel_->text());
  });
  // Proactive scan-to-pair QR: shown while a WiFi session waits, so the tablet can
  // scan BEFORE connecting. Encodes droppix://<host LAN IP>:<port>?code (not the
  // client IP, and not tied to the reactive "device connecting" popup). Lives on
  // the Status page (page 0) — see Task 9 for scan-to-pair placement there.
  pairingScanCaption_ = new QLabel; pairingScanCaption_->setObjectName("caption");
  pairingScanCaption_->setAlignment(Qt::AlignCenter); pairingScanCaption_->setWordWrap(true);
  pairingScanCaption_->hide();
  pairingScanQr_ = new QLabel; pairingScanQr_->setAlignment(Qt::AlignCenter); pairingScanQr_->hide();
  connect(stopMonBtn_, &QPushButton::clicked, this, &MainWindow::stopSelectedMonitor);
  connect(mirrorBtn_, &QPushButton::clicked, this, &MainWindow::toggleSelectedMonitorMirror);

  // --- Devices on network (mDNS-discovered tablets) ---
  devicesList_ = new QListWidget;
  connectBtn_ = new QPushButton("Connect");

  // Page 0 (Status) is built by StatusPage below, once the stack_ exists to mount it into.

  // --- Communication interfaces (adapters + LAN/USB toggles) ---
  // Laid out by InterfacesPage (mounted at stack_ index 2); MainWindow still
  // creates + owns + wires these widgets.
  loadInterfacePrefs();
  lanToggle_ = new QCheckBox("Network (Wi-Fi / Ethernet)");
  lanToggle_->setChecked(lanEnabled_);
  adapterRows_ = new QVBoxLayout;
  adapterRows_->setContentsMargins(18, 0, 0, 0);   // indent adapters under the LAN toggle
  usbToggle_ = new QCheckBox("USB (adb + tether + AOA)");
  usbToggle_->setChecked(usbEnabled_);
  connect(lanToggle_, &QCheckBox::toggled, this, &MainWindow::onLanToggled);
  connect(usbToggle_, &QCheckBox::toggled, this, &MainWindow::onUsbToggled);
  refreshInterfaces();   // populate the per-adapter checkbox rows

  stack_ = new QStackedWidget;
  for (int i = 0; i < 5; ++i) stack_->addWidget(new QWidget);   // 0..4 placeholders

  auto* connPage = new ConnectionsPage(devicesList_, connectBtn_, monitorsList_,
                                        stopMonBtn_, mirrorBtn_);
  stack_->removeWidget(stack_->widget(1));
  stack_->insertWidget(1, connPage);

  auto* ifacesPage = new InterfacesPage(lanToggle_, adapterRows_, usbToggle_,
                                         webUrlLabel_, webQrLabel_, webCopyBtn_);
  stack_->removeWidget(stack_->widget(2));
  stack_->insertWidget(2, ifacesPage);

  stack_->removeWidget(stack_->widget(3));
  stack_->insertWidget(3, settingsPage_);

  auto* aboutPage = new AboutPage;
  stack_->removeWidget(stack_->widget(4));
  stack_->insertWidget(4, aboutPage);

  // StatusPage must exist before any updateServerButton()/updateStatus() call — both now
  // reach through statusPage_ to drive the hero. Neither runs synchronously before this
  // point in the ctor (the "restore last server state" call below is a deferred
  // QTimer::singleShot(0, ...), so it always runs after the ctor — and this line — return).
  statusPage_ = new StatusPage(profileBox_, profSaveBtn_, profSaveAsBtn_, profDeleteBtn_,
                                statusDot_, streamLabel_, statsLabel_,
                                pairingScanCaption_, pairingScanQr_);
  stack_->removeWidget(stack_->widget(0));
  stack_->insertWidget(0, statusPage_);

  static const char* kNav[5] = {"Status", "Connections", "Interfaces", "Settings", "About"};
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
  root->setContentsMargins(16, 16, 16, 16); root->setSpacing(12);
  root->addLayout(headerRow);
  root->addLayout(navRow);
  root->addWidget(stack_, 1);
  auto* central = new QWidget; central->setLayout(root);
  setCentralWidget(central);
  selectSection(0);
  resize(720, 640);

  // --- Debug log console ---
  logBuffer_ = new LogBuffer(this);
  installLogForwarder(logBuffer_);            // capture GUI qInfo/qWarning/qCritical too
  logPanel_ = new LogPanel(logBuffer_, this);
  addDockWidget(Qt::BottomDockWidgetArea, logPanel_);
  logPanel_->hide();                          // start hidden; toggle with the action below
  QAction* toggleLog = logPanel_->toggleViewAction();
  toggleLog->setText(tr("&Debug log"));
  toggleLog->setShortcut(QKeySequence(Qt::Key_F12));
  addAction(toggleLog);                       // make F12 work window-wide
  menuBar()->addMenu(tr("&View"))->addAction(toggleLog);

  stageWebAssets();   // ensure the PWA assets are where the (root) streamer can read them

  // --- Wiring ---
  // `clicked`, not `toggled`: updateServerButton() drives the switch programmatically via
  // setChecked(), which emits toggled but NOT clicked — so wiring onServerToggled() to
  // `clicked` (a user gesture only) can never re-enter itself through that setChecked().
  connect(statusPage_->serverSwitch(), &QPushButton::clicked, this,
          [this](bool on){ onServerToggled(on); });
  connect(profSaveBtn_, &QPushButton::clicked, this, [this]{
    const QString n = profileBox_->currentText();
    if (!n.isEmpty()) { store_.save(n, collectSettings()); store_.setLastUsed(n); refreshProfiles(); }
  });
  connect(profSaveAsBtn_, &QPushButton::clicked, this, [this]{
    bool ok; QString n = QInputDialog::getText(this, "Save profile", "Name:",
                                               QLineEdit::Normal, "", &ok);
    if (ok && !n.isEmpty()) { store_.save(n, collectSettings()); store_.setLastUsed(n);
                              refreshProfiles(); profileBox_->setCurrentText(n); }
  });
  connect(profDeleteBtn_, &QPushButton::clicked, this, [this]{
    if (!profileBox_->currentText().isEmpty()) { store_.remove(profileBox_->currentText()); refreshProfiles(); }
  });
  connect(profileBox_, &QComboBox::currentTextChanged, this, [this](const QString& n){
    Settings s; if (!n.isEmpty() && store_.load(n, s)) { applySettings(s); store_.setLastUsed(n); }
  });

  // Per-session signal wiring happens in wireSession() when each session is created.
  // Unified "Available clients" list: network (mDNS) + USB-tether (UDP probe) sources.
  connect(&browser_, &MdnsBrowser::devicesChanged, this, &MainWindow::onDevicesChanged);
  connect(&tetherScanner_, &TetherScanner::clientsChanged, this, &MainWindow::onTetherClientsChanged);
  connect(&aoaScanner_, &AoaScanner::clientsChanged, this, &MainWindow::onAoaClientsChanged);
  connect(connectBtn_, &QPushButton::clicked, this, &MainWindow::onConnectToSelectedDevice);
  connect(devicesList_, &QListWidget::itemDoubleClicked, this,
          [this](QListWidgetItem*){ onConnectToSelectedDevice(); });

  autoConnectTimer_.setSingleShot(true);
  autoConnectTimer_.setInterval(750);   // let a just-appeared tablet settle before WAKE
  connect(&autoConnectTimer_, &QTimer::timeout, this, &MainWindow::evaluateAutoConnect);

  serverRefreshTimer_.setSingleShot(true);
  serverRefreshTimer_.setInterval(600);   // coalesce rapid setting/interface edits into one restart
  connect(&serverRefreshTimer_, &QTimer::timeout, this, &MainWindow::refreshServer);

  if (lanEnabled_ && browser_.available()) browser_.start();
  if (usbEnabled_) {
    tetherScanner_.start();   // always available (no external tool); keeps the devices list live
    aoaScanner_.start();      // poll USB for AOA-capable tablets while idle
  }

  refreshProfiles();
  restoreLastProfile();   // re-apply the profile that was in use last launch
  refreshAdvertising();   // publish this PC on the network from launch (idle-discoverable)

  // Restore the Server toggle: if it was on last launch, auto-start after the window paints
  // (deferred so the pkexec prompt, if any, doesn't block the first frame).
  if (QFile::exists(configDir() + "/server_enabled"))
    QTimer::singleShot(0, this, [this]{ onServerToggled(true); });

  audioSink_.ensure();   // create/adopt the droppix-audio sink for this session

  currentTheme_ = loadThemePref(configDir().toStdString());
  qApp->setStyleSheet(droppix::styleSheet(currentTheme_));   // honor the saved choice on launch
  settingsPage_->setTheme(currentTheme_);   // keep the Settings theme control in sync at startup

  setupTray();           // tray icon for "minimize to tray on close" (if a tray exists)

  pairingHideTimer_ = new QTimer(this);
  pairingHideTimer_->setSingleShot(true);
  connect(pairingHideTimer_, &QTimer::timeout, this, &MainWindow::hidePairingPopup);
}

void MainWindow::showPairingPopup(const QString& ip, int port) {
  if (ip.isEmpty() || ip == "127.0.0.1") return;   // USB / AOA / localhost: no pairing code
  if (!pairingPopup_) {
    pairingPopup_ = new QDialog(this);
    pairingPopup_->setWindowTitle("Pairing");
    pairingPopup_->setModal(false);
    auto* v = new QVBoxLayout(pairingPopup_);
    pairingInfo_ = new QLabel; pairingInfo_->setAlignment(Qt::AlignCenter); pairingInfo_->setWordWrap(true);
    pairingQrLabel_ = new QLabel; pairingQrLabel_->setAlignment(Qt::AlignCenter); pairingQrLabel_->hide();
    pairingCodeLabel_ = new QLabel; pairingCodeLabel_->setAlignment(Qt::AlignCenter);
    pairingCodeLabel_->setStyleSheet("font-size:34px; font-weight:700; letter-spacing:6px; color:#14b8a6;");
    auto* closeBtn = new QPushButton("Close");
    connect(closeBtn, &QPushButton::clicked, this, &MainWindow::hidePairingPopup);
    v->addWidget(pairingInfo_);
    v->addWidget(pairingQrLabel_);
    v->addWidget(pairingCodeLabel_);
    v->addWidget(closeBtn);
  }
  const QString code = cert_.pairingCode();
  pairingCodeLabel_->setText(code);
  // Build a scannable QR (droppix://<host LAN IP>:port?code). NOTE: `ip` here is the
  // connecting CLIENT's address (for the info text); the QR must encode the HOST's
  // own LAN IP so the tablet dials the PC, not itself.
  bool qrShown = false;
  const auto inc = included_ifaces(lan_ipv4_ifaces(), excludedAdapters_);
  const QString hostIp = inc.isEmpty() ? QString() : inc.first().ip;
  if (port > 0 && !hostIp.isEmpty() && hostIp != "127.0.0.1") {
    const std::string uri = generate_qr_uri(hostIp.toStdString(), port, code.toStdString());
    const QImage qr = make_qr_image(QString::fromStdString(uri), 8);
    if (!qr.isNull()) {
      pairingQrLabel_->setPixmap(QPixmap::fromImage(qr));
      pairingQrLabel_->show();
      qrShown = true;
    }
  }
  if (!qrShown) pairingQrLabel_->hide();
  pairingInfo_->setText(QString("A device (%1) is connecting.\n%2")
      .arg(ip, qrShown ? "Scan the QR code on the tablet, or enter this pairing code:"
                       : "Enter this pairing code on the tablet:"));
  pairingPopup_->show();
  pairingPopup_->raise();
  pairingPopup_->activateWindow();
  pairingHideTimer_->start(90000);   // give up showing it after 90s if pairing never completes
}

void MainWindow::hidePairingPopup() {
  if (pairingHideTimer_) pairingHideTimer_->stop();
  if (pairingPopup_) pairingPopup_->hide();
}

void MainWindow::manageDevices() {
  QDialog dlg(this);
  dlg.setWindowTitle("Remembered devices");
  auto* v = new QVBoxLayout(&dlg);
  v->addWidget(new QLabel("Devices allowed to connect without asking:"));
  auto* list = new QListWidget;
  v->addWidget(list);
  auto refill = [this, list]{
    list->clear();
    const QStringList ids = approved_.ids();
    if (ids.isEmpty()) { auto* it = new QListWidgetItem("(none)"); it->setFlags(Qt::NoItemFlags); list->addItem(it); }
    else list->addItems(ids);
  };
  refill();
  auto* forgetSel = new QPushButton("Forget selected");
  auto* forgetAll = new QPushButton("Forget all");
  connect(forgetSel, &QPushButton::clicked, &dlg, [this, list, refill]{
    auto* it = list->currentItem();
    if (it && (it->flags() & Qt::ItemIsSelectable)) { approved_.remove(it->text()); refill(); }
  });
  connect(forgetAll, &QPushButton::clicked, &dlg, [this, refill]{ approved_.clear(); refill(); });
  auto* row = new QHBoxLayout;
  row->addWidget(forgetSel); row->addWidget(forgetAll); row->addStretch();
  v->addLayout(row);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
  connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  v->addWidget(buttons);
  dlg.exec();
}

void MainWindow::setTheme(Theme t) {
  currentTheme_ = t;
  qApp->setStyleSheet(droppix::styleSheet(t));   // qualified: QWidget::styleSheet() would win unqualified
  saveThemePref(configDir().toStdString(), t);
  // Keep the header theme button (Task 4) and the Settings theme control in sync. Null-
  // guarded: setTheme() can run (e.g. via the header button) before settingsPage_ exists
  // is not expected in practice, but this stays defensive since ctor ordering can shift.
  if (settingsPage_) settingsPage_->setTheme(t);
}

void MainWindow::selectSection(int i) {
  stack_->setCurrentIndex(i);
  for (int k = 0; k < navButtons_.size(); ++k) {
    navButtons_[k]->setChecked(k == i);
    navButtons_[k]->setProperty("current", k == i);
    navButtons_[k]->style()->unpolish(navButtons_[k]);
    navButtons_[k]->style()->polish(navButtons_[k]);   // re-evaluate [current="true"] QSS
  }
}

void MainWindow::setupTray() {
  if (!QSystemTrayIcon::isSystemTrayAvailable()) return;   // no tray -> feature is a no-op
  tray_ = new QSystemTrayIcon(QIcon(":/icon.png"), this);
  tray_->setToolTip("Droppix");
  auto* menu = new QMenu(this);
  QAction* showAct = menu->addAction("Show Droppix");
  connect(showAct, &QAction::triggered, this, [this]{
    showNormal(); raise(); activateWindow(); tray_->hide();
  });
  menu->addSeparator();
  QAction* quitAct = menu->addAction("Quit");
  connect(quitAct, &QAction::triggered, this, [this]{ quitting_ = true; close(); });
  tray_->setContextMenu(menu);
  connect(tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r){
    if (r == QSystemTrayIcon::Trigger || r == QSystemTrayIcon::DoubleClick) {
      showNormal(); raise(); activateWindow(); tray_->hide();
    }
  });
}

void MainWindow::startMinimizedToTray() {
  if (tray_ && QSystemTrayIcon::isSystemTrayAvailable()) {
    tray_->show();          // make the tray icon visible; keep the window hidden
  } else {
    show();                 // no tray -> don't strand the user
  }
}

bool MainWindow::minimizeToTrayRequested() const {
  return QFile::exists(configDir() + "/minimize_on_close");
}

void MainWindow::onDevicesChanged(const QList<MdnsDevice>& devices) {
  if (!lanEnabled_) return;   // Network transport off — ignore discovery
  netDevices_ = devices;
  rebuildClientList();
  autoConnectTimer_.start();   // (re)arm the debounced auto-connect evaluation
}

void MainWindow::onTetherClientsChanged(const QList<TetherClient>& clients) {
  if (!usbEnabled_) return;   // USB transport off — ignore discovery
  tetherClients_ = clients;
  rebuildClientList();
  autoConnectTimer_.start();   // (re)arm the debounced auto-connect evaluation
}

void MainWindow::onAoaClientsChanged(const QList<AoaClient>& clients) {
  if (!usbEnabled_) return;   // USB transport off — ignore discovery
  aoaClients_ = clients;
  rebuildClientList();
  autoConnectTimer_.start();   // (re)arm the debounced auto-connect evaluation
}

// Repopulates the unified list from three sources (USB-tether first, then USB-AOA, then
// mDNS/network), tagging each item with its connect payload and preserving the prior
// selection by label. A row's transport (UserRole) is "net" (USB-tether + Wi-Fi connect
// over the net/WAKE path) or "usb-aoa" (streams over the cable, no WAKE). Roles:
// UserRole = transport; UserRole+1 = ident (net address, or AOA serial);
// UserRole+2 = net wake port (0 for AOA); UserRole+3 = id (device id, or AOA serial).
void MainWindow::rebuildClientList() {
  const QString prevSelected = devicesList_->currentItem()
      ? devicesList_->currentItem()->text() : QString();
  devicesList_->clear();

  for (const auto& t : tetherClients_) {
    auto* item = new QListWidgetItem(QString("%1 — USB").arg(t.name));
    item->setData(Qt::UserRole, "net");                 // connects via WAKE like Wi-Fi
    item->setData(Qt::UserRole + 1, t.address);
    item->setData(Qt::UserRole + 2, (uint)t.wakePort);
    item->setData(Qt::UserRole + 3, t.id);
    devicesList_->addItem(item);
    if (item->text() == prevSelected) devicesList_->setCurrentItem(item);
  }
  // Count display names among listable AOA tablets so identical-model duplicates can be
  // disambiguated by a serial tail (two "Nexus 10 — USB" rows would otherwise be identical).
  // NOTE: accessory mode alone does NOT mean "owned by a session" — a device can be left
  // in accessory mode by an ended session (nothing reverts it), so the only reliable
  // ownership check is an actual active session for this serial, below.
  QHash<QString, int> aoaNameCount;
  for (const auto& a : aoaClients_)
    aoaNameCount[a.product.isEmpty() ? a.serial : a.product]++;
  for (const auto& a : aoaClients_) {
    if (sessions_.has("usb-aoa:" + a.serial)) continue;  // already streaming this tablet
    const QString name = a.product.isEmpty() ? a.serial : a.product;
    const QString label = aoaNameCount.value(name) > 1
        ? QString("%1 — USB (%2)").arg(name, a.serial.right(6))   // duplicate model: add serial tail
        : QString("%1 — USB").arg(name);
    auto* item = new QListWidgetItem(label);
    item->setData(Qt::UserRole, "usb-aoa");
    item->setData(Qt::UserRole + 1, a.serial);        // ident = serial
    item->setData(Qt::UserRole + 2, (uint)0);         // no wake port
    item->setData(Qt::UserRole + 3, a.serial);        // id = serial (known-store key)
    devicesList_->addItem(item);
    if (item->text() == prevSelected) devicesList_->setCurrentItem(item);
  }
  for (const auto& d : netDevices_) {
    const QString name = QString::fromStdString(d.name);
    const QString address = QString::fromStdString(d.address);
    auto* item = new QListWidgetItem(QString("%1 — %2").arg(name, address));
    item->setData(Qt::UserRole, "net");
    item->setData(Qt::UserRole + 1, address);
    item->setData(Qt::UserRole + 2, (uint)d.port);
    item->setData(Qt::UserRole + 3, QString::fromStdString(d.id));   // for approved-store match
    devicesList_->addItem(item);
    if (item->text() == prevSelected) devicesList_->setCurrentItem(item);
  }
}

void MainWindow::onConnectToSelectedDevice() {
  auto* item = devicesList_->currentItem();
  if (!item) return;
  const QString transport = item->data(Qt::UserRole).toString();
  const QString ident = item->data(Qt::UserRole + 1).toString();   // net address (Wi-Fi or USB-tether)
  if (ident.isEmpty()) return;
  const QString key = transport + ":" + ident;
  const QString label = item->text();
  const QString id = item->data(Qt::UserRole + 3).toString();
  const quint16 wakePort = (quint16)item->data(Qt::UserRole + 2).toUInt();
  connectDevice(key, label, transport, ident, wakePort, id, /*quietIfBusy=*/false);
}

bool MainWindow::connectDevice(const QString& key, const QString& label, const QString& transport,
                               const QString& ident, quint16 wakePort, const QString& id,
                               bool quietIfBusy) {
  if (sessions_.has(key) || (!id.isEmpty() && sessions_.ids().contains(id))) {
    if (!quietIfBusy) QMessageBox::information(this, "Droppix", "That device already has an active monitor.");
    return false;
  }
  const int port = sessions_.allocatePort(collectSettings().port);
  if (port < 0) { if (!quietIfBusy) QMessageBox::information(this, "Droppix", "Monitor limit reached (4)."); return false; }
  std::function<void()> direct;
  if (transport == "usb-aoa") {
    direct = []{};   // accessory auto-launches the app; nothing to send
  } else {
    const QString addr = ident;
    direct = [this, addr, wakePort, port]{
      auto bytes = encode_wake((uint16_t)port);
      QByteArray dg(reinterpret_cast<const char*>(bytes.data()), (int)bytes.size());
      pendingWakes_[addr] = QDateTime::currentMSecsSinceEpoch();
      QUdpSocket sock; sock.writeDatagram(dg, QHostAddress(addr), wakePort);
    };
  }
  startSession(key, label, transport, port, id, direct);
  return true;
}

void MainWindow::evaluateAutoConnect() {
  if (!collectSettings().autoConnect) return;
  QList<AutoConnectCandidate> cands;
  for (int i = 0; i < devicesList_->count(); ++i) {
    auto* it = devicesList_->item(i);
    const QString transport = it->data(Qt::UserRole).toString();
    const QString ident = it->data(Qt::UserRole + 1).toString();
    const QString id = it->data(Qt::UserRole + 3).toString();
    const bool eligible = (transport == "usb-aoa")
        ? knownAoa_.contains(ident)                       // physical-trust: streamed before
        : (!id.isEmpty() && approved_.isApproved(id));    // net: paired/approved
    cands.push_back({transport + ":" + ident, id, eligible});
  }
  QList<ActiveSessionRef> active;
  for (const auto& s : sessions_.list()) active.push_back({s.key, s.id});
  const AutoConnectPlan plan = devicesToConnect(true, cands, active);
  // USB takeover: stop the tablet's Wi-Fi session; the re-armed evaluation connects the
  // cable once the teardown has removed the session.
  for (const QString& key : plan.disconnect)
    if (Session* s = sessions_.find(key))
      if (s->controller) s->controller->stop();
  if (!plan.disconnect.isEmpty()) autoConnectTimer_.start();
  for (const QString& key : plan.connect) {
    for (int i = 0; i < devicesList_->count(); ++i) {
      auto* it = devicesList_->item(i);
      const QString transport = it->data(Qt::UserRole).toString();
      const QString ident = it->data(Qt::UserRole + 1).toString();
      if (transport + ":" + ident != key) continue;
      connectDevice(key, it->text(), transport, ident,
                    (quint16)it->data(Qt::UserRole + 2).toUInt(),
                    it->data(Qt::UserRole + 3).toString(), /*quietIfBusy=*/true);
      break;
    }
  }
}

void MainWindow::refreshAdvertising() {
  if (!advertiser_.available()) return;
  if (!lanEnabled_) { advertiser_.stop(); advertisedPort_ = 0; return; }   // Network off
  const quint16 p = (quint16)collectSettings().port;
  if (p == advertisedPort_) return;   // already publishing this port — no churn
  advertiser_.stop();
  advertiser_.start(p);
  advertisedPort_ = p;
}

Settings MainWindow::collectSettings() const {
  Settings s;
  settingsPage_->store(s);   // all Settings fields (source/touch/bitrate/port/refresh/auto-adb/
                              // overlay/autoConnect/webClient + resolution/fps/audio/orientation)
  s.tls = true;
  // In a Flatpak the streamer runs on the host, so hand it the host-mirrored cert/key paths.
  if (!flatpakHostCert_.isEmpty()) {
    s.certPath = flatpakHostCert_.toStdString();
    s.keyPath  = flatpakHostKey_.toStdString();
  } else {
    s.certPath = cert_.certPath().toStdString();
    s.keyPath  = cert_.keyPath().toStdString();
  }
  if (!flatpakHostWeb_.isEmpty()) s.webRoot = flatpakHostWeb_.toStdString();
  else s.webRoot = resolve_web_root_for_gui();
  return s;
}

void MainWindow::applySettings(const Settings& s) {
  settingsPage_->load(s);   // all Settings fields, including resolution/fps/audio/orientation
}

void MainWindow::refreshProfiles() {
  const QString cur = profileBox_->currentText();
  QSignalBlocker block(profileBox_);
  profileBox_->clear();
  profileBox_->addItems(store_.names());
  if (!cur.isEmpty()) profileBox_->setCurrentText(cur);
}

void MainWindow::restoreLastProfile() {
  const QStringList names = store_.names();
  if (names.isEmpty()) return;
  QString want = store_.lastUsed();
  if (want.isEmpty() || !names.contains(want)) want = names.first();
  Settings s;
  if (!store_.load(want, s)) return;
  QSignalBlocker block(profileBox_);   // populate fired no signal; apply manually
  profileBox_->setCurrentText(want);
  applySettings(s);
  store_.setLastUsed(want);
}

void MainWindow::setupAuth() {
  // Install a polkit rule pre-authorizing this exact streamer binary for this user, so
  // Start never asks for a password again (polkit.Result.YES — permanent, survives
  // reboots). One pkexec prompt now writes the rule as root; after that, no prompts.
  const QString user = QString::fromLocal8Bit(qgetenv("USER"));
  const QString bin = QString::fromStdString(streamBin_);
  QString binAlt = bin;                                    // also match the /home <-> /var/home alias
  if (bin.startsWith("/var/home/")) binAlt = bin.mid(4);
  else if (bin.startsWith("/home/")) binAlt = "/var" + bin;

  const QString rule = QStringLiteral(
      "// droppix: pre-authorize pkexec for the evdi streamer (in-app setup).\n"
      "polkit.addRule(function(action, subject) {\n"
      "    if (action.id == \"org.freedesktop.policykit.exec\" &&\n"
      "        subject.user == \"%1\" &&\n"
      "        (action.lookup(\"program\") == \"%2\" ||\n"
      "         action.lookup(\"program\") == \"%3\")) {\n"
      "        return polkit.Result.YES;\n"
      "    }\n"
      "});\n").arg(user, bin, binAlt);

  static const QString kRulePath = QStringLiteral("/etc/polkit-1/rules.d/49-droppix.rules");
  bool ok = false;
  int rc = -1;
  if (inFlatpak()) {
    // The pkexec shim runs on the HOST, which can't read a sandbox temp-file path, so pipe
    // the rule's bytes to root over stdin instead (flatpak-spawn/pkexec preserve stdin).
    QProcess p;
    p.start("pkexec", {"/bin/sh", "-c", "umask 022; cat > " + kRulePath});
    if (p.waitForStarted(10000)) {
      p.write(rule.toUtf8());
      p.closeWriteChannel();
      p.waitForFinished(-1);   // blocks on the polkit auth prompt, then the write
      rc = (p.exitStatus() == QProcess::NormalExit) ? p.exitCode() : -1;
      ok = (rc == 0);
    }
  } else {
    // Normal / AppImage: pkexec installs the rule from a real temp-file path (mode 0644).
    QTemporaryFile tmp;
    if (!tmp.open()) {
      QMessageBox::warning(this, "Droppix", "Couldn't create a temporary file for auth setup.");
      return;
    }
    tmp.write(rule.toUtf8());
    tmp.flush();
    rc = QProcess::execute("pkexec", {"/usr/bin/install", "-m", "0644", tmp.fileName(), kRulePath});
    ok = (rc == 0);
  }
  if (ok) {
    QFile marker(configDir() + "/auth_configured");
    if (marker.open(QIODevice::WriteOnly)) { marker.write("1"); marker.close(); }
    QMessageBox::information(this, "Droppix",
        "Authentication remembered permanently.\nStart will never ask for a password again.");
  } else {
    QMessageBox::warning(this, "Droppix",
        QString("Authentication setup was cancelled or failed (pkexec exit %1).").arg(rc));
  }
}

void MainWindow::showAbout() {
  const QString repo = "https://github.com/Spinjitsudoom/droppix";
  QDialog dlg(this);
  dlg.setWindowTitle("About Droppix");
  auto* icon = new QLabel;
  icon->setPixmap(QPixmap(":/icon.png").scaled(72, 72, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  icon->setAlignment(Qt::AlignTop);
  auto* text = new QLabel(QString(
      "<b style='font-size:16px'>Droppix</b>&nbsp; v0.1<br>"
      "<span style='color:#9aa5b1'>extended display, touch, stylus &amp; audio over USB or WiFi</span><br><br>"
      "Source &amp; releases:<br><a href='%1'>%1</a><br><br>"
      "Built with Qt · evdi · x264 · PipeWire · MediaCodec<br>"
      "Licensed under the <b>MIT License</b>.").arg(repo));
  text->setOpenExternalLinks(true);
  text->setTextInteractionFlags(Qt::TextBrowserInteraction);
  text->setWordWrap(true);
  auto* topRow = new QHBoxLayout;
  topRow->addWidget(icon); topRow->addSpacing(16); topRow->addWidget(text, 1);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
  connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  auto* root = new QVBoxLayout(&dlg);
  root->addLayout(topRow);
  root->addWidget(buttons);
  dlg.exec();
}

void MainWindow::updateServerButton() {
  auto* sw = statusPage_->serverSwitch();
  sw->setChecked(serverEnabled_);
  sw->setProperty("on", serverEnabled_);
  sw->setText(serverEnabled_ ? "Server ON" : "Server OFF");
  sw->style()->unpolish(sw); sw->style()->polish(sw);   // re-apply [on="true"] QSS (see docs/lessons)
}

// The Server toggle: on -> start a primary listener that waits for a device (and re-arms
// after each session); off -> stop it. The state is persisted so it restores on launch.
void MainWindow::onServerToggled(bool on) {
  serverEnabled_ = on;
  const QString marker = configDir() + "/server_enabled";
  if (on) {
    QFile(marker).open(QIODevice::WriteOnly);   // touch: remember "server was on"
    startServerSession();
  } else {
    QFile::remove(marker);
    stopServerSession();
  }
  updateServerButton();
}

void MainWindow::startServerSession() {
  const int port = sessions_.allocatePort(collectSettings().port);
  if (port < 0) {
    qWarning("server: monitor limit reached (4) — cannot start another listener");
    serverEnabled_ = false;
    updateServerButton();
    return;
  }
  serverKey_ = QString("server:%1").arg(port);
  serverStartMs_ = QDateTime::currentMSecsSinceEpoch();
  serverEverConnected_ = false;
  // No row yet: this is a blind listener with no device identified. A row is added once
  // a real device is approved (see the approvalRequested handler in wireSession()), so
  // the Active-monitors list only ever shows actually-connecting devices, never a
  // permanent "waiting" placeholder.
  startSession(serverKey_, "Server", QString(), port, QString(), {}, /*mirror=*/false,
               /*addRowNow=*/false);
}

void MainWindow::stopServerSession() {
  if (serverKey_.isEmpty()) return;
  if (Session* s = sessions_.find(serverKey_))
    if (s->controller) s->controller->stop();   // runningChanged(false) tears the session down
}

void MainWindow::scheduleServerRefresh() {
  serverRefreshTimer_.start();   // (re)start the debounce; coalesces rapid edits
}

void MainWindow::refreshServer() {
  if (!serverEnabled_) return;   // nothing to restart; settings apply on next Start
  Session* s = sessions_.find(serverKey_);
  if (!s || !s->controller) return;   // nothing live to refresh; an in-flight start uses current settings
  StreamController* c = s->controller;
  serverRefreshing_ = true;
  // Respawn only AFTER the old listener's teardown has run. This one-shot is connected
  // here (after wireSession's teardown), so Qt fires them in connection order: teardown
  // first (clears serverKey_, guarded below), then this. Holds for the graceful <3s sync
  // exit and the async kill path. serverRefreshing_ suppresses the auto re-arm so this is
  // the SINGLE restart (no duplicate/orphaned session).
  connect(c, &StreamController::runningChanged, this, [this](bool r){
    if (r) return;
    serverRefreshing_ = false;
    startServerSession();      // fresh collectSettings() -> new --web/port/args
    refreshWebClientUi();
    updateStatus();
  }, Qt::SingleShotConnection);
  c->stop();
}

void MainWindow::addMonitorRow(const QString& key, const QString& label, const QString& transport,
                               int port, bool mirror) {
  for (int i = 0; i < monitorsList_->count(); ++i)
    if (monitorsList_->item(i)->data(Qt::UserRole).toString() == key) return;   // already shown
  auto* row = new QListWidgetItem(
      QString("%1  ·  %2  ·  :%3%4").arg(label, transport.isEmpty() ? "—" : transport)
          .arg(port).arg(mirror ? " — Mirror" : ""));
  row->setData(Qt::UserRole, key);
  monitorsList_->addItem(row);
}

void MainWindow::startSession(const QString& key, const QString& label, const QString& transport,
                              int port, const QString& id, std::function<void()> directTablet,
                              bool mirror, bool addRowNow) {
  auto* c = new StreamController(this);
  wireSession(c, key);
  Settings s = collectSettings();
  const std::string tname = ("droppix-touch-" + QString::number(port)).toStdString();
  const std::string aoaSerial = (transport == "usb-aoa") ? id.toStdString() : std::string();
  Command cmd = build_command(s, streamBin_, port, tname, aoaSerial, mirror);
  qInfo("$ %s ... (:%d)", cmd.program.c_str(), port);
  if (s.webClient && aoaSerial.empty() && s.tls && !s.certPath.empty() && s.webRoot.empty())
    qWarning("web client is enabled but web/dist assets were not found — starting WITHOUT the web "
             "UI. Build them (npm --prefix web run build) or set DROPPIX_WEB_ROOT.");
  c->start(cmd);

  Session sess;
  sess.controller = c; sess.port = port; sess.key = key; sess.label = label;
  sess.transport = transport; sess.id = id; sess.touchName = QString::fromStdString(tname);
  sess.mirror = mirror;
  sessions_.add(sess);

  if (addRowNow) addMonitorRow(key, label, transport, port, mirror);
  // (monitors list is always visible now — Task 9 adds an empty-state)
  refreshWebClientUi();
  refreshPairingUi();
  refreshAdvertising();
  updateStatus();
  if (directTablet) directTablet();
}

void MainWindow::refreshWebClientUi() {
  Settings s = collectSettings();
  if (!s.webClient || !lanEnabled_ || sessions_.count() == 0) {
    webUrlLabel_->hide();
    webQrLabel_->hide();
    webCopyBtn_->hide();
    return;
  }
  // Newest session is last in the list.
  const Session& sess = sessions_.list().last();
  if (sess.transport == "usb-aoa") {
    webUrlLabel_->setText("Web client unavailable for USB/AOA sessions");
    webUrlLabel_->show();
    webQrLabel_->hide();
    webCopyBtn_->hide();
    return;
  }
  const auto inc = included_ifaces(lan_ipv4_ifaces(), excludedAdapters_);
  const QString ip = inc.isEmpty() ? QStringLiteral("127.0.0.1") : inc.first().ip;
  const QString url = session_web_url(ip, sess.port);
  webUrlLabel_->setText(url);
  webUrlLabel_->show();
  webCopyBtn_->show();
  const QImage qr = make_qr_image(url, 4);
  if (!qr.isNull()) {
    webQrLabel_->setPixmap(QPixmap::fromImage(qr));
    webQrLabel_->show();
  } else {
    webQrLabel_->hide();
  }
}

void MainWindow::refreshPairingUi() {
  // Show a scannable pairing QR whenever a WiFi session is live and we have a real LAN
  // IP. Encodes droppix://<host LAN IP>:<session port>?code=<pairing code> so the
  // tablet can scan it to pair before any connection exists. USB/AOA use localhost +
  // the "Connect via USB" button instead, so no QR there. Respects the same LAN
  // enable/adapter-exclusion settings as the web-client QR.
  if (!lanEnabled_ || sessions_.count() == 0) {
    pairingScanCaption_->hide();
    pairingScanQr_->hide();
    return;
  }
  const Session& sess = sessions_.list().last();
  const auto inc = included_ifaces(lan_ipv4_ifaces(), excludedAdapters_);
  const QString ip = inc.isEmpty() ? QString() : inc.first().ip;
  if (sess.transport == "usb-aoa" || ip.isEmpty() || ip == "127.0.0.1") {
    pairingScanCaption_->hide();
    pairingScanQr_->hide();
    return;
  }
  const QString code = cert_.pairingCode();
  const std::string uri = generate_qr_uri(ip.toStdString(), sess.port, code.toStdString());
  const QImage qr = make_qr_image(QString::fromStdString(uri), 8);   // larger: scannable from a phone
  if (qr.isNull()) {
    pairingScanCaption_->hide();
    pairingScanQr_->hide();
    return;
  }
  pairingScanQr_->setPixmap(QPixmap::fromImage(qr));
  pairingScanQr_->show();
  pairingScanCaption_->setText(
      QString("Scan to pair this PC (%1:%2) — or enter code %3").arg(ip).arg(sess.port).arg(code));
  pairingScanCaption_->show();
}

void MainWindow::loadInterfacePrefs() {
  lanEnabled_ = !QFile::exists(configDir() + "/lan_disabled");
  usbEnabled_ = !QFile::exists(configDir() + "/usb_disabled");
  excludedAdapters_.clear();
  QFile f(configDir() + "/advertise_excluded_adapters");
  if (f.open(QIODevice::ReadOnly | QIODevice::Text))
    for (const QString& l : QString::fromUtf8(f.readAll()).split('\n', Qt::SkipEmptyParts))
      excludedAdapters_.insert(l.trimmed());
}

void MainWindow::saveInterfacePrefs() {
  const QString cfg = configDir();
  auto marker = [&](const char* name, bool present) {
    if (present) QFile(cfg + "/" + name).open(QIODevice::WriteOnly);
    else QFile::remove(cfg + "/" + name);
  };
  marker("lan_disabled", !lanEnabled_);
  marker("usb_disabled", !usbEnabled_);
  QFile f(cfg + "/advertise_excluded_adapters");
  if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QTextStream out(&f);
    for (const QString& n : excludedAdapters_) out << n << '\n';
  }
}

void MainWindow::onLanToggled(bool on) {
  lanEnabled_ = on;
  saveInterfacePrefs();
  if (on) {
    if (browser_.available()) browser_.start();
    refreshAdvertising();
  } else {
    browser_.stop();
    advertiser_.stop();
    advertisedPort_ = 0;
    netDevices_.clear();
    rebuildClientList();
  }
  refreshInterfaces();     // enable/disable the adapter checkboxes
  refreshWebClientUi();
  scheduleServerRefresh();
}

void MainWindow::onUsbToggled(bool on) {
  usbEnabled_ = on;
  saveInterfacePrefs();
  if (on) {
    tetherScanner_.start();
    aoaScanner_.start();
  } else {
    tetherScanner_.stop();   // stops the 2s USB poll — the idle-resource win
    aoaScanner_.stop();
    tetherClients_.clear();
    aoaClients_.clear();
    rebuildClientList();
  }
  scheduleServerRefresh();
}

void MainWindow::refreshInterfaces() {
  if (!adapterRows_) return;
  QLayoutItem* item;
  while ((item = adapterRows_->takeAt(0))) {   // clear previous rows
    if (QWidget* w = item->widget()) w->deleteLater();
    delete item;
  }
  for (const LanIface& i : lan_ipv4_ifaces()) {
    auto* cb = new QCheckBox(QStringLiteral("%1 · %2").arg(i.ip, i.name));
    cb->setChecked(!excludedAdapters_.contains(i.name));
    cb->setEnabled(lanEnabled_);
    const QString name = i.name;
    connect(cb, &QCheckBox::toggled, this, [this, name](bool included) {
      if (included) excludedAdapters_.remove(name);
      else excludedAdapters_.insert(name);
      saveInterfacePrefs();
      refreshWebClientUi();   // primary URL/QR follows the first included adapter
      scheduleServerRefresh();
    });
    adapterRows_->addWidget(cb);
  }
}

void MainWindow::logEvent(const QString& key, const QString& source, LogLevel level, const QString& text) {
  if (!logBuffer_) return;
  LogEntry e;
  e.epochMs = QDateTime::currentMSecsSinceEpoch();
  e.session = key;
  e.source = source;
  e.level = level;
  e.text = text;
  logBuffer_->append(e);
}

void MainWindow::wireSession(StreamController* c, const QString& key) {
  connect(c, &StreamController::logLine, this, [this, key](const QString& l){
    const Classified cl = classifyStreamerLine(l);
    logEvent(key, cl.source, cl.level, cl.text);
  });
  connect(c, &StreamController::statsReceived, this, [this, key](const Stats& s){
    if (s.client_connected) {
      anyConnected_ = true;
      if (key == serverKey_) serverEverConnected_ = true;   // for the re-arm/failed-start guard
      if (key.startsWith("usb-aoa:")) knownAoa_.add(key.mid(8));   // enable future auto-start
    }
    updateStatus();
  });
  connect(c, &StreamController::runningChanged, this, [this, key](bool r){
    if (r) return;   // ended -> tear the session down
    if (Session* s = sessions_.find(key)) if (s->controller) s->controller->deleteLater();
    sessions_.remove(key);
    for (int i = monitorsList_->count() - 1; i >= 0; --i)
      if (monitorsList_->item(i)->data(Qt::UserRole).toString() == key)
        delete monitorsList_->takeItem(i);
    if (sessions_.count() == 0) { anyConnected_ = false; hidePairingPopup(); }
    refreshWebClientUi();
    refreshPairingUi();
    updateStatus();
    if (key == serverKey_) {   // the primary Server-toggle listener ended
      serverKey_.clear();
      if (serverRefreshing_) {
        // Intentional refresh: refreshServer()'s one-shot restarts the listener with
        // fresh settings. Skip the auto re-arm/disable so we don't spawn a duplicate
        // (leaked) session or turn the server off.
      } else {
        const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - serverStartMs_;
        if (shouldRearm(serverEnabled_, elapsed, serverEverConnected_)) {
          QTimer::singleShot(0, this, [this]{ if (serverEnabled_) startServerSession(); });  // keep listening
        } else if (serverEnabled_) {   // enabled but a fast failure (pkexec denied, port clash…)
          serverEnabled_ = false;
          QFile::remove(configDir() + "/server_enabled");
          updateServerButton();
          qWarning("server failed to start (exited immediately) — turned off. See the log above.");
        }
      }
    }
  });
  connect(c, &StreamController::connecting, this, [this, key](const QString& ip){
    logEvent(key, QStringLiteral("conn"), LogLevel::Info, QStringLiteral("client connecting ip=%1").arg(ip));
    Session* s = sessions_.find(key);
    showPairingPopup(ip, s ? s->port : 0);
  });
  connect(c, &StreamController::approvalRequested, this,
    [this, c, key](const QString& id, const QString& name, const QString& ip){
      logEvent(key, QStringLiteral("pair"), LogLevel::Info,
               QStringLiteral("approval requested id=%1 name=%2 ip=%3").arg(id, name, ip));
      hidePairingPopup();
      // Only once this device is actually approved: add its Active-monitors row (no-op
      // if one already exists, e.g. USB/AOA/mDNS sessions that had one from the start —
      // only the blind "Server" listener reaches this with no row yet) and record its
      // identity as the session's label. A denied device never gets a row.
      auto onApproved = [this, key, name, ip]{
        if (Session* s = sessions_.find(key)) {
          const QString devLabel = name.isEmpty() ? ip : name;
          s->label = devLabel;
          addMonitorRow(key, devLabel, s->transport, s->port, s->mirror);
          updateStatus();   // header text ("Listening…" -> "N monitor(s) · waiting") follows the row
        }
      };
      const QString akey = id.isEmpty() ? ip : id;
      const qint64 woken = pendingWakes_.value(ip, 0);
      if (woken && QDateTime::currentMSecsSinceEpoch() - woken < 15000) {
        pendingWakes_.remove(ip);
        approved_.approve(akey);
        onApproved();
        c->writeLine("approve " + akey);
        return;
      }
      if (approved_.isApproved(akey)) { onApproved(); c->writeLine("approve " + akey); return; }
      auto btn = QMessageBox::question(this, "Allow connection?",
          QString("Allow \"%1\" (%2) to connect?").arg(name.isEmpty() ? ip : name, ip));
      if (btn == QMessageBox::Yes) { approved_.approve(akey); onApproved(); c->writeLine("approve " + akey); }
      else c->writeLine("deny " + akey);
    });
}

void MainWindow::stopSelectedMonitor() {
  auto* item = monitorsList_->currentItem();
  if (!item) return;
  if (Session* s = sessions_.find(item->data(Qt::UserRole).toString()))
    if (s->controller) s->controller->stop();   // runningChanged(false) removes the row + session
}

void MainWindow::toggleSelectedMonitorMirror() {
  // Selected-row -> Session lookup, exactly like stopSelectedMonitor().
  auto* item = monitorsList_->currentItem();
  if (!item) return;
  const QString key = item->data(Qt::UserRole).toString();
  Session* s = sessions_.find(key);
  if (!s) return;

  // Capture identity fields before the session is torn down.
  const QString label = s->label;
  const QString transport = s->transport;
  const int port = s->port;
  const QString id = s->id;
  const bool newMirror = !s->mirror;
  StreamController* c = s->controller;
  if (!c) { startSession(key, label, transport, port, id, {}, newMirror); return; }

  // Respawn on the same key/port with the flipped mode, but only AFTER the old
  // controller's teardown has actually run — not immediately after stop(). The
  // teardown lambda wired in wireSession() (connected back when this session was
  // started) removes sessions_[key] and every monitorsList_ row matching key when
  // runningChanged(false) fires. If we respawned synchronously here instead, a
  // slow streamer that needs kill() (>3s) would fire that teardown asynchronously
  // *after* the respawn already inserted a new session+row under the same key —
  // and the old teardown would then delete the brand-new row, orphaning the live
  // session (still streaming, but ungovernable from the GUI).
  //
  // A one-shot connection on the same signal, connected here (i.e. after
  // wireSession's), is guaranteed to run after the teardown lambda — Qt fires
  // same-signal handlers in connection order. So by the time this fires, the old
  // key/row is already gone and startSession() can't collide with it. This holds
  // for both exit paths: graceful exit (<3s) runs teardown synchronously inside
  // stop()'s waitForFinished, so both lambdas fire before stop() even returns;
  // kill() (>3s) fires runningChanged(false) asynchronously later, and this
  // handler simply waits for it.
  connect(c, &StreamController::runningChanged, this,
      [this, key, label, transport, port, id, newMirror](bool r) {
        // Safe one-shot: the session is already running when we connect this (c->stop()
        // is called below, after this connect), so the next runningChanged edge is always
        // false (stopped) — this guard never eats the single shot on a spurious `true`.
        if (r) return;   // only act on the stopped (false) edge
        startSession(key, label, transport, port, id, {}, newMirror);
      }, Qt::SingleShotConnection);

  // Stop path: same as stopSelectedMonitor() — this just triggers the streamer's
  // exit; the respawn happens in the one-shot handler above once teardown completes.
  c->stop();
}

void MainWindow::updateStatus() {
  // "Interfaces up" for the hero metric: how many transports are enabled (Network/USB),
  // independent of whether any device is actually connected over them.
  const int ifaces = (lanEnabled_ ? 1 : 0) + (usbEnabled_ ? 1 : 0);
  if (sessions_.count() == 0) {
    setStatusDot(kDotStopped); streamLabel_->setText("Stopped"); statsLabel_->setText("—");
    statusPage_->setMetrics(0, 0, ifaces);
    return;
  }
  // monitorsList_ only ever holds rows for identified/approved devices (see
  // addMonitorRow()) — the blind "Server" listener has zero rows until one connects, so
  // counting rows here (not sessions_.count()) keeps "N monitor(s)" from claiming a
  // monitor exists before any device actually has.
  const int devices = monitorsList_->count();
  if (devices == 0) {
    setStatusDot(kDotWaiting); streamLabel_->setText("Listening…"); statsLabel_->setText("—");
    statusPage_->setMetrics(devices, 0, ifaces);   // devices == 0 here
    return;
  }
  setStatusDot(anyConnected_ ? kDotConnected : kDotWaiting);
  streamLabel_->setText(QString("%1 monitor%2%3")
      .arg(devices).arg(devices == 1 ? "" : "s").arg(anyConnected_ ? "" : " · waiting"));
  statsLabel_->setText("—");
  statusPage_->setMetrics(devices, anyConnected_ ? devices : 0, ifaces);
}

void MainWindow::setStatusDot(const char* color) {
  statusDot_->setStyleSheet(QString(
      "background:%1; border-radius:6px;"
      "min-width:12px; max-width:12px; min-height:12px; max-height:12px;").arg(color));
}

void MainWindow::closeEvent(QCloseEvent* event) {
  // "Minimize to tray on close": hide to the tray instead of quitting (unless this
  // close came from the tray's Quit action). The streamer keeps running in the
  // background — matching the launch-at-login use case.
  if (!quitting_ && tray_ && minimizeToTrayRequested()) {
    hide();
    tray_->show();
    if (!trayHintShown_) {
      tray_->showMessage("Droppix", "Still running in the tray — click to restore.",
                         QSystemTrayIcon::Information, 3000);
      trayHintShown_ = true;
    }
    event->ignore();
    return;
  }
  for (auto& s : sessions_.list()) if (s.controller) s.controller->stop();  // don't orphan streamers
  advertiser_.stop();
  browser_.stop();
  tetherScanner_.stop();
  aoaScanner_.stop();
  QMainWindow::closeEvent(event);
}
}  // namespace droppix
