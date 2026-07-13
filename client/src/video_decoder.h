#pragma once
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <atomic>
#include <cstdint>
#include <vector>

extern "C" {
struct AVCodecContext;
struct AVCodec;
struct AVFrame;
struct AVPacket;
}

namespace droppix {

// Pure helper (no FFmpeg/decoder state involved) so the mirrored-format behavior is unit
// testable without needing a real H.264 stream: builds the YUV420P QVideoFrameFormat that
// submit() attaches to every produced QVideoFrame, applying the horizontal-flip setting.
QVideoFrameFormat make_frame_format(int w, int h, bool mirrored);

// Pure helper to adjust luma (Y channel) brightness and contrast.
int adjust_luma(int y, int brightness, int contrast);

// FFmpeg-based H.264 decoder: feeds Annex-B access units (one VIDEO message body = one
// access unit, SPS/PPS in-band ahead of every IDR — same assumption as the Android
// MediaCodec decoder, see VideoDecoder.kt) and emits QVideoFrame (YUV420P, no extra RGB
// conversion pass) for a QVideoSink to render.
class VideoDecoder {
 public:
  VideoDecoder();
  ~VideoDecoder();
  VideoDecoder(const VideoDecoder&) = delete;
  VideoDecoder& operator=(const VideoDecoder&) = delete;

  // (Re)configures for a new resolution; safe to call again on a CONFIG change.
  bool open(int width, int height);

  // Submits one access unit. Returns every frame the decoder had ready to emit as a
  // result (usually 0 or 1, sometimes more after a resolution change flushes B-frames).
  std::vector<QVideoFrame> submit(const std::vector<unsigned char>& nal, uint64_t pts_us);

  void close();

  // Enables/disables mirroring the produced frame's format horizontally (see
  // make_frame_format / ClientSettings::flip_horizontal). Takes effect on the next
  // submit() call.
  void setFlipHorizontal(bool f) { flip_ = f; }

  // Adjusts luma (Y plane) brightness/contrast applied during the next submit() calls.
  // Neutral values (brightness 0, contrast 100) keep the plain memcpy fast path.
  void setBrightness(int b) { brightness_.store(b, std::memory_order_relaxed); }
  void setContrast(int c) { contrast_.store(c, std::memory_order_relaxed); }

 private:
  AVCodecContext* ctx_ = nullptr;
  AVFrame* frame_ = nullptr;
  AVPacket* packet_ = nullptr;
  bool flip_ = false;
  // std::atomic<int> (relaxed ordering): written from the GUI thread (setBrightness/
  // setContrast), read on the decode thread inside submit(). Relaxed is sufficient — no
  // other state is published alongside these values, so no ordering beyond atomicity is
  // needed.
  std::atomic<int> brightness_{0};
  std::atomic<int> contrast_{100};
  // Decode-thread-only LUT cache: touched exclusively inside submit(), never from the GUI
  // thread, so plain (non-atomic) fields are correct here. luma_lut_[i] == adjust_luma(i,
  // b, c) for the currently-cached (lut_brightness_, lut_contrast_); lut_valid_ forces a
  // rebuild the first time a non-neutral frame is seen.
  uint8_t luma_lut_[256];
  int lut_brightness_ = 0;
  int lut_contrast_ = 100;
  bool lut_valid_ = false;
};

}  // namespace droppix
