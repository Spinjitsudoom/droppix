#include "log_buffer.h"

namespace droppix {

LogBuffer::LogBuffer(QObject* parent) : QObject(parent) {}

void LogBuffer::append(const LogEntry& e) {
  entries_.push_back(e);
  while (static_cast<int>(entries_.size()) > kCap) entries_.pop_front();
  emit entryAdded(e);
}

void LogBuffer::clear() {
  entries_.clear();
  emit cleared();
}

}  // namespace droppix
