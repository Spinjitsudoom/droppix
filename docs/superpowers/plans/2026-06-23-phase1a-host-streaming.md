# Phase 1a — Host Streaming Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the Phase 0 capture foundation into a live H.264 stream: capture (or synthesize) frames → software x264 encode → send over a framed TCP protocol, viewable on the host with `ffplay` over the adb tunnel — proving the encode + transport stack before the Android app (Plan 1b) is built.

**Architecture:** A `FrameSource` (test-pattern, or evdi-backed) feeds a `Frame` into an `Encoder` interface implemented by `SoftwareEncoder` (libavcodec `libx264` + libswscale BGRA→NV12). Encoded access units are sent by `TransportServer` over a length-prefixed message protocol. `stream_main` wires it together. The encoder lives behind an interface so a VAAPI hardware encoder is a later drop-in. Almost the entire stack is testable without evdi/sudo by using the test-pattern source; only the final task needs live hardware.

**Tech Stack:** C++17, CMake, libavcodec/libavutil/libswscale (ffmpeg 8.1, software `libx264` encoder), POSIX sockets, the Phase 0 `Frame`/`VirtualDisplay`/`Capturer`/`build_edid`, Python 3 (test client), host `ffplay`/`ffprobe` for verification.

## Global Constraints

- **Build/run environment:** Fedora distrobox `droppix-dev`. Repo is on a CIFS mount with NO exec bit — build OFF the mount. Use verbatim:
  `distrobox enter droppix-dev -- bash -lc 'SRC="/var/mnt/nas/Projects/Spacedesk for linux/host"; BUILD="/home/Spinjitsudoomyt/droppix-build"; cmake -S "$SRC" -B "$BUILD" -DCMAKE_BUILD_TYPE=Debug && cmake --build "$BUILD" -j && ctest --test-dir "$BUILD" --output-on-failure'`
- **ffmpeg version is 8.1** — use the MODERN libavcodec API only: `avcodec_send_frame`/`avcodec_receive_packet`, `avcodec_alloc_context3`, `av_frame_get_buffer`. No deprecated `avcodec_encode_video2`.
- **Encoder output format:** H.264 **Annex-B** (start-code delimited), pixel input **NV12**, software `libx264` with `preset=ultrafast`, `tune=zerolatency`, `max_b_frames=0`, and `x264-params=repeat-headers=1` so every IDR is self-contained.
- **Encoder is an interface** (`droppix::Encoder`); `SoftwareEncoder` is the only implementation in Phase 1a. Do not bake x264 specifics into callers — VAAPI is a future drop-in.
- **Wire protocol:** each message is `[u32 big-endian length][payload]`, where `length` covers the payload, `payload[0]` is the 1-byte `MsgType`, and `payload[1..]` is the body. Types: `Hello=1, Config=2, Video=3, Ping=4, Pong=5, Bye=6`. All multi-byte integer fields are big-endian.
- **C++17**, namespace `droppix`, all host code under `host/src`, tests under `host/tests`. New sources join the existing `droppix_core` library and `droppix_tests` target.
- **Resolution:** fixed **1920×1080** for the evdi source (reuse `timing_1080p60()`); dynamic resolution is deferred to a later phase. The test-pattern source may use any size.
- **Default TCP port:** 27000.
- **Reuse Phase 0 code:** `Frame` (host/src/capturer.h), `VirtualDisplay`, `Capturer`, `build_edid`, `timing_1080p60` — do not duplicate them.

---

## File Structure

```
host/src/
  protocol.h  protocol.cpp          # message framing + payload codecs (pure, tested)
  encoded_packet.h                  # struct EncodedPacket
  encoder.h                         # abstract Encoder interface
  software_encoder.h .cpp           # libavcodec libx264 + libswscale BGRA->NV12
  frame_source.h                    # abstract FrameSource interface
  test_pattern_source.h .cpp        # synthetic moving frames (no hardware)
  evdi_frame_source.h .cpp          # VirtualDisplay+Capturer as a FrameSource
  transport_server.h .cpp           # TCP listen/accept/handshake/send + control
  stream_daemon.h .cpp              # source -> encoder -> transport loop
  stream_main.cpp                   # CLI flags, adb reverse, lifecycle
host/tests/
  test_protocol.cpp
  test_software_encoder.cpp
  test_test_pattern_source.cpp
  test_transport_server.cpp
scripts/
  test-client.py                    # connect, handshake, dump Annex-B to stdout for ffplay/ffprobe
```

---

### Task 1: Wire protocol — framing + payload codecs (pure logic, TDD)

**Files:**
- Create: `host/src/protocol.h`, `host/src/protocol.cpp`
- Create: `host/tests/test_protocol.cpp`
- Modify: `host/CMakeLists.txt` (add `src/protocol.cpp` to `droppix_core`; add `tests/test_protocol.cpp` to `droppix_tests`)

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `enum class droppix::MsgType : uint8_t { Hello=1, Config=2, Video=3, Ping=4, Pong=5, Bye=6 };`
  - `std::vector<unsigned char> droppix::encode_message(MsgType type, const std::vector<unsigned char>& body);`
  - `struct droppix::ParsedMessage { MsgType type; std::vector<unsigned char> body; };`
  - `class droppix::MessageParser { void feed(const unsigned char* data, size_t n); bool next(ParsedMessage& out); };`
  - `encode_hello/decode_hello(width,height,density)`, `encode_config/decode_config(width,height,fps,extradata)`, `encode_video/decode_video(pts_us,keyframe,nal)` with the signatures shown in Step 3.

- [ ] **Step 1: Write the failing tests**

Create `host/tests/test_protocol.cpp`:

```cpp
#include <gtest/gtest.h>
#include "protocol.h"

using namespace droppix;

TEST(Protocol, EncodeMessageHasBigEndianLengthAndType) {
  auto m = encode_message(MsgType::Video, {0xAA, 0xBB});
  // length = 1 (type) + 2 (body) = 3
  ASSERT_EQ(m.size(), 4u + 3u);
  EXPECT_EQ(m[0], 0x00); EXPECT_EQ(m[1], 0x00);
  EXPECT_EQ(m[2], 0x00); EXPECT_EQ(m[3], 0x03);
  EXPECT_EQ(m[4], static_cast<unsigned char>(MsgType::Video));
  EXPECT_EQ(m[5], 0xAA); EXPECT_EQ(m[6], 0xBB);
}

TEST(Protocol, ParserReassemblesAcrossPartialFeeds) {
  auto m = encode_message(MsgType::Ping, {1, 2, 3});
  MessageParser p;
  // feed in two halves
  p.feed(m.data(), 3);
  ParsedMessage out;
  EXPECT_FALSE(p.next(out));         // incomplete
  p.feed(m.data() + 3, m.size() - 3);
  ASSERT_TRUE(p.next(out));
  EXPECT_EQ(out.type, MsgType::Ping);
  EXPECT_EQ(out.body, (std::vector<unsigned char>{1, 2, 3}));
  EXPECT_FALSE(p.next(out));         // nothing left
}

TEST(Protocol, ParserHandlesTwoBackToBackMessages) {
  auto a = encode_message(MsgType::Hello, {9});
  auto b = encode_message(MsgType::Bye, {});
  MessageParser p;
  p.feed(a.data(), a.size());
  p.feed(b.data(), b.size());
  ParsedMessage out;
  ASSERT_TRUE(p.next(out)); EXPECT_EQ(out.type, MsgType::Hello);
  ASSERT_TRUE(p.next(out)); EXPECT_EQ(out.type, MsgType::Bye);
  EXPECT_FALSE(p.next(out));
}

TEST(Protocol, HelloRoundTrip) {
  auto body = encode_hello(1920, 1080, 320);
  uint32_t w, h, d;
  ASSERT_TRUE(decode_hello(body, w, h, d));
  EXPECT_EQ(w, 1920u); EXPECT_EQ(h, 1080u); EXPECT_EQ(d, 320u);
}

TEST(Protocol, ConfigRoundTrip) {
  std::vector<unsigned char> extradata{0x67, 0x42, 0x00};
  auto body = encode_config(1920, 1080, 30, extradata);
  uint32_t w, h, fps; std::vector<unsigned char> ed;
  ASSERT_TRUE(decode_config(body, w, h, fps, ed));
  EXPECT_EQ(w, 1920u); EXPECT_EQ(h, 1080u); EXPECT_EQ(fps, 30u);
  EXPECT_EQ(ed, extradata);
}

TEST(Protocol, VideoRoundTrip) {
  std::vector<unsigned char> nal{0x00, 0x00, 0x00, 0x01, 0x65, 0x11};
  auto body = encode_video(123456, true, nal);
  uint64_t pts; bool key; std::vector<unsigned char> out;
  ASSERT_TRUE(decode_video(body, pts, key, out));
  EXPECT_EQ(pts, 123456u); EXPECT_TRUE(key); EXPECT_EQ(out, nal);
}
```

