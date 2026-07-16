#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "encoder.h"

namespace droppix {

enum class EncoderPref { Auto, Nvenc, Vaapi, Software };
enum class EncoderBackend { Nvenc, Vaapi, Software };

EncoderPref parse_encoder_pref(const std::string& s);
std::vector<EncoderBackend> select_encoder_order(EncoderPref pref);

// Tries each candidate factory in order; the first whose constructed Encoder
// opens successfully becomes `chosen_`, and all Encoder calls delegate to it.
// If no candidate opens, open() returns false and every other call is a no-op.
class AutoEncoder : public Encoder {
 public:
  explicit AutoEncoder(std::vector<std::function<std::unique_ptr<Encoder>()>> candidates)
      : candidates_(std::move(candidates)) {}
  bool open(int width, int height, int fps, int bitrate_kbps) override;
  std::vector<unsigned char> extradata() const override;
  std::vector<EncodedPacket> encode(const Frame& frame, int64_t pts_us) override;
  std::vector<EncodedPacket> flush() override;

 private:
  std::vector<std::function<std::unique_ptr<Encoder>()>> candidates_;
  std::unique_ptr<Encoder> chosen_;
};

// Builds an AutoEncoder whose candidates come from select_encoder_order(pref)
// (NVENC -> VAAPI -> Software for Auto; a single backend when forced).
std::unique_ptr<Encoder> make_encoder(EncoderPref pref);

}  // namespace droppix
