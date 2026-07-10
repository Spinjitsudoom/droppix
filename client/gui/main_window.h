#pragma once
#include <QMainWindow>
#include <atomic>
#include <memory>
#include <thread>
#include "host_store.h"
#include "tls_trust.h"
#include "transport_client.h"
#include "video_decoder.h"
#include "audio_player.h"

class QLabel;
class QAction;

namespace droppix {

class VideoWidget;

// Top-level window: owns the connection state and every subsystem, composed directly
// (no DI framework) — mirrors host/gui/main_window.h's ownership style.
class MainWindow : public QMainWindow {
  Q_OBJECT
 public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override;

 private slots:
  void onConnectAction();
  void onDisconnectAction();

 private:
  void startSession(const QString& host, quint16 port);
  void stopSession();
  void netThreadMain(QString host, quint16 port);   // runs on netThread_
  void showCertChangedDialog(const QString& host);  // invoked on the GUI thread

  HostStore hostStore_;
  TlsTrust tlsTrust_;
  std::unique_ptr<TransportClient> client_;
  std::unique_ptr<VideoDecoder> decoder_;   // touched only by netThread_
  AudioPlayer* audioPlayer_ = nullptr;      // QObject, GUI-thread owned (parented to this)
  VideoWidget* video_ = nullptr;
  QLabel* statusLabel_ = nullptr;
  QAction* connectAction_ = nullptr;
  QAction* disconnectAction_ = nullptr;

  std::thread netThread_;
  std::atomic<bool> running_{false};
  QString currentHost_;
};

}  // namespace droppix
