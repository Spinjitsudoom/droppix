#include "settings_page.h"
#include "autostart.h"
#include <QtWidgets>

namespace droppix {

namespace {
// --- ported verbatim from settings_dialog.cpp (app-level prefs are file-backed,
// independent of the profile store) ---
QString autostartPath() {
  return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
         + "/autostart/droppix.desktop";
}
QString appConfigDir() {
  return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
}
QString minimizeMarkerPath() { return appConfigDir() + "/minimize_on_close"; }

// Write (or remove) the XDG autostart entry that launches droppix at login.
// Exec resolution + .desktop text live in the pure, unit-tested autostart.{h,cpp}
// (handles the AppImage / Flatpak / plain-binary cases and appends --minimized).
void setLaunchAtLogin(bool on) {
  const QString path = autostartPath();
  if (!on) { QFile::remove(path); return; }
  QDir().mkpath(QFileInfo(path).absolutePath());
  const AutostartEnv env{ qEnvironmentVariable("APPIMAGE").toStdString(),
                          qEnvironmentVariable("FLATPAK_ID").toStdString(),
                          QCoreApplication::applicationFilePath().toStdString() };
  const std::string content = autostart_desktop(autostart_exec_command(env));
  QFile f(path);
  if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    f.write(content.c_str(), static_cast<qint64>(content.size()));
}

// Toggle the marker file MainWindow::closeEvent checks for minimize-to-tray.
void setMinimizeOnClose(bool on) {
  if (on) {
    QDir().mkpath(appConfigDir());
    QFile f(minimizeMarkerPath());
    if (f.open(QIODevice::WriteOnly)) { f.write("1"); }
  } else {
    QFile::remove(minimizeMarkerPath());
  }
}
}  // namespace

SettingsPage::SettingsPage(QWidget* parent) : QWidget(parent) {
  // ===================== Stream card =====================
  auto* streamCard = new QFrame(this);
  streamCard->setObjectName("card");
  auto* streamHeader = new QLabel("Stream", streamCard);
  streamHeader->setObjectName("header");

  srcTest_ = new QRadioButton("Test pattern (debug)");
  srcEvdi_ = new QRadioButton("Real monitor (evdi)");
  srcEvdi_->setChecked(true);
  resolution_ = new QComboBox;
  resolution_->addItems({"1280×800", "1920×1080", "Match client"});
  fps_ = new QComboBox;
  fps_->addItems({"30", "60"});
  fps_->setCurrentText("30");
  bitrate_ = new QSpinBox; bitrate_->setRange(500, 60000); bitrate_->setSuffix(" kbps"); bitrate_->setValue(8000);
  bitrate_->setObjectName("bitrateSpin");
  port_ = new QSpinBox; port_->setRange(1024, 65535); port_->setValue(27000);
  refresh_ = new QComboBox; refresh_->addItems({"30", "60"}); refresh_->setCurrentText("60");

  auto* streamForm = new QFormLayout;
  streamForm->setVerticalSpacing(10);
  streamForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
  auto* srcRow = new QHBoxLayout;
  srcRow->addWidget(srcEvdi_); srcRow->addSpacing(16); srcRow->addWidget(srcTest_); srcRow->addStretch();
  streamForm->addRow("Source:", srcRow);
  streamForm->addRow("Resolution:", resolution_);
  streamForm->addRow("FPS:", fps_);
  streamForm->addRow("Bitrate:", bitrate_);
  streamForm->addRow("Port:", port_);
  streamForm->addRow("Refresh (Hz):", refresh_);

  auto* streamCardLayout = new QVBoxLayout;
  streamCardLayout->addWidget(streamHeader);
  streamCardLayout->addLayout(streamForm);
  streamCard->setLayout(streamCardLayout);

  // ===================== Session & input card =====================
  auto* sessionCard = new QFrame(this);
  sessionCard->setObjectName("card");
  auto* sessionHeader = new QLabel("Session & input", sessionCard);
  sessionHeader->setObjectName("header");

  touch_ = new QCheckBox("Touch");
  audio_ = new QCheckBox("Audio (stream PC audio to the tablet)");
  orientation_ = new QComboBox; orientation_->addItems({"0", "90", "180", "270"});
  overlay_ = new QCheckBox("Performance Overlay");
  connect(overlay_, &QCheckBox::toggled, this, &SettingsPage::overlayToggled);  // live toggle
  autoConnect_ = new QCheckBox("Auto-connect known monitors");
  webClient_ = new QCheckBox("Offer web / PWA client (HTTPS on session port)");

  auto* sessionForm = new QFormLayout;
  sessionForm->setVerticalSpacing(10);
  sessionForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
  sessionForm->addRow("", touch_);
  sessionForm->addRow("", audio_);
  sessionForm->addRow("Orientation:", orientation_);
  sessionForm->addRow("", overlay_);
  sessionForm->addRow("", autoConnect_);
  sessionForm->addRow("", webClient_);

  auto* sessionCardLayout = new QVBoxLayout;
  sessionCardLayout->addWidget(sessionHeader);
  sessionCardLayout->addLayout(sessionForm);
  sessionCard->setLayout(sessionCardLayout);

  // ===================== Application card =====================
  auto* appCard = new QFrame(this);
  appCard->setObjectName("card");
  auto* appHeader = new QLabel("Application", appCard);
  appHeader->setObjectName("header");

  themeDark_ = new QRadioButton("Dark");
  themeLight_ = new QRadioButton("Light");
  themeDark_->setObjectName("themeDark");
  themeDark_->setChecked(true);
  connect(themeDark_, &QRadioButton::toggled, this, [this](bool on){
    if (on) emit themeChangeRequested(Theme::Dark);
  });
  connect(themeLight_, &QRadioButton::toggled, this, [this](bool on){
    if (on) emit themeChangeRequested(Theme::Light);
  });
  auto* themeRow = new QHBoxLayout;
  themeRow->addWidget(themeDark_); themeRow->addSpacing(16); themeRow->addWidget(themeLight_); themeRow->addStretch();

  auto* appForm = new QFormLayout;
  appForm->setVerticalSpacing(10);
  appForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
  appForm->addRow("Theme:", themeRow);

  launchAtLogin_ = new QCheckBox("Launch Droppix at login");
  minimizeOnClose_ = new QCheckBox("Minimize to tray on close");
  launchAtLogin_->setChecked(QFile::exists(autostartPath()));
  minimizeOnClose_->setChecked(QFile::exists(minimizeMarkerPath()));
  connect(launchAtLogin_, &QCheckBox::toggled, this, [](bool on){ setLaunchAtLogin(on); });
  connect(minimizeOnClose_, &QCheckBox::toggled, this, [](bool on){ setMinimizeOnClose(on); });

  auto* rememberAuth = new QPushButton("Remember authentication (never ask again)");
  connect(rememberAuth, &QPushButton::clicked, this, &SettingsPage::rememberAuthRequested);
  auto* manageDevices = new QPushButton("Manage remembered devices…");
  connect(manageDevices, &QPushButton::clicked, this, &SettingsPage::manageDevicesRequested);

  auto* appCardLayout = new QVBoxLayout;
  appCardLayout->addWidget(appHeader);
  appCardLayout->addLayout(appForm);
  appCardLayout->addWidget(launchAtLogin_);
  appCardLayout->addWidget(minimizeOnClose_);
  appCardLayout->addSpacing(6);
  appCardLayout->addWidget(rememberAuth);
  appCardLayout->addWidget(manageDevices);
  appCard->setLayout(appCardLayout);

  auto* root = new QVBoxLayout(this);
  root->addWidget(streamCard);
  root->addWidget(sessionCard);
  root->addWidget(appCard);
  root->addStretch();

  // --- live server refresh trigger: streamer-affecting controls only. NOT
  // overlay_ (already live via stdin), NOT autoConnect_ (discovery behavior),
  // NOT the theme/app-pref controls (no server impact). ---
  for (QRadioButton* r : {srcTest_, srcEvdi_})
    connect(r, &QRadioButton::toggled, this, &SettingsPage::settingsChanged);
  for (QComboBox* c : {resolution_, fps_, refresh_, orientation_})
    connect(c, &QComboBox::currentIndexChanged, this, &SettingsPage::settingsChanged);
  for (QSpinBox* s : {bitrate_, port_})
    connect(s, QOverload<int>::of(&QSpinBox::valueChanged), this, &SettingsPage::settingsChanged);
  for (QCheckBox* cb : {touch_, audio_, webClient_})
    connect(cb, &QCheckBox::toggled, this, &SettingsPage::settingsChanged);
}

void SettingsPage::load(const Settings& s) {
  // Block the streamer-trigger controls for the whole function: load() is a
  // programmatic widgets<-settings sync, not a user edit, so it must not fire
  // settingsChanged() (that signal exists to trigger a live server restart).
  const QSignalBlocker bSrcT(srcTest_), bSrcE(srcEvdi_), bRes(resolution_), bFps(fps_),
      bBit(bitrate_), bPort(port_), bRef(refresh_), bTouch(touch_), bAudio(audio_),
      bOri(orientation_), bWeb(webClient_);

  srcEvdi_->setChecked(s.source == Settings::Source::Evdi);
  srcTest_->setChecked(s.source == Settings::Source::TestPattern);
  touch_->setChecked(s.touch);
  bitrate_->setValue(s.bitrate_kbps);
  port_->setValue(s.port);
  refresh_->setCurrentText(QString::number(s.refresh_hz));
  overlay_->setChecked(s.overlay);
  autoConnect_->setChecked(s.autoConnect);
  webClient_->setChecked(s.webClient);

  // --- surfaced fields (new): width/height/fps/audio/orientation were left unset by
  // the old SettingsDialog::load() — now wired to their own widgets. ---
  if (s.width == 1280 && s.height == 800) resolution_->setCurrentText("1280×800");
  else if (s.width == 1920 && s.height == 1080) resolution_->setCurrentText("1920×1080");
  else resolution_->setCurrentText("Match client");   // 0/0 or any other pair
  fps_->setCurrentText(QString::number(s.fps));
  audio_->setChecked(s.audio);
  orientation_->setCurrentText(QString::number(s.orientation));
}

void SettingsPage::store(Settings& s) const {
  s.source = srcEvdi_->isChecked() ? Settings::Source::Evdi : Settings::Source::TestPattern;
  s.touch = touch_->isChecked();
  s.bitrate_kbps = bitrate_->value();
  s.port = port_->value();
  s.refresh_hz = refresh_->currentText().toInt();
  s.auto_adb_reverse = true;   // always on now (option removed from the GUI); USB just works
  s.overlay = overlay_->isChecked();
  s.autoConnect = autoConnect_->isChecked();
  s.webClient = webClient_->isChecked();

  // --- surfaced fields (new) ---
  const QString res = resolution_->currentText();
  if (res == "1280×800") { s.width = 1280; s.height = 800; }
  else if (res == "1920×1080") { s.width = 1920; s.height = 1080; }
  else { s.width = 0; s.height = 0; }   // "Match client"
  s.fps = fps_->currentText().toInt();
  s.audio = audio_->isChecked();
  s.orientation = orientation_->currentText().toInt();
}

void SettingsPage::setTheme(Theme t) {
  const QSignalBlocker b1(themeDark_);
  const QSignalBlocker b2(themeLight_);
  themeDark_->setChecked(t == Theme::Dark);
  themeLight_->setChecked(t == Theme::Light);
}

}  // namespace droppix
