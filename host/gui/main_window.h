#pragma once
#include <QMainWindow>
#include "settings.h"
#include "profile_store.h"
#include "stream_controller.h"
#include "adb_manager.h"
#include "mdns_advertiser.h"
#include "approved_store.h"

class QComboBox; class QSpinBox; class QCheckBox; class QPushButton;
class QLabel; class QPlainTextEdit; class QRadioButton; class QTimer;

namespace droppix {
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

  // widgets
  QRadioButton* srcTest_; QRadioButton* srcEvdi_;
  QComboBox* resolution_; QSpinBox* fps_; QSpinBox* bitrate_; QSpinBox* port_;
  QComboBox* refresh_;
  QComboBox* orientation_;
  QCheckBox* autoReverse_;
  QCheckBox* touch_;
  QComboBox* profileBox_; QPushButton* startBtn_;
  QLabel* statusDot_;
  QLabel* deviceLabel_; QLabel* streamLabel_; QLabel* statsLabel_;
  QWidget* authRow_; QLabel* authCaption_;   // "remember authentication" tip
  QPlainTextEdit* log_;

  ProfileStore store_;
  ApprovedStore approved_;
  StreamController controller_;
  AdbManager adb_;
  MdnsAdvertiser advertiser_;
  QTimer* adbTimer_;
  std::string streamBin_;
};
}  // namespace droppix
