#include <QApplication>
#include "main_window.h"

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  app.setStyle("Fusion");
  // The stylesheet is applied by MainWindow, which knows the persisted theme; setting a
  // themeless one here would flash the wrong palette on startup.
  droppix::MainWindow w;
  w.show();
  return app.exec();
}
