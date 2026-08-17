#include "main_window.h"
#include "video_widget.h"
#include "connect_dialog.h"
#include "settings_dialog.h"
#include "client_socket_channel.h"
#include "device_identity.h"
#include "style.h"        // ../host/gui — shared palette
#include "header_bar.h"
#include "idle_page.h"
#include "client_theme.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QStackedWidget>
#include <QApplication>
#include <QWidget>
#include <QMessageBox>
#include <QMetaObject>
#include <QGuiApplication>
#include <QScreen>
#include <QVideoSink>
#include <chrono>

namespace droppix {
namespace {

// Bridges TransportClient's callbacks (invoked on the net thread) to the decoder/audio/
// UI. Video frames go straight to the QVideoSink (documented thread-safe for a producer
// thread pushing frames); audio and UI-visible state are marshaled to the GUI thread.
class StreamListenerImpl : public StreamListener {
 public:
  StreamListenerImpl(MainWindow* win, VideoWidget* video, VideoDecoder* decoder,
                     AudioPlayer* audio)
      : win_(win), video_(video), decoder_(decoder), audio_(audio) {}

  void onConfig(uint32_t w, uint32_t h, uint32_t, const std::vector<unsigned char>&) override {
    decoder_->open(static_cast<int>(w), static_cast<int>(h));
  }
  void onVideo(uint64_t pts_us, bool, const std::vector<unsigned char>& nal) override {
    for (auto& frame : decoder_->submit(nal, pts_us))
      video_->videoSink()->setVideoFrame(frame);
  }
  void onAudio(const std::vector<unsigned char>& pcm) override {
    QByteArray bytes(reinterpret_cast<const char*>(pcm.data()), static_cast<int>(pcm.size()));
    QMetaObject::invokeMethod(audio_, "submit", Qt::QueuedConnection,
                              Q_ARG(QByteArray, bytes));
  }
  void onOverlay(bool) override {}  // no perf overlay in v1

 private:
  MainWindow* win_;
  VideoWidget* video_;
  VideoDecoder* decoder_;
  AudioPlayer* audio_;
};

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  setWindowTitle("Droppix Client");
  resize(1024, 640);

  // Header + a stacked body (idle card / video). The window used to be a bare toolbar over
  // an empty black central widget, which looks broken rather than idle when nothing is
  // streaming.
  header_ = new HeaderBar(this);
  idle_ = new IdlePage(this);
  video_ = new VideoWidget(this);

  stack_ = new QStackedWidget(this);
  stack_->addWidget(idle_);
  stack_->addWidget(video_);

  auto* body = new QWidget(this);
  auto* col = new QVBoxLayout(body);
  col->setContentsMargins(0, 0, 0, 0);
  col->setSpacing(0);
  col->addWidget(header_);
  col->addWidget(stack_, 1);
  setCentralWidget(body);

  connect(header_, &HeaderBar::connectRequested, this, &MainWindow::onConnectAction);
  connect(header_, &HeaderBar::disconnectRequested, this, &MainWindow::onDisconnectAction);
  connect(header_, &HeaderBar::settingsRequested, this, &MainWindow::onSettingsAction);
  connect(header_, &HeaderBar::themeToggled, this, [this]{
    setTheme(theme_ == Theme::Dark ? Theme::Light : Theme::Dark);
  });
  connect(idle_, &IdlePage::connectRequested, this, &MainWindow::onConnectAction);
  video_->setTouchCallback([this](const std::vector<TouchContact>& contacts) {
    if (client_) client_->sendTouch(contacts);
  });
  video_->setScrollCallback([this](int dx, int dy, uint16_t x, uint16_t y) {
    if (client_) client_->sendScroll(dx, dy, x, y);
  });
  video_->setMouseButtonCallback([this](uint8_t button, uint8_t action, uint16_t x, uint16_t y) {
    if (client_) client_->sendMouseButton(button, action, x, y);
  });
  video_->setKeyCallback([this](uint16_t kc, uint8_t a) {
    if (client_) client_->sendKey(kc, a);
  });

  audioPlayer_ = new AudioPlayer(this);
  decoder_ = std::make_unique<VideoDecoder>();
  client_ = std::make_unique<TransportClient>();

