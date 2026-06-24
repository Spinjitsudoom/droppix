# Phase 1c — Latency Instrumentation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Measure per-stage latency so tuning is data-driven: host prints encode-time / fps / frame-size stats; the tablet shows an on-screen overlay with RTT, received fps, and decode submit→output lag.

**Architecture:** Add small, unit-testable aggregators (a C++ `StatAccumulator`, a Kotlin `RateMeter`) and wire them in: the host `StreamDaemon` times each `encode()` and logs a stats line ~1/sec; the Android `TransportClient` sends a periodic PING (host already echoes PONG → RTT) and counts arriving frames; `VideoDecoder` measures submit→output lag; `MainActivity` shows all three in an overlay `TextView`. No wire-protocol change — PING/PONG already exist.

**Tech Stack:** C++17 (host daemon), Kotlin/Android (app), JUnit + GoogleTest for the pure aggregators, the existing droppix wire protocol.

## Global Constraints

- **Build envs (unchanged):** C++ host builds in distrobox `droppix-dev`, off-mount: `distrobox enter droppix-dev -- bash -lc 'cmake --build /home/Spinjitsudoomyt/droppix-build -j && ctest --test-dir /home/Spinjitsudoomyt/droppix-build --output-on-failure'` (configure with `cmake -S "/var/mnt/nas/Projects/Spacedesk for linux/host" -B /home/Spinjitsudoomyt/droppix-build` first if needed). Android builds in distrobox `droppix-android`: `distrobox enter droppix-android -- bash -lc 'export ANDROID_SDK_ROOT=/home/Spinjitsudoomyt/android-sdk JAVA_HOME=$(dirname $(dirname $(readlink -f $(which java)))) GRADLE_USER_HOME=/home/Spinjitsudoomyt/.droppix-gradle; cd "/var/mnt/nas/Projects/Spacedesk for linux/android"; bash gradlew --no-daemon assembleDebug test'`. Repo is on a CIFS mount — build off-mount, run gradlew via `bash gradlew`.
- **No wire-protocol change.** Reuse `MsgType.Ping`/`Pong`. The host's `TransportServer::poll_control` already echoes PING bodies as PONG; the app sends app-initiated PINGs and matches the echo. PING body = an 8-byte big-endian monotonic timestamp (nanoseconds), opaque to the host.
- **C++17**, namespace `droppix`, host code under `host/src`. **Kotlin** package `com.droppix.app`, app code under `android/app/src/main/java/com/droppix/app`.
- **Instrumentation must be cheap** — no measurable latency added by measuring (no blocking, no per-frame allocation in hot paths beyond a timestamp).
- This is display-only Phase 1; instrumentation only, no behavior change to the stream itself.

---

## File Structure

```
host/src/
  stat_accumulator.h  stat_accumulator.cpp   # pure: count/avg/peak/reset (tested)
  stream_daemon.cpp                           # MODIFY: time encode, log stats ~1/s
host/tests/
  test_stat_accumulator.cpp
android/app/src/main/java/com/droppix/app/
  stats/RateMeter.kt        # pure: events -> per-second rate (tested)
  stats/StatsSink.kt        # @Volatile holder: rttMs, fps, decodeLagMs
  net/TransportClient.kt    # MODIFY: periodic PING, RTT on PONG, frame rate
  decode/VideoDecoder.kt    # MODIFY: submit->output lag into StatsSink
  ui/MainActivity.kt        # MODIFY: overlay TextView updated ~1/s
android/app/src/main/res/layout/activity_main.xml  # MODIFY: add overlay TextView
android/app/src/test/java/com/droppix/app/stats/RateMeterTest.kt
android/app/src/test/java/com/droppix/app/net/TransportClientStatsTest.kt
```

---

### Task 1: Host `StatAccumulator` + stream stats logging

**Files:**
- Create: `host/src/stat_accumulator.h`, `host/src/stat_accumulator.cpp`, `host/tests/test_stat_accumulator.cpp`
- Modify: `host/src/stream_daemon.cpp` (time encode, accumulate, log ~1/s)
- Modify: `host/CMakeLists.txt` (add source + test)