- [ ] **Step 2: Add to CMake and run to verify failure**

In `host/CMakeLists.txt`, add `src/protocol.cpp` to the `droppix_core` source list and `tests/test_protocol.cpp` to the `droppix_tests` source list. Then build:

```
distrobox enter droppix-dev -- bash -lc 'cmake -S "/var/mnt/nas/Projects/Spacedesk for linux/host" -B /home/Spinjitsudoomyt/droppix-build && cmake --build /home/Spinjitsudoomyt/droppix-build -j'
```
Expected: compile failure — `protocol.h` not found.

- [ ] **Step 3: Write the header**

Create `host/src/protocol.h`:

```cpp
#pragma once
#include <cstdint>
#include <vector>

namespace droppix {

enum class MsgType : uint8_t {
  Hello = 1, Config = 2, Video = 3, Ping = 4, Pong = 5, Bye = 6
};

// Wire frame: [u32 big-endian length][payload]; length covers payload;
// payload[0] = type byte, payload[1..] = body.
std::vector<unsigned char> encode_message(MsgType type,
                                          const std::vector<unsigned char>& body);

struct ParsedMessage {
  MsgType type;
  std::vector<unsigned char> body;
};

// Incremental parser: feed arbitrary byte chunks, pull complete messages.
class MessageParser {
 public:
  void feed(const unsigned char* data, size_t n);
  bool next(ParsedMessage& out);  // true if a complete message was dequeued
 private:
  std::vector<unsigned char> buf_;
  size_t pos_ = 0;  // consumed prefix
};

// Payload codecs (all integers big-endian).
std::vector<unsigned char> encode_hello(uint32_t width, uint32_t height,
                                        uint32_t density);
bool decode_hello(const std::vector<unsigned char>& body,
                  uint32_t& width, uint32_t& height, uint32_t& density);

std::vector<unsigned char> encode_config(uint32_t width, uint32_t height,
                                         uint32_t fps,
                                         const std::vector<unsigned char>& extradata);
bool decode_config(const std::vector<unsigned char>& body,
                   uint32_t& width, uint32_t& height, uint32_t& fps,
                   std::vector<unsigned char>& extradata);

std::vector<unsigned char> encode_video(uint64_t pts_us, bool keyframe,
                                        const std::vector<unsigned char>& nal);
bool decode_video(const std::vector<unsigned char>& body,
                  uint64_t& pts_us, bool& keyframe,
                  std::vector<unsigned char>& nal);

}  // namespace droppix
```

- [ ] **Step 4: Write the implementation**

Create `host/src/protocol.cpp`:

```cpp
#include "protocol.h"

namespace droppix {
namespace {

void put_u32(std::vector<unsigned char>& v, uint32_t x) {
  v.push_back((x >> 24) & 0xFF); v.push_back((x >> 16) & 0xFF);
  v.push_back((x >> 8) & 0xFF);  v.push_back(x & 0xFF);
}
void put_u64(std::vector<unsigned char>& v, uint64_t x) {
  for (int s = 56; s >= 0; s -= 8) v.push_back((x >> s) & 0xFF);
}
uint32_t get_u32(const unsigned char* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
         (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
uint64_t get_u64(const unsigned char* p) {
  uint64_t x = 0;
  for (int i = 0; i < 8; ++i) x = (x << 8) | p[i];
  return x;
}

}  // namespace

std::vector<unsigned char> encode_message(MsgType type,
                                          const std::vector<unsigned char>& body) {
  std::vector<unsigned char> m;
  uint32_t len = 1 + static_cast<uint32_t>(body.size());
  put_u32(m, len);
  m.push_back(static_cast<unsigned char>(type));
  m.insert(m.end(), body.begin(), body.end());
  return m;
}

void MessageParser::feed(const unsigned char* data, size_t n) {
  buf_.insert(buf_.end(), data, data + n);
}

bool MessageParser::next(ParsedMessage& out) {
  if (buf_.size() - pos_ < 4) { return false; }
  uint32_t len = get_u32(buf_.data() + pos_);
  if (buf_.size() - pos_ < 4 + len) { return false; }
  if (len < 1) {  // malformed: drop the length word and continue
    pos_ += 4;
    return next(out);
  }
  const unsigned char* p = buf_.data() + pos_ + 4;
  out.type = static_cast<MsgType>(p[0]);
  out.body.assign(p + 1, p + len);
  pos_ += 4 + len;
  // Compact occasionally so buf_ doesn't grow without bound.
  if (pos_ > 65536) { buf_.erase(buf_.begin(), buf_.begin() + pos_); pos_ = 0; }
  return true;
}

std::vector<unsigned char> encode_hello(uint32_t w, uint32_t h, uint32_t d) {
  std::vector<unsigned char> b; put_u32(b, w); put_u32(b, h); put_u32(b, d);
  return b;
}
bool decode_hello(const std::vector<unsigned char>& b,
                  uint32_t& w, uint32_t& h, uint32_t& d) {
  if (b.size() != 12) return false;
  w = get_u32(b.data()); h = get_u32(b.data() + 4); d = get_u32(b.data() + 8);
  return true;
}

std::vector<unsigned char> encode_config(uint32_t w, uint32_t h, uint32_t fps,
                                         const std::vector<unsigned char>& ed) {
  std::vector<unsigned char> b;
  put_u32(b, w); put_u32(b, h); put_u32(b, fps);
  put_u32(b, static_cast<uint32_t>(ed.size()));
  b.insert(b.end(), ed.begin(), ed.end());
  return b;
}
bool decode_config(const std::vector<unsigned char>& b,
                   uint32_t& w, uint32_t& h, uint32_t& fps,
                   std::vector<unsigned char>& ed) {
  if (b.size() < 16) return false;
  w = get_u32(b.data()); h = get_u32(b.data() + 4); fps = get_u32(b.data() + 8);
  uint32_t n = get_u32(b.data() + 12);
  if (b.size() != 16 + n) return false;
  ed.assign(b.begin() + 16, b.end());
  return true;
}

std::vector<unsigned char> encode_video(uint64_t pts_us, bool key,
                                        const std::vector<unsigned char>& nal) {
  std::vector<unsigned char> b;
  put_u64(b, pts_us);
  b.push_back(key ? 1 : 0);
  b.insert(b.end(), nal.begin(), nal.end());
  return b;
}
bool decode_video(const std::vector<unsigned char>& b,
                  uint64_t& pts_us, bool& key, std::vector<unsigned char>& nal) {
  if (b.size() < 9) return false;
  pts_us = get_u64(b.data());
  key = b[8] != 0;
  nal.assign(b.begin() + 9, b.end());
  return true;
}

}  // namespace droppix
```