  theme_ = loadClientTheme();
  setTheme(theme_);
  showIdle();
}

void MainWindow::setTheme(Theme t) {
  theme_ = t;
  saveClientTheme(t);
  qApp->setStyleSheet(droppix::styleSheet(t));   // QWidget::styleSheet() would shadow this   // the shared host stylesheet, both palettes
  if (header_) header_->setTheme(t);
}

void MainWindow::showIdle(const QString& message) {
  idle_->setMessage(message);
  stack_->setCurrentWidget(idle_);
}

void MainWindow::showVideo() { stack_->setCurrentWidget(video_); }

MainWindow::~MainWindow() {
  stopSession();
}

void MainWindow::onConnectAction() {
  if (running_.load()) return;
  ConnectDialog dlg(hostStore_, tlsTrust_, this);
  if (dlg.exec() != QDialog::Accepted) return;
  startSession(dlg.chosenHost(), dlg.chosenPort());
}

void MainWindow::onDisconnectAction() {
  stopSession();
}

void MainWindow::onSettingsAction() {
  QSize scr = QGuiApplication::primaryScreen()
                ? QGuiApplication::primaryScreen()->geometry().size() : QSize(1920, 1080);
  ClientSettingsDialog dlg(settings_, QString("%1x%2").arg(scr.width()).arg(scr.height()), this);
  if (dlg.exec() != QDialog::Accepted) return;
  ClientSettings next = dlg.result();
  const bool changed = next.width != settings_.width || next.height != settings_.height ||
                       next.fps != settings_.fps || next.audio != settings_.audio ||
                       next.rotation != settings_.rotation ||
                       next.bitrate_kbps != settings_.bitrate_kbps ||
                       next.flip_horizontal != settings_.flip_horizontal ||
                       next.wall_col != settings_.wall_col || next.wall_row != settings_.wall_row;
  settings_ = next;
  ClientSettingsStore::save(settings_);
  if (decoder_) {  // brightness/contrast are pure display transforms: apply live, no reconnect
    decoder_->setBrightness(settings_.brightness);
    decoder_->setContrast(settings_.contrast);
  }
  if (changed && running_.load()) {  // apply immediately: reconnect with the new HELLO
    const QString host = currentHost_;
    stopSession();
    startSession(host, lastPort_);
  }
}

void MainWindow::startSession(const QString& host, quint16 port) {
  currentHost_ = host;
  lastPort_ = port;
  running_.store(true);
  header_->setConnected(true);
  header_->setStatus(QString("Connecting to %1:%2...").arg(host).arg(port));
  netThread_ = std::thread(&MainWindow::netThreadMain, this, host, port);
}

void MainWindow::stopSession() {
  if (!running_.exchange(false)) return;
  client_->close();
  if (netThread_.joinable()) netThread_.join();
  header_->setConnected(false);
  header_->setStatus("Not connected");
  showIdle();
}

void MainWindow::netThreadMain(QString hostQ, quint16 port) {
  const std::string host = hostQ.toStdString();
  const std::string name = DeviceIdentity::displayName();
  const std::string id = DeviceIdentity::stableId();
  const uint32_t density = static_cast<uint32_t>(
      QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->logicalDotsPerInch() : 96);
  const QSize scr = QGuiApplication::primaryScreen()
      ? QGuiApplication::primaryScreen()->geometry().size() : QSize(1920, 1080);
  const uint32_t w = settings_.width > 0 ? static_cast<uint32_t>(settings_.width) : static_cast<uint32_t>(scr.width());
  const uint32_t h = settings_.height > 0 ? static_cast<uint32_t>(settings_.height) : static_cast<uint32_t>(scr.height());
  const uint32_t fps = static_cast<uint32_t>(settings_.fps);
  const uint8_t audio = settings_.audio ? 1 : 0;
  const uint8_t orient = static_cast<uint8_t>(rotation_to_code(settings_.rotation));
  const uint32_t bitrate = static_cast<uint32_t>(settings_.bitrate_kbps);
  const uint16_t wall_col = static_cast<uint16_t>(settings_.wall_col);
  const uint16_t wall_row = static_cast<uint16_t>(settings_.wall_row);
  // Flip is applied here on netThread_; brightness/contrast are also applied live from
  // onSettingsAction (GUI thread; the decoder's fields are atomic). Applying flip here, once per
  // netThreadMain invocation, means both the initial connect and any settings-triggered
  // reconnect (stopSession+startSession spawns a fresh netThreadMain) pick up the current
  // settings_.flip_horizontal before any frames are submitted.
  decoder_->setFlipHorizontal(settings_.flip_horizontal);
  decoder_->setBrightness(settings_.brightness);
  decoder_->setContrast(settings_.contrast);

  StreamListenerImpl listener(this, video_, decoder_.get(), audioPlayer_);

  while (running_.load()) {
    auto channel = ClientSocketChannel::connect(host, port, /*use_tls=*/true, 5000);
    if (!channel) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
      continue;
    }
    if (host != "127.0.0.1") {
      auto pinned = tlsTrust_.pinnedFingerprint(host);
      const std::string fp = cert_fingerprint(channel->peer_certificate());
      if (!pinned || *pinned != fp) {
        QMetaObject::invokeMethod(this, [this, hostQ]{ showCertChangedDialog(hostQ); },
                                  Qt::QueuedConnection);
        break;
      }
    }
    QMetaObject::invokeMethod(this, [this]{ header_->setStatus("Streaming"); showVideo(); },
                              Qt::QueuedConnection);
    client_->runOverChannel(*channel, w, h, density, fps, audio, orient, bitrate,
                            wall_col, wall_row, listener,
                            [this]{ return running_.load(); }, name, id);
    channel->close();
    if (running_.load()) {
      QMetaObject::invokeMethod(this, [this]{ header_->setStatus("Reconnecting..."); },
                                Qt::QueuedConnection);
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
  }
  running_.store(false);
  // Set button state from running_ at *delivery* time, not hardcoded true/false: on a
  // settings-triggered reconnect this stale lambda from the old net thread is delivered
  // after startSession() has already set running_=true for the new session, so it must
  // leave Disconnect enabled rather than re-disabling it permanently.
  QMetaObject::invokeMethod(this, [this]{
    header_->setConnected(running_.load());
  }, Qt::QueuedConnection);
}

void MainWindow::showCertChangedDialog(const QString& host) {
  auto btn = QMessageBox::question(this, "Droppix",
      QString("This PC's security identity changed since you paired with %1. Re-pair?").arg(host));
  if (btn == QMessageBox::Yes) tlsTrust_.clear(host.toStdString());
  header_->setStatus("Not connected");
}

}  // namespace droppix
