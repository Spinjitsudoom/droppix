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
  resolution_ = new QComboBox;
  resolution_->addItems({"640x480", "800x600", "960x540", "1024x576", "1024x640",
                         "1280x720", "1920x1080", "2560x1440",
                         "1280x800", "1920x1200", "2560x1600"});
  resolution_->setCurrentText("1920x1080");
  fps_ = new QSpinBox; fps_->setRange(1, 120); fps_->setValue(30);
  bitrate_ = new QSpinBox; bitrate_->setRange(500, 60000); bitrate_->setSuffix(" kbps"); bitrate_->setValue(8000);
  port_ = new QSpinBox; port_->setRange(1024, 65535); port_->setValue(27000);
  refresh_ = new QComboBox; refresh_->addItems({"30", "60"}); refresh_->setCurrentText("60");
  orientation_ = new QComboBox;   // evdi only — start-up default rotation (live tablet rotation overrides)
  orientation_->addItem("Landscape (0°)", 0);
  orientation_->addItem("Portrait (90°)", 90);
  orientation_->addItem("Inverted (180°)", 180);
  orientation_->addItem("Portrait flipped (270°)", 270);
  touch_ = new QCheckBox("Touch");
  audio_ = new QCheckBox("Audio");
  overlay_ = new QCheckBox("Performance Overlay");
  connect(overlay_, &QCheckBox::toggled, this, &SettingsDialog::overlayToggled);  // live toggle

  auto* form = new QFormLayout;
  form->setVerticalSpacing(10);
  form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
  auto* srcRow = new QHBoxLayout;
  srcRow->addWidget(srcEvdi_); srcRow->addSpacing(16); srcRow->addWidget(srcTest_); srcRow->addStretch();
  form->addRow("Source:", srcRow);
  form->addRow("Resolution:", resolution_);
  form->addRow("FPS:", fps_);
  form->addRow("Bitrate:", bitrate_);
  form->addRow("Port:", port_);
  form->addRow("Refresh (Hz):", refresh_);
  // Bitrate/Port are hidden from the UI but kept functional — they still carry
  // their default (8000 kbps / 27000) or a profile's persisted value.
  form->setRowVisible(bitrate_, false);
  form->setRowVisible(port_, false);
  form->addRow("Orientation (default):", orientation_);
  form->addRow("", touch_);
  form->addRow("", audio_);
  form->addRow("", overlay_);

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
  root->addStretch();
  root->addWidget(buttons);
}

void SettingsDialog::load(const Settings& s) {
  srcEvdi_->setChecked(s.source == Settings::Source::Evdi);
  srcTest_->setChecked(s.source == Settings::Source::TestPattern);
  resolution_->setCurrentText(QString("%1x%2").arg(s.width).arg(s.height));
  touch_->setChecked(s.touch);
  audio_->setChecked(s.audio);
  fps_->setValue(s.fps);
  bitrate_->setValue(s.bitrate_kbps);
  port_->setValue(s.port);
  refresh_->setCurrentText(QString::number(s.refresh_hz));
  int i = orientation_->findData(s.orientation);
  orientation_->setCurrentIndex(i >= 0 ? i : 0);
  overlay_->setChecked(s.overlay);
}

void SettingsDialog::store(Settings& s) const {
  s.source = srcEvdi_->isChecked() ? Settings::Source::Evdi : Settings::Source::TestPattern;
  const QStringList wh = resolution_->currentText().split('x');
  s.width = wh.value(0).toInt(); s.height = wh.value(1).toInt();
  s.touch = touch_->isChecked();
  s.audio = audio_->isChecked();
  s.fps = fps_->value();
  s.bitrate_kbps = bitrate_->value();
  s.port = port_->value();
  s.refresh_hz = refresh_->currentText().toInt();
  s.orientation = orientation_->currentData().toInt();
  s.auto_adb_reverse = true;   // always on now (option removed from the GUI); USB just works
  s.overlay = overlay_->isChecked();
}

}  // namespace droppix
