#include "log_classify.h"
#include <QRegularExpression>

namespace droppix {
namespace {

LogLevel levelForText(const QString& raw) {
  const QString l = raw.toLower();
  static const char* kError[] = {"fail", "error", "errno", "refused", "cannot", "unable"};
  static const char* kWarn[]  = {"warn", "retry", "deprecated"};
  for (const char* w : kError) if (l.contains(QLatin1String(w))) return LogLevel::Error;
  for (const char* w : kWarn)  if (l.contains(QLatin1String(w))) return LogLevel::Warn;
  return LogLevel::Info;
}

}  // namespace

Classified classifyStreamerLine(const QString& raw) {
  // Tag must start with a lowercase letter so "12:04" style prefixes don't match.
  static const QRegularExpression kTag(QStringLiteral("^([a-z][a-z0-9_-]*):\\s*(.*)$"));
  Classified c;
  const QRegularExpressionMatch m = kTag.match(raw);
  if (m.hasMatch()) {
    c.source = m.captured(1);
    c.text = m.captured(2);
  } else {
    c.text = raw;
  }
  c.level = levelForText(raw);
  return c;
}

}  // namespace droppix
