#pragma once
#include <QAbstractButton>
#include <QColor>

namespace droppix {

// A pill-shaped on/off slider switch (track + sliding circular knob), used for the
// Status hero's Server control in place of a plain color-changing QPushButton. Track and
// knob colors are QSS-settable (qproperty-trackOnColor / trackOffColor / knobOnColor /
// knobOffColor — see style.h's ToggleSwitch#serverSwitch rule) so it stays theme-aware
// exactly like every other widget in the app; this class has no direct Theme/palette()
// dependency. As with the QPushButton it replaces, a QSS `[on="true"]` selector still
// needs an explicit unpolish()/polish() pair after setProperty() to take effect
// immediately — see docs/lessons/qss-property-repolish.md.
class ToggleSwitch : public QAbstractButton {
  Q_OBJECT
  Q_PROPERTY(QColor trackOnColor MEMBER trackOnColor_)
  Q_PROPERTY(QColor trackOffColor MEMBER trackOffColor_)
  Q_PROPERTY(QColor knobOnColor MEMBER knobOnColor_)
  Q_PROPERTY(QColor knobOffColor MEMBER knobOffColor_)
  Q_PROPERTY(qreal knobPos READ knobPos WRITE setKnobPos)
 public:
  explicit ToggleSwitch(QWidget* parent = nullptr);
  QSize sizeHint() const override;

 protected:
  void paintEvent(QPaintEvent*) override;
  // QAbstractButton's virtual hook: fires whenever the checked state actually changes,
  // via a user click OR a programmatic setChecked() — the single place to trigger the
  // slide animation so both paths (MainWindow::updateServerButton()'s setChecked() and a
  // direct click) stay in sync without duplicating logic.
  void checkStateSet() override;

 private:
  qreal knobPos() const { return knobPos_; }
  void setKnobPos(qreal v) { knobPos_ = v; update(); }

  QColor trackOnColor_{0x14, 0xb8, 0xa6};
  QColor trackOffColor_{0x5b, 0x65, 0x73};
  QColor knobOnColor_{0x06, 0x23, 0x1f};
  QColor knobOffColor_{0xe6, 0xe9, 0xef};
  qreal knobPos_ = 0.0;   // 0 = off (left), 1 = on (right); animated between the two
};

}  // namespace droppix
