#include "settings_dialog.h"
#include <QtWidgets>

namespace droppix {

namespace {
QString autostartPath() {
  return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
         + "/autostart/droppix.desktop";
}
QString appConfigDir() {
  return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
}
QString minimizeMarkerPath() { return appConfigDir() + "/minimize_on_close"; }

// Write (or remove) the XDG autostart entry that launches droppix at login.
void setLaunchAtLogin(bool on) {
  const QString path = autostartPath();
  if (!on) { QFile::remove(path); return; }
  QDir().mkpath(QFileInfo(path).absolutePath());
  // For an AppImage, $APPIMAGE is the real .AppImage path; applicationFilePath()
  // would point inside the mount, which is gone after exit.
  QString exec = qEnvironmentVariable("APPIMAGE");
  if (exec.isEmpty()) exec = QCoreApplication::applicationFilePath();
  QFile f(path);
  if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    QTextStream(&f)
        << "[Desktop Entry]\n" << "Type=Application\n" << "Name=Droppix\n"
        << "Comment=Use a tablet as a second monitor\n"
        << "Exec=" << exec << "\n" << "Icon=droppix\n" << "Terminal=false\n"
        << "X-GNOME-Autostart-enabled=true\n";
  }
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

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle("droppix — Settings");
  setModal(true);

  srcTest_ = new QRadioButton("Test pattern (debug)");
  srcEvdi_ = new QRadioButton("Real monitor (evdi)");
  srcEvdi_->setChecked(true);
  bitrate_ = new QSpinBox; bitrate_->setRange(500, 60000); bitrate_->setSuffix(" kbps"); bitrate_->setValue(8000);
  port_ = new QSpinBox; port_->setRange(1024, 65535); port_->setValue(27000);
  refresh_ = new QComboBox; refresh_->addItems({"30", "60"}); refresh_->setCurrentText("60");
  touch_ = new QCheckBox("Touch");
  overlay_ = new QCheckBox("Performance Overlay");
  connect(overlay_, &QCheckBox::toggled, this, &SettingsDialog::overlayToggled);  // live toggle
  autoConnect_ = new QCheckBox("Auto-connect known monitors");

  auto* form = new QFormLayout;
  form->setVerticalSpacing(10);
  form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
  auto* srcRow = new QHBoxLayout;
  srcRow->addWidget(srcEvdi_); srcRow->addSpacing(16); srcRow->addWidget(srcTest_); srcRow->addStretch();
  form->addRow("Source:", srcRow);
  form->addRow("Bitrate:", bitrate_);
  form->addRow("Port:", port_);
  form->addRow("Refresh (Hz):", refresh_);
  // Bitrate/Port are hidden from the UI but kept functional — they still carry
  // their default (8000 kbps / 27000) or a profile's persisted value.
  form->setRowVisible(bitrate_, false);
  form->setRowVisible(port_, false);
  form->addRow("", touch_);
  form->addRow("", overlay_);
  form->addRow("", autoConnect_);

  // --- App-level section (global prefs, file-backed; independent of profiles) ---
  auto* appLabel = new QLabel("Application"); appLabel->setObjectName("caption");
  launchAtLogin_ = new QCheckBox("Launch Droppix at login");
  minimizeOnClose_ = new QCheckBox("Minimize to tray on close");
  launchAtLogin_->setChecked(QFile::exists(autostartPath()));
  minimizeOnClose_->setChecked(QFile::exists(minimizeMarkerPath()));
  connect(launchAtLogin_, &QCheckBox::toggled, this, [](bool on){ setLaunchAtLogin(on); });
  connect(minimizeOnClose_, &QCheckBox::toggled, this, [](bool on){ setMinimizeOnClose(on); });

  auto* rememberAuth = new QPushButton("Remember authentication (never ask again)");
  connect(rememberAuth, &QPushButton::clicked, this, &SettingsDialog::rememberAuthRequested);
  auto* manageDevices = new QPushButton("Manage remembered devices…");
  connect(manageDevices, &QPushButton::clicked, this, &SettingsDialog::manageDevicesRequested);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);

  auto* root = new QVBoxLayout(this);
  root->addLayout(form);
  root->addSpacing(6);
  root->addWidget(appLabel);
  root->addWidget(launchAtLogin_);
  root->addWidget(minimizeOnClose_);
  root->addSpacing(6);
  root->addWidget(rememberAuth);
  root->addWidget(manageDevices);
  root->addStretch();
  root->addWidget(buttons);
}

void SettingsDialog::load(const Settings& s) {
  srcEvdi_->setChecked(s.source == Settings::Source::Evdi);
  srcTest_->setChecked(s.source == Settings::Source::TestPattern);
  touch_->setChecked(s.touch);
  bitrate_->setValue(s.bitrate_kbps);
  port_->setValue(s.port);
  refresh_->setCurrentText(QString::number(s.refresh_hz));
  overlay_->setChecked(s.overlay);
  autoConnect_->setChecked(s.autoConnect);
}

void SettingsDialog::store(Settings& s) const {
  s.source = srcEvdi_->isChecked() ? Settings::Source::Evdi : Settings::Source::TestPattern;
  s.touch = touch_->isChecked();
  s.bitrate_kbps = bitrate_->value();
  s.port = port_->value();
  s.refresh_hz = refresh_->currentText().toInt();
  // width/height/fps/audio/orientation are left unset here: they keep the Settings
  // struct defaults, used only as pre-v4 fallbacks (the client now drives these
  // per-session via HELLO; the GUI no longer exposes them).
  s.auto_adb_reverse = true;   // always on now (option removed from the GUI); USB just works
  s.overlay = overlay_->isChecked();
  s.autoConnect = autoConnect_->isChecked();
}

}  // namespace droppix
