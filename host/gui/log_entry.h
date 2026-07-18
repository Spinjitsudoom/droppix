#pragma once
#include <QString>
#include <QtGlobal>

namespace droppix {

enum class LogLevel { Info, Warn, Error };

struct LogEntry {
  qint64   epochMs = 0;      // GUI receipt time (QDateTime::currentMSecsSinceEpoch)
  QString  session;          // session key, e.g. "usb-aoa:R32..."; empty for GUI-global
  QString  source;           // e.g. "tls", "web", "enc", "gui"; may be empty
  LogLevel level = LogLevel::Info;
  QString  text;             // message body (tag stripped into `source` when parsed)
};

}  // namespace droppix
