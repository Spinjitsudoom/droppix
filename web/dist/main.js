// src/protocol.ts
var kProtocolVersion = 6;
var MsgType = {
  Hello: 1,
  Config: 2,
  Video: 3,
  Ping: 4,
  Pong: 5,
  Bye: 6,
  Input: 7,
  Orientation: 8,
  Audio: 9,
  Overlay: 10,
  Touch: 11,
  Scroll: 12,
  MouseButton: 13,
  Key: 14,
  Pen: 15
};
function putU16(v, out) {
  out.push(v >>> 8 & 255, v & 255);
}
function putU32(v, out) {
  out.push(v >>> 24 & 255, v >>> 16 & 255, v >>> 8 & 255, v & 255);
}
function getU32(b, outOff) {
  return (b[outOff] << 24 | b[outOff + 1] << 16 | b[outOff + 2] << 8 | b[outOff + 3]) >>> 0;
}
function getU64(b, o) {
  return BigInt(getU32(b, o)) << 32n | BigInt(getU32(b, o + 4));
}
function frameMessage(type, body) {
  const out = new Uint8Array(1 + body.length);
  out[0] = type;
  out.set(body, 1);
  return out;
}
function parseFrame(data) {
  const u = data instanceof Uint8Array ? data : new Uint8Array(data);
  if (u.length < 1) return null;
  return { type: u[0], body: u.subarray(1) };
}
function encodeHello(version, width, height, density, name, id, fps = 0, audioWanted = 0, orientationCode = 0, bitrateKbps = 0, wallCol = 0, wallRow = 0) {
  const nameBytes = new TextEncoder().encode(name);
  const idBytes = new TextEncoder().encode(id);
  const out = [];
  putU32(version, out);
  putU32(width, out);
  putU32(height, out);
  putU32(density, out);
  putU32(fps, out);
  out.push(audioWanted & 255, orientationCode & 255);
  putU32(bitrateKbps, out);
  if (version >= 6) {
    putU16(wallCol, out);
    putU16(wallRow, out);
  }
  putU16(nameBytes.length, out);
  for (const c of nameBytes) out.push(c);
  putU16(idBytes.length, out);
  for (const c of idBytes) out.push(c);
  return new Uint8Array(out);
}
function encodeTouch(contacts) {
  const n = Math.min(contacts.length, 10);
  const out = [n];
  for (let i = 0; i < n; i++) {
    const c = contacts[i];
    out.push(c.id & 255);
    putU16(c.x, out);
    putU16(c.y, out);
    putU16(c.pressure, out);
  }
  return new Uint8Array(out);
}
function encodeScroll(dx, dy, x, y) {
  const out = [];
  putU16(dx & 65535, out);
  putU16(dy & 65535, out);
  putU16(x, out);
  putU16(y, out);
  return new Uint8Array(out);
}
function encodeMouseButton(button, action, x, y) {
  const out = [button & 255, action & 255];
  putU16(x, out);
  putU16(y, out);
  return new Uint8Array(out);
}
function encodeKey(keycode, action) {
  const out = [];
  putU16(keycode, out);
  out.push(action & 255);
  return new Uint8Array(out);
}
function decodeConfig(body) {
  if (body.length < 12) return null;
  return {
    width: getU32(body, 0),
    height: getU32(body, 4),
    fps: getU32(body, 8),
    extradata: body.subarray(12)
  };
}
function decodeVideo(body) {
  if (body.length < 9) return null;
  return {
    ptsUs: getU64(body, 0),
    keyframe: body[8] !== 0,
    nal: body.subarray(9)
  };
}
function decodeOverlay(body) {
  return body.length > 0 ? body[0] : 0;
}

// src/transport.ts
var Transport = class {
  constructor(handlers) {
    this.handlers = handlers;
  }
  ws = null;
  pingTimer = null;
  connect(hello) {
    this.close();
    const proto = location.protocol === "https:" ? "wss:" : "ws:";
    const url = `${proto}//${location.host}/ws`;
    this.handlers.onStatus(`Connecting ${url}`);
    const ws = new WebSocket(url);
    ws.binaryType = "arraybuffer";
    this.ws = ws;
    ws.onopen = () => {
      if (this.ws !== ws) return;
      const body = encodeHello(
        kProtocolVersion,
        hello.width,
        hello.height,
        hello.density,
        hello.name,
        hello.id,
        hello.fps,
        hello.audioWanted,
        0,
        hello.bitrateKbps,
        hello.wallCol,
        hello.wallRow
      );
      ws.send(frameMessage(MsgType.Hello, body));
      this.handlers.onStatus("Connected - waiting for CONFIG");
      this.pingTimer = window.setInterval(() => {
        this.send(MsgType.Ping, new Uint8Array());
      }, 2e3);
    };
    ws.onmessage = (ev) => {
      if (this.ws !== ws) return;
      const parsed = parseFrame(ev.data);
      if (!parsed) return;
      switch (parsed.type) {
        case MsgType.Config: {
          const c = decodeConfig(parsed.body);
          if (c) this.handlers.onConfig(c.width, c.height, c.fps);
          break;
        }
        case MsgType.Video: {
          const v = decodeVideo(parsed.body);
          if (v) this.handlers.onVideo(v.ptsUs, v.keyframe, v.nal);
          break;
        }
        case MsgType.Audio:
          this.handlers.onAudio(parsed.body);
          break;
        case MsgType.Overlay:
          this.handlers.onOverlay(decodeOverlay(parsed.body) !== 0);
          break;
        case MsgType.Ping:
          this.send(MsgType.Pong, parsed.body);
          break;
        case MsgType.Pong:
          break;
        case MsgType.Bye:
          this.handlers.onClose("host bye");
          this.close();
          break;
        default:
          break;
      }
    };
    ws.onerror = () => {
      if (this.ws === ws) this.handlers.onStatus("WebSocket error");
    };
    ws.onclose = () => {
      if (this.ws !== ws) return;
      this.ws = null;
      this.clearPing();
      this.handlers.onClose("socket closed");
    };
  }
  send(type, body) {
    if (!this.ws || this.ws.readyState !== WebSocket.OPEN) return;
    this.ws.send(frameMessage(type, body));
  }
  close() {
    this.clearPing();
    const ws = this.ws;
    this.ws = null;
    if (ws) {
      try {
        if (ws.readyState === WebSocket.OPEN) {
          ws.send(frameMessage(MsgType.Bye, new Uint8Array()));
        }
      } catch {
      }
      try {
        ws.close();
      } catch {
      }
    }
  }
  clearPing() {
    if (this.pingTimer != null) {
      clearInterval(this.pingTimer);
      this.pingTimer = null;
    }
  }
};

