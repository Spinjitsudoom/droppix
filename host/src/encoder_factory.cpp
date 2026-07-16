#include "encoder_factory.h"

#include <cstdio>

#include "nvenc_encoder.h"
#include "vaapi_encoder.h"
#include "software_encoder.h"

namespace droppix {

EncoderPref parse_encoder_pref(const std::string& s) {
  if (s == "nvenc") return EncoderPref::Nvenc;
  if (s == "vaapi") return EncoderPref::Vaapi;
  if (s == "software") return EncoderPref::Software;
  if (s == "auto") return EncoderPref::Auto;
  return EncoderPref::Auto;  // default for unknown
}

std::vector<EncoderBackend> select_encoder_order(EncoderPref pref) {
  switch (pref) {
    case EncoderPref::Auto:
      return {EncoderBackend::Nvenc, EncoderBackend::Vaapi, EncoderBackend::Software};
    case EncoderPref::Nvenc:
      return {EncoderBackend::Nvenc};
    case EncoderPref::Vaapi:
      return {EncoderBackend::Vaapi};
    case EncoderPref::Software:
      return {EncoderBackend::Software};
  }
  return {};  // unreachable
}

bool AutoEncoder::open(int w, int h, int fps, int br) {
  for (size_t i = 0; i < candidates_.size(); ++i) {
    auto e = candidates_[i]();
    if (e && e->open(w, h, fps, br)) {
      std::fprintf(stderr, "encoder: backend %zu opened\n", i);
      chosen_ = std::move(e);
      return true;
    }
  }
  return false;
}

std::vector<unsigned char> AutoEncoder::extradata() const {
  return chosen_ ? chosen_->extradata() : std::vector<unsigned char>{};
}

std::vector<EncodedPacket> AutoEncoder::encode(const Frame& f, int64_t pts_us) {
  return chosen_ ? chosen_->encode(f, pts_us) : std::vector<EncodedPacket>{};
}

std::vector<EncodedPacket> AutoEncoder::flush() {
  return chosen_ ? chosen_->flush() : std::vector<EncodedPacket>{};
}

std::unique_ptr<Encoder> make_encoder(EncoderPref pref) {
  std::vector<std::function<std::unique_ptr<Encoder>()>> c;
  for (auto b : select_encoder_order(pref)) {
    switch (b) {
      case EncoderBackend::Nvenc:
        c.push_back([] { return std::unique_ptr<Encoder>(new NvencEncoder()); });
        break;
      case EncoderBackend::Vaapi:
        c.push_back([] { return std::unique_ptr<Encoder>(new VaapiEncoder()); });
        break;
      case EncoderBackend::Software:
        c.push_back([] { return std::unique_ptr<Encoder>(new SoftwareEncoder()); });
        break;
    }
  }
  return std::make_unique<AutoEncoder>(std::move(c));
}

}  // namespace droppix
