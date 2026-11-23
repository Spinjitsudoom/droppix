#include <QApplication>
#include <csignal>
#include "main_window.h"

int main(int argc, char** argv) {
  // SSL_write to a socket the host has closed raises SIGPIPE, which silently killed the
  // whole client (exit 141) whenever the PC stopped the session. See the host lesson
  // web-client-sigpipe-death.
  std::signal(SIGPIPE, SIG_IGN);
  QApplication app(argc, argv);
  app.setStyle("Fusion");
  // The stylesheet is applied by MainWindow, which knows the persisted theme; setting a
  // themeless one here would flash the wrong palette on startup.
  droppix::MainWindow w;
  w.show();
  return app.exec();
}
