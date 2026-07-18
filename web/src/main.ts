import { Transport } from "./transport.ts";
import { VideoPipeline } from "./decoder.ts";
import { AudioPlayer } from "./audio.ts";
import { InputBinder } from "./input.ts";
import { loadSettings, saveSettings } from "./settings.ts";
import { toggleFullscreen } from "./fullscreen.ts";
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

let settings = loadSettings();
fitSel.value = settings.fit;
muteEl.checked = !settings.audio;

const video = new VideoPipeline(canvas, {
  flip: settings.flip,
  brightness: settings.brightness,
  contrast: settings.contrast,
});
const audio = new AudioPlayer();
let transport: Transport | null = null;
let input: InputBinder | null = null;
let deferredPrompt: Event | null = null;
let showHud = false;
let bytesIn = 0;
let lastBytesAt = performance.now();
let kbps = 0;

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
    const j = (await r.json()) as { pairingCode?: string };
    pinCodeEl.textContent = j.pairingCode ?? "------";
    setStatus("Confirm PIN matches the PC, then Connect");
  } catch (e) {
    pinCodeEl.textContent = "------";
    setStatus(`config.json unavailable (${e}) — is the host serving with --web?`);
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
  input = new InputBinder(canvas, (type, body) => transport!.send(type, body));
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
  settings.fit = fitSel.value as FitMode;
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
  // @ts-expect-error beforeinstallprompt
  await deferredPrompt?.prompt?.();
  deferredPrompt = null;
  btnInstall.hidden = true;
});

if ("serviceWorker" in navigator) {
  void navigator.serviceWorker.register("./sw.js").catch((e) => console.warn("sw", e));
}

syncConnectEnabled();
void loadConfig();
