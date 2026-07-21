#pragma once
#include <QString>
#include "log_entry.h"

namespace droppix {

struct Classified {
  QString  source;
  LogLevel level = LogLevel::Info;
  QString  text;
};

// Split a raw streamer log line into a source tag (leading "word:" where word
// starts with a lowercase letter), an inferred level (keyword heuristic), and
// the remaining text. Best-effort; the raw text is always preserved.
Classified classifyStreamerLine(const QString& raw);

}  // namespace droppix