**Interfaces:**
- Produces: `class droppix::StatAccumulator { void add(double v); int count() const; double avg() const; double peak() const; void reset(); };` (`avg()`/`peak()` return 0 when empty).

- [ ] **Step 1: Write the failing test**

`host/tests/test_stat_accumulator.cpp`:

```cpp
#include <gtest/gtest.h>
#include "stat_accumulator.h"

using droppix::StatAccumulator;

TEST(StatAccumulator, EmptyIsZero) {
  StatAccumulator s;
  EXPECT_EQ(s.count(), 0);
  EXPECT_DOUBLE_EQ(s.avg(), 0.0);
  EXPECT_DOUBLE_EQ(s.peak(), 0.0);
}

TEST(StatAccumulator, AvgPeakCount) {
  StatAccumulator s;
  s.add(10); s.add(20); s.add(30);
  EXPECT_EQ(s.count(), 3);
  EXPECT_DOUBLE_EQ(s.avg(), 20.0);
  EXPECT_DOUBLE_EQ(s.peak(), 30.0);
}

TEST(StatAccumulator, ResetClears) {
  StatAccumulator s;
  s.add(5); s.reset();
  EXPECT_EQ(s.count(), 0);
  EXPECT_DOUBLE_EQ(s.avg(), 0.0);
}
```

- [ ] **Step 2: Add to CMake, build, verify failure.** Add `src/stat_accumulator.cpp` to `droppix_core` and `tests/test_stat_accumulator.cpp` to `droppix_tests`. Build → expect "stat_accumulator.h not found".

- [ ] **Step 3: Write the header**

`host/src/stat_accumulator.h`:

```cpp
#pragma once
namespace droppix {
// Accumulates samples for a reporting window: count, mean, peak. Not thread-safe;
// used from the single capture/encode loop.
class StatAccumulator {
 public:
  void add(double v) { sum_ += v; if (count_ == 0 || v > peak_) peak_ = v; ++count_; }
  int count() const { return count_; }
  double avg() const { return count_ ? sum_ / count_ : 0.0; }
  double peak() const { return count_ ? peak_ : 0.0; }
  void reset() { sum_ = 0.0; peak_ = 0.0; count_ = 0; }
 private:
  double sum_ = 0.0;
  double peak_ = 0.0;
  int count_ = 0;
};
}  // namespace droppix
```

- [ ] **Step 4: Write a trivial .cpp** (keeps CMake source list happy; the class is header-only).

`host/src/stat_accumulator.cpp`:

```cpp
#include "stat_accumulator.h"
// Header-only implementation; this translation unit anchors the target.
namespace droppix {}
```

- [ ] **Step 5: Build + test → pass.** Standard build+ctest. All `StatAccumulator.*` pass.

- [ ] **Step 6: Wire stats into the stream loop**

In `host/src/stream_daemon.cpp`: add `#include "stat_accumulator.h"` and `#include <chrono>` (chrono already present). In `run_until`, before the loop add accumulators and a report clock:

```cpp
  StatAccumulator encode_ms, frame_kb;
  int frames_since_report = 0;
  auto last_report = std::chrono::steady_clock::now();
```

Replace the per-frame encode call so it is timed, and accumulate packet sizes. Where the loop currently does the encode + send, use:

```cpp
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
      std::fprintf(stderr,
          "stats: encode avg %.1f ms peak %.1f ms | fps %.1f | frame avg %.1f KB peak %.1f KB\n",
          encode_ms.avg(), encode_ms.peak(), frames_since_report / elapsed_s,
          frame_kb.avg(), frame_kb.peak());
      encode_ms.reset(); frame_kb.reset(); frames_since_report = 0; last_report = now;
    }
```

(Remove the old un-timed `for (auto& pkt : enc_.encode(f, pts_us)) { ... }` block this replaces. Keep the surrounding `if (!f.valid) { tx_.poll_control(); continue; }` and the `tx_.poll_control();` after.)

- [ ] **Step 7: Build + verify stats appear (no hardware needed)**

Run the test-pattern stream for a couple of seconds and confirm stats lines print:

