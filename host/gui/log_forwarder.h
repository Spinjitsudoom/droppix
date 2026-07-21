#pragma once
#include <QtGlobal>
#include "log_entry.h"

namespace droppix {

class LogBuffer;

// Map a Qt message type to our level.
LogLevel levelForMsgType(QtMsgType t);

// Install a qInstallMessageHandler shim that forwards every Qt log message
// (qDebug/qInfo/qWarning/qCritical/qFatal) into `buffer` on the GUI thread,
// while chaining the previously installed handler so terminal/journald output
// is preserved. Call once, after QApplication is constructed.
void installLogForwarder(LogBuffer* buffer);

}  // namespace droppix
