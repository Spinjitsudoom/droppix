#include "log_classify.h"
#include <gtest/gtest.h>

using namespace droppix;

TEST(LogClassify, TaggedErrorLine) {
  const Classified c = classifyStreamerLine("tls: SSL_accept failed");
  EXPECT_EQ(c.source, QStringLiteral("tls"));
  EXPECT_EQ(c.text, QStringLiteral("SSL_accept failed"));
  EXPECT_EQ(c.level, LogLevel::Error);
}

TEST(LogClassify, TaggedInfoLine) {
  const Classified c = classifyStreamerLine("web: websocket client from 192.168.1.5");
  EXPECT_EQ(c.source, QStringLiteral("web"));
  EXPECT_EQ(c.text, QStringLiteral("websocket client from 192.168.1.5"));
  EXPECT_EQ(c.level, LogLevel::Info);
}

TEST(LogClassify, TaggedWarnLine) {
  const Classified c = classifyStreamerLine("vaapi: low_power entrypoint retry");
  EXPECT_EQ(c.source, QStringLiteral("vaapi"));
  EXPECT_EQ(c.level, LogLevel::Warn);
}

TEST(LogClassify, UntaggedLine) {
  const Classified c = classifyStreamerLine("starting encoder");
  EXPECT_TRUE(c.source.isEmpty());
  EXPECT_EQ(c.text, QStringLiteral("starting encoder"));
  EXPECT_EQ(c.level, LogLevel::Info);
}

TEST(LogClassify, TimestampIsNotATag) {
  // A leading digit run must NOT be treated as a source tag.
  const Classified c = classifyStreamerLine("12:04 something happened");
  EXPECT_TRUE(c.source.isEmpty());
  EXPECT_EQ(c.text, QStringLiteral("12:04 something happened"));
}
