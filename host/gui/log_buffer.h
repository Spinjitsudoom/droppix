#pragma once
#include <QObject>
#include <deque>
#include "log_entry.h"

namespace droppix {

// The single in-memory sink for all log entries. Ring buffer capped at kCap;
// oldest entries are dropped. GUI-thread affinity: append() is only called on
// the GUI thread (the log forwarder marshals cross-thread messages).
class LogBuffer : public QObject {
  Q_OBJECT
 public:
  static constexpr int kCap = 5000;
  explicit LogBuffer(QObject* parent = nullptr);

  void append(const LogEntry& e);
  void clear();
  const std::deque<LogEntry>& entries() const { return entries_; }

 signals:
  void entryAdded(const LogEntry& e);
  void cleared();

 private:
  std::deque<LogEntry> entries_;
};

}  // namespace droppix
