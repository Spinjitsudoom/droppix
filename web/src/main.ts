import { Transport } from "./transport.ts";
import { VideoPipeline } from "./decoder.ts";
import { AudioPlayer } from "./audio.ts";
import { InputBinder } from "./input.ts";
import { loadSettings, saveSettings } from "./settings.ts";
import { toggleFullscreen } from "./fullscreen.ts";
import { MockOverlay } from "./mock-overlay.ts";
import { initTheme, setTheme, nextTheme } from "./theme.ts";
import { ConnectView } from "./connect-view.ts";
import { SessionControls } from "./session-controls.ts";
import { SettingsDrawer } from "./settings-drawer.ts";
import type { FitMode } from "./fit.ts";

const canvas = document.getElementById("video") as HTMLCanvasElement;
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
const video = new VideoPipeline(canvas, {
  flip: settings.flip,
  brightness: settings.brightness,
  contrast: settings.contrast,
});
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

function setStatus(s: string) {
  statusEl.textContent = s;
  statusEl.hidden = false;
}

// The web client is served BY the host over its own HTTPS, so there is no PIN to
// enter — loading this page already means you reached the right PC. Connect goes
// straight through; the host prompts to approve the device if it isn't known yet.
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
      pairingCode?: string;
      mock?: boolean;
      e2eDesktop?: boolean;
      burnIn?: boolean;
    };
    isMock = !!j.mock;
    burnIn = !!j.burnIn;
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
    // Auto-connect on load: the page is host-served, so just connect and show video.
    // No PIN. On a drop, onClose returns to the connect card (Connect button).
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
    onClose: (r) => {
      setStatus(`Disconnected: ${r}`);
      transport = null;
      connecting = false;
      hud.hidden = true;
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
      input?.setVideoSize(w, h);
      mock.setVideoSize(w, h);
      video.setAdjust(settings.flip, settings.brightness, settings.contrast);
    },
    onVideo: (pts, key, nal) => {
      bytesIn += nal.length;
      video.submit(key, nal, pts);
      const now = performance.now();
      if (now - lastBytesAt >= 1000) {
        kbps = Math.round((bytesIn * 8) / 1000);
        bytesIn = 0;
        lastBytesAt = now;
        if (showHud) {
          hud.hidden = false;
          hud.textContent = `${video.currentFps} fps · ${kbps} kbps`;
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
    // Never let audio init block the stream - it must not throw or hang here.
    await audio.unlock();
    audio.setMuted(!settings.audio);
    audio.onStateChange = (s) => {
      if (s === "running" && statusEl.textContent?.includes("tap for audio")) {
        setStatus(statusEl.textContent.replace(" - tap for audio", ""));
      }
    };
    wireTransport();
    const w = isMock
      ? 1280
      : Math.max(640, Math.round(canvas.clientWidth * (window.devicePixelRatio || 1)));
    const h = isMock
      ? 720
      : Math.max(360, Math.round(canvas.clientHeight * (window.devicePixelRatio || 1)));
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
