#include "stream_controller.h"
#include <QStringList>

namespace droppix {

StreamController::StreamController(QObject* parent) : QObject(parent) {
  proc_.setProcessChannelMode(QProcess::MergedChannels);  // capture stderr+stdout
  connect(&proc_, &QProcess::readyRead, this, &StreamController::onReadyRead);
  connect(&proc_, &QProcess::started, this, [this]{ emit runningChanged(true); });
  connect(&proc_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
          this, [this](int, QProcess::ExitStatus){ emit runningChanged(false); });
}

void StreamController::start(const Command& cmd) {
  if (running()) return;
  QStringList args;
  for (const auto& a : cmd.args) args << QString::fromStdString(a);
  proc_.start(QString::fromStdString(cmd.program), args);
}

void StreamController::stop() {
  if (!running()) return;
  // The evdi streamer runs as ROOT (pkexec), so we can't signal it — but we own its
  // stdin pipe. Closing it makes the streamer exit on EOF (see stream_main). terminate()
  // additionally covers the unprivileged test-pattern path.
  proc_.closeWriteChannel();
  proc_.terminate();
  if (!proc_.waitForFinished(3000)) proc_.kill();
}

bool StreamController::running() const {
  return proc_.state() != QProcess::NotRunning;
}

void StreamController::onReadyRead() {
  buf_ += proc_.readAll();
  int nl;
  while ((nl = buf_.indexOf('\n')) >= 0) {
    QString line = QString::fromUtf8(buf_.left(nl)).trimmed();
    buf_.remove(0, nl + 1);
    if (line.isEmpty()) continue;
    Stats s = parse_stats_json(line.toStdString());
    if (s.valid) emit statsReceived(s);
    else emit logLine(line);
  }
}
}  // namespace droppix
