#include "stream_daemon.h"
#include "stat_accumulator.h"
#include "stats_json.h"
#include <chrono>
#include <cstdio>

namespace droppix {

bool StreamDaemon::run_until(const volatile std::sig_atomic_t& stop, int max_frames) {
  int w = 0, h = 0;
  if (!src_.start(w, h)) { std::fprintf(stderr, "source start failed\n"); return false; }
  std::fprintf(stderr, "source %dx%d\n", w, h);

  if (!tx_.accept_client(60000)) { std::fprintf(stderr, "no client\n"); return false; }
  uint32_t cver, cw, ch, density;
  if (!tx_.read_hello(cver, cw, ch, density, 10000)) { std::fprintf(stderr, "no HELLO\n"); return false; }
  std::fprintf(stderr, "client HELLO v%u %ux%u\n", cver, cw, ch);

  if (!enc_.open(w, h, cfg_.fps, cfg_.bitrate_kbps)) { std::fprintf(stderr, "encoder open failed\n"); return false; }
  if (!tx_.send_config(w, h, cfg_.fps, enc_.extradata())) return false;

  auto t0 = std::chrono::steady_clock::now();
  int sent = 0;
  StatAccumulator encode_ms, frame_kb;
  int frames_since_report = 0;
  auto last_report = std::chrono::steady_clock::now();
  while (!stop && tx_.connected()) {
    Frame f = src_.next(1000);
    if (!f.valid) { tx_.poll_control(); continue; }
    int64_t pts_us = std::chrono::duration_cast<std::chrono::microseconds>(
                         std::chrono::steady_clock::now() - t0).count();
    auto enc_t0 = std::chrono::steady_clock::now();
    auto packets = enc_.encode(f, pts_us);
    double enc_ms = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - enc_t0).count();
    encode_ms.add(enc_ms);
    for (auto& pkt : packets) {
      frame_kb.add(pkt.data.size() / 1024.0);
      if (!tx_.send_video(pkt.pts_us, pkt.keyframe, pkt.data)) break;
      ++sent;
    }
    ++frames_since_report;

    auto now = std::chrono::steady_clock::now();
    double elapsed_s = std::chrono::duration<double>(now - last_report).count();
    if (elapsed_s >= 1.0) {
      if (cfg_.stats_json) {
        std::fprintf(stderr, "%s\n", format_stats_json(
            encode_ms.avg(), encode_ms.peak(), frames_since_report / elapsed_s,
            frame_kb.avg(), frame_kb.peak(), tx_.connected()).c_str());
      } else {
        std::fprintf(stderr,
            "stats: encode avg %.1f ms peak %.1f ms | fps %.1f | frame avg %.1f KB peak %.1f KB\n",
            encode_ms.avg(), encode_ms.peak(), frames_since_report / elapsed_s,
            frame_kb.avg(), frame_kb.peak());
      }
      encode_ms.reset(); frame_kb.reset(); frames_since_report = 0; last_report = now;
    }

    tx_.poll_control();
    if (max_frames > 0 && sent >= max_frames) break;
  }
  for (auto& pkt : enc_.flush()) tx_.send_video(pkt.pts_us, pkt.keyframe, pkt.data);
  std::fprintf(stderr, "sent %d video packets\n", sent);
  return true;
}

}  // namespace droppix
