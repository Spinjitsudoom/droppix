#include <QApplication>
#include "main_window.h"
#include "style.h"

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  app.setStyle("Fusion");
  app.setStyleSheet(droppix::styleSheet());
  droppix::MainWindow w;
  w.show();
  return app.exec();
}
