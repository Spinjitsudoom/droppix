#include "log_buffer.h"
#include "log_model.h"
#include <gtest/gtest.h>

using namespace droppix;

static LogEntry mk(LogLevel lvl, const QString& src, const QString& text) {
  LogEntry e;
  e.level = lvl;
  e.source = src;
  e.text = text;
  return e;
}

TEST(LogFilter, LevelAndSearchAndSource) {
  LogBuffer buf;
  buf.append(mk(LogLevel::Info,  "web", "websocket client connected"));
  buf.append(mk(LogLevel::Error, "tls", "SSL_accept failed"));
  buf.append(mk(LogLevel::Info,  "enc", "vaapi ready"));

  LogModel model(&buf);
  LogFilterProxy proxy;
  proxy.setSourceModel(&model);
  EXPECT_EQ(proxy.rowCount(), 3);

  // Disable Info -> only the Error row remains.
  proxy.setLevelEnabled(LogLevel::Info, false);
  EXPECT_EQ(proxy.rowCount(), 1);
  proxy.setLevelEnabled(LogLevel::Info, true);
  EXPECT_EQ(proxy.rowCount(), 3);

  // Substring search over text.
  proxy.setSearchText("client");
  EXPECT_EQ(proxy.rowCount(), 1);
  proxy.setSearchText("");
  EXPECT_EQ(proxy.rowCount(), 3);

  // Exact source filter.
  proxy.setSourceFilter("tls");
  EXPECT_EQ(proxy.rowCount(), 1);
  proxy.setSourceFilter("");
  EXPECT_EQ(proxy.rowCount(), 3);
}

TEST(LogFilter, BackfillReflectsExistingEntries) {
  LogBuffer buf;
  buf.append(mk(LogLevel::Info, "web", "one"));
  buf.append(mk(LogLevel::Info, "web", "two"));
  LogModel model(&buf);   // constructed AFTER entries exist
  EXPECT_EQ(model.rowCount(), 2);
}
