/** Host format: 48000 Hz, s16le, stereo interleaved. */
export const AUDIO_RATE = 48000;
export const AUDIO_CHANNELS = 2;

/**
 * PCM playback. Prefers an AudioWorklet ring buffer (low latency, gapless);
 * falls back to aggregated AudioBuffer scheduling (~200ms buffers) if the
 * worklet module cannot load. No test chirps; volume via a shared GainNode.
 */
export class AudioPlayer {
  private ctx: AudioContext | null = null;
  private gain: GainNode | null = null;
  private node: AudioWorkletNode | null = null;
  private mode: "worklet" | "buffer" | "none" = "none";
  private muted = false;
  private ready = false;
  private packets = 0;
  /** Wire media clock (µs), advanced for every PCM chunk regardless of mute. */
  private wirePtsUs = 0;

  // buffer-mode state: aggregate small packets into larger scheduled buffers
  private pending: Float32Array[] = [];
  private pendingFrames = 0;
  private nextTime = 0;

  /** Kept for compat with callers; both paths work everywhere. */
  preferBuffer = false;

  /** Fires when the AudioContext state changes (e.g. suspended → running). */
  onStateChange: ((state: AudioContextState) => void) | null = null;
  private gestureHooked = false;

  get contextState(): AudioContextState | "none" {
    return this.ctx?.state ?? "none";
  }

  /** Test hook: force the suspended state the autoplay policy produces. */
  suspendForTest(): void {
    void this.ctx?.suspend();
  }

  async unlock(): Promise<void> {
    try {
      if (!this.ctx) {
        this.ctx = new AudioContext({ sampleRate: AUDIO_RATE });
        this.gain = this.ctx.createGain();
        this.gain.gain.value = 0.9;
        this.gain.connect(this.ctx.destination);
        this.ctx.onstatechange = () => {
          if (!this.ctx) return;
          // Re-arm the gesture hook whenever the context suspends
          // (autoplay policy, iOS route change, tab freeze, …).
          if (this.ctx.state === "suspended") this.hookGestureResume();
          this.onStateChange?.(this.ctx.state);
        };
      }
      if (this.ctx.state === "suspended") {
        // CRITICAL: do NOT await resume(). Under the autoplay policy the
        // promise stays pending until a user gesture, which would hang the
        // whole connect() flow. Fire it and arm a gesture fallback instead.
        void this.ctx.resume().catch(() => {});
        this.hookGestureResume();
      }

      if (this.mode === "none") {
        if (!this.preferBuffer) {
          try {
            await this.ctx.audioWorklet.addModule(new URL("./audio-worklet.js", import.meta.url));
            this.node = new AudioWorkletNode(this.ctx, "pcm-player", {
              numberOfInputs: 0,
              numberOfOutputs: 1,
              outputChannelCount: [2],
            });
            this.node.connect(this.gain!);
            this.mode = "worklet";
          } catch (e) {
            console.warn("AudioWorklet unavailable; using AudioBuffer fallback", e);
            this.node = null;
            this.mode = "buffer";
          }
        } else {
          this.mode = "buffer";
        }
      }
      this.nextTime = this.ctx.currentTime + 0.05;
      this.ready = true;
    } catch (e) {
      console.warn("audio init failed; video-only", e);
      this.ready = false;
      this.mode = "none";
    }
  }

  private hookGestureResume() {
    if (this.gestureHooked) return;
    this.gestureHooked = true;
    const resume = () => {
      void this.ctx?.resume().catch(() => {});
      if (this.ctx && this.ctx.state !== "suspended") {
        document.removeEventListener("pointerdown", resume, true);
        document.removeEventListener("keydown", resume, true);
        this.gestureHooked = false;
      }
    };
    document.addEventListener("pointerdown", resume, true);
    document.addEventListener("keydown", resume, true);
  }

  setMuted(m: boolean) {
    this.muted = m;
    // Mute is gain-only — keep feeding the player so the media clock and
    // lipsync stay continuous when the user unmutes.
    if (this.gain) this.gain.gain.value = m ? 0 : 0.9;
  }

  get packetCount() {
    return this.packets;
  }

  /**
   * Media presentation time (µs) for video sync. Based on wire audio chunks
   * (20 ms each). Subtracts worklet prebuffer so paint matches what is heard.
   */
  get mediaPtsUs(): number | null {
    if (!this.ready || this.wirePtsUs <= 0) return null;
    const lead = this.mode === "worklet" ? 60_000 : 40_000;
    return Math.max(0, this.wirePtsUs - lead);
  }

  submit(pcm: Uint8Array) {
    if (!this.ready || !this.ctx) return;
    if (pcm.length < 4) return;
    this.packets++;
    this.wirePtsUs += 20_000;

    const samples = new Float32Array(pcm.length / 2);
    const view = new DataView(pcm.buffer, pcm.byteOffset, pcm.byteLength);
    for (let i = 0; i < samples.length; i++) {
      samples[i] = view.getInt16(i * 2, true) / 32768;
    }

    if (this.mode === "worklet" && this.node) {
      this.node.port.postMessage({ type: "pcm", samples }, [samples.buffer]);
      return;
    }
    if (this.mode !== "buffer") return;

    this.pending.push(samples);
    this.pendingFrames += samples.length / AUDIO_CHANNELS;

    const now = this.ctx.currentTime;
    const queuedAhead = this.nextTime - now;
    const pendingSec = this.pendingFrames / AUDIO_RATE;
    // Flush in ~200ms blocks, or immediately when the schedule runs dry.
    if (pendingSec >= 0.2 || (queuedAhead < 0.12 && pendingSec >= 0.04)) {
      this.flushPending();
    }
  }

  private flushPending() {
    if (!this.ctx || !this.gain || this.pendingFrames < 1) return;
    const frames = this.pendingFrames;
    const buf = this.ctx.createBuffer(2, frames, AUDIO_RATE);
    const L = buf.getChannelData(0);
    const R = buf.getChannelData(1);
    let o = 0;
    for (const s of this.pending) {
      for (let i = 0; i + 1 < s.length; i += 2) {
        L[o] = s[i]!;
        R[o] = s[i + 1]!;
        o++;
      }
    }
    this.pending = [];
    this.pendingFrames = 0;

    const src = this.ctx.createBufferSource();
    src.buffer = buf;
    src.connect(this.gain);
    const now = this.ctx.currentTime;
    if (this.nextTime < now + 0.03) this.nextTime = now + 0.03;
    try {
      src.start(this.nextTime);
      this.nextTime += frames / AUDIO_RATE;
    } catch (e) {
      console.warn("audio schedule", e);
      this.nextTime = now + 0.05;
    }
  }

  close() {
    try {
      this.node?.disconnect();
      this.gain?.disconnect();
      void this.ctx?.close();
    } catch {
      /* ignore */
    }
    this.node = null;
    this.gain = null;
    this.ctx = null;
    this.mode = "none";
    this.ready = false;
    this.pending = [];
    this.pendingFrames = 0;
    this.nextTime = 0;
    this.packets = 0;
    this.wirePtsUs = 0;
  }
}
