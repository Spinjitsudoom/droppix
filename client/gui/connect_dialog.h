#pragma once
#include <QDialog>
#include "host_store.h"
#include "tls_trust.h"

class QListWidget;
class QLineEdit;
class QPushButton;

namespace droppix {

// Gathers a host:port to connect to (saved-hosts list or manual entry) and ensures
// pairing is established before returning control to MainWindow — mirrors
// ConnectActivity.kt's role: manual address entry (v1; mDNS discovery deferred) plus
// the first-connect PIN flow (probe -> derive 6-digit code -> user confirms -> pin).
//
// v1 simplification: the pairing probe blocks the UI thread briefly (bounded by its own
// short timeout) rather than using a worker thread — acceptable for a one-time,
// user-initiated action; not used on the hot path of an already-paired reconnect.
class ConnectDialog : public QDialog {
  Q_OBJECT
 public:
  ConnectDialog(HostStore& hostStore, TlsTrust& tlsTrust, QWidget* parent = nullptr);

  QString chosenHost() const { return host_; }
  quint16 chosenPort() const { return port_; }

 private slots:
  void onConnectClicked();
  void onHostListDoubleClicked();

 private:
  bool ensurePaired(const QString& host, quint16 port);   // probes + prompts if needed
  void connectWith(const QString& host, quint16 port);     // ensurePaired + remember + accept
  void refreshList();

  HostStore& hostStore_;
  TlsTrust& tlsTrust_;
  QListWidget* list_;
  QLineEdit* addrEdit_;
  QPushButton* connectBtn_;
  QString host_;
  quint16 port_ = 0;
};

}  // namespace droppix
