#include "stream_daemon.h"
#include "stat_accumulator.h"
#include "stats_json.h"
#include "input_injector.h"
#include "monitor_geometry.h"
#include "orientation.h"
#include "audio_streamer.h"
#include <chrono>
#include <cstdio>
#include <future>
#include <string>
#include <thread>

namespace droppix {

bool StreamDaemon::run_until(const volatile std::sig_atomic_t& stop, int max_frames) {
  // We need the droppix output's NAME for orientation (incl. live auto-rotate) and
  // touch: snapshot the outputs BEFORE the source creates its monitor, so we can
  // identify it as the one that newly appears afterwards. Timeout-guarded and graceful
  // (finds nothing for the test-pattern source, which has no droppix output).
  std::vector<OutputInfo> before_outputs = desktop_->outputs();

  // AOA: the caller already adopted a ByteChannel (USB accessory), so skip the TCP accept.
  if (!cfg_.preconnected) {
    if (!tx_.accept_client(60000)) { std::fprintf(stderr, "no client\n"); return false; }
  }
  // Fires during the tablet's TLS pairing probe too (it connects, grabs the cert, then
  // prompts for the PIN) — the GUI uses this to show the pairing code right on time.
  std::fprintf(stderr, "client-connecting ip=%s\n", tx_.peer_ip().c_str());
  uint32_t cver, cw, ch, density; std::string cname, cid;
  if (!tx_.read_hello(cver, cw, ch, density, cname, cid, 10000)) { std::fprintf(stderr, "no HELLO\n"); return false; }
  std::fprintf(stderr, "client HELLO v%u %ux%u name=%s id=%s\n", cver, cw, ch, cname.c_str(), cid.c_str());

  // Approval gates real remote (Wi-Fi) peers only. An empty peer ip means USB/AOA
  // (adopt_channel over the cable) — physically trusted, like localhost — so it must
  // never engage the approval gate (which would block until a dialog it should not show).
  if (cfg_.approve && cfg_.gate && !tx_.peer_ip().empty() && tx_.peer_ip() != "127.0.0.1") {
    std::fprintf(stderr, "approve-request id=%s ip=%s name=%s\n",
                 cid.c_str(), tx_.peer_ip().c_str(), cname.c_str());
    bool allow = false;
    if (!cfg_.gate->wait(cid.empty() ? tx_.peer_ip() : cid, 60000, allow) || !allow) {
      std::fprintf(stderr, "connection from %s denied\n", tx_.peer_ip().c_str());
      return false;   // closes the socket; reconnect loop continues
    }
  }

  // Build the source at the tablet's native resolution (the factory substitutes defaults
  // when the client reports 0). The evdi monitor therefore matches the tablet exactly; on a
  // portrait<->landscape rotation the session restarts and the reconnect's HELLO gives the
  // swapped dims. start() writes the actual chosen dimensions into w/h.
  int w = static_cast<int>(cw), h = static_cast<int>(ch);
  // The app always sends landscape HELLO dims (1920x1080). If the tablet last reported a
  // portrait orientation, build the source portrait-SHAPED (swap) so cur_portrait below
  // matches the source and the orientation handler does not restart-loop forever — otherwise
  // every reconnect rebuilds landscape, the tablet re-reports portrait, and it never settles.
  int ocode = cfg_.live_orientation ? *cfg_.live_orientation : cfg_.orientation;
  if (orientation_is_portrait(ocode) && w > h) std::swap(w, h);
  src_ = make_source_(w, h);
  if (!src_ || !src_->start(w, h)) { std::fprintf(stderr, "source start failed\n"); return false; }
  std::fprintf(stderr, "source %dx%d\n", w, h);

  if (!enc_.open(w, h, cfg_.fps, cfg_.bitrate_kbps)) { std::fprintf(stderr, "encoder open failed\n"); return false; }
  if (!tx_.send_config(w, h, cfg_.fps, enc_.extradata())) return false;
  // Seed the app's overlay from the live host-side toggle if present (so a reconnect
  // keeps whatever the GUI last set), else the start-up flag. Tracked below so the
  // stream loop can push later host-side changes mid-session.
  int last_overlay = cfg_.live_overlay ? cfg_.live_overlay->load() : (cfg_.overlay ? 1 : 0);
  tx_.send_overlay(last_overlay ? 1 : 0);  // app shows its perf overlay only if asked

  // Once the evdi source is live, any desktop query/command that makes X re-probe
  // outputs blocks on THIS process answering the evdi connector probe. A plain blocking
  // call here would deadlock until its timeout ("xrandr query returned 0 bytes"), so run
  // backend calls on a helper thread while this thread keeps servicing evdi events.
  auto serviced = [&](auto fn) {
    auto fut = std::async(std::launch::async, fn);
    while (fut.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
      src_->next(50);   // pump evdi so X's probe gets its answer
    return fut.get();
  };
  auto query_outputs = [&]() {
    return serviced([this]{ return desktop_->outputs(); });
  };

  // Identify the droppix output (for orientation and/or touch): prefer the
  // newly-appeared one (unambiguous), else fall back to a size match. Right after the
  // evdi mode-set the desktop can be mid-reconfigure (X11 GPU hotplug) — retry briefly.
  auto after_outputs = query_outputs();
  OutputInfo droppix;
  bool found_output = select_new_output(before_outputs, after_outputs, droppix) ||
                      select_droppix(after_outputs, before_outputs, w, h, droppix);
  for (int tries = 0; !found_output && !before_outputs.empty() && tries < 10; ++tries) {
    src_->next(500);
    after_outputs = query_outputs();
    found_output = select_new_output(before_outputs, after_outputs, droppix) ||
                   select_droppix(after_outputs, before_outputs, w, h, droppix);
  }
  const bool have_output = found_output && safe_output_name(droppix.name);
  if (!found_output)
    std::fprintf(stderr, "warning: could not identify the droppix output\n");

  // X11 must explicitly adopt the new output (reverse-PRIME link + placement); Wayland
  // backends no-op. Adoption moves outputs, so refresh the identified geometry after.
  if (have_output) {
    std::string out_name = droppix.name;
    bool adopted = serviced([this, out_name]{ return desktop_->adopt_output(out_name); });
    if (adopted) {
      after_outputs = query_outputs();
      for (const auto& o : after_outputs)
        if (o.name == droppix.name) { droppix = o; break; }
    }
  }

  // Auto-orientation: the tablet reports its physical orientation. The stream is
  // landscape- or portrait-SHAPED (this session's w/h); when the tablet crosses the
  // portrait<->landscape boundary the dimensions must change, so we record the new
  // orientation and END the session — the caller (stream_main) rebuilds the source at
  // the new dims and the app reconnects. Same-class flips (0<->180, 90<->270) are
  // handled visually by the tablet's Android auto-rotate, so they don't restart.
  bool restart_for_orientation = false;
  const bool cur_portrait = h > w;
  tx_.set_orientation_handler([this, &restart_for_orientation, cur_portrait](uint8_t code) {
    if (cfg_.live_orientation) *cfg_.live_orientation = code;
    if (orientation_is_portrait(code) != cur_portrait) {
      std::fprintf(stderr, "orientation: tablet -> %s; restarting stream at new dims\n",
                   orientation_is_portrait(code) ? "portrait" : "landscape");
      restart_for_orientation = true;
    }
  });

  // Touch input (opt-in via --touch): inject the tablet's touches via a uinput
  // touchscreen (needs root) that KWin binds to the droppix output. OFF by default
  // so a geometry-query or uinput issue can never affect the display-only path.
  InputInjector injector;
  tx_.set_touch_handler(nullptr);  // drop any handler from a prior session (its injector is gone)
  if (cfg_.touch && !have_output) {
    // We can't pin the touchscreen to the droppix output on this compositor (e.g. GNOME,
    // before its DesktopBackend exists) — injecting anyway moves the WRONG monitor's cursor.
    // Skip touch entirely until the backend can bind it: better no touch than wrong-screen touch.
    std::fprintf(stderr, "input: touch disabled — could not identify/bind the droppix output on "
                 "this desktop; skipping touch injection (it would land on the wrong screen)\n");
  } else if (cfg_.touch) {   // have_output is true here: safe to inject and bind
    if (injector.open(cfg_.touch_name)) {
      tx_.set_touch_handler([&injector](const std::vector<TouchContact>& contacts) {
        injector.inject(contacts);
      });
      std::fprintf(stderr, "input: binding touch -> output %s (%dx%d)\n",
                   droppix.name.c_str(), droppix.geom.w, droppix.geom.h);
      auto backend = desktop_;                       // shared_ptr copy keeps it alive
      std::string out_name = droppix.name, tname = cfg_.touch_name;
      std::thread([backend, out_name, tname]{ backend->map_touch(out_name, tname); }).detach();
      // Desktop bounds for the two-finger-tap right-click pointer: prefer --desktop, else
      // the bounding box of all outputs.
      int deskW = cfg_.desktop_w, deskH = cfg_.desktop_h;
      if (deskW <= 0 || deskH <= 0) {
        deskW = 0; deskH = 0;
        for (const auto& o : after_outputs) {
          if (o.geom.x + o.geom.w > deskW) deskW = o.geom.x + o.geom.w;
          if (o.geom.y + o.geom.h > deskH) deskH = o.geom.y + o.geom.h;
        }
      }
      injector.set_geometry(droppix.geom.x, droppix.geom.y, droppix.geom.w, droppix.geom.h,
                            deskW, deskH);
    } else {
      std::fprintf(stderr, "input: uinput unavailable (need root); input disabled\n");
    }
  }

  // Audio capture (opt-in via --audio): capture the droppix-audio sink monitor in
  // the user session and stream it. Best-effort; never blocks the video path.
  AudioStreamer audio;
  if (cfg_.audio) {
    if (audio.start(user_session_prefix()))
      std::fprintf(stderr, "audio: capturing droppix-audio.monitor\n");
    else
      std::fprintf(stderr, "audio: capture unavailable (pw-record/droppix-audio missing)\n");
  }

  auto t0 = std::chrono::steady_clock::now();
  int sent = 0;
  StatAccumulator encode_ms, frame_kb;
  int frames_since_report = 0;
  auto last_report = std::chrono::steady_clock::now();
  // With touch on, poll the loop tightly so incoming touch is handled promptly
  // instead of being gated by the up-to-1s damage-driven frame wait.
  const int frame_timeout = (cfg_.touch || cfg_.audio) ? 8 : 1000;
  while (!stop && !restart_for_orientation && tx_.connected()) {
    if (cfg_.live_overlay) {  // host-side overlay toggle changed -> tell the app (loop thread = single writer)
      int ov = cfg_.live_overlay->load();
      if (ov != last_overlay) { tx_.send_overlay(ov ? 1 : 0); last_overlay = ov; }
    }
    if (cfg_.audio) {
      std::vector<std::vector<unsigned char>> chunks;
      if (audio.drain(chunks))
        for (auto& c : chunks) tx_.send_audio(c);
    }
    Frame f = src_->next(frame_timeout);
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
  // On an orientation-driven restart, drop the client so it reconnects and the caller
  // rebuilds the source at the new dimensions.
  if (restart_for_orientation) tx_.close_all();
  return true;
}

}  // namespace droppix
