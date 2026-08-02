#include <QApplication>
#include <QIcon>
#include <string>
#include "main_window.h"
#include "style.h"

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  app.setStyle("Fusion");                  // consistent base for the custom dark QSS
  app.setStyleSheet(droppix::styleSheet(droppix::Theme::Dark));
  app.setWindowIcon(QIcon(":/icon.png"));  // taskbar / window-manager icon

  // --minimized: launch-at-login passes this so the GUI doesn't pop a window every
  // boot; starts hidden to the tray (falls back to a normal show() if no tray exists).
  bool minimized = false;
  for (int i = 1; i < argc; ++i) if (std::string(argv[i]) == "--minimized") minimized = true;

  droppix::MainWindow w;
  if (minimized) w.startMinimizedToTray(); else w.show();
  return app.exec();
}
