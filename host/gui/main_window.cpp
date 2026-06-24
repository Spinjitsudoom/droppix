#include "main_window.h"
#include <QLabel>

namespace droppix {
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  setWindowTitle("droppix");
  setCentralWidget(new QLabel("droppix host control (scaffold)", this));
  resize(480, 360);
}
}  // namespace droppix