- [ ] **Step 5: Build and run tests to verify they pass**

```
distrobox enter droppix-dev -- bash -lc 'cmake --build /home/Spinjitsudoomyt/droppix-build -j && ctest --test-dir /home/Spinjitsudoomyt/droppix-build --output-on-failure'
```
Expected: all `Protocol.*` tests pass, prior tests still pass.

- [ ] **Step 6: Commit**

```bash
git add host/src/protocol.h host/src/protocol.cpp host/tests/test_protocol.cpp host/CMakeLists.txt
git commit -m "feat(protocol): wire framing + hello/config/video payload codecs"
```

---

### Task 2: Encoder interface + SoftwareEncoder (libx264) — TDD

**Files:**
- Create: `host/src/encoded_packet.h`, `host/src/encoder.h`, `host/src/software_encoder.h`, `host/src/software_encoder.cpp`
- Create: `host/tests/test_software_encoder.cpp`
- Modify: `scripts/dev-container.sh` (add `ffmpeg-devel x264-devel` to the install line)
- Modify: `host/CMakeLists.txt` (pkg-config libav*, add sources/tests)

**Interfaces:**
- Consumes: `droppix::Frame` (host/src/capturer.h — fields `width,height,stride,bgra,rects,valid`).
- Produces:
  - `struct droppix::EncodedPacket { std::vector<unsigned char> data; int64_t pts_us; bool keyframe; };`
  - `class droppix::Encoder { virtual bool open(int w,int h,int fps,int bitrate_kbps)=0; virtual std::vector<unsigned char> extradata() const=0; virtual std::vector<EncodedPacket> encode(const Frame&, int64_t pts_us)=0; virtual std::vector<EncodedPacket> flush()=0; virtual ~Encoder()=default; };`
  - `class droppix::SoftwareEncoder : public Encoder` implementing the above with libx264.

- [ ] **Step 1: Add ffmpeg deps to the container and dev-container.sh**

Edit `scripts/dev-container.sh`: append `ffmpeg-devel x264-devel` to the package list on the `dnf install` line(s). Then install into the existing container now:

```
distrobox enter droppix-dev -- bash -lc 'sudo dnf install -y ffmpeg-devel x264-devel && pkg-config --modversion libavcodec libswscale libavutil'
```
Expected: prints versions (libavcodec ~61/62, libswscale, libavutil). These come from the already-enabled fedora-multimedia repo.

- [ ] **Step 2: Wire CMake for ffmpeg**

In `host/CMakeLists.txt`, after the `find_library(EVDI_LIB ...)` block, add:

```cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavcodec libavutil libswscale)
```
Add `src/software_encoder.cpp` to `droppix_core`, change its link line to:
```cmake
target_link_libraries(droppix_core PUBLIC ${EVDI_LIB} PkgConfig::FFMPEG)
```
Add `tests/test_software_encoder.cpp` to `droppix_tests`.

- [ ] **Step 3: Write the failing test**

Create `host/tests/test_software_encoder.cpp`:

```cpp
#include <gtest/gtest.h>
#include "software_encoder.h"
#include "capturer.h"

using namespace droppix;

// Build a solid-color BGRA frame.
static Frame make_frame(int w, int h, unsigned char b, unsigned char g, unsigned char r) {
  Frame f;
  f.width = w; f.height = h; f.stride = w * 4; f.valid = true;
  f.bgra.resize(static_cast<size_t>(w) * h * 4);
  for (size_t i = 0; i + 3 < f.bgra.size(); i += 4) {
    f.bgra[i] = b; f.bgra[i+1] = g; f.bgra[i+2] = r; f.bgra[i+3] = 0xFF;
  }
  return f;
}

static bool starts_with_annexb(const std::vector<unsigned char>& d) {
  return d.size() >= 4 &&
         ((d[0]==0 && d[1]==0 && d[2]==0 && d[3]==1) ||
          (d[0]==0 && d[1]==0 && d[2]==1));
}

TEST(SoftwareEncoder, OpensAndEmitsKeyframeAnnexB) {
  SoftwareEncoder enc;
  ASSERT_TRUE(enc.open(320, 240, 30, 1000));
  std::vector<EncodedPacket> packets;
  // Feed several frames; zerolatency should emit promptly.
  for (int i = 0; i < 5; ++i) {
    auto f = make_frame(320, 240, (i*30)&0xFF, 0x40, 0x80);
    auto out = enc.encode(f, i * 33333);
    packets.insert(packets.end(), out.begin(), out.end());
  }
  auto tail = enc.flush();
  packets.insert(packets.end(), tail.begin(), tail.end());

  ASSERT_FALSE(packets.empty());
  EXPECT_TRUE(packets[0].keyframe);             // first output is an IDR
  EXPECT_TRUE(starts_with_annexb(packets[0].data));
}
```

- [ ] **Step 4: Build to verify it fails**

```
distrobox enter droppix-dev -- bash -lc 'cmake -S "/var/mnt/nas/Projects/Spacedesk for linux/host" -B /home/Spinjitsudoomyt/droppix-build && cmake --build /home/Spinjitsudoomyt/droppix-build -j'
```
Expected: compile failure — `software_encoder.h` not found.

- [ ] **Step 5: Write encoded_packet.h, encoder.h**

Create `host/src/encoded_packet.h`:

```cpp
#pragma once
#include <cstdint>
#include <vector>

namespace droppix {
struct EncodedPacket {
  std::vector<unsigned char> data;  // Annex-B H.264 access unit
  int64_t pts_us = 0;
  bool keyframe = false;
};
}  // namespace droppix
```

Create `host/src/encoder.h`:

```cpp
#pragma once
#include <vector>
#include "encoded_packet.h"
#include "capturer.h"  // droppix::Frame

namespace droppix {
class Encoder {
 public:
  virtual ~Encoder() = default;
  virtual bool open(int width, int height, int fps, int bitrate_kbps) = 0;
  // SPS/PPS for the CONFIG message; may be empty if headers are in-band.
  virtual std::vector<unsigned char> extradata() const = 0;
  virtual std::vector<EncodedPacket> encode(const Frame& frame, int64_t pts_us) = 0;
  virtual std::vector<EncodedPacket> flush() = 0;
};
}  // namespace droppix
```

- [ ] **Step 6: Write software_encoder.h**

Create `host/src/software_encoder.h`:

```cpp
#pragma once
#include "encoder.h"

struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace droppix {
class SoftwareEncoder : public Encoder {
 public:
  ~SoftwareEncoder() override;
  bool open(int width, int height, int fps, int bitrate_kbps) override;
  std::vector<unsigned char> extradata() const override;
  std::vector<EncodedPacket> encode(const Frame& frame, int64_t pts_us) override;
  std::vector<EncodedPacket> flush() override;

 private:
  std::vector<EncodedPacket> drain();  // pull packets from the codec

  AVCodecContext* ctx_ = nullptr;
  AVFrame* nv12_ = nullptr;
  AVPacket* pkt_ = nullptr;
  SwsContext* sws_ = nullptr;
  int width_ = 0, height_ = 0, fps_ = 30;
  int64_t frame_index_ = 0;
};
}  // namespace droppix
```

