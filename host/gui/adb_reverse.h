#pragma once
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

namespace droppix {

// Serials from `adb devices` output that are ready to carry a tunnel.
//
// Only the "device" state qualifies: a phone sitting at the "Allow USB debugging?" prompt
// reports "unauthorized", and one mid-handshake reports "offline". Tunnelling either fails,
// and treating them as done would skip the device forever once it did come ready.
QSet<QString> parse_ready_serials(const QString& devices_output);

// Keeps `adb reverse tcp:P tcp:P` in place for every attached adb device.
//
// The Android client's "Connect via USB" button dials 127.0.0.1:<port>, which only reaches
// this PC if that reverse tunnel exists. Nothing else creates it: the original AdbManager
// was removed when the GUI moved to tether/AOA discovery (a349ee5), which left the client's
// USB button dialling a port with nothing behind it — the connection was refused and no
// virtual display was ever created.
//
// Polls rather than firing once at startup, because the tunnel belongs to the adb
// connection and not to droppix: it does not exist until the cable is plugged in, and it
// disappears on unplug or when the adb server restarts. A device is re-tunnelled whenever
// it reappears.
//
// `adb` is optional. If it is missing this goes quiet after one message — USB tethering and
// AOA are separate paths that do not need it.
class AdbReverse : public QObject {
  Q_OBJECT
 public:
  explicit AdbReverse(QObject* parent = nullptr);

  // Begin (or re-target) tunnelling to `port`. Idempotent; a new port re-tunnels every
  // attached device.
  void start(int port);
  void stop();

 signals:
  void message(const QString& text);   // one-line status for the log panel

 private:
  void poll();                          // `adb devices` -> tunnel any device we have not done
  void applyTo(const QString& serial);

  QTimer timer_;
  int port_ = 0;
  bool adbMissing_ = false;      // stop nagging once we know adb is not installed
  QSet<QString> tunnelled_;      // serials currently carrying our reverse tunnel
};

}  // namespace droppix