```
distrobox enter droppix-dev -- bash -lc '
  /home/Spinjitsudoomyt/droppix-build/droppix_stream --test-pattern --port 27051 --fps 30 --frames 90 --width 640 --height 480 2>/tmp/droppix_stats.log &
  SRV=$!; sleep 1
  python3 "/var/mnt/nas/Projects/Spacedesk for linux/scripts/test-client.py" 27051 640 480 >/dev/null 2>&1 || true
  wait $SRV 2>/dev/null || true
  grep "^stats:" /tmp/droppix_stats.log | head'
```
Expected: one or more `stats: encode avg ... fps ... frame avg ...` lines. Also confirm the prior unit tests still pass via ctest.

- [ ] **Step 8: Commit**

```bash
git add host/src/stat_accumulator.h host/src/stat_accumulator.cpp host/tests/test_stat_accumulator.cpp host/src/stream_daemon.cpp host/CMakeLists.txt
git commit -m "feat(stats): host encode-time/fps/frame-size stats per second"
```

---

### Task 2: App `RateMeter` + `StatsSink` + TransportClient RTT/fps — TDD

**Files:**
- Create: `android/app/src/main/java/com/droppix/app/stats/RateMeter.kt`, `android/app/src/main/java/com/droppix/app/stats/StatsSink.kt`
- Modify: `android/app/src/main/java/com/droppix/app/net/TransportClient.kt`
- Create: `android/app/src/test/java/com/droppix/app/stats/RateMeterTest.kt`, `android/app/src/test/java/com/droppix/app/net/TransportClientStatsTest.kt`

**Interfaces:**
- Produces:
  - `class RateMeter(windowMs: Long = 1000)` with `fun mark(nowMs: Long)` and `fun ratePerSec(nowMs: Long): Double`.
  - `class StatsSink { @Volatile var rttMs: Double; @Volatile var fps: Double; @Volatile var decodeLagMs: Double }`.
  - `TransportClient.run(...)` gains two trailing optional params: `stats: StatsSink? = null, pingIntervalMs: Long = 1000`. It sends a PING (8-byte BE monotonic-ns body) every `pingIntervalMs`, sets `stats.rttMs` on the matching PONG, and sets `stats.fps` from a `RateMeter` marked on each VIDEO.

- [ ] **Step 1: Write the failing tests**

`android/app/src/test/java/com/droppix/app/stats/RateMeterTest.kt`:

```kotlin
package com.droppix.app.stats

import org.junit.Assert.assertEquals
import org.junit.Test

class RateMeterTest {
    @Test fun countsEventsWithinWindow() {
        val m = RateMeter(1000)
        var t = 10_000L
        repeat(30) { m.mark(t); t += 10 }   // 30 events over 300ms, all within 1s window
        assertEquals(30.0, m.ratePerSec(t), 0.001)
    }

    @Test fun dropsEventsOlderThanWindow() {
        val m = RateMeter(1000)
        m.mark(0); m.mark(100)
        // far future: both events are older than the window
        assertEquals(0.0, m.ratePerSec(5000), 0.001)
    }
}
```

`android/app/src/test/java/com/droppix/app/net/TransportClientStatsTest.kt`:

