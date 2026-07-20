#pragma once
#include <QMainWindow>
#include <QHash>
#include <QString>
#include <QTimer>
#include <functional>
#include "settings.h"
#include "profile_store.h"
#include "stream_controller.h"
#include "session_manager.h"
#include "mdns_advertiser.h"
#include "mdns_browser.h"
#include "tether_scanner.h"
#include "aoa_scanner.h"
#include "aoa_known_store.h"
#include "approved_store.h"
#include "cert_manager.h"
#include "audio_sink.h"
#include "log_buffer.h"
#include "log_panel.h"
#include "log_entry.h"

class QComboBox; class QSpinBox; class QCheckBox; class QPushButton;
class QLabel; class QPlainTextEdit; class QRadioButton; class QTimer;
class QListWidget; class QGroupBox; class QSystemTrayIcon; class QDialog;

namespace droppix {
class SettingsDialog;
class MainWindow : public QMainWindow {
  Q_OBJECT
 public:
  explicit MainWindow(QWidget* parent = nullptr);
 protected:
  void closeEvent(QCloseEvent* event) override;
 private:
  std::string resolveStreamBin();   // sibling binary, extracted AppImage copy, or host-staged (Flatpak)
  void stageWebAssets();             // dev/local: copy source web/dist into the root-readable runtime dir
  void stageCertsToHost();           // Flatpak: mirror cert/key to the host for the streamer
  Settings collectSettings() const;
  void applySettings(const Settings& s);
  void onServerToggled(bool on);   // Server toggle -> start/stop the primary listener + persist
  void startServerSession();       // spawn the primary "server:<port>" listener on a free port
  void stopServerSession();        // stop the primary listener (no re-arm)
  void updateServerButton();       // reflect serverEnabled_ in the toggle's text/checked state
  // Spawn a streaming session: a new StreamController on `port`, wired, started, added to
  // the Active-monitors panel; `directTablet` (may be empty) WAKEs the tablet to dial in.
  // `mirror` selects Mirror mode (evdi mirrors an existing display) vs. the default Extend.
  void startSession(const QString& key, const QString& label, const QString& transport,
                    int port, const QString& id, std::function<void()> directTablet,
                    bool mirror = false);
  void wireSession(StreamController* c, const QString& key);
  void stopSelectedMonitor();   // stop the session selected in the Active-monitors list
  void toggleSelectedMonitorMirror();   // flip Extend<->Mirror for the selected monitor (stop+restart)
  void updateStatus();          // status dot/text from session count + connectivity
  void refreshProfiles();
  void restoreLastProfile();
  void setStatusDot(const char* color);
  void setupAuth();              // install the polkit rule via one pkexec prompt
  void showAbout();             // Help -> About developer-info dialog
  void onDevicesChanged(const QList<MdnsDevice>& devices);
  void onTetherClientsChanged(const QList<TetherClient>& clients);
  void onAoaClientsChanged(const QList<AoaClient>& clients);
  void rebuildClientList();     // merge tetherClients_ + aoaClients_ + netDevices_ into devicesList_
  void onConnectToSelectedDevice();
  // Start a monitor for one specific device. quietIfBusy=true suppresses the
  // "already connected"/"limit reached" popups (used by auto-connect). Returns
  // true if a session was started.
  bool connectDevice(const QString& key, const QString& label, const QString& transport,
                     const QString& ident, quint16 wakePort, const QString& id, bool quietIfBusy);
  void evaluateAutoConnect();   // pick + start auto-connect sessions from discovery
  void refreshAdvertising();    // (re)publish _droppix._tcp for the current port; idempotent
  bool minimizeToTrayRequested() const;   // reads the <config>/minimize_on_close marker
  void setupTray();             // create the tray icon + Show/Quit menu (if a tray exists)
  void showPairingPopup(const QString& ip);   // pop the pairing code when a device connects
  void hidePairingPopup();
  // Append a synthetic (non-streamer) event into the debug-log console.
  void logEvent(const QString& key, const QString& source, LogLevel level, const QString& text);
  void manageDevices();         // dialog to view/forget remembered (approved) devices

  // widgets — ALL stream options (source/resolution/touch/audio/fps/bitrate/port/
  // refresh/orientation/auto-adb/overlay) now live in SettingsDialog (gear icon).
  SettingsDialog* settingsDialog_;
  QComboBox* profileBox_; QPushButton* startBtn_;
  QLabel* statusDot_;
  QLabel* deviceLabel_; QLabel* streamLabel_; QLabel* statsLabel_;
  QGroupBox* devicesBox_;
  QDialog* pairingPopup_ = nullptr;   // non-modal "Pairing code: NNNNNN" shown on connect
  QLabel* pairingInfo_ = nullptr;
  QLabel* pairingCodeLabel_ = nullptr;
  QTimer* pairingHideTimer_ = nullptr;
  QListWidget* devicesList_;
  QPushButton* connectBtn_;
  QGroupBox* monitorsBox_;      // "Active monitors" panel
  QListWidget* monitorsList_;   // one row per live session
  QLabel* webUrlLabel_ = nullptr;
  QLabel* webQrLabel_ = nullptr;
  QPushButton* webCopyBtn_ = nullptr;
  bool anyConnected_ = false;   // any session has a client connected (drives the status dot)
  void refreshWebClientUi();    // URL + QR for the newest session when webClient enabled

  ProfileStore store_;
  ApprovedStore approved_;
  AoaKnownStore knownAoa_;
  CertManager cert_;
  DroppixAudioSink audioSink_;
  SessionManager sessions_;     // one session (= streamer = monitor) per connected tablet
  LogBuffer* logBuffer_ = nullptr;   // app-wide log sink (streamer + GUI messages)
  LogPanel*  logPanel_ = nullptr;    // bottom "Debug log" dock
  bool serverEnabled_ = false;       // Server toggle logical state
  QString serverKey_;                // key of the live "server:<port>" session (empty = none)
  qint64 serverStartMs_ = 0;         // start time of the current server session
  bool serverEverConnected_ = false; // did the current server session ever have a client
  MdnsAdvertiser advertiser_;
  quint16 advertisedPort_ = 0;     // port currently published via _droppix._tcp (0 = none)
  MdnsBrowser browser_;
  TetherScanner tetherScanner_;
  AoaScanner aoaScanner_;
  QList<MdnsDevice> netDevices_;   // last network-discovered clients
  QList<TetherClient> tetherClients_;   // last USB-tether-discovered clients
  QList<AoaClient> aoaClients_;   // last USB-discovered AOA tablets
  QTimer autoConnectTimer_;   // debounces discovery bursts before auto-connecting
  QHash<QString, qint64> pendingWakes_;
  QString flatpakHostRuntime_;         // Flatpak: host dir the streamer runtime is staged to
  QString flatpakHostCert_, flatpakHostKey_;   // Flatpak: host cert/key paths for the streamer
  QString flatpakHostWeb_;             // Flatpak: host path to staged web/ PWA assets
  QSystemTrayIcon* tray_ = nullptr;   // present only if a system tray is available
  bool quitting_ = false;             // true => closeEvent really quits (from tray Quit)
  bool trayHintShown_ = false;        // show the "still running" balloon only once
  std::string streamBin_;
};
}  // namespace droppix
