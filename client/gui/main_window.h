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
#include "client_settings.h"
#include "theme.h"   // ../host/gui

class QLabel;
class QAction;
class QStackedWidget;

namespace droppix {

class VideoWidget;
class HeaderBar;
class IdlePage;

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
  void onSettingsAction();

 private:
  void startSession(const QString& host, quint16 port);
  void stopSession();
  void setTheme(Theme t);
  void showIdle(const QString& message = QString());   // swap the stack to the idle card
  void showVideo();
  void netThreadMain(QString host, quint16 port);   // runs on netThread_
  void showCertChangedDialog(const QString& host);  // invoked on the GUI thread

  HostStore hostStore_;
  TlsTrust tlsTrust_;
  ClientSettings settings_ = ClientSettingsStore::load();
  std::unique_ptr<TransportClient> client_;
  std::unique_ptr<VideoDecoder> decoder_;   // created once, never reset; deref'd on
                                             // netThread_ + live brightness/contrast writes
                                             // from GUI thread (decoder's fields are atomic)
  AudioPlayer* audioPlayer_ = nullptr;      // QObject, GUI-thread owned (parented to this)
  VideoWidget* video_ = nullptr;
  HeaderBar* header_ = nullptr;
  IdlePage* idle_ = nullptr;
  QStackedWidget* stack_ = nullptr;   // idle card <-> video, so an idle window is never blank
  Theme theme_ = Theme::Dark;

  std::thread netThread_;
  std::atomic<bool> running_{false};
  QString currentHost_;
  quint16 lastPort_ = 0;
};

}  // namespace droppix
