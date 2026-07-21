#include "log_buffer.h"
#include <gtest/gtest.h>

using namespace droppix;

static LogEntry mk(const QString& t) {
  LogEntry e;
  e.text = t;
  return e;
}

TEST(LogBuffer, AppendEmitsAndStores) {
  LogBuffer buf;
  int count = 0;
  LogEntry last;
  QObject::connect(&buf, &LogBuffer::entryAdded, &buf,
                   [&](const LogEntry& e) { ++count; last = e; });
  buf.append(mk("hello"));
  EXPECT_EQ(count, 1);
  EXPECT_EQ(last.text, QStringLiteral("hello"));
  EXPECT_EQ(buf.entries().size(), static_cast<size_t>(1));
}

TEST(LogBuffer, RingCapDropsOldest) {
  LogBuffer buf;
  for (int i = 0; i < LogBuffer::kCap + 10; ++i) buf.append(mk(QString::number(i)));
  EXPECT_EQ(static_cast<int>(buf.entries().size()), LogBuffer::kCap);
  EXPECT_EQ(buf.entries().front().text, QString::number(10));            // 0..9 dropped
  EXPECT_EQ(buf.entries().back().text, QString::number(LogBuffer::kCap + 9));
}

TEST(LogBuffer, ClearEmpties) {
  LogBuffer buf;
  buf.append(mk("x"));
  int cleared = 0;
  QObject::connect(&buf, &LogBuffer::cleared, &buf, [&] { ++cleared; });
  buf.clear();
  EXPECT_TRUE(buf.entries().empty());
  EXPECT_EQ(cleared, 1);
}
