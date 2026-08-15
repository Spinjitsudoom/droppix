#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "jpeg_stripe_encoder.h"
#include "spacedesk_protocol.h"

using droppix::JpegStripeEncoder;

namespace {

// A BGRA test image with a horizontal gradient so stripes differ from one another.
std::vector<unsigned char> make_bgra(int w, int h) {
  std::vector<unsigned char> buf(static_cast<size_t>(w) * h * 4);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      size_t i = (static_cast<size_t>(y) * w + x) * 4;
      buf[i + 0] = static_cast<unsigned char>(x & 0xff);
      buf[i + 1] = static_cast<unsigned char>(y & 0xff);
      buf[i + 2] = static_cast<unsigned char>((x + y) & 0xff);
      buf[i + 3] = 0xff;
    }
  }
  return buf;
}

// SOF0 carries the true encoded dimensions; that is what the viewer decodes.
bool jpeg_size(const std::vector<unsigned char>& j, int& w, int& h) {
  for (size_t i = 0; i + 9 < j.size(); ++i) {
    if (j[i] == 0xff && j[i + 1] == 0xc0) {
      h = (j[i + 5] << 8) | j[i + 6];
      w = (j[i + 7] << 8) | j[i + 8];
      return true;
    }
  }
  return false;
}

}  // namespace

TEST(JpegStripeEncoder, ProducesAValidBaselineJfif) {
  const int W = 640, STRIPE = 90;
  JpegStripeEncoder enc;
  ASSERT_TRUE(enc.open(W, STRIPE));
  auto img = make_bgra(W, 360);

  auto jpg = enc.encode(img.data(), W * 4, 0, STRIPE);
  ASSERT_FALSE(jpg.empty());
  // SOI ... EOI, exactly what the viewer expects to hand to its decoder.
  EXPECT_EQ(jpg[0], 0xff);
  EXPECT_EQ(jpg[1], 0xd8);
  EXPECT_EQ(jpg[jpg.size() - 2], 0xff);
  EXPECT_EQ(jpg[jpg.size() - 1], 0xd9);
}

TEST(JpegStripeEncoder, EncodedDimensionsAreTheStripeNotTheScreen) {
  const int W = 640, STRIPE = 90;
  JpegStripeEncoder enc;
  ASSERT_TRUE(enc.open(W, STRIPE));
  auto img = make_bgra(W, 360);

  auto jpg = enc.encode(img.data(), W * 4, 180, STRIPE);
  ASSERT_FALSE(jpg.empty());
  int jw = 0, jh = 0;
  ASSERT_TRUE(jpeg_size(jpg, jw, jh));
  EXPECT_EQ(jw, W);
  EXPECT_EQ(jh, STRIPE) << "each stripe must decode standalone at its own height";
}

TEST(JpegStripeEncoder, DifferentStripesEncodeDifferentContent) {
  const int W = 320, STRIPE = 40;
  JpegStripeEncoder enc;
  ASSERT_TRUE(enc.open(W, STRIPE));
  auto img = make_bgra(W, 160);

  auto a = enc.encode(img.data(), W * 4, 0, STRIPE);
  auto b = enc.encode(img.data(), W * 4, 120, STRIPE);
  ASSERT_FALSE(a.empty());
  ASSERT_FALSE(b.empty());
  EXPECT_NE(a, b) << "a y_offset change must actually select different pixels";
}

TEST(JpegStripeEncoder, RejectsRowCountThatDoesNotMatchOpen) {
  JpegStripeEncoder enc;
  ASSERT_TRUE(enc.open(320, 40));
  auto img = make_bgra(320, 160);
  EXPECT_TRUE(enc.encode(img.data(), 320 * 4, 0, 41).empty());
  EXPECT_TRUE(enc.encode(nullptr, 320 * 4, 0, 40).empty());
}

TEST(JpegStripeEncoder, EncodesEveryStripeOfARealisticDisplay) {
  // The captured session: 2340x1080 split into 4 stripes of 270.
  const uint32_t W = 2340, H = 1080;
  uint32_t y0, y1;
  droppix::spacedesk::stripe_bounds(H, 0, y0, y1);
  const int stripe_h = static_cast<int>(y1 - y0);

  JpegStripeEncoder enc;
  ASSERT_TRUE(enc.open(static_cast<int>(W), stripe_h));
  auto img = make_bgra(static_cast<int>(W), static_cast<int>(H));

  for (int i = 0; i < droppix::spacedesk::kStripes; ++i) {
    uint32_t s, e;
    droppix::spacedesk::stripe_bounds(H, i, s, e);
    auto jpg = enc.encode(img.data(), static_cast<int>(W) * 4,
                          static_cast<int>(s), static_cast<int>(e - s));
    ASSERT_FALSE(jpg.empty()) << "stripe " << i;
    int jw = 0, jh = 0;
    ASSERT_TRUE(jpeg_size(jpg, jw, jh));
    EXPECT_EQ(jw, 2340);
    EXPECT_EQ(jh, 270);
  }
}

TEST(JpegStripeEncoder, HigherQualityYieldsMoreBytes) {
  const int W = 320, STRIPE = 80;
  auto img = make_bgra(W, STRIPE);
  JpegStripeEncoder lo, hi;
  ASSERT_TRUE(lo.open(W, STRIPE, 20));
  ASSERT_TRUE(hi.open(W, STRIPE, 95));
  auto a = lo.encode(img.data(), W * 4, 0, STRIPE);
  auto b = hi.encode(img.data(), W * 4, 0, STRIPE);
  ASSERT_FALSE(a.empty());
  ASSERT_FALSE(b.empty());
  EXPECT_LT(a.size(), b.size()) << "the quality knob must actually reach the encoder";
}
