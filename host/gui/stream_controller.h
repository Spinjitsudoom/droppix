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
 signals:
  void statsReceived(const droppix::Stats& stats);
  void logLine(const QString& line);
  void runningChanged(bool running);
 private:
  void onReadyRead();
  QProcess proc_;
  QByteArray buf_;
};
}  // namespace droppix
