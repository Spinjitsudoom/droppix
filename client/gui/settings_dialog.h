#pragma once
#include <QDialog>
#include "client_settings.h"

class QComboBox;
class QCheckBox;

namespace droppix {

// Lets the user edit ClientSettings (resolution/fps/audio/rotation) sent to the host in
// HELLO. Resolution offers a "Native" entry (this device's current screen size, shown for
// context only — width/height 0 means "resolve at connect time") plus a few fixed presets.
class ClientSettingsDialog : public QDialog {
  Q_OBJECT
 public:
  ClientSettingsDialog(const ClientSettings& current, const QString& nativeLabel,
                       QWidget* parent = nullptr);
  ClientSettings result() const;

 private:
  ClientSettings cur_;  // settings passed into the ctor; result() seeds from this so any
                        // field without a UI control (or not touched) survives round-trip.
  QComboBox* resolution_;
  QComboBox* fps_;
  QCheckBox* audio_;
  QComboBox* rotation_;
  QComboBox* bitrate_;
};

}  // namespace droppix
