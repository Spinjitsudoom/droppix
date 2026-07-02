#pragma once
#include <QMainWindow>
#include <QHash>
#include <QString>
#include "settings.h"
#include "profile_store.h"
#include "stream_controller.h"
#include "adb_manager.h"
#include "mdns_advertiser.h"
#include "mdns_browser.h"
#include "usb_client_scanner.h"
#include "approved_store.h"
#include "cert_manager.h"
#include "audio_sink.h"

class QComboBox; class QSpinBox; class QCheckBox; class QPushButton;
class QLabel; class QPlainTextEdit; class QRadioButton; class QTimer;
class QListWidget; class QGroupBox; class QSystemTrayIcon;

namespace droppix {
class SettingsDialog;
class MainWindow : public QMainWindow {
  Q_OBJECT
 public:
  explicit MainWindow(QWidget* parent = nullptr);
 protected:
  void closeEvent(QCloseEvent* event) override;
 private:
  Settings collectSettings() const;
  void applySettings(const Settings& s);
  void onStartStop();
  void refreshProfiles();
  void restoreLastProfile();
  void setRunningUi(bool running);
  void setStatusDot(const char* color);
  void setupAuth();              // install the polkit rule via one pkexec prompt
  void showAbout();             // Help -> About developer-info dialog
  void onDevicesChanged(const QList<MdnsDevice>& devices);
  void onUsbClientsChanged(const QList<UsbClient>& clients);
  void rebuildClientList();     // merge netDevices_ + usbClients_ into devicesList_
  void onConnectToSelectedDevice();
  void refreshAdvertising();    // (re)publish _droppix._tcp for the current port; idempotent
  bool minimizeToTrayRequested() const;   // reads the <config>/minimize_on_close marker
  void setupTray();             // create the tray icon + Show/Quit menu (if a tray exists)

  // widgets — ALL stream options (source/resolution/touch/audio/fps/bitrate/port/
  // refresh/orientation/auto-adb/overlay) now live in SettingsDialog (gear icon).
  SettingsDialog* settingsDialog_;
  QComboBox* profileBox_; QPushButton* startBtn_;
  QLabel* statusDot_;
  QLabel* deviceLabel_; QLabel* streamLabel_; QLabel* statsLabel_;
  QLabel* pairingLabel_;
  QGroupBox* devicesBox_;
  QListWidget* devicesList_;
  QPushButton* connectBtn_;

  ProfileStore store_;
  ApprovedStore approved_;
  CertManager cert_;
  DroppixAudioSink audioSink_;
  StreamController controller_;
  AdbManager adb_;
  MdnsAdvertiser advertiser_;
  quint16 advertisedPort_ = 0;     // port currently published via _droppix._tcp (0 = none)
  MdnsBrowser browser_;
  UsbClientScanner usbScanner_;
  QList<MdnsDevice> netDevices_;   // last network-discovered clients
  QList<UsbClient> usbClients_;    // last USB-discovered clients
  QHash<QString, qint64> pendingWakes_;
  QSystemTrayIcon* tray_ = nullptr;   // present only if a system tray is available
  bool quitting_ = false;             // true => closeEvent really quits (from tray Quit)
  bool trayHintShown_ = false;        // show the "still running" balloon only once
  std::string streamBin_;
};
}  // namespace droppix
