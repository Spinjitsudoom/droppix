import { Transport } from "./transport.ts";
import { MsgType } from "./protocol.ts";
import { VideoPipeline } from "./decoder.ts";
import { MseVideoPipeline } from "./mse-decoder.ts";
import type { VideoRenderer } from "./video-renderer.ts";
import { AudioPlayer } from "./audio.ts";
import { InputBinder } from "./input.ts";
import { loadSettings, saveSettings, resolveResolution } from "./settings.ts";
import { toggleFullscreen } from "./fullscreen.ts";
import { MockOverlay } from "./mock-overlay.ts";
import { initTheme, setTheme, nextTheme } from "./theme.ts";
import { ConnectView } from "./connect-view.ts";
import { SessionControls } from "./session-controls.ts";
import { SettingsDrawer } from "./settings-drawer.ts";
import type { FitMode } from "./fit.ts";

const canvas = document.getElementById("video") as HTMLCanvasElement;
const videoEl = document.getElementById("video-el") as HTMLVideoElement;
const stage = document.getElementById("stage") as HTMLElement;
const app = document.getElementById("app")!;
const statusEl = document.getElementById("status-pill") as HTMLElement;
const btnTheme = document.getElementById("btn-theme") as HTMLButtonElement;
const btnSettings = document.getElementById("btn-settings") as HTMLButtonElement;
const btnInstall = document.getElementById("btn-install") as HTMLButtonElement;
const hud = document.getElementById("hud") as HTMLElement;
const clickLayer = document.getElementById("click-layer") as HTMLElement;
const mockLog = document.getElementById("mock-log") as HTMLElement;
const mockBackdrop = document.getElementById("mock-backdrop") as HTMLCanvasElement;
const mockBadge = document.getElementById("mock-badge") as HTMLElement;

let settings = loadSettings();
let theme = initTheme();

const mock = new MockOverlay(stage, clickLayer, mockLog, mockBackdrop, canvas);

let mseVideo: MseVideoPipeline | null = null;
// Pick the render path from settings: "mse" → native <video> (hardware decode + compositor),
// otherwise WebCodecs → Canvas 2D. Rebuilt at each connect so a change applies on reconnect.
function makeVideo(): VideoRenderer {
  const useMse =
    settings.renderer === "mse" &&
    typeof MediaSource !== "undefined" &&
    typeof MediaSource.isTypeSupported === "function";
  app.classList.toggle("render-mse", useMse);
  videoEl.hidden = !useMse;
  const adj = { flip: settings.flip, brightness: settings.brightness, contrast: settings.contrast };
  if (useMse) {
    mseVideo = new MseVideoPipeline(videoEl, adj, setStatus);
    return mseVideo;
  }
  mseVideo = null;
  return new VideoPipeline(canvas, adj, setStatus);
}
let video: VideoRenderer = makeVideo();
const audio = new AudioPlayer();
// Paint video against the audio wire media clock (stream PTS), not wall time.
video.setClock(() => audio.mediaPtsUs);
video.setFit(settings.fit);
mock.setFit(settings.fit);

let transport: Transport | null = null;
let input: InputBinder | null = null;
let deferredPrompt: Event | null = null;
let showHud = false;
let bytesIn = 0;
let lastBytesAt = performance.now();
let kbps = 0;
let isMock = false;
let burnIn = false;
let pinRequired = false;

function setStatus(s: string) {
  statusEl.textContent = s;
  statusEl.hidden = false;
}

// Pair overlay: the page connects live, then the host holds the stream until the user
// types the code shown on the PC. The code is never given to the browser — the host
// verifies it. Shown while awaiting the PIN; hidden once paired.
const pairEl = document.getElementById("pair")!;
const pairStatus = document.getElementById("pair-status")!;
const pinInputs = [...document.querySelectorAll<HTMLInputElement>("#pin input")];
function pinValue(): string {
  return pinInputs.map((i) => i.value).join("");
}
function resetPin(): void {
  pinInputs.forEach((i) => (i.value = ""));
}
pinInputs.forEach((inp, i) => {
  inp.addEventListener("input", () => {
    inp.value = inp.value.replace(/\D/g, "").slice(0, 1);
    if (inp.value && i < pinInputs.length - 1) pinInputs[i + 1]!.focus();
    if (pinValue().length === 6) {
      transport?.submitPin(pinValue());
      pairStatus.textContent = "Checking…";
    }
  });
  inp.addEventListener("keydown", (e) => {
    if (e.key === "Backspace" && !inp.value && i > 0) pinInputs[i - 1]!.focus();
  });
});