- [ ] **Step 7: Write software_encoder.cpp**

Create `host/src/software_encoder.cpp`:

```cpp
#include "software_encoder.h"
#include <cstdio>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace droppix {

bool SoftwareEncoder::open(int width, int height, int fps, int bitrate_kbps) {
  width_ = width; height_ = height; fps_ = fps;
  const AVCodec* codec = avcodec_find_encoder_by_name("libx264");
  if (!codec) { std::fprintf(stderr, "libx264 encoder not found\n"); return false; }

  ctx_ = avcodec_alloc_context3(codec);
  if (!ctx_) return false;
  ctx_->width = width;
  ctx_->height = height;
  ctx_->pix_fmt = AV_PIX_FMT_NV12;
  ctx_->time_base = AVRational{1, fps};
  ctx_->framerate = AVRational{fps, 1};
  ctx_->gop_size = fps * 2;        // keyframe every ~2s
  ctx_->max_b_frames = 0;          // no B-frames: lowest latency
  ctx_->bit_rate = int64_t(bitrate_kbps) * 1000;
  av_opt_set(ctx_->priv_data, "preset", "ultrafast", 0);
  av_opt_set(ctx_->priv_data, "tune", "zerolatency", 0);
  // Make every IDR self-contained (SPS/PPS repeated in-band).
  av_opt_set(ctx_->priv_data, "x264-params", "repeat-headers=1", 0);

  if (avcodec_open2(ctx_, codec, nullptr) < 0) {
    std::fprintf(stderr, "avcodec_open2 failed\n"); return false;
  }

  nv12_ = av_frame_alloc();
  nv12_->format = AV_PIX_FMT_NV12;
  nv12_->width = width;
  nv12_->height = height;
  if (av_frame_get_buffer(nv12_, 32) < 0) return false;

  pkt_ = av_packet_alloc();

  sws_ = sws_getContext(width, height, AV_PIX_FMT_BGRA,
                        width, height, AV_PIX_FMT_NV12,
                        SWS_BILINEAR, nullptr, nullptr, nullptr);
  return sws_ != nullptr;
}

std::vector<unsigned char> SoftwareEncoder::extradata() const {
  if (!ctx_ || !ctx_->extradata || ctx_->extradata_size <= 0) return {};
  return std::vector<unsigned char>(ctx_->extradata,
                                    ctx_->extradata + ctx_->extradata_size);
}

std::vector<EncodedPacket> SoftwareEncoder::drain() {
  std::vector<EncodedPacket> out;
  for (;;) {
    int r = avcodec_receive_packet(ctx_, pkt_);
    if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) break;
    if (r < 0) { std::fprintf(stderr, "receive_packet error\n"); break; }
    EncodedPacket ep;
    ep.data.assign(pkt_->data, pkt_->data + pkt_->size);
    ep.pts_us = pkt_->pts;  // set below in encode() via frame pts
    ep.keyframe = (pkt_->flags & AV_PKT_FLAG_KEY) != 0;
    out.push_back(std::move(ep));
    av_packet_unref(pkt_);
  }
  return out;
}

std::vector<EncodedPacket> SoftwareEncoder::encode(const Frame& frame, int64_t pts_us) {
  if (!ctx_ || !frame.valid) return {};
  // BGRA -> NV12.
  const uint8_t* src[1] = { frame.bgra.data() };
  int src_stride[1] = { frame.stride };
  sws_scale(sws_, src, src_stride, 0, height_, nv12_->data, nv12_->linesize);
  nv12_->pts = frame_index_++;  // in time_base (1/fps) units

  if (avcodec_send_frame(ctx_, nv12_) < 0) return {};
  auto out = drain();
  for (auto& p : out) p.pts_us = pts_us;  // tag with caller's wall-clock pts
  return out;
}

std::vector<EncodedPacket> SoftwareEncoder::flush() {
  if (!ctx_) return {};
  avcodec_send_frame(ctx_, nullptr);  // enter draining mode
  return drain();
}

SoftwareEncoder::~SoftwareEncoder() {
  if (sws_) sws_freeContext(sws_);
  if (pkt_) av_packet_free(&pkt_);
  if (nv12_) av_frame_free(&nv12_);
  if (ctx_) avcodec_free_context(&ctx_);
}

}  // namespace droppix
```

- [ ] **Step 8: Build and run the test**

```
distrobox enter droppix-dev -- bash -lc 'cmake --build /home/Spinjitsudoomyt/droppix-build -j && ctest --test-dir /home/Spinjitsudoomyt/droppix-build --output-on-failure'
```
Expected: `SoftwareEncoder.OpensAndEmitsKeyframeAnnexB` passes, prior tests still pass.

- [ ] **Step 9: Commit**

```bash
git add host/src/encoded_packet.h host/src/encoder.h host/src/software_encoder.h host/src/software_encoder.cpp host/tests/test_software_encoder.cpp host/CMakeLists.txt scripts/dev-container.sh
git commit -m "feat(encoder): Encoder interface + libx264 software encoder (BGRA->NV12)"
```

---

### Task 3: FrameSource interface + TestPatternSource — TDD

**Files:**
- Create: `host/src/frame_source.h`, `host/src/test_pattern_source.h`, `host/src/test_pattern_source.cpp`
- Create: `host/tests/test_test_pattern_source.cpp`
- Modify: `host/CMakeLists.txt` (add source + test)

**Interfaces:**
- Consumes: `droppix::Frame`.
- Produces:
  - `class droppix::FrameSource { virtual bool start(int& width, int& height)=0; virtual Frame next(int timeout_ms)=0; virtual ~FrameSource()=default; };`
  - `class droppix::TestPatternSource : public FrameSource` — ctor `TestPatternSource(int width, int height, int fps)`; generates a moving pattern; `next()` always returns a valid frame.

- [ ] **Step 1: Write the failing test**

Create `host/tests/test_test_pattern_source.cpp`:

```cpp
#include <gtest/gtest.h>
#include "test_pattern_source.h"

using namespace droppix;

TEST(TestPatternSource, ProducesCorrectlySizedValidFrames) {
  TestPatternSource s(160, 120, 30);
  int w = 0, h = 0;
  ASSERT_TRUE(s.start(w, h));
  EXPECT_EQ(w, 160); EXPECT_EQ(h, 120);
  Frame f = s.next(0);
  ASSERT_TRUE(f.valid);
  EXPECT_EQ(f.width, 160); EXPECT_EQ(f.height, 120);
  EXPECT_EQ(f.stride, 160 * 4);
  EXPECT_EQ(f.bgra.size(), size_t(160) * 120 * 4);
}

TEST(TestPatternSource, ContentChangesBetweenFrames) {
  TestPatternSource s(160, 120, 30);
  int w, h; s.start(w, h);
  Frame a = s.next(0);
  Frame b = s.next(0);
  EXPECT_NE(a.bgra, b.bgra);  // pattern animates
}
```

- [ ] **Step 2: Add to CMake and verify failure**

Add `src/test_pattern_source.cpp` to `droppix_core` and `tests/test_test_pattern_source.cpp` to `droppix_tests`. Build → expect failure (`test_pattern_source.h` not found).

- [ ] **Step 3: Write frame_source.h**