// src/fit.ts
function contentBox(cw, ch, vw, vh, mode) {
  if (cw <= 0 || ch <= 0 || vw <= 0 || vh <= 0) return { x: 0, y: 0, w: cw, h: ch };
  if (mode === "stretch") return { x: 0, y: 0, w: cw, h: ch };
  const scale = mode === "cover" ? Math.max(cw / vw, ch / vh) : Math.min(cw / vw, ch / vh);
  const w = vw * scale;
  const h = vh * scale;
  return { x: (cw - w) / 2, y: (ch - h) / 2, w, h };
}
function normalizePointer(px, py, box, outsideOk) {
  const lx = px - box.x;
  const ly = py - box.y;
  if (!outsideOk && (lx < 0 || ly < 0 || lx > box.w || ly > box.h)) return null;
  const nx = Math.max(0, Math.min(1, box.w > 0 ? lx / box.w : 0));
  const ny = Math.max(0, Math.min(1, box.h > 0 ? ly / box.h : 0));
  return { x: Math.round(nx * 65535), y: Math.round(ny * 65535) };
}

// src/avc-codec.ts
function avcCodecString(nal) {
  const n = nal.length;
  for (let i = 0; i + 3 < n; i++) {
    if (nal[i] === 0 && nal[i + 1] === 0 && nal[i + 2] === 1) {
      const header = i + 3;
      if ((nal[header] & 31) === 7) {
        if (header + 3 >= n) return null;
        const profile = nal[header + 1];
        const constraints = nal[header + 2];
        const level = nal[header + 3];
        const hex = (b) => b.toString(16).toUpperCase().padStart(2, "0");
        return `avc1.${hex(profile)}${hex(constraints)}${hex(level)}`;
      }
    }
  }
  return null;
}

// src/decoder.ts
var VideoPipeline = class {
  constructor(canvas2, opts, onInfo) {
    this.canvas = canvas2;
    this.opts = opts;
    this.onInfo = onInfo;
  }
  decoder = null;
  configured = false;
  closed = false;
  dropUntilKey = false;
  vw = 0;
  vh = 0;
  frames = 0;
  painted = 0;
  received = 0;
  lastFpsAt = performance.now();
  fps = 0;
  fit = "contain";
  lastError = "";
  pending = [];
  raf = 0;
  /** Returns media PTS in microseconds, or null to paint ASAP. */
  getClock = null;
  get size() {
    return { w: this.vw, h: this.vh };
  }
  get currentFps() {
    return this.fps;
  }
  get hasPainted() {
    return this.painted > 0;
  }
  get stats() {
    return { received: this.received, painted: this.painted, fps: this.fps, lastError: this.lastError };
  }
  setFit(mode) {
    this.fit = mode;
  }
  setAdjust(flip, brightness, contrast) {
    this.opts = { flip, brightness, contrast };
  }
  /** Master clock for presentation (typically audio wire PTS). */
  setClock(fn) {
    this.getClock = fn;
  }
  submit(keyframe, nal, ptsUs) {
    this.closed = false;
    this.received++;
    if (typeof VideoDecoder === "undefined") {
      this.lastError = "WebCodecs VideoDecoder missing - use Chromium";
      this.onInfo?.(this.lastError);
      return;
    }
    if (!this.decoder || this.decoder.state === "closed") {
      this.decoder = new VideoDecoder({
        output: (frame) => this.onDecoded(frame),
        error: (e) => {
          this.lastError = String(e?.message || e);
          this.onInfo?.(`VideoDecoder: ${this.lastError}`);
          this.configured = false;
          try {
            this.decoder?.close();
          } catch {
          }
          this.decoder = null;
        }
      });
      this.configured = false;
      this.dropUntilKey = false;
    }
    if (!this.configured) {
      if (!keyframe) return;
      const codec = avcCodecString(nal) ?? "avc1.42E01F";
      this.decoder.configure({
        codec,
        optimizeForLatency: true
      });
      this.configured = true;
    }
    if (keyframe) {
      this.dropUntilKey = false;
    } else if (this.dropUntilKey) {
      return;
    }
    if (this.decoder.decodeQueueSize > 20 && !keyframe) {
      this.dropUntilKey = true;
      return;
    }
    const chunk = new EncodedVideoChunk({
      type: keyframe ? "key" : "delta",
      timestamp: Number(ptsUs),
      data: nal
    });
    try {
      this.decoder.decode(chunk);
    } catch (e) {
      this.lastError = String(e?.message || e);
      this.onInfo?.(`decode: ${this.lastError}`);
      this.configured = false;
      this.dropUntilKey = true;
    }
  }
  onDecoded(frame) {
    if (this.closed) {
      frame.close();
      return;
    }
    this.pending.push(frame);
    while (this.pending.length > 12) {
      this.pending.shift().close();
    }
    this.schedulePaint();
  }
  schedulePaint() {
    if (this.raf) return;
    this.raf = requestAnimationFrame(() => {
      this.raf = 0;
      this.paintDue();
    });
  }
  paintDue() {
    if (this.closed) return;
    const clock = this.getClock?.() ?? null;
    if (clock == null) {
      while (this.pending.length > 1) this.pending.shift().close();
      const f2 = this.pending.shift();
      if (f2) this.draw(f2);
      return;
    }
    let best = -1;
    for (let i = 0; i < this.pending.length; i++) {
      if (this.pending[i].timestamp <= clock) best = i;
    }
    if (best < 0) {
      this.schedulePaint();
      return;
    }
    for (let i = 0; i < best; i++) this.pending.shift().close();
    const f = this.pending.shift();
    if (f) this.draw(f);
    if (this.pending.length) this.schedulePaint();
  }
  draw(frame) {
    if (this.closed) {
      frame.close();
      return;
    }
    this.vw = frame.displayWidth;
    this.vh = frame.displayHeight;
    const ctx = this.canvas.getContext("2d");
    if (!ctx) {
      frame.close();
      return;
    }
    const dpr = window.devicePixelRatio || 1;
    const cssW = this.canvas.clientWidth;
    const cssH = this.canvas.clientHeight;
    const pw = Math.max(1, Math.round(cssW * dpr));
    const ph = Math.max(1, Math.round(cssH * dpr));
    if (this.canvas.width !== pw || this.canvas.height !== ph) {
      this.canvas.width = pw;
      this.canvas.height = ph;
    }
    const box = contentBox(pw, ph, this.vw, this.vh, this.fit);
    ctx.save();
    ctx.fillStyle = "#000";
    ctx.fillRect(0, 0, pw, ph);
    ctx.beginPath();
    ctx.rect(box.x, box.y, box.w, box.h);
    ctx.clip();
    ctx.filter = `brightness(${this.opts.brightness}) contrast(${this.opts.contrast})`;
    if (this.opts.flip) {
      ctx.translate(box.x + box.w, box.y);
      ctx.scale(-1, 1);
      ctx.drawImage(frame, 0, 0, box.w, box.h);
    } else {
      ctx.drawImage(frame, box.x, box.y, box.w, box.h);
    }
    ctx.restore();
    frame.close();
    this.frames++;
    this.painted++;
    const now = performance.now();
    if (now - this.lastFpsAt >= 1e3) {
      this.fps = this.frames;
      this.frames = 0;
      this.lastFpsAt = now;
    }
  }
  close() {
    this.closed = true;
    if (this.raf) cancelAnimationFrame(this.raf);
    this.raf = 0;
    for (const f of this.pending) f.close();
    this.pending = [];
    try {
      this.decoder?.close();
    } catch {
    }
    this.decoder = null;
    this.configured = false;
    this.dropUntilKey = false;
    this.painted = 0;
    this.received = 0;
    const ctx = this.canvas.getContext("2d");
    if (ctx) {
      ctx.save();
      ctx.setTransform(1, 0, 0, 1, 0, 0);
      ctx.filter = "none";
      ctx.fillStyle = "#000";
      ctx.fillRect(0, 0, this.canvas.width || 1, this.canvas.height || 1);
      ctx.restore();
    }
  }
};

