#include "log_forwarder.h"
#include "log_buffer.h"
#include <QDateTime>
#include <QMetaObject>
#include <QString>

namespace droppix {
namespace {

LogBuffer*       g_buffer = nullptr;
QtMessageHandler g_previous = nullptr;

void handler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
  if (g_previous) g_previous(type, ctx, msg);   // keep terminal/journald output
  LogBuffer* buf = g_buffer;
  if (!buf) return;
  LogEntry e;
  e.epochMs = QDateTime::currentMSecsSinceEpoch();
  e.source = QStringLiteral("gui");
  e.level = levelForMsgType(type);
  e.text = msg;
  // Qt messages may originate on any thread; marshal onto the buffer's (GUI) thread.
  QMetaObject::invokeMethod(buf, [buf, e] { buf->append(e); }, Qt::QueuedConnection);
}

}  // namespace

LogLevel levelForMsgType(QtMsgType t) {
  switch (t) {
    case QtWarningMsg:  return LogLevel::Warn;
    case QtCriticalMsg:
    case QtFatalMsg:    return LogLevel::Error;
    case QtDebugMsg:
    case QtInfoMsg:
    default:            return LogLevel::Info;
  }
}

void installLogForwarder(LogBuffer* buffer) {
  g_buffer = buffer;
  g_previous = qInstallMessageHandler(handler);
}

}  // namespace droppix