Create `host/src/frame_source.h`:

```cpp
#pragma once
#include "capturer.h"  // droppix::Frame

namespace droppix {
class FrameSource {
 public:
  virtual ~FrameSource() = default;
  // Begin producing; outputs the chosen frame dimensions. Returns success.
  virtual bool start(int& width, int& height) = 0;
  // Next frame; Frame.valid == false on timeout / no update.
  virtual Frame next(int timeout_ms) = 0;
};
}  // namespace droppix
```

- [ ] **Step 4: Write test_pattern_source.h/.cpp**

Create `host/src/test_pattern_source.h`:

```cpp
#pragma once
#include "frame_source.h"

namespace droppix {
class TestPatternSource : public FrameSource {
 public:
  TestPatternSource(int width, int height, int fps);
  bool start(int& width, int& height) override;
  Frame next(int timeout_ms) override;
 private:
  int width_, height_, fps_;
  int tick_ = 0;
};
}  // namespace droppix
```

Create `host/src/test_pattern_source.cpp`:

```cpp
#include "test_pattern_source.h"
#include <ctime>

namespace droppix {

TestPatternSource::TestPatternSource(int width, int height, int fps)
    : width_(width), height_(height), fps_(fps) {}

bool TestPatternSource::start(int& width, int& height) {
  width = width_; height = height_;
  return true;
}

Frame TestPatternSource::next(int timeout_ms) {
  // Pace to ~fps so the stream isn't faster than real time.
  if (tick_ > 0 && fps_ > 0) {
    struct timespec ts{0, (1000L * 1000L * 1000L) / fps_};
    nanosleep(&ts, nullptr);
  }
  (void)timeout_ms;
  Frame f;
  f.width = width_; f.height = height_; f.stride = width_ * 4; f.valid = true;
  f.bgra.resize(static_cast<size_t>(width_) * height_ * 4);
  const int t = tick_++;
  for (int y = 0; y < height_; ++y) {
    for (int x = 0; x < width_; ++x) {
      size_t i = (static_cast<size_t>(y) * width_ + x) * 4;
      f.bgra[i + 0] = static_cast<unsigned char>((x + t * 4) & 0xFF);  // B
      f.bgra[i + 1] = static_cast<unsigned char>((y + t * 2) & 0xFF);  // G
      f.bgra[i + 2] = static_cast<unsigned char>((x + y + t) & 0xFF);  // R
      f.bgra[i + 3] = 0xFF;
    }
  }
  return f;
}

}  // namespace droppix
```

- [ ] **Step 5: Build and run tests; expect pass.** Use the standard build+ctest command. Both `TestPatternSource.*` tests pass; prior tests still pass.

- [ ] **Step 6: Commit**

```bash
git add host/src/frame_source.h host/src/test_pattern_source.h host/src/test_pattern_source.cpp host/tests/test_test_pattern_source.cpp host/CMakeLists.txt
git commit -m "feat(source): FrameSource interface + animated TestPatternSource"
```

---

### Task 4: TransportServer — TCP listen, handshake, send (integration test)

**Files:**
- Create: `host/src/transport_server.h`, `host/src/transport_server.cpp`
- Create: `host/tests/test_transport_server.cpp`
- Modify: `host/CMakeLists.txt` (add source + test)

**Interfaces:**
- Consumes: `droppix::MessageParser`, `encode_message`, `encode_config`, `encode_video`, `decode_hello`, `MsgType` (protocol.h).
- Produces:
  - `class droppix::TransportServer` with: `bool listen(uint16_t port);` (port 0 = ephemeral), `uint16_t port() const;`, `bool accept_client(int timeout_ms);`, `bool read_hello(uint32_t& w, uint32_t& h, uint32_t& density, int timeout_ms);`, `bool send_config(uint32_t w, uint32_t h, uint32_t fps, const std::vector<unsigned char>& extradata);`, `bool send_video(uint64_t pts_us, bool keyframe, const std::vector<unsigned char>& nal);`, `void poll_control();` (answers PING with PONG, detects disconnect), `bool connected() const;`, `void close_all();`.

- [ ] **Step 1: Write the failing integration test**

Create `host/tests/test_transport_server.cpp`:

```cpp
#include <gtest/gtest.h>
#include <thread>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include "transport_server.h"
#include "protocol.h"

using namespace droppix;

// Minimal in-test client: connect, send HELLO, read one CONFIG + one VIDEO.
static void client_thread(uint16_t port, bool* ok) {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(port);
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  if (::connect(fd, (sockaddr*)&addr, sizeof(addr)) != 0) { ::close(fd); return; }

  auto hello = encode_message(MsgType::Hello, encode_hello(1920, 1080, 320));
  ::send(fd, hello.data(), hello.size(), 0);

  MessageParser p; ParsedMessage m;
  unsigned char buf[4096];
  bool gotConfig = false, gotVideo = false;
  for (int i = 0; i < 50 && !(gotConfig && gotVideo); ++i) {
    ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
    if (n <= 0) break;
    p.feed(buf, n);
    while (p.next(m)) {
      if (m.type == MsgType::Config) gotConfig = true;
      if (m.type == MsgType::Video) gotVideo = true;
    }
  }
  *ok = gotConfig && gotVideo;
  ::close(fd);
}

TEST(TransportServer, HandshakeThenVideo) {
  TransportServer s;
  ASSERT_TRUE(s.listen(0));      // ephemeral port
  uint16_t port = s.port();
  ASSERT_NE(port, 0);

  bool client_ok = false;
  std::thread t(client_thread, port, &client_ok);

  ASSERT_TRUE(s.accept_client(2000));
  uint32_t w, h, d;
  ASSERT_TRUE(s.read_hello(w, h, d, 2000));
  EXPECT_EQ(w, 1920u); EXPECT_EQ(h, 1080u);
  ASSERT_TRUE(s.send_config(1920, 1080, 30, {0x67, 0x42}));
  ASSERT_TRUE(s.send_video(1000, true, {0x00, 0x00, 0x00, 0x01, 0x65}));

  t.join();
  EXPECT_TRUE(client_ok);
}
```

- [ ] **Step 2: Add to CMake and verify failure.** Add `src/transport_server.cpp` to `droppix_core`, `tests/test_transport_server.cpp` to `droppix_tests`. The test target needs threads — add `find_package(Threads REQUIRED)` near the top of `host/CMakeLists.txt` (once) and `Threads::Threads` to `droppix_tests`'s link libraries. Build → expect failure (`transport_server.h` not found).

- [ ] **Step 3: Write transport_server.h**

Create `host/src/transport_server.h`:

```cpp
#pragma once
#include <cstdint>
#include <vector>
#include "protocol.h"

namespace droppix {
class TransportServer {
 public:
  ~TransportServer();
  bool listen(uint16_t port);          // 0 = ephemeral
  uint16_t port() const { return port_; }
  bool accept_client(int timeout_ms);
  bool read_hello(uint32_t& w, uint32_t& h, uint32_t& density, int timeout_ms);
  bool send_config(uint32_t w, uint32_t h, uint32_t fps,
                   const std::vector<unsigned char>& extradata);
  bool send_video(uint64_t pts_us, bool keyframe,
                  const std::vector<unsigned char>& nal);
  void poll_control();                 // respond to PING, detect disconnect
  bool connected() const { return client_fd_ >= 0; }
  void close_all();

 private:
  bool send_all(const std::vector<unsigned char>& bytes);
  bool wait_readable(int fd, int timeout_ms);

  int listen_fd_ = -1;
  int client_fd_ = -1;
  uint16_t port_ = 0;
  MessageParser parser_;
};
}  // namespace droppix
```