// Connect button (disconnected state): re-open the WSS. No PIN here — the PIN is the
// post-connect pair overlay above, verified by the host.
const connectView = new ConnectView(() => {
  void tryConnect();
});

async function tryConnect() {
  app.dataset.view = "session";
  controls.show();
  await connect();
}

async function loadConfig() {
  try {
    const r = await fetch("./config.json", { cache: "no-store" });
    if (!r.ok) throw new Error(String(r.status));
    const j = (await r.json()) as {
      pinRequired?: boolean;
      mock?: boolean;
      e2eDesktop?: boolean;
      burnIn?: boolean;
    };
    isMock = !!j.mock;
    burnIn = !!j.burnIn;
    pinRequired = !!j.pinRequired;
    mock.showIdle();
    mockBadge.hidden = !isMock;
    if (isMock) {
      // Mock host has no audio gesture; start muted (autoplay policy).
      settings.audio = false;
      saveSettings(settings);
    }
    if (typeof VideoDecoder === "undefined") {
      const msg = "This browser can't decode video (no WebCodecs) — use Chromium/Chrome.";
      setStatus(msg);
      connectView.setStatus(msg);
      return;
    }
    // Auto-connect on load: the page is host-served, so open the socket right away.
    // If the host requires a PIN (pinRequired), the transport shows the pair overlay
    // on connect and holds until the code is entered; otherwise video starts directly.
    // On a drop, onClose returns to the connect card (Connect button).
    app.dataset.view = "session";
    controls.show();
    window.setTimeout(() => {
      void connect();
    }, 300);
  } catch (e) {
    // Most common failure: host not serving --web. This must land on the visible
    // connect card (#c-status) too — #status-pill is hidden while data-view="connect",
    // so writing it there alone leaves the real error invisible.
    const msg = `Can't reach the PC — is droppix serving with --web? (${e})`;
    setStatus(msg);
    connectView.setStatus(msg);
  }
}

function wireTransport() {
  // Never allow two live transports: an orphaned socket would keep
  // painting video into the shared canvas after "disconnect".
  transport?.close();
  transport = new Transport({
    onStatus: setStatus,
    onAwaitingPin: () => {
      pairEl.classList.add("show");
      pairStatus.textContent = "Enter the code shown on your PC";
      resetPin();
      pinInputs[0]?.focus();
    },
    onPinRejected: (left) => {
      resetPin();
      pairStatus.textContent =
        left > 0 ? `Wrong code — ${left} tr${left === 1 ? "y" : "ies"} left` : "Too many attempts";
      if (left > 0) pinInputs[0]?.focus();
    },
    onPaired: () => {
      pairEl.classList.remove("show");
    },
    onClose: (r) => {
      setStatus(`Disconnected: ${r}`);
      transport = null;
      connecting = false;
      hud.hidden = true;
      pairEl.classList.remove("show");
      // Stop decode + blank canvas so no stale frames linger after close.
      video.close();
      audio.close();
      mock.showIdle();
      app.dataset.view = "connect";
      connectView.reset();
      // reset() only clears the PIN inputs; set the reason after it so it isn't
      // wiped, and so a session that bounces back to connect isn't silent.
      connectView.setStatus(`Disconnected: ${r}`);
    },
    onConfig: (w, h) => {
      // Autoplay policy: without a user gesture the context stays suspended
      // until the first click/keypress (AudioPlayer resumes it then).
      const audioHint = audio.contextState === "suspended" ? " - tap for audio" : "";
      setStatus(`Streaming ${w}x${h}${audioHint}`);
      mseVideo?.setDisplaySize(w, h);
      input?.setVideoSize(w, h);
      mock.setVideoSize(w, h);
      video.setAdjust(settings.flip, settings.brightness, settings.contrast);
    },
    onVideo: (pts, key, nal) => {
      bytesIn += nal.length;
      video.submit(key, nal, pts);
      const now = performance.now();
      if (now - lastBytesAt >= 1000) {
        // Divide by the REAL window, not a fixed 1s: at low/bursty frame rates the
        // window stretches well past 1s, and a fixed divisor inflated kbps several-fold
        // (the misleading "22 Mbps" readings during the fps debugging).
        kbps = Math.round((bytesIn * 8) / (now - lastBytesAt));
        bytesIn = 0;
        lastBytesAt = now;
        if (showHud) {
          hud.hidden = false;
          // in/out fps + decode(d)/paint(p) backlog pinpoint the bottleneck on-device:
          // in≈out low ⇒ decode-bound; in high & out low ⇒ paint-bound; d climbing ⇒ decode.
          const { w, h } = video.size;
          hud.textContent =
            `${w}x${h} · in ${video.inFps}/out ${video.currentFps} fps` +
            ` · d${video.decodeQueue} p${video.paintQueue} · ${kbps} kbps`;
        }
      }
    },
    onAudio: (pcm) => audio.submit(pcm),
    onOverlay: (show) => {
      showHud = show;
      hud.hidden = !show;
    },
  });
  // One binder for the page lifetime; it always targets the current transport.
  input ??= new InputBinder(canvas, (type, body) => transport?.send(type, body));
  input.setFit(settings.fit);
}

