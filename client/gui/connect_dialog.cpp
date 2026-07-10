#include "connect_dialog.h"
#include "transport_client.h"
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QInputDialog>
#include <QGuiApplication>

namespace droppix {

ConnectDialog::ConnectDialog(HostStore& hostStore, TlsTrust& tlsTrust, QWidget* parent)
    : QDialog(parent), hostStore_(hostStore), tlsTrust_(tlsTrust) {
  setWindowTitle("Connect to droppix host");
  auto* layout = new QVBoxLayout(this);

  layout->addWidget(new QLabel("Known hosts:"));
  list_ = new QListWidget(this);
  connect(list_, &QListWidget::itemDoubleClicked, this, &ConnectDialog::onHostListDoubleClicked);
  layout->addWidget(list_);
  refreshList();

  layout->addWidget(new QLabel("Or enter host[:port] manually:"));
  addrEdit_ = new QLineEdit(this);
  addrEdit_->setPlaceholderText("192.168.1.100:27000");
  layout->addWidget(addrEdit_);

  auto* buttons = new QHBoxLayout();
  connectBtn_ = new QPushButton("Connect", this);
  auto* cancelBtn = new QPushButton("Cancel", this);
  connect(connectBtn_, &QPushButton::clicked, this, &ConnectDialog::onConnectClicked);
  connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
  buttons->addWidget(connectBtn_);
  buttons->addWidget(cancelBtn);
  layout->addLayout(buttons);
}

void ConnectDialog::refreshList() {
  list_->clear();
  for (const auto& h : hostStore_.hosts()) {
    auto* item = new QListWidgetItem(h.label, list_);
    item->setData(Qt::UserRole, h.host);
    item->setData(Qt::UserRole + 1, h.port);
  }
}

void ConnectDialog::onHostListDoubleClicked() {
  // Double-clicking a row is an unambiguous choice — use it directly rather than
  // routing through onConnectClicked(), which would otherwise re-read whatever is
  // (or isn't) in the manual-entry field.
  auto* item = list_->currentItem();
  if (!item) return;
  connectWith(item->data(Qt::UserRole).toString(),
              static_cast<quint16>(item->data(Qt::UserRole + 1).toUInt()));
}

void ConnectDialog::onConnectClicked() {
  // The manually-typed address always wins when present — a leftover/auto-highlighted
  // selection in the known-hosts list must never silently override what the user typed.
  QString text = addrEdit_->text().trimmed();
  if (!text.isEmpty()) {
    QString host; quint16 port = 27000;
    int colon = text.lastIndexOf(':');
    if (colon > 0) { host = text.left(colon); port = static_cast<quint16>(text.mid(colon + 1).toUInt()); }
    else host = text;
    if (host.isEmpty()) { QMessageBox::warning(this, "Droppix", "Enter a valid host address."); return; }
    connectWith(host, port);
    return;
  }
  if (auto* item = list_->currentItem()) {
    connectWith(item->data(Qt::UserRole).toString(),
                static_cast<quint16>(item->data(Qt::UserRole + 1).toUInt()));
    return;
  }
  QMessageBox::warning(this, "Droppix", "Enter a host address.");
}

void ConnectDialog::connectWith(const QString& host, quint16 port) {
  QGuiApplication::setOverrideCursor(Qt::WaitCursor);
  bool ok = ensurePaired(host, port);
  QGuiApplication::restoreOverrideCursor();
  if (!ok) return;

  hostStore_.remember(host, port);
  host_ = host;
  port_ = port;
  accept();
}

bool ConnectDialog::ensurePaired(const QString& host, quint16 port) {
  const std::string h = host.toStdString();
  if (h == "127.0.0.1" || tlsTrust_.isPaired(h)) return true;

  auto probe = probe_pairing_code(h, port);
  if (!probe) {
    QMessageBox::warning(this, "Droppix",
        QString("Couldn't reach %1:%2.").arg(host).arg(port));
    return false;
  }
  bool entered = false;
  QString code = QInputDialog::getText(this, "Pair with this PC",
      QString("Enter the 6-digit code shown on the PC:"),
      QLineEdit::Normal, QString(), &entered);
  if (!entered) return false;
  if (code.trimmed().toStdString() != probe->code) {
    QMessageBox::warning(this, "Droppix", "Wrong code.");
    return false;
  }
  tlsTrust_.pin(h, probe->fingerprint);
  return true;
}

}  // namespace droppix
