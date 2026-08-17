#include <gtest/gtest.h>
#include "orientation.h"
using namespace droppix;

TEST(Orientation, PortraitCodes) {
  EXPECT_FALSE(orientation_is_portrait(0));   // 0°   landscape
  EXPECT_TRUE(orientation_is_portrait(1));    // 90°  portrait
  EXPECT_FALSE(orientation_is_portrait(2));   // 180° landscape (flipped)
  EXPECT_TRUE(orientation_is_portrait(3));    // 270° portrait (flipped)
}

// A landscape-natural tablet sends landscape dims. Held portrait, the monitor must be built
// portrait-shaped — the case that always worked.
TEST(OrientDims, LandscapeDimsHeldPortraitSwap) {
  int w = 1920, h = 1080;
  orient_dims(1, w, h);
  EXPECT_EQ(w, 1080);
  EXPECT_EQ(h, 1920);
}

// The case that was broken. A portrait-natural phone reports the dims it measured at launch
// (1080x2340) even when turned landscape, because configChanges keeps the Activity alive.
// Without swapping here the host builds a portrait monitor for a landscape phone: the picture
// is a narrow strip between black bars, and the session restart-loops forever.
TEST(OrientDims, PortraitDimsHeldLandscapeSwap) {
  int w = 1080, h = 2340;
  orient_dims(0, w, h);
  EXPECT_EQ(w, 2340);
  EXPECT_EQ(h, 1080);

  int w2 = 1080, h2 = 2340;
  orient_dims(2, w2, h2);      // 180° is landscape too
  EXPECT_EQ(w2, 2340);
  EXPECT_EQ(h2, 1080);
}

TEST(OrientDims, LeavesAlreadyCorrectShapeAlone) {
  int w = 2340, h = 1080;
  orient_dims(0, w, h);        // landscape dims, landscape orientation
  EXPECT_EQ(w, 2340);
  EXPECT_EQ(h, 1080);

  int w2 = 1080, h2 = 2340;
  orient_dims(3, w2, h2);      // portrait dims, portrait orientation
  EXPECT_EQ(w2, 1080);
  EXPECT_EQ(h2, 2340);
}

// The restart handler compares the reported shape against the built one (`h > w`). If
// normalising did not make those agree, every reconnect would restart the session again.
// This is the property that stops the loop, so assert it directly.
TEST(OrientDims, ResultAgreesWithTheRestartPredicate) {
  for (int code = 0; code <= 3; ++code) {
    for (auto dims : {std::pair<int,int>{1920,1080}, {1080,2340}, {800,800}}) {
      int w = dims.first, h = dims.second;
      orient_dims(code, w, h);
      const bool built_portrait = h > w;
      if (w == h) continue;    // square is shapeless; the handler cannot disagree usefully
      EXPECT_EQ(built_portrait, orientation_is_portrait(code))
          << "code=" << code << " would restart-loop for " << dims.first << "x" << dims.second;
    }
  }
}
