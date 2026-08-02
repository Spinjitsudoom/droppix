#pragma once
#include <QWidget>

class QCheckBox;
class QLabel;
class QPushButton;
class QVBoxLayout;

namespace droppix {

// "Interfaces" (Communication Interfaces) section: arranges MainWindow-owned
// LAN/USB toggle + per-adapter rows and the web-client URL/QR/copy widgets
// into two cards. This is a layout container only — it does not create,
// own, or wire any signals; MainWindow keeps all existing connect()s and
// refresh logic (refreshInterfaces()/refreshWebClientUi()) against the same
// widget pointers.
class InterfacesPage : public QWidget {
  Q_OBJECT
 public:
  InterfacesPage(QCheckBox* lan, QVBoxLayout* adapters, QCheckBox* usb,
                 QLabel* webUrl, QLabel* webQr, QPushButton* webCopy,
                 QWidget* parent = nullptr);
};

}  // namespace droppix
