// src/protocol.ts
var kProtocolVersion = 5;
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
function encodeHello(version, width, height, density, name, id, fps = 0, audioWanted = 0, orientationCode = 0, bitrateKbps = 0) {
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
        hello.bitrateKbps
      );
      ws.send(frameMessage(MsgType.Hello, body));
      this.handlers.onStatus("Connected \u2014 waiting for CONFIG");
      this.pingTimer = window.setInterval(() => {
        this.send(MsgType.Ping, new Uint8Array());
      }, 2e3);
    };
    ws.onmessage = (ev) => {
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
    ws.onerror = () => this.handlers.onStatus("WebSocket error");
    ws.onclose = () => {
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
    if (this.ws) {
      try {
        this.ws.send(frameMessage(MsgType.Bye, new Uint8Array()));
      } catch {
      }
      this.ws.close();
      this.ws = null;
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

// src/decoder.ts
var VideoPipeline = class {
  constructor(canvas2, opts) {
    this.canvas = canvas2;
    this.opts = opts;
  }
  decoder = null;
  configured = false;
  vw = 0;
  vh = 0;
  frames = 0;
  lastFpsAt = performance.now();
  fps = 0;
  fit = "contain";
  get size() {
    return { w: this.vw, h: this.vh };
  }
  get currentFps() {
    return this.fps;
  }
  setFit(mode) {
    this.fit = mode;
  }
  setAdjust(flip, brightness, contrast) {
    this.opts = { flip, brightness, contrast };
  }
  async submit(keyframe, nal) {
    if (typeof VideoDecoder === "undefined") {
      throw new Error("WebCodecs VideoDecoder not available");
    }
    if (!this.decoder || this.decoder.state === "closed") {
      this.decoder = new VideoDecoder({
        output: (frame) => this.draw(frame),
        error: (e) => console.error("VideoDecoder", e)
      });
      this.configured = false;
    }
    if (!this.configured) {
      if (!keyframe) return;
      this.decoder.configure({
        codec: "avc1.42E01E",
        optimizeForLatency: true
      });
      this.configured = true;
    }
    const chunk = new EncodedVideoChunk({
      type: keyframe ? "key" : "delta",
      timestamp: performance.now() * 1e3,
      data: nal
    });
    try {
      this.decoder.decode(chunk);
    } catch (e) {
      console.warn("decode", e);
      if (keyframe) this.configured = false;
    }
  }
  draw(frame) {
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
    const now = performance.now();
    if (now - this.lastFpsAt >= 1e3) {
      this.fps = this.frames;
      this.frames = 0;
      this.lastFpsAt = now;
    }
  }
  close() {
    try {
      this.decoder?.close();
    } catch {
    }
    this.decoder = null;
    this.configured = false;
  }
};

// src/audio.ts
var AUDIO_RATE = 48e3;
var AudioPlayer = class {
  ctx = null;
  node = null;
  muted = false;
  ready = false;
  async unlock() {
    try {
      if (!this.ctx) {
        this.ctx = new AudioContext({ sampleRate: AUDIO_RATE });
        await this.ctx.audioWorklet.addModule(new URL("./audio-worklet.js", import.meta.url));
        this.node = new AudioWorkletNode(this.ctx, "pcm-player", {
          numberOfInputs: 0,
          numberOfOutputs: 1,
          outputChannelCount: [2]
        });
        this.node.connect(this.ctx.destination);
      }
      if (this.ctx.state === "suspended") await this.ctx.resume();
      this.ready = true;
    } catch (e) {
      console.warn("audio init failed; video-only", e);
      this.ready = false;
    }
  }
  setMuted(m) {
    this.muted = m;
    if (m) this.node?.port.postMessage({ type: "clear" });
  }
  submit(pcm) {
    if (!this.ready || this.muted || !this.node) return;
    if (pcm.length < 4) return;
    const samples = new Float32Array(pcm.length / 2);
    const view = new DataView(pcm.buffer, pcm.byteOffset, pcm.byteLength);
    for (let i = 0; i < samples.length; i++) {
      samples[i] = view.getInt16(i * 2, true) / 32768;
    }
    this.node.port.postMessage({ type: "pcm", samples }, [samples.buffer]);
  }
  close() {
    try {
      this.node?.disconnect();
      void this.ctx?.close();
    } catch {
    }
    this.node = null;
    this.ctx = null;
    this.ready = false;
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
    contrast: 1
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

// src/main.ts
var canvas = document.getElementById("video");
var stage = document.getElementById("stage");
var statusEl = document.getElementById("status");
var pinCodeEl = document.getElementById("pin-code");
var pinOk = document.getElementById("pin-ok");
var btnConnect = document.getElementById("btn-connect");
var btnDisconnect = document.getElementById("btn-disconnect");
var btnFullscreen = document.getElementById("btn-fullscreen");
var btnInstall = document.getElementById("btn-install");
var fitSel = document.getElementById("fit-mode");
var muteEl = document.getElementById("mute");
var hud = document.getElementById("hud");
var settings = loadSettings();
fitSel.value = settings.fit;
muteEl.checked = !settings.audio;
var video = new VideoPipeline(canvas, {
  flip: settings.flip,
  brightness: settings.brightness,
  contrast: settings.contrast
});
var audio = new AudioPlayer();
var transport = null;
var input = null;
var deferredPrompt = null;
var showHud = false;
var bytesIn = 0;
var lastBytesAt = performance.now();
var kbps = 0;
function setStatus(s) {
  statusEl.textContent = s;
}
function syncConnectEnabled() {
  btnConnect.disabled = !pinOk.checked;
}
async function loadConfig() {
  try {
    const r = await fetch("./config.json", { cache: "no-store" });
    if (!r.ok) throw new Error(String(r.status));
    const j = await r.json();
    pinCodeEl.textContent = j.pairingCode ?? "------";
    setStatus("Confirm PIN matches the PC, then Connect");
  } catch (e) {
    pinCodeEl.textContent = "------";
    setStatus(`config.json unavailable (${e}) \u2014 is the host serving with --web?`);
  }
}
function wireTransport() {
  transport = new Transport({
    onStatus: setStatus,
    onClose: (r) => {
      setStatus(`Disconnected: ${r}`);
      btnConnect.hidden = false;
      btnDisconnect.hidden = true;
    },
    onConfig: (w, h) => {
      setStatus(`Streaming ${w}x${h}`);
      input?.setVideoSize(w, h);
      video.setAdjust(settings.flip, settings.brightness, settings.contrast);
    },
    onVideo: (pts, key, nal) => {
      bytesIn += nal.length;
      void video.submit(key, nal);
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
  input = new InputBinder(canvas, (type, body) => transport.send(type, body));
  input.setFit(settings.fit);
}
async function connect() {
  settings = loadSettings();
  settings.audio = !muteEl.checked;
  saveSettings(settings);
  await audio.unlock();
  audio.setMuted(muteEl.checked);
  wireTransport();
  const w = Math.max(640, Math.round(canvas.clientWidth * (window.devicePixelRatio || 1)));
  const h = Math.max(360, Math.round(canvas.clientHeight * (window.devicePixelRatio || 1)));
  transport.connect({
    width: w,
    height: h,
    density: 160,
    name: settings.name,
    id: settings.id,
    fps: settings.fps,
    audioWanted: settings.audio && !muteEl.checked ? 1 : 0,
    bitrateKbps: settings.bitrateKbps
  });
  btnConnect.hidden = true;
  btnDisconnect.hidden = false;
  canvas.focus();
}
function disconnect() {
  transport?.close();
  transport = null;
  video.close();
  audio.close();
  btnConnect.hidden = false;
  btnDisconnect.hidden = true;
  setStatus("Disconnected");
}
pinOk.addEventListener("change", syncConnectEnabled);
btnConnect.addEventListener("click", () => void connect());
btnDisconnect.addEventListener("click", disconnect);
btnFullscreen.addEventListener("click", () => toggleFullscreen(stage));
fitSel.addEventListener("change", () => {
  settings.fit = fitSel.value;
  saveSettings(settings);
  input?.setFit(settings.fit);
  video.setFit(settings.fit);
});
video.setFit(settings.fit);
muteEl.addEventListener("change", () => {
  audio.setMuted(muteEl.checked);
  settings.audio = !muteEl.checked;
  saveSettings(settings);
});
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
  void navigator.serviceWorker.register("./sw.js").catch((e) => console.warn("sw", e));
}
syncConnectEnabled();
void loadConfig();
//# sourceMappingURL=main.js.map
