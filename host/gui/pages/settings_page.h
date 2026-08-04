#pragma once
#include <QWidget>
#include "settings.h"
#include "theme.h"

class QSpinBox;
class QComboBox;
class QCheckBox;
class QRadioButton;

namespace droppix {

// "Settings" section: the full stream/session/application options form, mounted
// at stack_ index 3 (replaces the old modal SettingsDialog). Creates and owns all
// its widgets; the owning MainWindow reads/writes through load()/store() during
// apply/collect, exactly like the dialog did. Additionally surfaces the Settings
// fields the dialog left unset (resolution/fps/audio/orientation — see
// settings_dialog.cpp's historical comment) and a theme control.
class SettingsPage : public QWidget {
  Q_OBJECT
 public:
  explicit SettingsPage(QWidget* parent = nullptr);

  void load(const Settings& s);    // widgets <- settings
  void store(Settings& s) const;   // widgets -> settings
  void setTheme(Theme t);          // reflect current theme in the toggle WITHOUT emitting

 signals:
  void rememberAuthRequested();     // user clicked "Remember authentication"
  void manageDevicesRequested();    // user clicked "Manage remembered devices"
  void overlayToggled(bool show);   // perf-overlay checkbox flipped (apply live if streaming)
  void themeChangeRequested(Theme t);   // user flipped the Dark/Light control
  void settingsChanged();   // a streamer control changed (not app-prefs, not during load())

 private:
  // --- Stream card ---
  QRadioButton* srcTest_;
  QRadioButton* srcEvdi_;
  QComboBox* resolution_;   // NEW: "1280x800" / "1920x1080" / "Match client" -> width/height
  QComboBox* fps_;          // NEW: "30" / "60" -> s.fps
  QSpinBox* bitrate_;
  QSpinBox* port_;
  QComboBox* refresh_;

  // --- Session & input card ---
  QCheckBox* touch_;
  QCheckBox* audio_;        // NEW -> s.audio
  QComboBox* orientation_;  // NEW: "0"/"90"/"180"/"270" -> s.orientation
  QCheckBox* overlay_;
  QCheckBox* autoConnect_;
  QCheckBox* webClient_;

  // --- Application card ---
  QRadioButton* themeDark_;   // NEW
  QRadioButton* themeLight_;  // NEW
  // App-level prefs (global, NOT per-profile): persisted as files, not via load/store.
  QCheckBox* launchAtLogin_;    // ~/.config/autostart/droppix.desktop present?
  QCheckBox* minimizeOnClose_;  // <config>/minimize_on_close marker present?
};

}  // namespace droppix