- [ ] **Step 4: Write transport_server.cpp**

Create `host/src/transport_server.cpp`:

```cpp
#include "transport_server.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>

namespace droppix {

bool TransportServer::listen(uint16_t port) {
  listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd_ < 0) return false;
  int yes = 1;
  setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);
  if (::bind(listen_fd_, (sockaddr*)&addr, sizeof(addr)) != 0) return false;
  if (::listen(listen_fd_, 1) != 0) return false;
  socklen_t len = sizeof(addr);
  if (getsockname(listen_fd_, (sockaddr*)&addr, &len) == 0) {
    port_ = ntohs(addr.sin_port);
  }
  return true;
}

bool TransportServer::wait_readable(int fd, int timeout_ms) {
  if (fd < 0) return false;
  pollfd pfd{fd, POLLIN, 0};
  return ::poll(&pfd, 1, timeout_ms) > 0 && (pfd.revents & POLLIN);
}

bool TransportServer::accept_client(int timeout_ms) {
  if (!wait_readable(listen_fd_, timeout_ms)) return false;
  client_fd_ = ::accept(listen_fd_, nullptr, nullptr);
  if (client_fd_ < 0) return false;
  int yes = 1;
  setsockopt(client_fd_, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
  return true;
}

bool TransportServer::read_hello(uint32_t& w, uint32_t& h, uint32_t& density,
                                 int timeout_ms) {
  unsigned char buf[1024];
  ParsedMessage m;
  for (;;) {
    if (parser_.next(m)) {
      if (m.type != MsgType::Hello) continue;
      return decode_hello(m.body, w, h, density);
    }
    if (!wait_readable(client_fd_, timeout_ms)) return false;
    ssize_t n = ::recv(client_fd_, buf, sizeof(buf), 0);
    if (n <= 0) { close_all(); return false; }
    parser_.feed(buf, static_cast<size_t>(n));
  }
}

bool TransportServer::send_all(const std::vector<unsigned char>& bytes) {
  if (client_fd_ < 0) return false;
  size_t off = 0;
  while (off < bytes.size()) {
    ssize_t n = ::send(client_fd_, bytes.data() + off, bytes.size() - off,
                       MSG_NOSIGNAL);
    if (n <= 0) { close_all(); return false; }
    off += static_cast<size_t>(n);
  }
  return true;
}

bool TransportServer::send_config(uint32_t w, uint32_t h, uint32_t fps,
                                  const std::vector<unsigned char>& ed) {
  return send_all(encode_message(MsgType::Config, encode_config(w, h, fps, ed)));
}

bool TransportServer::send_video(uint64_t pts_us, bool key,
                                 const std::vector<unsigned char>& nal) {
  return send_all(encode_message(MsgType::Video, encode_video(pts_us, key, nal)));
}

void TransportServer::poll_control() {
  if (client_fd_ < 0) return;
  if (!wait_readable(client_fd_, 0)) return;
  unsigned char buf[1024];
  ssize_t n = ::recv(client_fd_, buf, sizeof(buf), 0);
  if (n <= 0) { close_all(); return; }
  parser_.feed(buf, static_cast<size_t>(n));
  ParsedMessage m;
  while (parser_.next(m)) {
    if (m.type == MsgType::Ping) {
      send_all(encode_message(MsgType::Pong, m.body));
    }
  }
}

void TransportServer::close_all() {
  if (client_fd_ >= 0) { ::close(client_fd_); client_fd_ = -1; }
}

TransportServer::~TransportServer() {
  close_all();
  if (listen_fd_ >= 0) ::close(listen_fd_);
}

}  // namespace droppix
```

- [ ] **Step 5: Build and run tests; expect pass.** Standard build+ctest. `TransportServer.HandshakeThenVideo` passes; prior tests still pass.

- [ ] **Step 6: Commit**

```bash
git add host/src/transport_server.h host/src/transport_server.cpp host/tests/test_transport_server.cpp host/CMakeLists.txt
git commit -m "feat(transport): TCP server with hello/config handshake + video send"
```

---

### Task 5: StreamDaemon + CLI + test client — end-to-end test-pattern stream (auto-verifiable)

**Files:**
- Create: `host/src/stream_daemon.h`, `host/src/stream_daemon.cpp`, `host/src/stream_main.cpp`
- Create: `scripts/test-client.py`
- Modify: `host/CMakeLists.txt` (add `droppix_stream` executable; add `src/stream_daemon.cpp` to `droppix_core`)

**Interfaces:**
- Consumes: `FrameSource`, `Encoder`/`SoftwareEncoder`, `TransportServer`, `TestPatternSource`.
- Produces:
  - `struct droppix::StreamConfig { int fps=30; int bitrate_kbps=8000; };`
  - `class droppix::StreamDaemon { StreamDaemon(FrameSource& src, Encoder& enc, TransportServer& tx, StreamConfig cfg); bool run_until(const volatile sig_atomic_t& stop, int max_frames /*0 = unlimited*/); };` — handshake, then capture→encode→send loop; returns after `max_frames` encoded frames (for tests) or when `stop` is set.
  - Executable `droppix_stream` with flags: `--test-pattern`, `--port N` (default 27000), `--fps N`, `--bitrate N` (kbps), `--width N --height N` (test-pattern size; default 1280x720), `--frames N` (exit after N; 0 = run forever), `--adb-reverse` (run `adb reverse tcp:PORT tcp:PORT`).

- [ ] **Step 1: Write stream_daemon.h**

Create `host/src/stream_daemon.h`:

```cpp
#pragma once
#include <csignal>
#include "frame_source.h"
#include "encoder.h"
#include "transport_server.h"

namespace droppix {
struct StreamConfig { int fps = 30; int bitrate_kbps = 8000; };

class StreamDaemon {
 public:
  StreamDaemon(FrameSource& src, Encoder& enc, TransportServer& tx, StreamConfig cfg)
      : src_(src), enc_(enc), tx_(tx), cfg_(cfg) {}
  // Waits for a client + HELLO, opens the encoder at the source's dimensions,
  // sends CONFIG, then streams. Stops when `stop` is set or after `max_frames`
  // encoded frames (max_frames == 0 means unlimited). Returns true if it ran a
  // session (handshake completed).
  bool run_until(const volatile std::sig_atomic_t& stop, int max_frames);

 private:
  FrameSource& src_;
  Encoder& enc_;
  TransportServer& tx_;
  StreamConfig cfg_;
};
}  // namespace droppix
```

- [ ] **Step 2: Write stream_daemon.cpp**

Create `host/src/stream_daemon.cpp`:

