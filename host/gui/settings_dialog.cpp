#include "settings_dialog.h"
#include <QtWidgets>

namespace droppix {

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
  touch_ = new QCheckBox("Touch input (evdi only — tap/drag the cursor)");
  audio_ = new QCheckBox("Stream audio to tablet (route an app's output to 'droppix-audio')");
  autoReverse_ = new QCheckBox("Auto adb reverse on start"); autoReverse_->setChecked(true);
  overlay_ = new QCheckBox("Show performance overlay on the tablet (RTT / fps / decode)");
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
  form->addRow("Orientation (default):", orientation_);
  form->addRow("", touch_);
  form->addRow("", audio_);
  form->addRow("", autoReverse_);
  form->addRow("", overlay_);

  auto* rememberAuth = new QPushButton("Remember authentication (ask once per login)");
  connect(rememberAuth, &QPushButton::clicked, this, &SettingsDialog::rememberAuthRequested);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);

  auto* root = new QVBoxLayout(this);
  root->addLayout(form);
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
  autoReverse_->setChecked(s.auto_adb_reverse);
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
  s.auto_adb_reverse = autoReverse_->isChecked();
  s.overlay = overlay_->isChecked();
}

}  // namespace droppix
