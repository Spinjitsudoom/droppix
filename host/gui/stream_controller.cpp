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

void StreamController::writeLine(const QString& s) {
  if (proc_.state() == QProcess::Running) { proc_.write((s + "\n").toUtf8()); }
}

namespace {
// Parses "approve-request id=<id> name=<name> ip=<ip>". The device name may itself
// contain spaces (e.g. "Nexus 10"), so we locate the markers rather than splitting
// naively on whitespace: id is between "id=" and " name=", name is between "name="
// and " ip=", and ip is everything after "ip=".
bool parseApproveRequest(const QString& line, QString& id, QString& name, QString& ip) {
  static const QString kPrefix = QStringLiteral("approve-request ");
  if (!line.startsWith(kPrefix)) return false;
  const QString rest = line.mid(kPrefix.size());
  const int idPos = rest.indexOf(QStringLiteral("id="));
  const int namePos = rest.indexOf(QStringLiteral(" name="));
  const int ipPos = rest.indexOf(QStringLiteral(" ip="));
  if (idPos < 0 || namePos < 0 || ipPos < 0) return false;
  id = rest.mid(idPos + 3, namePos - (idPos + 3));
  name = rest.mid(namePos + 6, ipPos - (namePos + 6));
  ip = rest.mid(ipPos + 4);
  return true;
}
}  // namespace

void StreamController::onReadyRead() {
  buf_ += proc_.readAll();
  int nl;
  while ((nl = buf_.indexOf('\n')) >= 0) {
    QString line = QString::fromUtf8(buf_.left(nl)).trimmed();
    buf_.remove(0, nl + 1);
    if (line.isEmpty()) continue;
    QString id, name, ip;
    if (parseApproveRequest(line, id, name, ip)) {
      emit approvalRequested(id, name, ip);
      continue;
    }
    Stats s = parse_stats_json(line.toStdString());
    if (s.valid) emit statsReceived(s);
    else emit logLine(line);
  }
}
}  // namespace droppix