```kotlin
package com.droppix.app.net

import com.droppix.app.protocol.MsgType
import com.droppix.app.protocol.Protocol
import com.droppix.app.stats.StatsSink
import org.junit.Assert.*
import org.junit.Test
import java.io.DataInputStream
import java.net.ServerSocket
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.concurrent.thread

class TransportClientStatsTest {
    @Test fun rttAndFpsArePopulated() {
        val server = ServerSocket(0)
        val port = server.localPort
        val stop = AtomicBoolean(false)

        // Fake host: read HELLO, send CONFIG + a VIDEO, then echo any PING as PONG.
        val serverThread = thread {
            server.use {
                val sock = it.accept()
                val input = DataInputStream(sock.getInputStream())
                val out = sock.getOutputStream()
                // read HELLO
                var len = input.readInt(); input.readFully(ByteArray(len))
                out.write(Protocol.encodeMessage(MsgType.CONFIG,
                    beU32(640) + beU32(480) + beU32(30) + beU32(0)))
                out.write(Protocol.encodeMessage(MsgType.VIDEO,
                    beU64(1L) + byteArrayOf(1) + byteArrayOf(0,0,0,1,0x65)))
                out.flush()
                // echo one PING -> PONG
                len = input.readInt()
                val frame = ByteArray(len); input.readFully(frame)
                if (frame[0].toInt() == MsgType.PING.code) {
                    out.write(Protocol.encodeMessage(MsgType.PONG,
                        frame.copyOfRange(1, frame.size)))
                    out.flush()
                }
                while (!stop.get()) Thread.sleep(20)
            }
        }

        val stats = StatsSink()
        val client = TransportClient()
        val listener = object : StreamListener {
            override fun onConfig(config: Protocol.Config) {}
            override fun onVideo(video: Protocol.Video) {}
        }
        val clientThread = thread {
            // pingIntervalMs=0 -> ping on the first loop iteration so the test is fast
            client.run("127.0.0.1", port, 640, 480, 320, listener, { !stop.get() },
                stats, 0)
        }

        val deadline = System.currentTimeMillis() + 3000
        while (System.currentTimeMillis() < deadline && (stats.rttMs <= 0.0 || stats.fps <= 0.0)) {
            Thread.sleep(20)
        }
        stop.set(true)
        clientThread.join(1000); serverThread.join(1000)

        assertTrue("rtt not measured: ${stats.rttMs}", stats.rttMs > 0.0)
        assertTrue("fps not measured: ${stats.fps}", stats.fps > 0.0)
    }

    private fun beU32(x: Int) = byteArrayOf(
        (x ushr 24).toByte(), (x ushr 16).toByte(), (x ushr 8).toByte(), x.toByte())
    private fun beU64(x: Long) = ByteArray(8) { i -> (x ushr (56 - i * 8)).toByte() }
}
```

- [ ] **Step 2: Run tests → verify failure** (gradle `test`) — `RateMeter`/`StatsSink`/new `run` overload unresolved.

- [ ] **Step 3: Write RateMeter.kt**

```kotlin
package com.droppix.app.stats

// Sliding-window event rate. Not thread-safe; called from the single net thread.
class RateMeter(private val windowMs: Long = 1000) {
    private val times = ArrayDeque<Long>()
    fun mark(nowMs: Long) { times.addLast(nowMs); trim(nowMs) }
    fun ratePerSec(nowMs: Long): Double {
        trim(nowMs)
        return times.size * 1000.0 / windowMs
    }
    private fun trim(nowMs: Long) {
        while (times.isNotEmpty() && nowMs - times.first() > windowMs) times.removeFirst()
    }
}
```

- [ ] **Step 4: Write StatsSink.kt**

```kotlin
package com.droppix.app.stats

// Cross-thread snapshot of the latest measurements. Writers: net thread (rtt,fps),
// decoder (decodeLag). Reader: UI overlay. Volatile primitives are sufficient.
class StatsSink {
    @Volatile var rttMs: Double = 0.0
    @Volatile var fps: Double = 0.0
    @Volatile var decodeLagMs: Double = 0.0
}
```

- [ ] **Step 5: Modify TransportClient.kt** to send PINGs, measure RTT, and count frames. Update the signature and the loop:

