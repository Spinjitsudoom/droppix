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
  void overlayToggled(bool show);  // perf-overlay checkbox flipped (apply live if streaming)

 private:
  QRadioButton* srcTest_;
  QRadioButton* srcEvdi_;
  QComboBox* resolution_;
  QSpinBox* fps_;
  QSpinBox* bitrate_;
  QSpinBox* port_;
  QComboBox* refresh_;
  QComboBox* orientation_;
  QCheckBox* touch_;
  QCheckBox* audio_;
  QCheckBox* autoReverse_;
  QCheckBox* overlay_;
};

}  // namespace droppix
