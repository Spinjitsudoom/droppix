#pragma once
#include <QObject>
#include <QProcess>
#include "args_builder.h"
#include "stats_parser.h"

namespace droppix {
class StreamController : public QObject {
  Q_OBJECT
 public:
  explicit StreamController(QObject* parent = nullptr);
  void start(const Command& cmd);
  void stop();
  bool running() const;
  void writeLine(const QString& s);
 signals:
  void statsReceived(const droppix::Stats& stats);
  void logLine(const QString& line);
  void runningChanged(bool running);
  void approvalRequested(QString id, QString name, QString ip);
  void connecting(QString ip);   // a client's socket/TLS was accepted (pre-HELLO)
 private:
  void onReadyRead();
  QProcess proc_;
  QByteArray buf_;
  // Stats-json lines are consumed for statsReceived and never reach the log console;
  // echo a compact human-readable summary there every few seconds so users can read
  // (and paste) live fps/interval/send diagnostics from F12 without extra tooling.
  qint64 lastStatsEchoMs_ = 0;
};
}  // namespace droppix
