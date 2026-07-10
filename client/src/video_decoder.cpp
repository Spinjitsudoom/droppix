#include "video_decoder.h"
#include <QVideoFrameFormat>
#include <cstdio>
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace droppix {

VideoDecoder::VideoDecoder() {
  frame_ = av_frame_alloc();
  packet_ = av_packet_alloc();
}

VideoDecoder::~VideoDecoder() {
  close();
  av_frame_free(&frame_);
  av_packet_free(&packet_);
}

bool VideoDecoder::open(int width, int height) {
  close();
  const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
  if (!codec) { std::fprintf(stderr, "video: no H.264 decoder available\n"); return false; }
  ctx_ = avcodec_alloc_context3(codec);
  ctx_->width = width;
  ctx_->height = height;
  // No csd-0/csd-1 (extradata) set: SPS/PPS travel in-band ahead of every IDR (host's
  // x264 repeat-headers=1) — FFmpeg's H.264 parser handles that natively, same as how
  // the Android MediaCodec decoder is deliberately configured with only width/height.
  if (avcodec_open2(ctx_, codec, nullptr) < 0) {
    std::fprintf(stderr, "video: avcodec_open2 failed\n");
    avcodec_free_context(&ctx_);
    return false;
  }
  return true;
}

void VideoDecoder::close() {
  if (ctx_) avcodec_free_context(&ctx_);
}

std::vector<QVideoFrame> VideoDecoder::submit(const std::vector<unsigned char>& nal,
                                              uint64_t pts_us) {
  std::vector<QVideoFrame> out;
  if (!ctx_) return out;

  av_new_packet(packet_, static_cast<int>(nal.size()));
  std::memcpy(packet_->data, nal.data(), nal.size());
  packet_->pts = static_cast<int64_t>(pts_us);

  if (avcodec_send_packet(ctx_, packet_) < 0) { av_packet_unref(packet_); return out; }
  av_packet_unref(packet_);

  while (avcodec_receive_frame(ctx_, frame_) == 0) {
    if (frame_->format != AV_PIX_FMT_YUV420P) { av_frame_unref(frame_); continue; }
    QVideoFrameFormat fmt(QSize(frame_->width, frame_->height),
                          QVideoFrameFormat::Format_YUV420P);
    QVideoFrame vf(fmt);
    if (vf.map(QVideoFrame::WriteOnly)) {
      for (int plane = 0; plane < 3; ++plane) {
        const int srcStride = frame_->linesize[plane];
        const int dstStride = vf.bytesPerLine(plane);
        const int planeH = (plane == 0) ? frame_->height : (frame_->height + 1) / 2;
        uchar* dst = vf.bits(plane);
        const uint8_t* src = frame_->data[plane];
        const int copyBytes = std::min(srcStride, dstStride);
        for (int row = 0; row < planeH; ++row) {
          std::memcpy(dst + row * dstStride, src + row * srcStride, copyBytes);
        }
      }
      vf.unmap();
      vf.setStartTime(static_cast<qint64>(frame_->pts));
      out.push_back(vf);
    }
    av_frame_unref(frame_);
  }
  return out;
}

}  // namespace droppix