```kotlin
package com.droppix.app.net

import com.droppix.app.protocol.MessageParser
import com.droppix.app.protocol.MsgType
import com.droppix.app.protocol.Protocol
import com.droppix.app.stats.RateMeter
import com.droppix.app.stats.StatsSink
import java.net.InetSocketAddress
import java.net.Socket

interface StreamListener {
    fun onConfig(config: Protocol.Config)
    fun onVideo(video: Protocol.Video)
}

class TransportClient {
    private fun longToBytes(x: Long) = ByteArray(8) { i -> (x ushr (56 - i * 8)).toByte() }
    private fun bytesToLong(b: ByteArray): Long {
        var x = 0L; for (i in 0 until 8) x = (x shl 8) or (b[i].toLong() and 0xFF); return x
    }

    fun run(host: String, port: Int, width: Int, height: Int, density: Int,
            listener: StreamListener, isRunning: () -> Boolean,
            stats: StatsSink? = null, pingIntervalMs: Long = 1000) {
        val socket = Socket()
        try {
            socket.tcpNoDelay = true
            socket.connect(InetSocketAddress(host, port), 5000)
            socket.soTimeout = 1000

            val out = socket.getOutputStream()
            val input = socket.getInputStream()

            out.write(Protocol.encodeMessage(MsgType.HELLO,
                Protocol.encodeHello(Protocol.VERSION, width, height, density)))
            out.flush()

            val parser = MessageParser()
            val chunk = ByteArray(65536)
            val frameRate = RateMeter(1000)
            var lastPing = 0L
            while (isRunning()) {
                val nowMs = System.currentTimeMillis()
                if (stats != null && nowMs - lastPing >= pingIntervalMs) {
                    out.write(Protocol.encodeMessage(MsgType.PING, longToBytes(System.nanoTime())))
                    out.flush()
                    lastPing = nowMs
                }
                val n = try { input.read(chunk) } catch (e: java.net.SocketTimeoutException) { 0 }
                if (n > 0) {
                    parser.feed(chunk, n)
                    var msg = parser.next()
                    while (msg != null) {
                        when (msg.type) {
                            MsgType.CONFIG -> Protocol.decodeConfig(msg.body)?.let(listener::onConfig)
                            MsgType.VIDEO -> {
                                Protocol.decodeVideo(msg.body)?.let(listener::onVideo)
                                if (stats != null) {
                                    frameRate.mark(System.currentTimeMillis())
                                    stats.fps = frameRate.ratePerSec(System.currentTimeMillis())
                                }
                            }
                            MsgType.PING -> { out.write(Protocol.encodeMessage(MsgType.PONG, msg.body)); out.flush() }
                            MsgType.PONG -> if (stats != null && msg.body.size >= 8) {
                                stats.rttMs = (System.nanoTime() - bytesToLong(msg.body)) / 1_000_000.0
                            }
                            MsgType.BYE -> return
                            else -> { /* ignore */ }
                        }
                        msg = parser.next()
                    }
                } else if (n < 0) {
                    return
                }
            }
        } finally {
            try { socket.close() } catch (_: Exception) {}
        }
    }
}
```

- [ ] **Step 6: Run tests → pass** (gradle `test`). `RateMeterTest` + `TransportClientStatsTest` + all prior tests pass.

- [ ] **Step 7: Commit**

```bash
git add android/app/src/main/java/com/droppix/app/stats/RateMeter.kt android/app/src/main/java/com/droppix/app/stats/StatsSink.kt android/app/src/main/java/com/droppix/app/net/TransportClient.kt android/app/src/test/java/com/droppix/app/stats/RateMeterTest.kt android/app/src/test/java/com/droppix/app/net/TransportClientStatsTest.kt
git commit -m "feat(android): RTT + fps instrumentation (RateMeter, StatsSink, PING)"
```

---

### Task 3: Decode-lag measurement + on-screen overlay (APK gate + device)

**Files:**
- Modify: `android/app/src/main/java/com/droppix/app/decode/VideoDecoder.kt` (measure submit→output lag into a `StatsSink`)
- Modify: `android/app/src/main/res/layout/activity_main.xml` (add overlay `TextView`)
- Modify: `android/app/src/main/java/com/droppix/app/ui/MainActivity.kt` (create `StatsSink`, pass to client + decoder, update overlay ~1/s)

**Interfaces:**
- Consumes: `StatsSink` (Task 2), `TransportClient.run(..., stats, pingIntervalMs)` (Task 2).
- Produces: `VideoDecoder(surface, width, height, stats: StatsSink? = null)` — records each submitted frame's pts→submit-time and, when the matching output buffer appears (`info.presentationTimeUs == pts`), writes the lag (ms) to `stats.decodeLagMs`.

- [ ] **Step 1: Modify VideoDecoder.kt** to take an optional `StatsSink` and measure decode lag. Add the import and constructor param, a pts→submit-time map, and set the lag in the drain loop:

