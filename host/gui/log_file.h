#pragma once
#include <QString>
#include "log_entry.h"

namespace droppix {

// Every log line (GUI + every streamer session) also goes to a file, so a problem can be
// diagnosed after the fact; the F12 panel only holds the live session in memory. Default path:
// ~/.local/state/droppix/droppix.log. Past `maxBytes` the file moves to "<path>.1" (one
// generation kept) so a long-running tray app can't fill the disk.
class LogFile {
 public:
  LogFile(QString path, qint64 maxBytes = 2 * 1024 * 1024);
  void write(const LogEntry& e);
  const QString& path() const { return path_; }

  // "2026-11-26T00:11:13 [session][source] text" — also what the panel's "Save log" writes.
  static QString format(const LogEntry& e);
  // $XDG_STATE_HOME/droppix/droppix.log (default ~/.local/state/droppix/droppix.log).
  static QString defaultPath();

 private:
  QString path_;
  qint64 maxBytes_;
  bool dirReady_ = false;
};

}  // namespace droppix
