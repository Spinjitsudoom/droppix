#include "main_window.h"
#include "args_builder.h"
#include "style.h"
#include <QtWidgets>
#include <QCloseEvent>
#include <QCoreApplication>

namespace droppix {

static QString configDir() {
  return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      store_(configDir()) {
  streamBin_ = (QCoreApplication::applicationDirPath() + "/droppix_stream").toStdString();
  setWindowTitle("droppix");

  // --- Header ---
  auto* logo = new QLabel; logo->setObjectName("logo");
  auto* title = new QLabel("droppix"); title->setObjectName("header");
  auto* tagline = new QLabel("use a tablet as a second monitor"); tagline->setObjectName("tagline");
  auto* titleCol = new QVBoxLayout; titleCol->setSpacing(0);
  titleCol->addWidget(title); titleCol->addWidget(tagline);
  auto* headerRow = new QHBoxLayout;
  headerRow->addWidget(logo); headerRow->addSpacing(10);
  headerRow->addLayout(titleCol); headerRow->addStretch();

  // --- Profile row ---
  profileBox_ = new QComboBox;
  auto* saveBtn = new QPushButton("Save");
  auto* saveAsBtn = new QPushButton("Save As");
  auto* delBtn = new QPushButton("Delete");
  auto* profRow = new QHBoxLayout;
  profRow->addWidget(new QLabel("Profile:")); profRow->addWidget(profileBox_, 1);
  profRow->addWidget(saveBtn); profRow->addWidget(saveAsBtn); profRow->addWidget(delBtn);

  // --- Settings group ---
  srcTest_ = new QRadioButton("Test pattern");
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
  orientation_ = new QComboBox;   // evdi only — rotates the droppix output via KWin
  orientation_->addItem("Landscape (0°)", 0);
  orientation_->addItem("Portrait (90°)", 90);
  orientation_->addItem("Inverted (180°)", 180);
  orientation_->addItem("Portrait flipped (270°)", 270);
  autoReverse_ = new QCheckBox("Auto adb reverse on start"); autoReverse_->setChecked(true);
  touch_ = new QCheckBox("Touch input (evdi only — tap/drag the cursor)");

  auto* form = new QFormLayout;
  auto* srcRow = new QHBoxLayout;
  srcRow->addWidget(srcTest_); srcRow->addSpacing(18); srcRow->addWidget(srcEvdi_); srcRow->addStretch();
  form->addRow("Source:", srcRow);
  form->addRow("Resolution:", resolution_);
  auto* refreshOrient = new QHBoxLayout;
  refreshOrient->addWidget(refresh_);
  refreshOrient->addSpacing(12);
  refreshOrient->addWidget(new QLabel("Orientation:"));
  refreshOrient->addWidget(orientation_, 1);
  form->addRow("Refresh (Hz):", refreshOrient);
  form->addRow("FPS:", fps_);
  form->addRow("Bitrate:", bitrate_);
  form->addRow("Port:", port_);
  form->addRow("", autoReverse_);
  form->addRow("", touch_);
  form->setVerticalSpacing(10);
  form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
  auto* settingsBox = new QGroupBox("Settings");
  settingsBox->setLayout(form);

  // --- Status row: colored dot + state + compact stats ---
  statusDot_   = new QLabel; statusDot_->setObjectName("statusDot");
  streamLabel_ = new QLabel("Stopped"); streamLabel_->setObjectName("statusText");
  statsLabel_  = new QLabel("—");       statsLabel_->setObjectName("statusStats");
  setStatusDot(kDotStopped);
  auto* statusRow = new QHBoxLayout;
  statusRow->addWidget(statusDot_); statusRow->addSpacing(8);
  statusRow->addWidget(streamLabel_); statusRow->addStretch();
  statusRow->addWidget(statsLabel_);
  deviceLabel_ = new QLabel("Device: —"); deviceLabel_->setObjectName("caption");

  // --- Start/Stop + log ---
  startBtn_ = new QPushButton("▶  Start streaming");
  startBtn_->setObjectName("startButton");
  auto* logCaption = new QLabel("Log"); logCaption->setObjectName("caption");
  log_ = new QPlainTextEdit; log_->setReadOnly(true);
  log_->setMaximumBlockCount(1000);
  log_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

  auto* root = new QVBoxLayout;
  root->setContentsMargins(16, 16, 16, 16);
  root->setSpacing(12);
  root->addLayout(headerRow);
  root->addLayout(profRow);
  root->addWidget(settingsBox);
  root->addLayout(statusRow);
  root->addWidget(deviceLabel_);
  root->addWidget(startBtn_);
  root->addWidget(logCaption);
  root->addWidget(log_, 1);
  auto* central = new QWidget; central->setLayout(root);
  setCentralWidget(central);
  resize(600, 720);

  // --- Wiring ---
  connect(startBtn_, &QPushButton::clicked, this, &MainWindow::onStartStop);
  connect(saveBtn, &QPushButton::clicked, this, [this]{
    if (!profileBox_->currentText().isEmpty()) {
      store_.save(profileBox_->currentText(), collectSettings()); refreshProfiles();
    }
  });
  connect(saveAsBtn, &QPushButton::clicked, this, [this]{
    bool ok; QString n = QInputDialog::getText(this, "Save profile", "Name:",
                                               QLineEdit::Normal, "", &ok);
    if (ok && !n.isEmpty()) { store_.save(n, collectSettings()); refreshProfiles();
                              profileBox_->setCurrentText(n); }
  });
  connect(delBtn, &QPushButton::clicked, this, [this]{
    if (!profileBox_->currentText().isEmpty()) { store_.remove(profileBox_->currentText()); refreshProfiles(); }
  });
  connect(profileBox_, &QComboBox::currentTextChanged, this, [this](const QString& n){
    Settings s; if (!n.isEmpty() && store_.load(n, s)) applySettings(s);
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
  connect(&controller_, &StreamController::runningChanged, this, [this](bool r){ setRunningUi(r); });
  connect(&adb_, &AdbManager::deviceStatus, this, [this](const QString& st){ deviceLabel_->setText("Device: " + st); });

  adbTimer_ = new QTimer(this);
  connect(adbTimer_, &QTimer::timeout, this, [this]{ adb_.refresh(); });
  adbTimer_->start(3000);
  adb_.refresh();
  refreshProfiles();
}

Settings MainWindow::collectSettings() const {
  Settings s;
  s.source = srcEvdi_->isChecked() ? Settings::Source::Evdi : Settings::Source::TestPattern;
  const QStringList wh = resolution_->currentText().split('x');
  s.width = wh.value(0).toInt(); s.height = wh.value(1).toInt();
  s.fps = fps_->value(); s.bitrate_kbps = bitrate_->value(); s.port = port_->value();
  s.refresh_hz = refresh_->currentText().toInt();
  s.auto_adb_reverse = autoReverse_->isChecked();
  s.touch = touch_->isChecked();
  s.orientation = orientation_->currentData().toInt();
  return s;
}

void MainWindow::applySettings(const Settings& s) {
  srcEvdi_->setChecked(s.source == Settings::Source::Evdi);
  srcTest_->setChecked(s.source == Settings::Source::TestPattern);
  resolution_->setCurrentText(QString("%1x%2").arg(s.width).arg(s.height));
  fps_->setValue(s.fps); bitrate_->setValue(s.bitrate_kbps); port_->setValue(s.port);
  refresh_->setCurrentText(QString::number(s.refresh_hz));
  { int i = orientation_->findData(s.orientation); orientation_->setCurrentIndex(i >= 0 ? i : 0); }
  autoReverse_->setChecked(s.auto_adb_reverse);
  touch_->setChecked(s.touch);
}

void MainWindow::refreshProfiles() {
  const QString cur = profileBox_->currentText();
  QSignalBlocker block(profileBox_);
  profileBox_->clear();
  profileBox_->addItems(store_.names());
  if (!cur.isEmpty()) profileBox_->setCurrentText(cur);
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
  QMainWindow::closeEvent(event);
}
}  // namespace droppix
