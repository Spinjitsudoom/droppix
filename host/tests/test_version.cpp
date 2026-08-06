#include <gtest/gtest.h>

#include <cstring>

#include "version.h"

// app_version() is fed by a build-time-generated header; if the generation or the
// fallback ever breaks, the string goes empty/null. Guard both.
TEST(Version, IsNonEmpty) {
  const char* v = droppix::app_version();
  ASSERT_NE(v, nullptr);
  EXPECT_GT(std::strlen(v), 0u);
}
