#include "stream_daemon.h"
#include "stat_accumulator.h"
#include "stats_json.h"
#include "input_injector.h"
#include "monitor_geometry.h"
#include <chrono>
#include <cstdio>
#include <string>

namespace droppix {

static std::string run_kscreen() {
  std::string out;
  // timeout-guarded so a hung kscreen-doctor (e.g. run as root vs a user session)
  // can never block the stream loop.
  FILE* p = popen("timeout 2 kscreen-doctor -o 2>/dev/null", "r");
  if (!p) return out;
  char buf[4096]; size_t n;
  while ((n = fread(buf, 1, sizeof(buf), p)) > 0) out.append(buf, n);
  pclose(p);
  return out;
}

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

  // Touch input (opt-in via --touch): map the tablet's touches onto the droppix
  // monitor and inject via uinput (needs root). OFF by default so a geometry-query
  // or uinput issue can never affect the default display-only path. Geometry is
  // taken from --monitor/--desktop when given (the GUI queries it as the user);
  // otherwise a best-effort, timeout-guarded kscreen query is attempted.
  InputInjector injector;
  tx_.set_input_handler(nullptr);  // drop any handler from a prior session (its injector is gone)
  if (cfg_.touch) {
    Rect mon = cfg_.monitor;
    int dw = cfg_.desktop_w, dh = cfg_.desktop_h;
    if (mon.w <= 0 || mon.h <= 0 || dw <= 0 || dh <= 0) {
      auto outs = parse_kscreen_outputs(run_kscreen());
      if (select_droppix(outs, w, h, mon)) {
        Rect db = desktop_bounds(outs); dw = db.w; dh = db.h;
      }
    }
    if (mon.w > 0 && mon.h > 0 && dw > 0 && dh > 0 && injector.open(mon, dw, dh)) {
      tx_.set_input_handler([&injector](uint8_t a, uint16_t x, uint16_t y) {
        injector.inject(a, x, y);
      });
      std::fprintf(stderr, "input: injecting into %dx%d at (%d,%d), desktop %dx%d\n",
                   mon.w, mon.h, mon.x, mon.y, dw, dh);
    } else {
      std::fprintf(stderr, "input: geometry/uinput unavailable; input disabled\n");
    }
  }

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
