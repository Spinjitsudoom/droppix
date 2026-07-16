#include <gtest/gtest.h>
#include <functional>
#include <memory>
#include <vector>
#include "encoder_factory.h"
using namespace droppix;

namespace {
struct FakeEnc : droppix::Encoder {
  bool ok; int* opened; const char* tag;
  FakeEnc(bool o, int* c, const char* t) : ok(o), opened(c), tag(t) {}
  bool open(int,int,int,int) override { if (opened) ++*opened; return ok; }
  std::vector<unsigned char> extradata() const override { return { (unsigned char)tag[0] }; }
  std::vector<droppix::EncodedPacket> encode(const droppix::Frame&, int64_t) override { return {}; }
  std::vector<droppix::EncodedPacket> flush() override { return {}; }
};
}
TEST(AutoEncoder, CascadesToFirstSuccess) {
  int a=0,b=0,c=0;
  std::vector<std::function<std::unique_ptr<droppix::Encoder>()>> cands;
  cands.push_back([&]{ return std::make_unique<FakeEnc>(false,&a,"A"); });
  cands.push_back([&]{ return std::make_unique<FakeEnc>(true, &b,"B"); });
  cands.push_back([&]{ return std::make_unique<FakeEnc>(true, &c,"C"); });
  droppix::AutoEncoder e(std::move(cands));
  ASSERT_TRUE(e.open(16,16,30,4000));
  EXPECT_EQ(a,1); EXPECT_EQ(b,1); EXPECT_EQ(c,0);          // stopped at B
  EXPECT_EQ(e.extradata(), (std::vector<unsigned char>{ 'B' }));  // delegates to B
}
TEST(AutoEncoder, ForcedFailureDoesNotFallBack) {
  int a=0;
  std::vector<std::function<std::unique_ptr<droppix::Encoder>()>> cands;
  cands.push_back([&]{ return std::make_unique<FakeEnc>(false,&a,"A"); });
  droppix::AutoEncoder e(std::move(cands));
  EXPECT_FALSE(e.open(16,16,30,4000));                      // single forced candidate failed
  EXPECT_EQ(a,1);
}

TEST(EncoderPrefParse, KnownAndUnknown) {
  EXPECT_EQ(parse_encoder_pref("auto"),     EncoderPref::Auto);
  EXPECT_EQ(parse_encoder_pref("nvenc"),    EncoderPref::Nvenc);
  EXPECT_EQ(parse_encoder_pref("vaapi"),    EncoderPref::Vaapi);
  EXPECT_EQ(parse_encoder_pref("software"), EncoderPref::Software);
  EXPECT_EQ(parse_encoder_pref("garbage"),  EncoderPref::Auto);   // default
}
TEST(EncoderOrder, AutoIsNvencVaapiSoftware) {
  EXPECT_EQ(select_encoder_order(EncoderPref::Auto),
            (std::vector<EncoderBackend>{EncoderBackend::Nvenc, EncoderBackend::Vaapi, EncoderBackend::Software}));
}
TEST(EncoderOrder, ForcedIsSingleton) {
  EXPECT_EQ(select_encoder_order(EncoderPref::Vaapi), (std::vector<EncoderBackend>{EncoderBackend::Vaapi}));
  EXPECT_EQ(select_encoder_order(EncoderPref::Software), (std::vector<EncoderBackend>{EncoderBackend::Software}));
}
