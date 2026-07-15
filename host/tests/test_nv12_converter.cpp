#include <gtest/gtest.h>
#include "nv12_converter.h"
#include "capturer.h"
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}
using namespace droppix;

TEST(Nv12Converter, ConvertsToNv12Plane) {
  Nv12Converter c;
  ASSERT_TRUE(c.open(16, 16));
  Frame f; f.width = 16; f.height = 16; f.stride = 16 * 4; f.valid = true;
  f.bgra.assign(16 * 16 * 4, 0); for (size_t i = 2; i < f.bgra.size(); i += 4) f.bgra[i] = 255; // red
  AVFrame* out = c.convert(f);
  ASSERT_NE(out, nullptr);
  EXPECT_EQ(out->format, AV_PIX_FMT_NV12);
  EXPECT_EQ(out->width, 16); EXPECT_EQ(out->height, 16);
  EXPECT_GT(out->linesize[0], 0);              // Y plane present
  EXPECT_GT(out->data[0][0], 0);               // red -> nonzero luma
}
