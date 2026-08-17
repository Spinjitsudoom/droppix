#pragma once
#include <QWidget>

class QLabel;
class QPushButton;

namespace droppix {

/**
 * What the window shows when nothing is streaming.
 *
 * The client used to present an empty black central widget, which reads as "broken" rather
 * than "idle" — there was nothing to tell you the app was working or what to do next. This
 * is the host's Status hero in miniature: state word, one line of guidance, and the single
 * action that matters.
 */
class IdlePage : public QWidget {
  Q_OBJECT
 public:
  explicit IdlePage(QWidget* parent = nullptr);

  /** Show why there is no picture (e.g. a failed connect), or "" for the neutral prompt. */
  void setMessage(const QString& text);

 signals:
  void connectRequested();

 private:
  QLabel* state_;
  QLabel* caption_;
  QPushButton* connectBtn_;
};

}  // namespace droppix
