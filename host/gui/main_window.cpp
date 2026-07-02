#include "main_window.h"
#include "args_builder.h"
#include "settings_dialog.h"
#include "style.h"
#include <QtWidgets>
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
#include "wake.h"

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
  return QFileInfo::exists(dstBin) ? dstBin.toStdString() : dev.toStdString();
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
      cert_(configDir()) {
  streamBin_ = resolveStreamBin();
  cert_.regenerate();   // fresh cert => new pairing code every launch (per-restart rotation)
  stageCertsToHost();   // Flatpak: mirror cert/key to the host for the streamer (else no-op)
  setWindowTitle("Droppix");
  setWindowIcon(QIcon(":/icon.png"));
  settingsDialog_ = new SettingsDialog(this);   // advanced options live in this dialog

  // --- Header: logo + wordmark, with Settings + About icon buttons top-right ---
  auto* logo = new QLabel; logo->setObjectName("logo");
  logo->setPixmap(QPixmap(":/logo.png").scaled(36, 36, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  auto* title = new QLabel("Droppix"); title->setObjectName("header");

  auto* settingsBtn = new QToolButton; settingsBtn->setObjectName("iconButton");
  settingsBtn->setIcon(QIcon(":/ic-settings.png")); settingsBtn->setIconSize(QSize(22, 22));
  settingsBtn->setAutoRaise(true); settingsBtn->setToolTip("Settings");
  settingsBtn->setCursor(Qt::PointingHandCursor);
  connect(settingsBtn, &QToolButton::clicked, this, [this]{ settingsDialog_->exec(); });
  auto* aboutBtn = new QToolButton; aboutBtn->setObjectName("iconButton");
  aboutBtn->setIcon(QIcon(":/ic-about.png")); aboutBtn->setIconSize(QSize(22, 22));
  aboutBtn->setAutoRaise(true); aboutBtn->setToolTip("About Droppix");
  aboutBtn->setCursor(Qt::PointingHandCursor);
  connect(aboutBtn, &QToolButton::clicked, this, &MainWindow::showAbout);
  connect(settingsDialog_, &SettingsDialog::rememberAuthRequested, this, &MainWindow::setupAuth);
  connect(settingsDialog_, &SettingsDialog::manageDevicesRequested, this, &MainWindow::manageDevices);
  // Perf-overlay checkbox applies live: if a stream is running, push "overlay N" to the
  // streamer's stdin so the tablet shows/hides it without a restart. store()/load() still
  // persist the setting for the next launch.
  connect(settingsDialog_, &SettingsDialog::overlayToggled, this, [this](bool on){
    if (controller_.running()) controller_.writeLine(QString("overlay %1").arg(on ? 1 : 0));
  });

  auto* headerRow = new QHBoxLayout;
  headerRow->addWidget(logo); headerRow->addSpacing(10);
  headerRow->addWidget(title); headerRow->addStretch();
  headerRow->addWidget(settingsBtn); headerRow->addWidget(aboutBtn);

  // --- Profile row ---
  profileBox_ = new QComboBox;
  auto* saveBtn = new QPushButton("Save");
  auto* saveAsBtn = new QPushButton("Save As");
  auto* delBtn = new QPushButton("Delete");
  auto* profRow = new QHBoxLayout;
  profRow->addWidget(new QLabel("Profile:")); profRow->addWidget(profileBox_, 1);
  profRow->addWidget(saveBtn); profRow->addWidget(saveAsBtn); profRow->addWidget(delBtn);

  // (all stream options — source, resolution, touch, audio, fps/bitrate/port/refresh/
  // orientation/auto-adb/overlay — live in the Settings dialog; open it via the gear icon)

  // --- Status row: colored dot + state + compact stats ---
  statusDot_   = new QLabel; statusDot_->setObjectName("statusDot");
  streamLabel_ = new QLabel("Stopped"); streamLabel_->setObjectName("statusText");
  statsLabel_  = new QLabel("—");       statsLabel_->setObjectName("statusStats");
  setStatusDot(kDotStopped);
  auto* statusRow = new QHBoxLayout;
  statusRow->addWidget(statusDot_); statusRow->addSpacing(8);
  statusRow->addWidget(streamLabel_); statusRow->addStretch();
  statusRow->addWidget(statsLabel_);
  deviceLabel_ = new QLabel; deviceLabel_->setObjectName("caption");
  deviceLabel_->hide();   // shown only to surface an adb hint (unauthorized / not found)

  // --- Start/Stop + log ---
  startBtn_ = new QPushButton("▶  Start streaming");
  startBtn_->setObjectName("startButton");

  // (auth setup moved to the Settings menu → "Remember authentication")

  // --- Devices on network (mDNS-discovered tablets) ---
  devicesList_ = new QListWidget;
  connectBtn_ = new QPushButton("Connect");
  auto* devicesLayout = new QVBoxLayout;
  devicesLayout->addWidget(devicesList_);
  devicesLayout->addWidget(connectBtn_);
  devicesBox_ = new QGroupBox("Available clients");
  devicesBox_->setLayout(devicesLayout);

  auto* root = new QVBoxLayout;
  root->setContentsMargins(16, 16, 16, 16);
  root->setSpacing(12);
  root->addLayout(headerRow);
  root->addLayout(profRow);
  root->addLayout(statusRow);
  root->addWidget(deviceLabel_);
  root->addWidget(startBtn_);
  root->addWidget(devicesBox_, 1);   // the client list now fills the space the log used to
  auto* central = new QWidget; central->setLayout(root);
  setCentralWidget(central);
  resize(600, 560);

  // --- Wiring ---
  connect(startBtn_, &QPushButton::clicked, this, &MainWindow::onStartStop);
  connect(saveBtn, &QPushButton::clicked, this, [this]{
    const QString n = profileBox_->currentText();
    if (!n.isEmpty()) { store_.save(n, collectSettings()); store_.setLastUsed(n); refreshProfiles(); }
  });
  connect(saveAsBtn, &QPushButton::clicked, this, [this]{
    bool ok; QString n = QInputDialog::getText(this, "Save profile", "Name:",
                                               QLineEdit::Normal, "", &ok);
    if (ok && !n.isEmpty()) { store_.save(n, collectSettings()); store_.setLastUsed(n);
                              refreshProfiles(); profileBox_->setCurrentText(n); }
  });
  connect(delBtn, &QPushButton::clicked, this, [this]{
    if (!profileBox_->currentText().isEmpty()) { store_.remove(profileBox_->currentText()); refreshProfiles(); }
  });
  connect(profileBox_, &QComboBox::currentTextChanged, this, [this](const QString& n){
    Settings s; if (!n.isEmpty() && store_.load(n, s)) { applySettings(s); store_.setLastUsed(n); }
  });

  connect(&controller_, &StreamController::logLine, this, [](const QString& l){ qInfo("%s", qUtf8Printable(l)); });
  connect(&controller_, &StreamController::statsReceived, this, [this](const Stats& s){
    if (s.client_connected) {
      setStatusDot(kDotConnected);
      streamLabel_->setText("Connected");
      statsLabel_->setText(QString("fps %1 · %2 KB · enc %3 ms")
          .arg(s.fps,0,'f',0).arg(s.frame_kb_avg,0,'f',0).arg(s.encode_ms_avg,0,'f',1));
    } else {
      setStatusDot(kDotWaiting);
      streamLabel_->setText("Waiting for client");
      statsLabel_->setText("—");
    }
  });
  connect(&controller_, &StreamController::runningChanged, this, [this](bool r){
    setRunningUi(r);
    // Keep advertising this PC whether idle or streaming so tablets can discover it
    // either way (detection is bidirectional). On stream start, refresh in case the
    // configured port changed; never stop on stream end — only at app close.
    if (r) refreshAdvertising(); else hidePairingPopup();
  });
  // A device connected (or is probing to pair) -> show the pairing code right then.
  connect(&controller_, &StreamController::connecting, this,
          [this](const QString& ip){ showPairingPopup(ip); });
  connect(&controller_, &StreamController::approvalRequested, this,
    [this](const QString& id, const QString& name, const QString& ip){
      hidePairingPopup();   // device got past TLS pairing (sent HELLO) — code no longer needed
      // Auto-approve a peer we just woke ourselves (the PC already chose it by
      // clicking Connect on the Devices panel) — skip the remembered-id/dialog path.
      const qint64 woken = pendingWakes_.value(ip, 0);
      if (woken && QDateTime::currentMSecsSinceEpoch() - woken < 15000) {
        pendingWakes_.remove(ip);
        const QString key = id.isEmpty() ? ip : id;
        approved_.approve(key);                 // remember it too
        controller_.writeLine("approve " + key);
        return;
      }
      const QString key = id.isEmpty() ? ip : id;
      if (approved_.isApproved(key)) { controller_.writeLine("approve " + key); return; }
      auto btn = QMessageBox::question(this, "Allow connection?",
          QString("Allow \"%1\" (%2) to connect?").arg(name.isEmpty()?ip:name, ip));
      if (btn == QMessageBox::Yes) { approved_.approve(key); controller_.writeLine("approve " + key); }
      else controller_.writeLine("deny " + key);
    });
  // Unified "Available clients" list: network (mDNS) + USB (adb) sources.
  connect(&browser_, &MdnsBrowser::devicesChanged, this, &MainWindow::onDevicesChanged);
  connect(&usbScanner_, &UsbClientScanner::clientsChanged, this, &MainWindow::onUsbClientsChanged);
  connect(&usbScanner_, &UsbClientScanner::statusChanged, this, [this](const QString& s){
    deviceLabel_->setText(s);
    deviceLabel_->setVisible(!s.isEmpty());   // hide when there's nothing to say
  });
  connect(connectBtn_, &QPushButton::clicked, this, &MainWindow::onConnectToSelectedDevice);
  connect(devicesList_, &QListWidget::itemDoubleClicked, this,
          [this](QListWidgetItem*){ onConnectToSelectedDevice(); });

  if (browser_.available()) browser_.start();
  if (usbScanner_.available()) usbScanner_.start();
  if (!browser_.available() && !usbScanner_.available()) devicesBox_->hide();

  refreshProfiles();
  restoreLastProfile();   // re-apply the profile that was in use last launch
  refreshAdvertising();   // publish this PC on the network from launch (idle-discoverable)

  audioSink_.ensure();   // create/adopt the droppix-audio sink for this session
  setupTray();           // tray icon for "minimize to tray on close" (if a tray exists)

  pairingHideTimer_ = new QTimer(this);
  pairingHideTimer_->setSingleShot(true);
  connect(pairingHideTimer_, &QTimer::timeout, this, &MainWindow::hidePairingPopup);
}

void MainWindow::showPairingPopup(const QString& ip) {
  if (ip == "127.0.0.1") return;   // USB / localhost is pairing-exempt — no code needed
  if (!pairingPopup_) {
    pairingPopup_ = new QDialog(this);
    pairingPopup_->setWindowTitle("Pairing");
    pairingPopup_->setModal(false);
    auto* v = new QVBoxLayout(pairingPopup_);
    pairingInfo_ = new QLabel; pairingInfo_->setAlignment(Qt::AlignCenter); pairingInfo_->setWordWrap(true);
    pairingCodeLabel_ = new QLabel; pairingCodeLabel_->setAlignment(Qt::AlignCenter);
    pairingCodeLabel_->setStyleSheet("font-size:34px; font-weight:700; letter-spacing:6px; color:#14b8a6;");
    auto* closeBtn = new QPushButton("Close");
    connect(closeBtn, &QPushButton::clicked, this, &MainWindow::hidePairingPopup);
    v->addWidget(pairingInfo_);
    v->addWidget(pairingCodeLabel_);
    v->addWidget(closeBtn);
  }
  pairingInfo_->setText(QString("A device (%1) is connecting.\nEnter this pairing code on the tablet:").arg(ip));
  pairingCodeLabel_->setText(cert_.pairingCode());
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

bool MainWindow::minimizeToTrayRequested() const {
  return QFile::exists(configDir() + "/minimize_on_close");
}

void MainWindow::onDevicesChanged(const QList<MdnsDevice>& devices) {
  netDevices_ = devices;
  rebuildClientList();
}

void MainWindow::onUsbClientsChanged(const QList<UsbClient>& clients) {
  usbClients_ = clients;
  rebuildClientList();
}

// Repopulates the unified list from both sources (USB first, then network),
// tagging each item with its transport + connect payload and preserving the
// prior selection by label. Roles: UserRole = transport ("usb"/"net");
// UserRole+1 = usb serial OR net address; UserRole+2 = net wake port.
void MainWindow::rebuildClientList() {
  const QString prevSelected = devicesList_->currentItem()
      ? devicesList_->currentItem()->text() : QString();
  devicesList_->clear();

  for (const auto& c : usbClients_) {
    auto* item = new QListWidgetItem(QString("%1 — USB").arg(c.model));
    item->setData(Qt::UserRole, "usb");
    item->setData(Qt::UserRole + 1, c.serial);
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
    devicesList_->addItem(item);
    if (item->text() == prevSelected) devicesList_->setCurrentItem(item);
  }
}

void MainWindow::onConnectToSelectedDevice() {
  auto* item = devicesList_->currentItem();
  if (!item) return;
  const QString transport = item->data(Qt::UserRole).toString();
  const int port = collectSettings().port;   // PC stream port the tablet will dial

  if (transport == "usb") {
    const QString serial = item->data(Qt::UserRole + 1).toString();
    if (serial.isEmpty()) return;
    if (!controller_.running()) onStartStop();
    adb_.usbConnect(serial, port);            // adb reverse + launch app on the tablet
    return;
  }

  // network: WAKE the tablet, which then dials the PC
  const QString addr = item->data(Qt::UserRole + 1).toString();
  const quint16 wakePort = (quint16)item->data(Qt::UserRole + 2).toUInt();
  if (addr.isEmpty()) return;
  if (!controller_.running()) onStartStop();
  auto bytes = encode_wake((uint16_t)port);
  QByteArray dg(reinterpret_cast<const char*>(bytes.data()), (int)bytes.size());
  pendingWakes_[addr] = QDateTime::currentMSecsSinceEpoch();
  QUdpSocket sock;
  sock.writeDatagram(dg, QHostAddress(addr), wakePort);
}

void MainWindow::refreshAdvertising() {
  if (!advertiser_.available()) return;
  const quint16 p = (quint16)collectSettings().port;
  if (p == advertisedPort_) return;   // already publishing this port — no churn
  advertiser_.stop();
  advertiser_.start(p);
  advertisedPort_ = p;
}

Settings MainWindow::collectSettings() const {
  Settings s;
  settingsDialog_->store(s);   // source/resolution/touch/audio/fps/bitrate/port/refresh/orientation/auto-adb/overlay
  s.tls = true;
  // In a Flatpak the streamer runs on the host, so hand it the host-mirrored cert/key paths.
  if (!flatpakHostCert_.isEmpty()) {
    s.certPath = flatpakHostCert_.toStdString();
    s.keyPath  = flatpakHostKey_.toStdString();
  } else {
    s.certPath = cert_.certPath().toStdString();
    s.keyPath  = cert_.keyPath().toStdString();
  }
  return s;
}

void MainWindow::applySettings(const Settings& s) {
  settingsDialog_->load(s);    // source/resolution/touch/audio/fps/bitrate/port/refresh/orientation/auto-adb/overlay
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

  QTemporaryFile tmp;
  if (!tmp.open()) {
    QMessageBox::warning(this, "Droppix", "Couldn't create a temporary file for auth setup.");
    return;
  }
  tmp.write(rule.toUtf8());
  tmp.flush();

  // pkexec prompts once, then installs the rule as root (mode 0644 so polkitd can read it).
  int rc = QProcess::execute("pkexec", {"/usr/bin/install", "-m", "0644", tmp.fileName(),
                                        "/etc/polkit-1/rules.d/49-droppix.rules"});
  if (rc == 0) {
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

void MainWindow::onStartStop() {
  if (controller_.running()) { controller_.stop(); return; }
  Settings s = collectSettings();
  Command cmd = build_command(s, streamBin_);
  if (cmd.needs_adb_reverse) adb_.setupReverse(s.port);
  qInfo("$ %s ...", cmd.program.c_str());
  controller_.start(cmd);
}

void MainWindow::setStatusDot(const char* color) {
  statusDot_->setStyleSheet(QString(
      "background:%1; border-radius:6px;"
      "min-width:12px; max-width:12px; min-height:12px; max-height:12px;").arg(color));
}

void MainWindow::setRunningUi(bool running) {
  startBtn_->setText(running ? "■  Stop" : "▶  Start streaming");
  startBtn_->setProperty("running", running);   // drives the red Stop style (QSS)
  startBtn_->style()->unpolish(startBtn_);
  startBtn_->style()->polish(startBtn_);
  if (running) {
    setStatusDot(kDotWaiting); streamLabel_->setText("Waiting for client");
  } else {
    setStatusDot(kDotStopped); streamLabel_->setText("Stopped");
  }
  statsLabel_->setText("—");
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
  controller_.stop();          // don't orphan the streamer on quit
  advertiser_.stop();
  browser_.stop();
  usbScanner_.stop();
  QMainWindow::closeEvent(event);
}
}  // namespace droppix
