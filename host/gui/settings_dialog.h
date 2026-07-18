#pragma once
#include <QDialog>
#include "settings.h"

class QSpinBox;
class QComboBox;
class QCheckBox;
class QRadioButton;

namespace droppix {

// Holds the rarely-changed streaming options, moved off the main window to keep
// it uncluttered. Opened from the Settings menu. The owning MainWindow reads/
// writes these fields through load()/store() during apply/collect.
class SettingsDialog : public QDialog {
  Q_OBJECT
 public:
  explicit SettingsDialog(QWidget* parent = nullptr);

  void load(const Settings& s);    // widgets <- settings
  void store(Settings& s) const;   // widgets -> settings

 signals:
  void rememberAuthRequested();    // user clicked "Remember authentication"
  void manageDevicesRequested();   // user clicked "Manage remembered devices"
  void overlayToggled(bool show);  // perf-overlay checkbox flipped (apply live if streaming)

 private:
  QRadioButton* srcTest_;
  QRadioButton* srcEvdi_;
  QSpinBox* bitrate_;
  QSpinBox* port_;
  QComboBox* refresh_;
  QCheckBox* touch_;
  QCheckBox* overlay_;
  QCheckBox* autoConnect_;
  QCheckBox* webClient_;
  // App-level prefs (global, NOT per-profile): persisted as files, not via load/store.
  QCheckBox* launchAtLogin_;    // ~/.config/autostart/droppix.desktop present?
  QCheckBox* minimizeOnClose_;  // <config>/minimize_on_close marker present?
};

}  // namespace droppix