```kotlin
package com.droppix.app.decode

import android.media.MediaCodec
import android.media.MediaFormat
import android.os.Build
import android.os.SystemClock
import android.util.Log
import android.view.Surface
import com.droppix.app.stats.StatsSink

class VideoDecoder(surface: Surface, width: Int, height: Int,
                   private val stats: StatsSink? = null) {
    private companion object { const val TAG = "droppix" }

    private val codec = MediaCodec.createDecoderByType(MediaFormat.MIMETYPE_VIDEO_AVC)
    private val info = MediaCodec.BufferInfo()
    @Volatile private var released = false
    private val submitNs = HashMap<Long, Long>()  // ptsUs -> submit SystemClock ns

    init {
        val fmt = MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_AVC, width, height)
        if (Build.VERSION.SDK_INT >= 30) fmt.setInteger(MediaFormat.KEY_LOW_LATENCY, 1)
        fmt.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, maxOf(width * height, 1024 * 1024))
        codec.configure(fmt, surface, null, 0)
        codec.start()
    }

    @Synchronized
    fun submit(nal: ByteArray, ptsUs: Long) {
        if (released) return
        try {
            val inIndex = codec.dequeueInputBuffer(100_000)
            if (inIndex >= 0) {
                val buf = codec.getInputBuffer(inIndex)!!
                buf.clear()
                if (nal.size > buf.capacity()) {
                    Log.w(TAG, "NAL ${nal.size}B exceeds input buffer ${buf.capacity()}B; dropping")
                    codec.queueInputBuffer(inIndex, 0, 0, ptsUs, 0)
                } else {
                    buf.put(nal)
                    if (stats != null) submitNs[ptsUs] = SystemClock.elapsedRealtimeNanos()
                    codec.queueInputBuffer(inIndex, 0, nal.size, ptsUs, 0)
                }
            } else {
                Log.w(TAG, "no input buffer available; dropping ${nal.size}B NAL")
            }
            var outIndex = codec.dequeueOutputBuffer(info, 0)
            while (outIndex >= 0) {
                if (stats != null) {
                    val t0 = submitNs.remove(info.presentationTimeUs)
                    if (t0 != null) {
                        stats.decodeLagMs = (SystemClock.elapsedRealtimeNanos() - t0) / 1_000_000.0
                    }
                    if (submitNs.size > 240) submitNs.clear()  // safety bound
                }
                codec.releaseOutputBuffer(outIndex, true)
                outIndex = codec.dequeueOutputBuffer(info, 0)
            }
        } catch (e: IllegalStateException) {
            Log.w(TAG, "decoder submit failed: ${e.message}")
        }
    }

    @Synchronized
    fun release() {
        released = true
        try { codec.stop() } catch (_: Exception) {}
        codec.release()
        submitNs.clear()
    }
}
```

- [ ] **Step 2: Add the overlay TextView to the layout**

`android/app/src/main/res/layout/activity_main.xml`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<FrameLayout xmlns:android="http://schemas.android.com/apk/res/android"
    android:layout_width="match_parent"
    android:layout_height="match_parent"
    android:background="#000000">

    <com.droppix.app.ui.DisplaySurfaceView
        android:id="@+id/surface"
        android:layout_width="match_parent"
        android:layout_height="match_parent" />

    <TextView
        android:id="@+id/overlay"
        android:layout_width="wrap_content"
        android:layout_height="wrap_content"
        android:layout_gravity="top|start"
        android:padding="6dp"
        android:textColor="#00FF00"
        android:textSize="12sp"
        android:background="#80000000"
        android:fontFamily="monospace"
        android:text="" />
</FrameLayout>
```

- [ ] **Step 3: Modify MainActivity.kt** to create a `StatsSink`, pass it to the client and decoder, and refresh the overlay ~1/s with a `Handler`.

Add imports:
```kotlin
import android.os.Handler
import android.os.Looper
import android.widget.TextView
import com.droppix.app.stats.StatsSink
```
Add fields (near the other private fields):
```kotlin
    private val stats = StatsSink()
    private val uiHandler = Handler(Looper.getMainLooper())
    private lateinit var overlay: TextView
    private val overlayTick = object : Runnable {
        override fun run() {
            overlay.text = String.format(
                "RTT %.0f ms  |  fps %.0f  |  decode %.0f ms",
                stats.rttMs, stats.fps, stats.decodeLagMs)
            uiHandler.postDelayed(this, 1000)
        }
    }
