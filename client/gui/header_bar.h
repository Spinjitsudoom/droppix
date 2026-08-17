#pragma once
#include <QFrame>
#include "theme.h"

class QLabel;
class QPushButton;

namespace droppix {

/**
 * Header bar, matching the host GUI's: logo + title on the left, live status in the middle,
 * actions and the theme toggle on the right.
 *
 * Replaces a bare QToolBar. It exists to carry the same identity as the host app rather than
 * to add features, so it uses the host's object names (#logo, #header, #caption, #statusText)
 * and is styled entirely by the shared stylesheet.
 *
 * Deliberately stays visible while streaming. Auto-hiding chrome would buy a little more
 * video area but strands the user: Disconnect lives here, and the client has no menu bar to
 * fall back on.
 */
class HeaderBar : public QFrame {
  Q_OBJECT
 public:
  explicit HeaderBar(QWidget* parent = nullptr);

  /** Reflect session state: enables/disables actions and sets the dot + text. */
  void setConnected(bool connected);   // flips the primary action's label + signal
  void setStatus(const QString& text);
  /** Secondary line (resolution / fps); empty hides it. */
  void setDetail(const QString& text);
  void setTheme(Theme t);

 signals:
  void connectRequested();
  void disconnectRequested();
  void settingsRequested();
  void themeToggled();

 private:
  void paintDot(const char* color);

  bool connected_ = false;

  QLabel* logo_;
  QLabel* title_;
  QLabel* dot_;
  QLabel* status_;
  QLabel* detail_;
  QPushButton* actionBtn_;   // Connect <-> Disconnect; see setConnected()
  QPushButton* settingsBtn_;
  QPushButton* themeBtn_;
};

}  // namespace droppix
