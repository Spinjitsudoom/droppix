import { Transport } from "./transport.ts";
import { VideoPipeline } from "./decoder.ts";
import { AudioPlayer } from "./audio.ts";
import { InputBinder } from "./input.ts";
import { loadSettings, saveSettings } from "./settings.ts";
import { toggleFullscreen } from "./fullscreen.ts";
import { MockOverlay } from "./mock-overlay.ts";
import type { FitMode } from "./fit.ts";

const canvas = document.getElementById("video") as HTMLCanvasElement;
const stage = document.getElementById("stage") as HTMLElement;
const statusEl = document.getElementById("status")!;
const pinCodeEl = document.getElementById("pin-code")!;
const pinOk = document.getElementById("pin-ok") as HTMLInputElement;
const btnConnect = document.getElementById("btn-connect") as HTMLButtonElement;
const btnDisconnect = document.getElementById("btn-disconnect") as HTMLButtonElement;
const btnFullscreen = document.getElementById("btn-fullscreen") as HTMLButtonElement;
const btnInstall = document.getElementById("btn-install") as HTMLButtonElement;
const fitSel = document.getElementById("fit-mode") as HTMLSelectElement;
const muteEl = document.getElementById("mute") as HTMLInputElement;
const hud = document.getElementById("hud") as HTMLElement;
const clickLayer = document.getElementById("click-layer") as HTMLElement;
const mockLog = document.getElementById("mock-log") as HTMLElement;
const mockBackdrop = document.getElementById("mock-backdrop") as HTMLCanvasElement;
const mockBadge = document.getElementById("mock-badge") as HTMLElement;

let settings = loadSettings();
fitSel.value = settings.fit;
muteEl.checked = !settings.audio;

const mock = new MockOverlay(stage, clickLayer, mockLog, mockBackdrop, canvas);
const video = new VideoPipeline(canvas, {
  flip: settings.flip,
  brightness: settings.brightness,
  contrast: settings.contrast,
});
const audio = new AudioPlayer();
// Paint video against the audio wire media clock (stream PTS), not wall time.
video.setClock(() => audio.mediaPtsUs);
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
}

function syncConnectEnabled() {
  btnConnect.disabled = !pinOk.checked;
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
    pinCodeEl.textContent = j.pairingCode ?? "------";
    isMock = !!j.mock;
    burnIn = !!j.burnIn;
    mock.showIdle();
    if (isMock) {
      mockBadge.hidden = false;
      // Start muted: autoplay policy blocks sound until a gesture anyway,
      // and unmuting is the gesture that resumes audio cleanly.
      muteEl.checked = true;
      settings.audio = false;
      saveSettings(settings);
      pinOk.checked = true;
      syncConnectEnabled();
      setStatus("Ready - Connect for server video + audio");
      if (typeof VideoDecoder === "undefined") {
        setStatus("WebCodecs VideoDecoder missing - use Chromium");
      } else {
        window.setTimeout(() => {
          if (!btnDisconnect.hidden) return;
          void connect();
        }, 300);
      }
    } else {
      mockBadge.hidden = true;
      setStatus("Confirm PIN matches the PC, then Connect");
    }
  } catch (e) {
    pinCodeEl.textContent = "------";
    setStatus(`config.json unavailable (${e}) - is the host serving with --web?`);
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
      btnConnect.hidden = false;
      btnDisconnect.hidden = true;
      hud.hidden = true;
      // Stop decode + blank canvas so no stale frames linger after close.
      video.close();
      audio.close();
      mock.showIdle();
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
    settings.audio = !muteEl.checked;
    saveSettings(settings);
    // Never let audio init block the stream - it must not throw or hang here.
    await audio.unlock();
    audio.setMuted(muteEl.checked);
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
      audioWanted: settings.audio && !muteEl.checked ? 1 : 0,
      bitrateKbps: settings.bitrateKbps,
    });
    btnConnect.hidden = true;
    btnDisconnect.hidden = false;
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
  btnConnect.hidden = false;
  btnDisconnect.hidden = true;
  setStatus(isMock ? "Disconnected" : "Disconnected");
}

pinOk.addEventListener("change", syncConnectEnabled);
btnConnect.addEventListener("click", () => void connect());
btnDisconnect.addEventListener("click", disconnect);
btnFullscreen.addEventListener("click", () => toggleFullscreen(stage));
fitSel.addEventListener("change", () => {
  settings.fit = fitSel.value as FitMode;
  saveSettings(settings);
  input?.setFit(settings.fit);
  video.setFit(settings.fit);
  mock.setFit(settings.fit);
});
video.setFit(settings.fit);
mock.setFit(settings.fit);
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

syncConnectEnabled();
void loadConfig();