let connecting = false;

async function connect() {
  // Single-flight: auto-connect timer and a manual click must not both run.
  if (connecting || transport) return;
  connecting = true;
  try {
    settings = loadSettings();
    // Rebuild the renderer for the current setting; start each session on a clean pipeline.
    video.close();
    video = makeVideo();
    video.setClock(() => audio.mediaPtsUs);
    video.setFit(settings.fit);
    video.setAdjust(settings.flip, settings.brightness, settings.contrast);
    // Decoder desync recovery: ask the host for an IDR instead of freezing until its
    // next scheduled keyframe (up to 2s at the default GOP).
    video.setKeyframeRequester(() => transport?.send(MsgType.KeyframeRequest, new Uint8Array()));
    // Never let audio init block the stream - it must not throw or hang here.
    await audio.unlock();
    audio.setMuted(!settings.audio);
    audio.onStateChange = (s) => {
      if (s === "running" && statusEl.textContent?.includes("tap for audio")) {
        setStatus(statusEl.textContent.replace(" - tap for audio", ""));
      }
    };
    wireTransport();
    // "auto" tracks the canvas at physical-pixel resolution; a fixed setting (e.g. 1280x720)
    // caps what the host renders, so weak clients don't have to decode/paint a huge frame.
    const auto = {
      w: Math.max(640, Math.round(canvas.clientWidth * (window.devicePixelRatio || 1))),
      h: Math.max(360, Math.round(canvas.clientHeight * (window.devicePixelRatio || 1))),
    };
    const { w, h } = isMock ? { w: 1280, h: 720 } : resolveResolution(settings.resolution, auto);
    transport!.connect({
      width: w,
      height: h,
      density: 160,
      name: settings.name,
      id: settings.id,
      fps: settings.fps,
      audioWanted: settings.audio ? 1 : 0,
      bitrateKbps: settings.bitrateKbps,
      wallCol: settings.wallCol,
      wallRow: settings.wallRow,
      pinRequired,
    });
    canvas.focus();
    // burnIn: marks are drawn into the video server-side, so no client poll.
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

const drawer = new SettingsDrawer((s) => {
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
  const order: FitMode[] = ["contain", "cover", "stretch"];
  settings.fit = order[(order.indexOf(settings.fit) + 1) % 3]!;
  saveSettings(settings);
  video.setFit(settings.fit);
  input?.setFit(settings.fit);
  mock.setFit(settings.fit);
}

const controls = new SessionControls({
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
  onFit: () => cycleFit(),
});

btnTheme.addEventListener("click", () => {
  theme = nextTheme(theme);
  setTheme(theme);
});

btnSettings.addEventListener("click", () => drawer.open());

// Floating settings button: the control bar auto-hides, so this is the always-present
// way into settings while streaming (the same affordance spacedesk offers).
const fabSettings = document.getElementById("fab-settings") as HTMLButtonElement | null;
fabSettings?.addEventListener("click", (e) => {
  e.stopPropagation();   // do not let the tap fall through to the canvas as input
  drawer.open();
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
  // @ts-expect-error beforeinstallprompt
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

// Debug/e2e hooks: let Playwright assert real playback state and simulate
// the autoplay policy (bundled Chromium doesn't enforce it).
const dbg = window as unknown as Record<string, unknown>;
dbg.__droppixDebug = () => ({
  audio: { state: audio.contextState, packets: audio.packetCount },
  video: video.stats,
  connected: transport !== null,
});
dbg.__droppixSuspendAudio = () => audio.suspendForTest();

void loadConfig();
