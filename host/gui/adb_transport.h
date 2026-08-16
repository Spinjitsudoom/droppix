#pragma once
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QMetaType>

namespace droppix {

// A tablet attached over USB with debugging enabled.
struct AdbClient {
  QString serial;    // adb serial — the stable identity for this transport
  QString model;     // ro.product.model, or the serial if it could not be read
};

// Serials from `adb devices` output that are ready to carry a tunnel.
//
// Only the "device" state qualifies: a phone sitting at the "Allow USB debugging?" prompt
// reports "unauthorized", and one mid-handshake reports "offline". Tunnelling either fails,
// and treating them as done would skip the device forever once it did come ready.
QSet<QString> parse_ready_serials(const QString& devices_output);

// The adb USB transport: discovers attached tablets, keeps their reverse tunnels alive, and
// starts a session on one without anybody touching the tablet.
//
// The client's "Connect via USB" button dials 127.0.0.1:<port>, which only reaches this PC
// through `adb reverse`. Nothing else creates it: the original AdbManager was removed when
// the GUI moved to tether/AOA discovery (a349ee5), leaving the client's USB button dialling
// a port with nothing behind it.
//
// Polls rather than firing once at startup, because the tunnel belongs to the adb
// connection and not to droppix: it does not exist until the cable is plugged in, and it
// disappears on unplug or when the adb server restarts. A device is re-tunnelled whenever
// it reappears.
//
// `adb` is optional. If it is missing this goes quiet after one message — USB tethering and
// AOA are separate paths that do not need it.
class AdbTransport : public QObject {
  Q_OBJECT
 public:
  explicit AdbTransport(QObject* parent = nullptr);

  // Begin (or re-target) tunnelling to `port`. Idempotent; a new port re-tunnels every
  // attached device.
  void start(int port);
  void stop();

  // Point one tablet at 127.0.0.1:<port> and launch the client there: tunnel first, then
  // `am start` with the autoconnect extras, so the user never taps anything on the tablet.
  void usbConnect(const QString& serial, int port);

 signals:
  void message(const QString& text);                     // one-line status for the log panel
  void clientsChanged(QList<droppix::AdbClient> clients); // attached tablets, for the device list

 private:
  void poll();                          // `adb devices` -> tunnel any device we have not done
  void applyTo(const QString& serial);
  void resolveModel(const QString& serial);

  QTimer timer_;
  int port_ = 0;
  bool adbMissing_ = false;      // stop nagging once we know adb is not installed
  QSet<QString> tunnelled_;      // serials currently carrying our reverse tunnel
  QHash<QString, QString> models_;   // serial -> ro.product.model (cached; one lookup each)
  QList<AdbClient> clients_;
};

}  // namespace droppix

Q_DECLARE_METATYPE(droppix::AdbClient)
