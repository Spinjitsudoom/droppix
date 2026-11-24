#include "tether_scanner.h"
#include "tether_discovery.h"
#include <QFileInfo>
#include <QNetworkInterface>

namespace {
// Only USB adapters carry a tether. Probing every interface made tablets on Wi-Fi answer
// and show up as "— USB" beside their Wi-Fi row.
bool isUsbTether(const QString& ifname) {
  const QString base = "/sys/class/net/" + ifname;
  const bool wireless = QFileInfo::exists(base + "/wireless") || QFileInfo::exists(base + "/phy80211");
  return droppix::is_usb_tether_netdev(QFileInfo(base + "/device").canonicalFilePath().toStdString(),
                                       wireless);
}
}  // namespace

namespace droppix {

TetherScanner::TetherScanner(QObject* p) : QObject(p) {
  sock_.bind(QHostAddress::AnyIPv4, 0, QAbstractSocket::ShareAddress);
  connect(&sock_, &QUdpSocket::readyRead, this, &TetherScanner::onDatagram);
  timer_.setInterval(2000);
  connect(&timer_, &QTimer::timeout, this, &TetherScanner::tick);
  window_.setSingleShot(true);
  window_.setInterval(600);   // collect replies for 600ms, then emit
  connect(&window_, &QTimer::timeout, this, [this]{ emit clientsChanged(acc_); });
}

void TetherScanner::start() { tick(); timer_.start(); }
void TetherScanner::stop()  { timer_.stop(); window_.stop(); }

void TetherScanner::tick() {
  acc_.clear();
  const auto probe = encode_probe();
  const QByteArray dg(reinterpret_cast<const char*>(probe.data()), (int)probe.size());
  for (const auto& iface : QNetworkInterface::allInterfaces()) {
    if (!(iface.flags() & QNetworkInterface::IsUp) ||
        (iface.flags() & QNetworkInterface::IsLoopBack) || !isUsbTether(iface.name())) continue;
    for (const auto& entry : iface.addressEntries()) {
      const QHostAddress bc = entry.broadcast();
      if (!bc.isNull()) sock_.writeDatagram(dg, bc, kTetherDiscoveryPort);
    }
  }
  window_.start();   // (re)arm; emit the collected set when it fires
}

void TetherScanner::onDatagram() {
  while (sock_.hasPendingDatagrams()) {
    QByteArray buf; buf.resize((int)sock_.pendingDatagramSize());
    QHostAddress from;
    sock_.readDatagram(buf.data(), buf.size(), &from);
    std::vector<unsigned char> b(buf.begin(), buf.end());
    TetherReply r;
    if (!decode_reply(b, r)) continue;
    TetherClient c;
    c.id = QString::fromStdString(r.id);
    c.name = QString::fromStdString(r.name);
    c.address = from.toString();
    // QHostAddress may render IPv4 as "::ffff:1.2.3.4"; keep the tail.
    if (c.address.startsWith("::ffff:")) c.address = c.address.mid(7);
    c.wakePort = r.wake_port;
    bool dup = false;
    for (const auto& e : acc_) if (e.id == c.id) { dup = true; break; }
    if (!dup) acc_.push_back(c);
  }
}

}  // namespace droppix
