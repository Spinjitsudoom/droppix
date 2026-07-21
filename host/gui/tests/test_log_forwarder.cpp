#include "log_forwarder.h"
#include <gtest/gtest.h>

using namespace droppix;

TEST(LogForwarder, LevelMapping) {
  EXPECT_EQ(levelForMsgType(QtDebugMsg), LogLevel::Info);
  EXPECT_EQ(levelForMsgType(QtInfoMsg), LogLevel::Info);
  EXPECT_EQ(levelForMsgType(QtWarningMsg), LogLevel::Warn);
  EXPECT_EQ(levelForMsgType(QtCriticalMsg), LogLevel::Error);
  EXPECT_EQ(levelForMsgType(QtFatalMsg), LogLevel::Error);
}