```cpp
#include "stream_daemon.h"
#include <chrono>
#include <cstdio>

namespace droppix {

bool StreamDaemon::run_until(const volatile std::sig_atomic_t& stop, int max_frames) {
  int w = 0, h = 0;
  if (!src_.start(w, h)) { std::fprintf(stderr, "source start failed\n"); return false; }
  std::fprintf(stderr, "source %dx%d\n", w, h);

  if (!tx_.accept_client(60000)) { std::fprintf(stderr, "no client\n"); return false; }
  uint32_t cw, ch, density;
  if (!tx_.read_hello(cw, ch, density, 10000)) { std::fprintf(stderr, "no HELLO\n"); return false; }
  std::fprintf(stderr, "client HELLO %ux%u\n", cw, ch);

  if (!enc_.open(w, h, cfg_.fps, cfg_.bitrate_kbps)) { std::fprintf(stderr, "encoder open failed\n"); return false; }
  if (!tx_.send_config(w, h, cfg_.fps, enc_.extradata())) return false;

  auto t0 = std::chrono::steady_clock::now();
  int sent = 0;
  while (!stop && tx_.connected()) {
    Frame f = src_.next(1000);
    if (!f.valid) { tx_.poll_control(); continue; }
    int64_t pts_us = std::chrono::duration_cast<std::chrono::microseconds>(
                         std::chrono::steady_clock::now() - t0).count();
    for (auto& pkt : enc_.encode(f, pts_us)) {
      if (!tx_.send_video(pkt.pts_us, pkt.keyframe, pkt.data)) break;
      ++sent;
    }
    tx_.poll_control();
    if (max_frames > 0 && sent >= max_frames) break;
  }
  for (auto& pkt : enc_.flush()) tx_.send_video(pkt.pts_us, pkt.keyframe, pkt.data);
  std::fprintf(stderr, "sent %d video packets\n", sent);
  return true;
}

}  // namespace droppix
```

- [ ] **Step 3: Write stream_main.cpp**

Create `host/src/stream_main.cpp`:

```cpp
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "stream_daemon.h"
#include "test_pattern_source.h"
#include "software_encoder.h"

static volatile std::sig_atomic_t g_stop = 0;
static void on_sigint(int) { g_stop = 1; }

int main(int argc, char** argv) {
  std::signal(SIGINT, on_sigint);
  int port = 27000, fps = 30, bitrate = 8000, frames = 0;
  int width = 1280, height = 720;
  bool test_pattern = false, adb_reverse = false;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto val = [&]() { return (i + 1 < argc) ? std::atoi(argv[++i]) : 0; };
    if (a == "--test-pattern") test_pattern = true;
    else if (a == "--adb-reverse") adb_reverse = true;
    else if (a == "--port") port = val();
    else if (a == "--fps") fps = val();
    else if (a == "--bitrate") bitrate = val();
    else if (a == "--width") width = val();
    else if (a == "--height") height = val();
    else if (a == "--frames") frames = val();
    else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); return 2; }
  }

  if (!test_pattern) {
    std::fprintf(stderr, "Phase 1a: only --test-pattern is wired here. "
                         "evdi source arrives in Task 6.\n");
    return 2;
  }

  droppix::TransportServer tx;
  if (!tx.listen(static_cast<uint16_t>(port))) {
    std::fprintf(stderr, "listen on %d failed\n", port); return 1;
  }
  std::fprintf(stderr, "listening on port %d\n", tx.port());

  if (adb_reverse) {
    std::string cmd = "adb reverse tcp:" + std::to_string(port) +
                      " tcp:" + std::to_string(port);
    std::fprintf(stderr, "running: %s\n", cmd.c_str());
    if (std::system(cmd.c_str()) != 0)
      std::fprintf(stderr, "warning: adb reverse failed\n");
  }

  droppix::TestPatternSource src(width, height, fps);
  droppix::SoftwareEncoder enc;
  droppix::StreamDaemon daemon(src, enc, tx, {fps, bitrate});
  bool ran = daemon.run_until(g_stop, frames);
  return ran ? 0 : 1;
}
```

- [ ] **Step 4: Write the test client**

Create `scripts/test-client.py`:

```python
#!/usr/bin/env python3
"""Connect to droppix_stream, do the HELLO/CONFIG handshake, and write the
received H.264 Annex-B stream (CONFIG extradata first, then each VIDEO NAL) to
stdout. Pipe to a player or ffprobe:

    python3 scripts/test-client.py 27000 1920 1080 | ffplay -fflags nobuffer -
    python3 scripts/test-client.py 27000 1920 1080 > out.h264   # then ffprobe out.h264
"""
import socket, struct, sys

HELLO, CONFIG, VIDEO, PING, PONG, BYE = 1, 2, 3, 4, 5, 6

def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 27000
    w = int(sys.argv[2]) if len(sys.argv) > 2 else 1280
    h = int(sys.argv[3]) if len(sys.argv) > 3 else 720
    out = sys.stdout.buffer

    s = socket.create_connection(("127.0.0.1", port))
    s.sendall(struct.pack(">IB", 1 + 12, HELLO) + struct.pack(">III", w, h, 320))

    buf = b""
    def read_msg():
        nonlocal buf
        while len(buf) < 4:
            d = s.recv(65536)
            if not d: return None
            buf += d
        (length,) = struct.unpack(">I", buf[:4])
        while len(buf) < 4 + length:
            d = s.recv(65536)
            if not d: return None
            buf += d
        mtype = buf[4]
        body = buf[5:4 + length]
        buf = buf[4 + length:]
        return mtype, body

    while True:
        msg = read_msg()
        if msg is None: break
        mtype, body = msg
        if mtype == CONFIG:
            w2, h2, fps, edlen = struct.unpack(">IIII", body[:16])
            extradata = body[16:16 + edlen]
            sys.stderr.write(f"CONFIG {w2}x{h2}@{fps} extradata={len(extradata)}B\n")
            if extradata:
                out.write(extradata); out.flush()
        elif mtype == VIDEO:
            # body = u64 pts, u8 keyframe, then NAL
            nal = body[9:]
            out.write(nal); out.flush()
        elif mtype == BYE:
            break

if __name__ == "__main__":
    main()
```

