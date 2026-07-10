#include "main_window.h"
#include "video_widget.h"
#include "connect_dialog.h"
#include "client_socket_channel.h"
#include "device_identity.h"
#include "style.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QToolBar>
#include <QStatusBar>
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

  video_ = new VideoWidget(this);
  setCentralWidget(video_);
  video_->setTouchCallback([this](const std::vector<TouchContact>& contacts) {
    if (client_) client_->sendTouch(contacts);
  });

  audioPlayer_ = new AudioPlayer(this);
  decoder_ = std::make_unique<VideoDecoder>();
  client_ = std::make_unique<TransportClient>();

  auto* toolbar = addToolBar("main");
  connectAction_ = toolbar->addAction("Connect", this, &MainWindow::onConnectAction);
  disconnectAction_ = toolbar->addAction("Disconnect", this, &MainWindow::onDisconnectAction);
  disconnectAction_->setEnabled(false);

  statusLabel_ = new QLabel("Not connected", this);
  statusLabel_->setObjectName("statusText");
  statusBar()->addWidget(statusLabel_);
}

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

void MainWindow::startSession(const QString& host, quint16 port) {
  currentHost_ = host;
  running_.store(true);
  connectAction_->setEnabled(false);
  disconnectAction_->setEnabled(true);
  statusLabel_->setText(QString("Connecting to %1:%2...").arg(host).arg(port));
  netThread_ = std::thread(&MainWindow::netThreadMain, this, host, port);
}

void MainWindow::stopSession() {
  if (!running_.exchange(false)) return;
  client_->close();
  if (netThread_.joinable()) netThread_.join();
  connectAction_->setEnabled(true);
  disconnectAction_->setEnabled(false);
  statusLabel_->setText("Not connected");
}

void MainWindow::netThreadMain(QString hostQ, quint16 port) {
  const std::string host = hostQ.toStdString();
  const std::string name = DeviceIdentity::displayName();
  const std::string id = DeviceIdentity::stableId();
  const uint32_t density = static_cast<uint32_t>(
      QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->logicalDotsPerInch() : 96);

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
    QMetaObject::invokeMethod(this, [this]{ statusLabel_->setText("Streaming"); },
                              Qt::QueuedConnection);
    client_->runOverChannel(*channel, 1920, 1080, density, listener,
                            [this]{ return running_.load(); }, name, id);
    channel->close();
    if (running_.load()) {
      QMetaObject::invokeMethod(this, [this]{ statusLabel_->setText("Reconnecting..."); },
                                Qt::QueuedConnection);
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
  }
  running_.store(false);
  QMetaObject::invokeMethod(this, [this]{
    connectAction_->setEnabled(true);
    disconnectAction_->setEnabled(false);
  }, Qt::QueuedConnection);
}

void MainWindow::showCertChangedDialog(const QString& host) {
  auto btn = QMessageBox::question(this, "Droppix",
      QString("This PC's security identity changed since you paired with %1. Re-pair?").arg(host));
  if (btn == QMessageBox::Yes) tlsTrust_.clear(host.toStdString());
  statusLabel_->setText("Not connected");
}

}  // namespace droppix
