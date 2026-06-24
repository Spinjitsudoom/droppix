#pragma once
#include <QMainWindow>
#include "settings.h"
#include "profile_store.h"
#include "stream_controller.h"
#include "adb_manager.h"

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
  void setRunningUi(bool running);

  // widgets
  QRadioButton* srcTest_; QRadioButton* srcEvdi_;
  QComboBox* resolution_; QSpinBox* fps_; QSpinBox* bitrate_; QSpinBox* port_;
  QComboBox* refresh_;
  QCheckBox* autoReverse_;
  QComboBox* profileBox_; QPushButton* startBtn_;
  QLabel* deviceLabel_; QLabel* streamLabel_; QLabel* statsLabel_;
  QPlainTextEdit* log_;

  ProfileStore store_;
  StreamController controller_;
  AdbManager adb_;
  QTimer* adbTimer_;
  std::string streamBin_;
};
}  // namespace droppix