Make it executable in git: `git update-index --chmod=+x scripts/test-client.py` after adding (the CIFS mount can't carry the exec bit, but git records it).

- [ ] **Step 5: Wire CMake for the executable**

In `host/CMakeLists.txt`, add `src/stream_daemon.cpp` to `droppix_core`, and add:

```cmake
add_executable(droppix_stream src/stream_main.cpp)
target_link_libraries(droppix_stream PRIVATE droppix_core)
```

- [ ] **Step 6: Build**

Standard build command. Expected: `droppix_stream` links.

- [ ] **Step 7: End-to-end auto-verification (no hardware, no GUI)**

Run the server (test-pattern, exit after 60 frames) in the background and capture the stream with the test client, then validate with `ffprobe`. Run inside the container:

```
distrobox enter droppix-dev -- bash -lc '
  set -e
  BIN=/home/Spinjitsudoomyt/droppix-build/droppix_stream
  OUT=/tmp/droppix_e2e.h264
  "$BIN" --test-pattern --port 27031 --fps 30 --frames 60 --width 640 --height 480 &
  SRV=$!
  sleep 1
  python3 "/var/mnt/nas/Projects/Spacedesk for linux/scripts/test-client.py" 27031 640 480 > "$OUT" || true
  wait $SRV 2>/dev/null || true
  ls -l "$OUT"
  ffprobe -v error -show_entries stream=codec_name,width,height -of default=nokey=1:noprint_wrappers=1 "$OUT"
  FRAMES=$(ffprobe -v error -count_frames -select_streams v:0 -show_entries stream=nb_read_frames -of default=nokey=1:noprint_wrappers=1 "$OUT")
  echo "decoded frames: $FRAMES"
  test "$FRAMES" -ge 30
'
```
Expected: `ffprobe` reports `h264`, `640`, `480`, and "decoded frames" ≥ 30 (the test asserts this with `test ... -ge 30`). This proves source→encode→protocol→transport→decodable H.264 end-to-end **without evdi/sudo/GUI**.

(Optional visual check, operator: on the host, `python3 scripts/test-client.py 27031 640 480 | ffplay -fflags nobuffer -i -` while the server runs without `--frames` shows the animated pattern.)

- [ ] **Step 8: Commit**

```bash
git add host/src/stream_daemon.h host/src/stream_daemon.cpp host/src/stream_main.cpp scripts/test-client.py host/CMakeLists.txt
git commit -m "feat(stream): daemon + CLI + test client; e2e test-pattern stream verified via ffprobe"
```

---

### Task 6: EvdiFrameSource — stream the real extended monitor (hardware gate)

**Files:**
- Create: `host/src/evdi_frame_source.h`, `host/src/evdi_frame_source.cpp`
- Modify: `host/src/stream_main.cpp` (use `EvdiFrameSource` when `--test-pattern` is absent)
- Modify: `host/CMakeLists.txt` (add `src/evdi_frame_source.cpp` to `droppix_core`)

**Interfaces:**
- Consumes: `VirtualDisplay`, `Capturer`, `build_edid`, `timing_1080p60` (Phase 0), `FrameSource`.
- Produces: `class droppix::EvdiFrameSource : public FrameSource` — `start()` opens+connects the evdi monitor and waits for the KWin mode (returns 1920×1080); `next(timeout_ms)` returns `Capturer::grab(timeout_ms)`.

- [ ] **Step 1: Write evdi_frame_source.h**

Create `host/src/evdi_frame_source.h`:

```cpp
#pragma once
#include <memory>
#include "frame_source.h"
#include "virtual_display.h"
#include "capturer.h"

namespace droppix {
class EvdiFrameSource : public FrameSource {
 public:
  bool start(int& width, int& height) override;
  Frame next(int timeout_ms) override;
 private:
  VirtualDisplay display_;
  std::unique_ptr<Capturer> cap_;
};
}  // namespace droppix
```

- [ ] **Step 2: Write evdi_frame_source.cpp**

Create `host/src/evdi_frame_source.cpp`:

```cpp
#include "evdi_frame_source.h"
#include "edid.h"
#include <cstdio>

namespace droppix {

bool EvdiFrameSource::start(int& width, int& height) {
  if (!display_.open()) return false;
  display_.connect(build_edid(timing_1080p60()));
  cap_ = std::make_unique<Capturer>(display_.handle());
  if (!cap_->wait_for_mode(5000)) {
    std::fprintf(stderr, "evdi: no KWin mode within 5s\n");
    return false;
  }
  width = cap_->width();
  height = cap_->height();
  return true;
}

Frame EvdiFrameSource::next(int timeout_ms) {
  if (!cap_) return Frame{};
  return cap_->grab(timeout_ms);
}

}  // namespace droppix
```

- [ ] **Step 3: Wire it into the CLI**

In `host/src/stream_main.cpp`: add `#include "evdi_frame_source.h"`. Replace the block that currently rejects the non-test-pattern path:

```cpp
  if (!test_pattern) {
    std::fprintf(stderr, "Phase 1a: only --test-pattern is wired here. "
                         "evdi source arrives in Task 6.\n");
    return 2;
  }
```
with a source chosen at runtime. Restructure so the daemon uses a `FrameSource&` that points at either source. Concretely, replace the `TestPatternSource src(...)` construction and daemon run with:

```cpp
  droppix::SoftwareEncoder enc;
  droppix::TestPatternSource pattern(width, height, fps);
  droppix::EvdiFrameSource evdi;
  droppix::FrameSource& src =
      test_pattern ? static_cast<droppix::FrameSource&>(pattern)
                   : static_cast<droppix::FrameSource&>(evdi);
  droppix::StreamDaemon daemon(src, enc, tx, {fps, bitrate});
  bool ran = daemon.run_until(g_stop, frames);
  return ran ? 0 : 1;
```
(Remove the old `if (!test_pattern) {...}` early-return and the later duplicate daemon block.)

- [ ] **Step 4: Add source to CMake and build**

Add `src/evdi_frame_source.cpp` to `droppix_core`. Standard build. Expected: `droppix_stream` links cleanly against libevdi + libav*; all prior tests (Tasks 1–5) still pass.

- [ ] **Step 5: Commit**

```bash
git add host/src/evdi_frame_source.h host/src/evdi_frame_source.cpp host/src/stream_main.cpp host/CMakeLists.txt
git commit -m "feat(stream): evdi frame source — stream the real extended monitor"
```

- [ ] **Step 6: Operator hardware verification (sudo + GUI)**

This step is performed by the human operator (needs root for evdi + a GUI to drag a window). Two terminals on the host:

Terminal A — run the streamer against the real evdi monitor:
```bash
sudo /home/Spinjitsudoomyt/droppix-build/droppix_stream --port 27000 --fps 30 --bitrate 8000
```
Terminal B — connect the test client and play the stream:
```bash
python3 "/var/mnt/nas/Projects/Spacedesk for linux/scripts/test-client.py" 27000 1920 1080 | ffplay -fflags nobuffer -flags low_delay -framedrop -i -
```
Then: in KDE Display settings enable/arrange the new **droppix** monitor and drag a window onto it.

Expected: `ffplay` shows the live contents of the virtual monitor. Record in a short note (latency feel, fps, any artifacts) — this informs Plan 1b (the Android decoder) and the VAAPI follow-up.

---

## Self-Review

**1. Spec coverage (Phase 1a scope):** The design spec's Phase 1 host side = "evdi → capture → VAAPI H.264 → TCP." Phase 1a delivers capture→**software** H.264→TCP (VAAPI deferred per the user's chosen "interface + software first" approach, satisfied by the `Encoder` interface in Task 2 making VAAPI a drop-in). Protocol matches the spec's framing and HELLO/CONFIG/VIDEO/PING/PONG/BYE message set (Task 1). `TransportServer` implements the handshake (Task 4). The capture→encode→send loop is damage-driven via `FrameSource::next` returning invalid on timeout (Task 5/6), matching the spike-findings guidance. WiFi/mDNS, input, and dynamic resolution are correctly out of Phase 1a scope (later phases). Android receiver is Plan 1b.

**2. Placeholder scan:** No TBD/TODO. Every code step has complete code. The Task-6 CLI edit references the exact prior text to replace. The only deferred items are explicitly later-phase (VAAPI, evdi-source live run) and are real, scoped tasks/steps, not placeholders.

**3. Type consistency:** `Frame` (Phase 0) fields used consistently. `Encoder::encode(const Frame&, int64_t)` and `extradata()` match between encoder.h, software_encoder, and StreamDaemon. `EncodedPacket{data,pts_us,keyframe}` consistent. `TransportServer` method names (`listen/port/accept_client/read_hello/send_config/send_video/poll_control/connected/close_all`) match between header, .cpp, the test, and StreamDaemon. Protocol functions (`encode_message/encode_hello/encode_config/encode_video/decode_*`, `MsgType`, `MessageParser`) match across protocol.h, transport_server, the test, and test-client.py (which mirrors the same wire format in Python). `FrameSource::{start(int&,int&),next(int)}` consistent across TestPatternSource, EvdiFrameSource, and StreamDaemon. `StreamDaemon::run_until(const volatile sig_atomic_t&, int)` matches stream_main usage.

**Known external-API caveat (by design):** the libavcodec calls target ffmpeg 8.1's modern API; if a struct field or option name differs at build time, follow the installed headers. The `repeat-headers` x264 option produces in-band SPS/PPS; `extradata()` may therefore be empty, which is handled (CONFIG carries empty extradata and the decoder syncs on the in-band IDR headers).
