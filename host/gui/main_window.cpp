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
#include "wake.h"

namespace droppix {

static QString configDir() {
  return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      store_(configDir()),
      approved_(configDir()),
      cert_(configDir()) {
  streamBin_ = (QCoreApplication::applicationDirPath() + "/droppix_stream").toStdString();
  const bool certsReady = cert_.ensure();
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
  pairingLabel_ = new QLabel(certsReady
      ? "Pairing code: " + cert_.pairingCode()
      : "Pairing code: unavailable");
  pairingLabel_->setObjectName("caption");

  // --- Start/Stop + log ---
  startBtn_ = new QPushButton("▶  Start streaming");
  startBtn_->setObjectName("startButton");

  // (auth setup moved to the Settings menu → "Remember authentication")

  // --- Devices on network (mDNS-discovered tablets) ---
  devicesList_ = new QListWidget;
  devicesList_->setMaximumHeight(120);
  connectBtn_ = new QPushButton("Connect");
  auto* devicesLayout = new QVBoxLayout;
  devicesLayout->addWidget(devicesList_);
  devicesLayout->addWidget(connectBtn_);
  devicesBox_ = new QGroupBox("Available clients");
  devicesBox_->setLayout(devicesLayout);

  auto* logCaption = new QLabel("Log"); logCaption->setObjectName("caption");
  log_ = new QPlainTextEdit; log_->setReadOnly(true);
  log_->setMaximumBlockCount(1000);
  log_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

  auto* root = new QVBoxLayout;
  root->setContentsMargins(16, 16, 16, 16);
  root->setSpacing(12);
  root->addLayout(headerRow);
  root->addLayout(profRow);
  root->addLayout(statusRow);
  root->addWidget(deviceLabel_);
  root->addWidget(pairingLabel_);
  root->addWidget(startBtn_);
  root->addWidget(devicesBox_);
  root->addWidget(logCaption);
  root->addWidget(log_, 1);
  auto* central = new QWidget; central->setLayout(root);
  setCentralWidget(central);
  resize(600, 720);

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

  connect(&controller_, &StreamController::logLine, this, [this](const QString& l){ log_->appendPlainText(l); });
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
    if (r) advertiser_.start(collectSettings().port); else advertiser_.stop();
  });
  connect(&controller_, &StreamController::approvalRequested, this,
    [this](const QString& id, const QString& name, const QString& ip){
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

  audioSink_.ensure();   // create/adopt the droppix-audio sink for this session
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

Settings MainWindow::collectSettings() const {
  Settings s;
  settingsDialog_->store(s);   // source/resolution/touch/audio/fps/bitrate/port/refresh/orientation/auto-adb/overlay
  s.tls = true;
  s.certPath = cert_.certPath().toStdString();
  s.keyPath = cert_.keyPath().toStdString();
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
  // Start asks for the password once per login (AUTH_SELF_KEEP) instead of every time.
  // One pkexec prompt now writes the rule as root.
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
      "        return polkit.Result.AUTH_SELF_KEEP;\n"
      "    }\n"
      "});\n").arg(user, bin, binAlt);

  QTemporaryFile tmp;
  if (!tmp.open()) { log_->appendPlainText("auth setup: could not create a temp file"); return; }
  tmp.write(rule.toUtf8());
  tmp.flush();

  // pkexec prompts once, then installs the rule as root (mode 0644 so polkitd can read it).
  int rc = QProcess::execute("pkexec", {"/usr/bin/install", "-m", "0644", tmp.fileName(),
                                        "/etc/polkit-1/rules.d/49-droppix.rules"});
  if (rc == 0) {
    QFile marker(configDir() + "/auth_configured");
    if (marker.open(QIODevice::WriteOnly)) { marker.write("1"); marker.close(); }
    log_->appendPlainText("Authentication remembered. Start will prompt once per login, then stay quiet.");
  } else {
    log_->appendPlainText("Authentication setup was cancelled or failed (pkexec exit " +
                          QString::number(rc) + ").");
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
  log_->appendPlainText("$ " + QString::fromStdString(cmd.program) + " ...");
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
  controller_.stop();          // don't orphan the streamer on quit
  advertiser_.stop();
  browser_.stop();
  usbScanner_.stop();
  QMainWindow::closeEvent(event);
}
}  // namespace droppix
