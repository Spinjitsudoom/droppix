#include <QApplication>
#include <QIcon>
#include "main_window.h"
#include "style.h"

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  app.setStyle("Fusion");                  // consistent base for the custom dark QSS
  app.setStyleSheet(droppix::styleSheet());
  app.setWindowIcon(QIcon(":/icon.png"));  // taskbar / window-manager icon
  droppix::MainWindow w;
  w.show();
  return app.exec();
}