// src/audio.ts
var AUDIO_RATE = 48e3;
var AUDIO_CHANNELS = 2;
var AudioPlayer = class {
  ctx = null;
  gain = null;
  node = null;
  mode = "none";
  muted = false;
  ready = false;
  packets = 0;
  /** Wire media clock (µs), advanced for every PCM chunk regardless of mute. */
  wirePtsUs = 0;
  // buffer-mode state: aggregate small packets into larger scheduled buffers
  pending = [];
  pendingFrames = 0;
  nextTime = 0;
  /** Kept for compat with callers; both paths work everywhere. */
  preferBuffer = false;
  /** Fires when the AudioContext state changes (e.g. suspended → running). */
  onStateChange = null;
  gestureHooked = false;
  get contextState() {
    return this.ctx?.state ?? "none";
  }
  /** Test hook: force the suspended state the autoplay policy produces. */
  suspendForTest() {
    void this.ctx?.suspend();
  }
  async unlock() {
    try {
      if (!this.ctx) {
        this.ctx = new AudioContext({ sampleRate: AUDIO_RATE });
        this.gain = this.ctx.createGain();
        this.gain.gain.value = 0.9;
        this.gain.connect(this.ctx.destination);
        this.ctx.onstatechange = () => {
          if (!this.ctx) return;
          if (this.ctx.state === "suspended") this.hookGestureResume();
          this.onStateChange?.(this.ctx.state);
        };
      }
      if (this.ctx.state === "suspended") {
        void this.ctx.resume().catch(() => {
        });
        this.hookGestureResume();
      }
      if (this.mode === "none") {
        if (!this.preferBuffer) {
          try {
            await this.ctx.audioWorklet.addModule(new URL("./audio-worklet.js", import.meta.url));
            this.node = new AudioWorkletNode(this.ctx, "pcm-player", {
              numberOfInputs: 0,
              numberOfOutputs: 1,
              outputChannelCount: [2]
            });
            this.node.connect(this.gain);
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
  hookGestureResume() {
    if (this.gestureHooked) return;
    this.gestureHooked = true;
    const resume = () => {
      void this.ctx?.resume().catch(() => {
      });
      if (this.ctx && this.ctx.state !== "suspended") {
        document.removeEventListener("pointerdown", resume, true);
        document.removeEventListener("keydown", resume, true);
        this.gestureHooked = false;
      }
    };
    document.addEventListener("pointerdown", resume, true);
    document.addEventListener("keydown", resume, true);
  }
  setMuted(m) {
    this.muted = m;
    if (this.gain) this.gain.gain.value = m ? 0 : 0.9;
  }
  get packetCount() {
    return this.packets;
  }
  /**
   * Media presentation time (µs) for video sync. Based on wire audio chunks
   * (20 ms each). Subtracts worklet prebuffer so paint matches what is heard.
   */
  get mediaPtsUs() {
    if (!this.ready || this.wirePtsUs <= 0) return null;
    const lead = this.mode === "worklet" ? 6e4 : 4e4;
    return Math.max(0, this.wirePtsUs - lead);
  }
  submit(pcm) {
    if (!this.ready || !this.ctx) return;
    if (pcm.length < 4) return;
    this.packets++;
    this.wirePtsUs += 2e4;
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
    if (pendingSec >= 0.2 || queuedAhead < 0.12 && pendingSec >= 0.04) {
      this.flushPending();
    }
  }
  flushPending() {
    if (!this.ctx || !this.gain || this.pendingFrames < 1) return;
    const frames = this.pendingFrames;
    const buf = this.ctx.createBuffer(2, frames, AUDIO_RATE);
    const L = buf.getChannelData(0);
    const R = buf.getChannelData(1);
    let o = 0;
    for (const s of this.pending) {
      for (let i = 0; i + 1 < s.length; i += 2) {
        L[o] = s[i];
        R[o] = s[i + 1];
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
};

// src/keymap.ts
var CODE_TO_EVDEV = {
  Escape: 1,
  Digit1: 2,
  Digit2: 3,
  Digit3: 4,
  Digit4: 5,
  Digit5: 6,
  Digit6: 7,
  Digit7: 8,
  Digit8: 9,
  Digit9: 10,
  Digit0: 11,
  Minus: 12,
  Equal: 13,
  Backspace: 14,
  Tab: 15,
  KeyQ: 16,
  KeyW: 17,
  KeyE: 18,
  KeyR: 19,
  KeyT: 20,
  KeyY: 21,
  KeyU: 22,
  KeyI: 23,
  KeyO: 24,
  KeyP: 25,
  BracketLeft: 26,
  BracketRight: 27,
  Enter: 28,
  ControlLeft: 29,
  KeyA: 30,
  KeyS: 31,
  KeyD: 32,
  KeyF: 33,
  KeyG: 34,
  KeyH: 35,
  KeyJ: 36,
  KeyK: 37,
  KeyL: 38,
  Semicolon: 39,
  Quote: 40,
  Backquote: 41,
  ShiftLeft: 42,
  Backslash: 43,
  KeyZ: 44,
  KeyX: 45,
  KeyC: 46,
  KeyV: 47,
  KeyB: 48,
  KeyN: 49,
  KeyM: 50,
  Comma: 51,
  Period: 52,
  Slash: 53,
  ShiftRight: 54,
  AltLeft: 56,
  Space: 57,
  CapsLock: 58,
  F1: 59,
  F2: 60,
  F3: 61,
  F4: 62,
  F5: 63,
  F6: 64,
  F7: 65,
  F8: 66,
  F9: 67,
  F10: 68,
  F11: 87,
  F12: 88,
  ControlRight: 97,
  AltRight: 100,
  Home: 102,
  ArrowUp: 103,
  PageUp: 104,
  ArrowLeft: 105,
  ArrowRight: 106,
  End: 107,
  ArrowDown: 108,
  PageDown: 109,
  Insert: 110,
  Delete: 111,
  MetaLeft: 125,
  MetaRight: 126
};
function codeToEvdev(code) {
  return CODE_TO_EVDEV[code] ?? 0;
}

// src/input.ts
var InputBinder = class {
  constructor(canvas2, send) {
    this.canvas = canvas2;
    this.send = send;
    canvas2.addEventListener("pointerdown", (e) => this.onPointerDown(e));
    canvas2.addEventListener("pointermove", (e) => this.onPointerMove(e));
    canvas2.addEventListener("pointerup", (e) => this.onPointerUp(e));
    canvas2.addEventListener("pointercancel", (e) => this.onPointerUp(e));
    canvas2.addEventListener("contextmenu", (e) => e.preventDefault());
    canvas2.addEventListener("wheel", (e) => this.onWheel(e), { passive: false });
    window.addEventListener("keydown", (e) => this.onKey(e, 1));
    window.addEventListener("keyup", (e) => this.onKey(e, 0));
  }
  pointers = /* @__PURE__ */ new Map();
  videoW = 1280;
  videoH = 720;
  fit = "contain";
  lastNorm = { x: 32767, y: 32767 };
  setVideoSize(w, h) {
    this.videoW = w;
    this.videoH = h;
  }
  setFit(mode) {
    this.fit = mode;
  }
  localXY(e) {
    const r = this.canvas.getBoundingClientRect();
    return { x: e.clientX - r.left, y: e.clientY - r.top };
  }
  box() {
    const r = this.canvas.getBoundingClientRect();
    return contentBox(r.width, r.height, this.videoW, this.videoH, this.fit);
  }
  normFromEvent(e, outsideOk) {
    const { x, y } = this.localXY(e);
    return normalizePointer(x, y, this.box(), outsideOk);
  }
  flushTouch() {
    const contacts = [...this.pointers.values()];
    this.send(MsgType.Touch, encodeTouch(contacts));
  }
  onPointerDown(e) {
    this.canvas.setPointerCapture(e.pointerId);
    const n = this.normFromEvent(e, this.fit !== "contain");
    if (!n) return;
    this.lastNorm = n;
    if (e.button === 2) {
      this.send(MsgType.MouseButton, encodeMouseButton(1, 1, n.x, n.y));
      return;
    }
    if (e.button === 1) {
      this.send(MsgType.MouseButton, encodeMouseButton(2, 1, n.x, n.y));
      return;
    }
    if (e.button !== 0) return;
    const pressure = Math.round(Math.min(1, Math.max(0, e.pressure || 1)) * 1023);
    this.pointers.set(e.pointerId, { id: e.pointerId & 255, x: n.x, y: n.y, pressure });
    this.flushTouch();
  }
  onPointerMove(e) {
    if (!this.pointers.has(e.pointerId)) return;
    const n = this.normFromEvent(e, true);
    if (!n) return;
    this.lastNorm = n;
    const pressure = Math.round(Math.min(1, Math.max(0, e.pressure || 1)) * 1023);
    this.pointers.set(e.pointerId, { id: e.pointerId & 255, x: n.x, y: n.y, pressure });
    this.flushTouch();
  }
  onPointerUp(e) {
    const n = this.normFromEvent(e, true) ?? this.lastNorm;
    if (e.button === 2) {
      this.send(MsgType.MouseButton, encodeMouseButton(1, 0, n.x, n.y));
    } else if (e.button === 1) {
      this.send(MsgType.MouseButton, encodeMouseButton(2, 0, n.x, n.y));
    }
    if (this.pointers.delete(e.pointerId)) this.flushTouch();
  }
  onWheel(e) {
    e.preventDefault();
    const r = this.canvas.getBoundingClientRect();
    const n = normalizePointer(e.clientX - r.left, e.clientY - r.top, this.box(), true) ?? this.lastNorm;
    const dy = Math.round(-e.deltaY / 120) || (e.deltaY < 0 ? 1 : e.deltaY > 0 ? -1 : 0);
    const dx = Math.round(e.deltaX / 120) || (e.deltaX > 0 ? 1 : e.deltaX < 0 ? -1 : 0);
    if (dx === 0 && dy === 0) return;
    this.send(MsgType.Scroll, encodeScroll(dx, dy, n.x, n.y));
  }
  onKey(e, action) {
    if (e.key === "F" || e.key === "f") return;
    const code = codeToEvdev(e.code);
    if (!code) return;
    e.preventDefault();
    this.send(MsgType.Key, encodeKey(code, action));
  }
};

// src/settings.ts
var KEY = "droppix.web.settings.v1";
function randomId() {
  const a = new Uint8Array(8);
  crypto.getRandomValues(a);
  return Array.from(a, (b) => b.toString(16).padStart(2, "0")).join("");
}
function loadSettings() {
  const defaults = {
    name: "Web PWA",
    id: randomId(),
    fps: 30,
    bitrateKbps: 8e3,
    audio: true,
    fit: "contain",
    flip: false,
    brightness: 1,
    contrast: 1,
    wallCol: 0,
    wallRow: 0,
    theme: "dark"
  };
  try {
    const raw = localStorage.getItem(KEY);
    if (!raw) {
      saveSettings(defaults);
      return defaults;
    }
    return { ...defaults, ...JSON.parse(raw) };
  } catch {
    return defaults;
  }
}
function saveSettings(s) {
  localStorage.setItem(KEY, JSON.stringify(s));
}

// src/fullscreen.ts
async function enterFullscreen(el) {
  if (!document.fullscreenElement) await el.requestFullscreen?.();
}
async function exitFullscreen() {
  if (document.fullscreenElement) await document.exitFullscreen?.();
}
function toggleFullscreen(el) {
  if (document.fullscreenElement) void exitFullscreen();
  else void enterFullscreen(el);
}

// src/mock-overlay.ts
var MockOverlay = class {
  constructor(_stage, layer, logEl, backdrop, canvas2) {
    this.canvas = canvas2;
    this.layer = layer;
    this.logEl = logEl;
    this.backdrop = backdrop;
    this.showIdle();
  }
  layer;
  logEl;
  backdrop;
  pollTimer = 0;
  seenMarks = /* @__PURE__ */ new Set();
  videoW = 1280;
  videoH = 720;
  fit = "contain";
  connected = false;
  /** Idle / disconnected: black stage, no mock chrome. */
  showIdle() {
    this.connected = false;
    this.stopServerMarkPoll();
    this.backdrop.hidden = true;
    this.backdrop.classList.add("is-hidden");
    this.logEl.hidden = true;
    this.logEl.textContent = "";
    this.layer.hidden = true;
    this.layer.replaceChildren();
    this.seenMarks.clear();
    this.clearCanvas();
  }
  /** Connected: only server mark layer (no local preview). */
  showConnected() {
    this.connected = true;
    this.backdrop.hidden = true;
    this.backdrop.classList.add("is-hidden");
    this.logEl.hidden = true;
    this.layer.hidden = false;
  }
  setVideoSize(w, h) {
    this.videoW = w;
    this.videoH = h;
  }
  setFit(mode) {
    this.fit = mode;
  }
  markVideoAlive() {
  }
  startServerMarkPoll() {
    this.stopServerMarkPoll();
    this.showConnected();
    void fetch("./debug/server-marks?clear=1").catch(() => {
    });
    this.pollTimer = window.setInterval(() => void this.pullMarks(), 150);
  }
  stopServerMarkPoll() {
    if (this.pollTimer) clearInterval(this.pollTimer);
    this.pollTimer = 0;
  }
  /** Kept for API compat; unused in idle-clean mode. */
  note(_msg) {
  }
  showClick(_x, _y, _label) {
  }
  clearCanvas() {
    const c = this.canvas;
    const ctx = c.getContext("2d");
    if (!ctx) return;
    ctx.save();
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.fillStyle = "#000";
    ctx.fillRect(0, 0, c.width || 1, c.height || 1);
    ctx.restore();
  }
  async pullMarks() {
    if (!this.connected) return;
    try {
      const r = await fetch("./debug/server-marks", { cache: "no-store" });
      if (!r.ok) return;
      const j = await r.json();
      for (const m of j.marks || []) {
        if (m.x == null || m.y == null) continue;
        if (m.kind === "touch-up" || m.kind === "mouse-up" || m.kind === "scroll" || m.kind === "key") {
          continue;
        }
        const id = `${m.t}-${m.kind}-${m.x}-${m.y}`;
        if (this.seenMarks.has(id)) continue;
        this.seenMarks.add(id);
        const css = this.videoToCss(m.x, m.y);
        if (!css) continue;
        this.spawnMark(css.x, css.y, `SRV ${m.kind} ${m.x},${m.y}`);
      }
    } catch {
    }
  }
  videoToCss(vx, vy) {
    const r = this.canvas.getBoundingClientRect();
    if (r.width < 1 || r.height < 1) return null;
    const box = contentBox(r.width, r.height, this.videoW, this.videoH, this.fit);
    return {
      x: box.x + vx / Math.max(1, this.videoW - 1) * box.w,
      y: box.y + vy / Math.max(1, this.videoH - 1) * box.h
    };
  }
  spawnMark(x, y, label) {
    const mark = document.createElement("div");
    mark.className = "click-mark click-mark-srv";
    mark.style.left = `${x}px`;
    mark.style.top = `${y}px`;
    mark.innerHTML = `<span class="click-ring"></span><span class="click-label">${label}</span>`;
    this.layer.appendChild(mark);
    window.setTimeout(() => mark.remove(), 1600);
  }
  dispose() {
    this.showIdle();
  }
};

// src/theme.ts
function nextTheme(t) {
  return t === "dark" ? "light" : "dark";
}
function applyTheme(t) {
  document.documentElement.setAttribute("data-theme", t);
}
function initTheme() {
  const t = loadSettings().theme;
  applyTheme(t);
  return t;
}
function setTheme(t) {
  applyTheme(t);
  const s = loadSettings();
  s.theme = t;
  saveSettings(s);
}

// src/pin.ts
function normalizePin(raw) {
  return raw.replace(/\D/g, "").slice(0, 6);
}
function pinComplete(code) {
  return /^\d{6}$/.test(code);
}
function pinMatches(entered, served) {
  return normalizePin(entered) === normalizePin(served);
}

// src/connect-view.ts
var ConnectView = class {
  constructor(onConnect) {
    this.onConnect = onConnect;
    this.wrap = document.getElementById("pin");
    this.inputs = [...this.wrap.querySelectorAll("input")];
    this.btn = document.getElementById("btn-connect");
    this.statusEl = document.getElementById("c-status");
    this.inputs.forEach((inp, i) => {
      inp.addEventListener("input", () => {
        inp.value = normalizePin(inp.value).slice(0, 1);
        inp.classList.toggle("filled", !!inp.value);
        this.wrap.classList.remove("err");
        if (inp.value && i < this.inputs.length - 1) this.inputs[i + 1].focus();
        this.sync();
      });
      inp.addEventListener("keydown", (e) => {
        if (e.key === "Backspace" && !inp.value && i > 0) this.inputs[i - 1].focus();
      });
    });
    this.btn.addEventListener("click", () => {
      if (!this.btn.disabled) this.onConnect(this.value());
    });
    this.sync();
  }
  inputs;
  btn;
  wrap;
  statusEl;
  value() {
    return normalizePin(this.inputs.map((i) => i.value).join(""));
  }
  sync() {
    this.btn.disabled = !pinComplete(this.value());
  }
  showError(msg) {
    this.wrap.classList.add("err");
    this.statusEl.textContent = msg;
  }
  setStatus(msg) {
    this.statusEl.textContent = msg;
  }
  reset() {
    this.inputs.forEach((i) => {
      i.value = "";
      i.classList.remove("filled");
    });
    this.wrap.classList.remove("err");
    this.sync();
  }
  focus() {
    this.inputs[0]?.focus();
  }
};

// src/session-controls.ts
var IDLE_MS = 2500;
var SessionControls = class {
  app;
  stage;
  controls;
  hideTimer = null;
  constructor(opts) {
    this.app = document.getElementById("app");
    this.stage = document.getElementById("stage");
    this.controls = document.getElementById("controls");
    const bind = (act, fn) => {
      this.controls.querySelector(`[data-act="${act}"]`)?.addEventListener("click", fn);
    };
    bind("fit", opts.onFit);
    bind("mute", opts.onMute);
    bind("hud", opts.onHud);
    bind("settings", opts.onSettings);
    bind("fullscreen", opts.onFullscreen);
    bind("disconnect", opts.onDisconnect);
    this.stage.addEventListener("pointermove", () => this.show());
    this.stage.addEventListener("pointerdown", () => this.show());
  }
  /** Clears `.hidden` and (re)arms the auto-hide timer. */
  show() {
    this.controls.classList.remove("hidden");
    if (this.hideTimer !== null) window.clearTimeout(this.hideTimer);
    this.hideTimer = window.setTimeout(() => {
      if (this.app.dataset.view === "session") this.controls.classList.add("hidden");
    }, IDLE_MS);
  }
};

// src/settings-drawer.ts
function intOrZero(v) {
  const n = parseInt(v, 10);
  return Number.isFinite(n) && n >= 0 ? n : 0;
}
function floatOr(v, fallback) {
  const n = parseFloat(v);
  return Number.isFinite(n) ? n : fallback;
}
var SettingsDrawer = class {
  constructor(onChange) {
    this.onChange = onChange;
    this.seed();
    this.quality.addEventListener("change", () => this.commit());
    this.fps.addEventListener("change", () => this.commit());
    this.audio.addEventListener("change", () => this.commit());
    this.flip.addEventListener("change", () => this.commit());
    this.brightness.addEventListener("input", () => this.commit());
    this.contrast.addEventListener("input", () => this.commit());
    this.wallCol.addEventListener("input", () => this.commit());
    this.wallRow.addEventListener("input", () => this.commit());
    this.name.addEventListener("input", () => this.commit());
    this.bindSeg(this.fitSeg, (v) => {
      this.fit = v;
      this.commit();
    });
    this.bindSeg(this.themeSeg, (v) => {
      this.theme = v;
      setTheme(this.theme);
      this.commit();
    });
    this.scrim.addEventListener("click", () => this.close());
    this.closeBtn.addEventListener("click", () => this.close());
    window.addEventListener("keydown", (e) => {
      if (e.key === "Escape" && this.app.classList.contains("drawer-open")) this.close();
    });
  }
  app = document.getElementById("app");
  scrim = document.getElementById("scrim");
  drawer = document.getElementById("drawer");
  closeBtn = document.getElementById("drawer-close");
  quality = document.getElementById("set-quality");
  fps = document.getElementById("set-fps");
  audio = document.getElementById("set-audio");
  fitSeg = document.getElementById("set-fit");
  flip = document.getElementById("set-flip");
  brightness = document.getElementById("set-brightness");
  contrast = document.getElementById("set-contrast");
  wallCol = document.getElementById("set-wall-col");
  wallRow = document.getElementById("set-wall-row");
  name = document.getElementById("set-name");
  themeSeg = document.getElementById("set-theme");
  fit = "contain";
  theme = "dark";
  /**
   * Reads current persisted settings into every control (values, checkboxes,
   * and segmented/`.seg` active states). Called at construction and again at
   * the start of every `open()` so a control-bar change (mute/fit) made
   * while the drawer was closed is reflected, not clobbered by a stale cache.
   */
  seed() {
    const s = loadSettings();
    this.quality.value = String(s.bitrateKbps);
    this.fps.value = String(s.fps);
    this.audio.checked = s.audio;
    this.flip.checked = s.flip;
    this.brightness.value = String(s.brightness);
    this.contrast.value = String(s.contrast);
    this.wallCol.value = String(s.wallCol);
    this.wallRow.value = String(s.wallRow);
    this.name.value = s.name;
    this.fit = s.fit;
    this.theme = s.theme;
    this.setSeg(this.fitSeg, this.fit);
    this.setSeg(this.themeSeg, this.theme);
  }
  setSeg(el, value) {
    el.querySelectorAll(".seg-btn").forEach((b) => {
      const active = b.dataset.value === value;
      b.classList.toggle("is-active", active);
      b.setAttribute("aria-pressed", String(active));
    });
  }
  bindSeg(el, onPick) {
    el.querySelectorAll(".seg-btn").forEach((b) => {
      b.addEventListener("click", () => {
        const value = b.dataset.value;
        this.setSeg(el, value);
        onPick(value);
      });
    });
  }
  /** Reads every control, persists the merged settings, and notifies the caller. */
  commit() {
    const prev = loadSettings();
    const s = {
      ...prev,
      bitrateKbps: parseInt(this.quality.value, 10) || prev.bitrateKbps,
      fps: parseInt(this.fps.value, 10) || prev.fps,
      audio: this.audio.checked,
      fit: this.fit,
      flip: this.flip.checked,
      brightness: floatOr(this.brightness.value, 1),
      contrast: floatOr(this.contrast.value, 1),
      wallCol: intOrZero(this.wallCol.value),
      wallRow: intOrZero(this.wallRow.value),
      name: this.name.value,
      theme: this.theme
    };
    saveSettings(s);
    this.onChange(s);
  }
  open() {
    this.seed();
    this.scrim.hidden = false;
    this.drawer.hidden = false;
    this.app.classList.add("drawer-open");
  }
  close() {
    this.app.classList.remove("drawer-open");
  }
};

// src/main.ts
var canvas = document.getElementById("video");
var stage = document.getElementById("stage");
var app = document.getElementById("app");
var statusEl = document.getElementById("status-pill");
var btnTheme = document.getElementById("btn-theme");
var btnSettings = document.getElementById("btn-settings");
var btnInstall = document.getElementById("btn-install");
var hud = document.getElementById("hud");
var clickLayer = document.getElementById("click-layer");
var mockLog = document.getElementById("mock-log");
var mockBackdrop = document.getElementById("mock-backdrop");
var mockBadge = document.getElementById("mock-badge");
var settings = loadSettings();
var theme = initTheme();
var mock = new MockOverlay(stage, clickLayer, mockLog, mockBackdrop, canvas);
var video = new VideoPipeline(canvas, {
  flip: settings.flip,
  brightness: settings.brightness,
  contrast: settings.contrast
});
var audio = new AudioPlayer();
video.setClock(() => audio.mediaPtsUs);
video.setFit(settings.fit);
mock.setFit(settings.fit);
var transport = null;
var input = null;
var deferredPrompt = null;
var showHud = false;
var bytesIn = 0;
var lastBytesAt = performance.now();
var kbps = 0;
var isMock = false;
var burnIn = false;
var pairingCode = "------";
function setStatus(s) {
  statusEl.textContent = s;
  statusEl.hidden = false;
}
var connectView = new ConnectView((code) => {
  void tryConnect(code);
});
async function tryConnect(code) {
  if (!isMock && !pinMatches(code, pairingCode)) {
    connectView.showError("That code doesn't match your PC \u2014 check the screen");
    return;
  }
  app.dataset.view = "session";
  controls.show();
  await connect();
}
async function loadConfig() {
  try {
    const r = await fetch("./config.json", { cache: "no-store" });
    if (!r.ok) throw new Error(String(r.status));
    const j = await r.json();
    pairingCode = j.pairingCode ?? "------";
    isMock = !!j.mock;
    burnIn = !!j.burnIn;
    mock.showIdle();
    if (isMock) {
      mockBadge.hidden = false;
      settings.audio = false;
      saveSettings(settings);
      app.dataset.view = "session";
      controls.show();
      setStatus("Ready - Connect for server video + audio");
      if (typeof VideoDecoder === "undefined") {
        setStatus("WebCodecs VideoDecoder missing - use Chromium");
      } else {
        window.setTimeout(() => {
          void connect();
        }, 300);
      }
    } else {
      mockBadge.hidden = true;
      setStatus("Confirm PIN matches the PC, then Connect");
    }
  } catch (e) {
    pairingCode = "------";
    const msg = `Can't reach the PC \u2014 is droppix serving with --web? (${e})`;
    setStatus(msg);
    connectView.setStatus(msg);
  }
}
function wireTransport() {
  transport?.close();
  transport = new Transport({
    onStatus: setStatus,
    onClose: (r) => {
      setStatus(`Disconnected: ${r}`);
      transport = null;
      connecting = false;
      hud.hidden = true;
      video.close();
      audio.close();
      mock.showIdle();
      app.dataset.view = "connect";
      connectView.reset();
      connectView.setStatus(`Disconnected: ${r}`);
    },
    onConfig: (w, h) => {
      const audioHint = audio.contextState === "suspended" ? " - tap for audio" : "";
      setStatus(`Streaming ${w}x${h}${audioHint}`);
      input?.setVideoSize(w, h);
      mock.setVideoSize(w, h);
      video.setAdjust(settings.flip, settings.brightness, settings.contrast);
    },
    onVideo: (pts, key, nal) => {
      bytesIn += nal.length;
      video.submit(key, nal, pts);
      const now = performance.now();
      if (now - lastBytesAt >= 1e3) {
        kbps = Math.round(bytesIn * 8 / 1e3);
        bytesIn = 0;
        lastBytesAt = now;
        if (showHud) {
          hud.hidden = false;
          hud.textContent = `${video.currentFps} fps \xB7 ${kbps} kbps`;
        }
      }
    },
    onAudio: (pcm) => audio.submit(pcm),
    onOverlay: (show) => {
      showHud = show;
      hud.hidden = !show;
    }
  });
  input ??= new InputBinder(canvas, (type, body) => transport?.send(type, body));
  input.setFit(settings.fit);
}
var connecting = false;
async function connect() {
  if (connecting || transport) return;
  connecting = true;
  try {
    settings = loadSettings();
    await audio.unlock();
    audio.setMuted(!settings.audio);
    audio.onStateChange = (s) => {
      if (s === "running" && statusEl.textContent?.includes("tap for audio")) {
        setStatus(statusEl.textContent.replace(" - tap for audio", ""));
      }
    };
    wireTransport();
    const w = isMock ? 1280 : Math.max(640, Math.round(canvas.clientWidth * (window.devicePixelRatio || 1)));
    const h = isMock ? 720 : Math.max(360, Math.round(canvas.clientHeight * (window.devicePixelRatio || 1)));
    transport.connect({
      width: w,
      height: h,
      density: 160,
      name: settings.name,
      id: settings.id,
      fps: settings.fps,
      audioWanted: settings.audio ? 1 : 0,
      bitrateKbps: settings.bitrateKbps,
      wallCol: settings.wallCol,
      wallRow: settings.wallRow
    });
    canvas.focus();
    if (isMock && !burnIn) mock.startServerMarkPoll();
  } catch (e) {
    setStatus(`Connect failed: ${e instanceof Error ? e.message : String(e)}`);
    transport?.close();
    transport = null;
  } finally {
    connecting = false;
  }
}
function disconnect() {
  transport?.close();
  transport = null;
  video.close();
  audio.close();
  mock.showIdle();
  hud.hidden = true;
  setStatus("Disconnected");
  app.dataset.view = "connect";
  connectView.reset();
}
var drawer = new SettingsDrawer((s) => {
  settings = s;
  theme = s.theme;
  video.setAdjust(s.flip, s.brightness, s.contrast);
  video.setFit(s.fit);
  input?.setFit(s.fit);
  mock.setFit(s.fit);
  audio.setMuted(!s.audio);
});
function openDrawer() {
  drawer.open();
}
function cycleFit() {
  const order = ["contain", "cover", "stretch"];
  settings.fit = order[(order.indexOf(settings.fit) + 1) % 3];
  saveSettings(settings);
  video.setFit(settings.fit);
  input?.setFit(settings.fit);
  mock.setFit(settings.fit);
}
var controls = new SessionControls({
  onDisconnect: disconnect,
  onFullscreen: () => toggleFullscreen(stage),
  onMute: () => {
    settings.audio = !settings.audio;
    saveSettings(settings);
    audio.setMuted(!settings.audio);
  },
  onHud: () => {
    showHud = !showHud;
    hud.hidden = !showHud;
  },
  onSettings: () => openDrawer(),
  onFit: () => cycleFit()
});
btnTheme.addEventListener("click", () => {
  theme = nextTheme(theme);
  setTheme(theme);
});
btnSettings.addEventListener("click", () => drawer.open());
window.addEventListener("keydown", (e) => {
  if (e.key === "f" || e.key === "F") {
    if (!(e.target instanceof HTMLInputElement)) {
      e.preventDefault();
      toggleFullscreen(stage);
    }
  }
});
window.addEventListener("beforeinstallprompt", (e) => {
  e.preventDefault();
  deferredPrompt = e;
  btnInstall.hidden = false;
});
btnInstall.addEventListener("click", async () => {
  await deferredPrompt?.prompt?.();
  deferredPrompt = null;
  btnInstall.hidden = true;
});
if ("serviceWorker" in navigator) {
  if (location.port === "8443") {
    void navigator.serviceWorker.getRegistrations().then((regs) => {
      for (const r of regs) void r.unregister();
    });
    void caches.keys().then((keys) => Promise.all(keys.map((k) => caches.delete(k))));
  } else {
    void navigator.serviceWorker.register("./sw.js").catch((e) => console.warn("sw", e));
  }
}
var dbg = window;
dbg.__droppixDebug = () => ({
  audio: { state: audio.contextState, packets: audio.packetCount },
  video: video.stats,
  connected: transport !== null
});
dbg.__droppixSuspendAudio = () => audio.suspendForTest();
void loadConfig();
//# sourceMappingURL=main.js.map