```
In `onCreate`, after `surfaceView = findViewById(R.id.surface)`:
```kotlin
        overlay = findViewById(R.id.overlay)
```
In `onResume`, start the overlay ticker (after `setSurfaceListener`):
```kotlin
        uiHandler.post(overlayTick)
```
In `onPause`, stop it (before `stopStreaming()`):
```kotlin
        uiHandler.removeCallbacks(overlayTick)
```
In `onConfig`, pass `stats` to the decoder:
```kotlin
                    decoder = try {
                        VideoDecoder(s, config.width, config.height, stats)
                    } catch (e: Exception) {
                        Log.w(TAG, "decoder create failed: ${e.message}"); null
                    }
```
In the `client.run(...)` call, pass `stats` (keep the default ping interval):
```kotlin
                    client.run(HOST, PORT, 1920, 1080,
                        resources.displayMetrics.densityDpi, listener, { running }, stats)
```

- [ ] **Step 4: Build (APK gate) + tests**

```
distrobox enter droppix-android -- bash -lc 'export ANDROID_SDK_ROOT=/home/Spinjitsudoomyt/android-sdk JAVA_HOME=$(dirname $(dirname $(readlink -f $(which java)))) GRADLE_USER_HOME=/home/Spinjitsudoomyt/.droppix-gradle; cd "/var/mnt/nas/Projects/Spacedesk for linux/android"; bash gradlew --no-daemon assembleDebug test'
```
Expected: `BUILD SUCCESSFUL`, APK produced, all unit tests (prior + RateMeter + TransportClientStats) pass. The decoder-lag/overlay behavior is verified on-device in Step 6.

- [ ] **Step 5: Commit**

```bash
git add android/app/src/main/java/com/droppix/app/decode/VideoDecoder.kt android/app/src/main/res/layout/activity_main.xml android/app/src/main/java/com/droppix/app/ui/MainActivity.kt
git commit -m "feat(android): decode-lag measurement + on-screen latency overlay"
```

- [ ] **Step 6: Operator on-device check.** Reinstall the APK (`adb install -r .../app-debug.apk`), `adb reverse tcp:27000 tcp:27000`, run the host streamer, launch the app. Expected: a green overlay in the corner showing live **RTT / fps / decode ms**, and the host terminal printing `stats:` lines. Record a baseline (e.g., "RTT ~Xms, fps ~Y, decode ~Zms, host encode ~W ms") in `docs/superpowers/specs/2026-06-23-phase1c-latency-baseline.md` — this is the data that drives tuning.

```bash
git add docs/superpowers/specs/2026-06-23-phase1c-latency-baseline.md
git commit -m "docs: phase 1c latency baseline measurements"
```

---

## Self-Review

**1. Coverage:** The chosen design (host encode/fps/size stats; tablet overlay with RTT, fps, decode lag) is fully covered: Task 1 = host stats, Task 2 = RTT + fps (with the PING/PONG reuse — no wire change), Task 3 = decode lag + overlay. Pure aggregators (`StatAccumulator`, `RateMeter`) are unit-tested; RTT/fps wiring is unit-tested via the loopback `TransportClientStatsTest` (pingIntervalMs=0 makes it fast); decode-lag + overlay are APK-build-gated and device-verified (Android runtime types).

**2. Placeholder scan:** No TBD/TODO; every code step is complete. The baseline-doc (Task 3 Step 6) is filled from the operator's real numbers, not a placeholder.

**3. Type consistency:** `StatsSink{rttMs,fps,decodeLagMs}` defined in Task 2, consumed by TransportClient (Task 2) and VideoDecoder + MainActivity (Task 3). `TransportClient.run(...stats, pingIntervalMs)` signature consistent between Task 2's definition/test and Task 3's MainActivity call. `VideoDecoder(surface,w,h,stats?)` adds an optional trailing param — existing call sites without it still compile; MainActivity passes `stats`. `RateMeter.mark/ratePerSec(nowMs)` consistent. Host `StatAccumulator.add/avg/peak/count/reset` consistent between header, test, and stream_daemon usage. PING body = 8-byte BE nanos on both send (longToBytes) and receive (bytesToLong); host echoes the body unchanged.
