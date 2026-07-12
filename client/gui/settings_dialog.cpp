#include "settings_dialog.h"
#include <QtWidgets>
#include <algorithm>

namespace droppix {

ClientSettingsDialog::ClientSettingsDialog(const ClientSettings& cur, const QString& nativeLabel,
                                           QWidget* parent) : QDialog(parent), cur_(cur) {
  setWindowTitle("Droppix Client — Settings");
  setModal(true);

  resolution_ = new QComboBox;
  resolution_->addItem("Native (" + nativeLabel + ")", QSize(0, 0));  // width 0 => native
  for (const char* r : {"1280x720", "1920x1080", "2560x1440", "1024x640", "800x600"}) {
    const QStringList wh = QString(r).split('x');
    resolution_->addItem(r, QSize(wh[0].toInt(), wh[1].toInt()));
  }
  if (cur.width > 0) {
    int i = resolution_->findData(QSize(cur.width, cur.height));
    resolution_->setCurrentIndex(i >= 0 ? i : 0);
  }

  fps_ = new QComboBox;
  fps_->addItems({"30", "60"});
  fps_->setCurrentText(QString::number(cur.fps));

  audio_ = new QCheckBox("Audio");
  audio_->setChecked(cur.audio);

  rotation_ = new QComboBox;
  rotation_->addItem("0°", 0);
  rotation_->addItem("90°", 90);
  rotation_->addItem("180°", 180);
  rotation_->addItem("270°", 270);
  rotation_->setCurrentIndex(std::max(0, rotation_->findData(cur.rotation)));

  bitrate_ = new QComboBox;
  bitrate_->addItem("Low", QVariant(4000));
  bitrate_->addItem("Medium", QVariant(8000));
  bitrate_->addItem("High", QVariant(16000));
  bitrate_->setCurrentIndex(std::max(0, bitrate_->findData(cur.bitrate_kbps)));

  auto* form = new QFormLayout;
  form->addRow("Resolution:", resolution_);
  form->addRow("FPS:", fps_);
  form->addRow("", audio_);
  form->addRow("Rotation:", rotation_);
  form->addRow("Quality:", bitrate_);

  auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto* root = new QVBoxLayout(this);
  root->addLayout(form);
  root->addWidget(bb);
}

ClientSettings ClientSettingsDialog::result() const {
  ClientSettings s = cur_;  // seed from the settings passed into the ctor so any field not
                            // controlled by this dialog's UI keeps its stored value.
  const QSize wh = resolution_->currentData().toSize();
  s.width = wh.width();
  s.height = wh.height();
  s.fps = fps_->currentText().toInt();
  s.audio = audio_->isChecked();
  s.rotation = rotation_->currentData().toInt();
  s.bitrate_kbps = bitrate_->currentData().toInt();
  return s;
}

}  // namespace droppix
