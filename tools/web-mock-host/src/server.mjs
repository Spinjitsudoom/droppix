#!/usr/bin/env node
/**
 * Mock droppix host: HTTPS + WSS, loops an MP4 (synced A/V) for lipsync checks,
 * and applies wire input to a lightweight mark tracker (/debug/server-marks).
 */
import fs from "node:fs";
import path from "node:path";
import https from "node:https";
import { fileURLToPath } from "node:url";
import { spawnSync } from "node:child_process";
import { WebSocketServer } from "ws";
import {
  MsgType,
  TYPE_NAME,
  frame,
  parseFrame,
  encodeConfig,
  encodeVideo,
  encodeOverlay,
  decodeHello,
  decodeTouch,
  decodeScroll,
  decodeMouseButton,
  decodeKey,
} from "./protocol.mjs";
import { createMockDesktop } from "./mock-desktop.mjs";
import { resolveSampleMp4, probeMp4 } from "./mp4-stream.mjs";
import { startOverlayStream } from "./overlay-stream.mjs";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const TOOL_ROOT = path.resolve(__dirname, "..");
const REPO_ROOT = path.resolve(TOOL_ROOT, "../..");

const PORT = Number(process.env.PORT || 8443);
const PAIRING = process.env.PAIRING_CODE || "123456";
const WEB_ROOT =
  process.env.DROPPIX_WEB_ROOT || path.join(REPO_ROOT, "web", "dist");
const CERT_DIR = path.join(TOOL_ROOT, "certs");
const CERT = path.join(CERT_DIR, "cert.pem");
const KEY = path.join(CERT_DIR, "key.pem");

const recentInputs = [];
const MAX_INPUTS = 200;
let lastDesktop = null;
let lastStatsFn = null;
let SAMPLE_MP4;

function recordInput(kind, detail) {
  recentInputs.push({ t: Date.now(), kind, detail });
  if (recentInputs.length > MAX_INPUTS) recentInputs.shift();
}

function ensureCerts() {
  if (fs.existsSync(CERT) && fs.existsSync(KEY)) return;
  console.log("generating self-signed certs…");
  fs.mkdirSync(CERT_DIR, { recursive: true });
  const r = spawnSync("bash", [path.join(TOOL_ROOT, "scripts/gen-cert.sh")], {
    stdio: "inherit",
  });
  if (r.status !== 0) {
    console.error("cert generation failed - install openssl");
    process.exit(1);
  }
}

function mime(p) {
  if (p.endsWith(".html")) return "text/html; charset=utf-8";
  if (p.endsWith(".js")) return "text/javascript; charset=utf-8";
  if (p.endsWith(".css")) return "text/css; charset=utf-8";
  if (p.endsWith(".json") || p.endsWith(".webmanifest")) return "application/json";
  if (p.endsWith(".png")) return "image/png";
  if (p.endsWith(".map")) return "application/json";
  return "application/octet-stream";
}

function safeJoin(root, urlPath) {
  let rel = decodeURIComponent(urlPath.split("?")[0]);
  if (rel === "/" || rel === "") rel = "/index.html";
  if (rel.startsWith("/")) rel = rel.slice(1);
  if (rel.includes("..")) return null;
  const full = path.join(root, rel);
  if (!full.startsWith(root)) return null;
  return full;
}

function logInput(kind, detail) {
  const ts = new Date().toISOString().slice(11, 23);
  console.log(`[${ts}] ← ${kind}`, detail);
  recordInput(kind, detail);
}

ensureCerts();

if (!fs.existsSync(path.join(WEB_ROOT, "index.html"))) {
  console.error(`web root missing index.html: ${WEB_ROOT}`);
  console.error("Run: cd web && npm ci && npm run build");
  process.exit(1);
}

try {
  SAMPLE_MP4 = resolveSampleMp4();
  console.log(`MP4 source: ${SAMPLE_MP4}`);
} catch (e) {
  console.error(String(e));
  process.exit(1);
}

const server = https.createServer(
  {
    cert: fs.readFileSync(CERT),
    key: fs.readFileSync(KEY),
  },
  (req, res) => {
    const url = new URL(req.url || "/", `https://localhost:${PORT}`);
    if (url.pathname === "/config.json") {
      const body =
        JSON.stringify({
          pairingCode: PAIRING,
          mock: true,
          e2eDesktop: true,
          burnIn: true,
          mp4: path.basename(SAMPLE_MP4),
        }) + "\n";
      res.writeHead(200, {
        "Content-Type": "application/json",
        "Cache-Control": "no-store",
      });
      res.end(body);
      return;
    }
    if (url.pathname === "/health") {
      res.writeHead(200, { "Content-Type": "text/plain" });
      res.end("ok\n");
      return;
    }
    if (url.pathname === "/debug/inputs") {
      if (url.searchParams.get("clear") === "1") recentInputs.length = 0;
      res.writeHead(200, {
        "Content-Type": "application/json",
        "Cache-Control": "no-store",
      });
      res.end(JSON.stringify({ inputs: recentInputs }));
      return;
    }
    if (url.pathname === "/debug/session") {
      res.writeHead(200, {
        "Content-Type": "application/json",
        "Cache-Control": "no-store",
      });
      res.end(JSON.stringify({ active: activeSession !== null }));
      return;
    }
    if (url.pathname === "/debug/stats") {
      res.writeHead(200, {
        "Content-Type": "application/json",
        "Cache-Control": "no-store",
      });
      const snap = lastStatsFn ? lastStatsFn() : { active: false };
      res.end(JSON.stringify({ geeks: lastDesktop?.geeks ?? false, ...snap }));
      return;
    }
    if (url.pathname === "/debug/server-marks") {
      if (url.searchParams.get("clear") === "1") lastDesktop?.clearMarksDebug?.();
      res.writeHead(200, {
        "Content-Type": "application/json",
        "Cache-Control": "no-store",
      });
      res.end(JSON.stringify({ marks: lastDesktop?.marks ?? [] }));
      return;
    }
    const file = safeJoin(WEB_ROOT, url.pathname);
    if (!file || !fs.existsSync(file) || fs.statSync(file).isDirectory()) {
      res.writeHead(404);
      res.end("not found");
      return;
    }
    res.writeHead(200, { "Content-Type": mime(file) });
    fs.createReadStream(file).pipe(res);
  },
);

const wss = new WebSocketServer({ server, path: "/ws" });

// Single active media session, like the real host: a new HELLO preempts any
// previous stream so an orphaned socket can never keep playing in parallel.
let activeSession = null;

wss.on("connection", (ws, req) => {
  const peer = req.socket.remoteAddress;
  console.log(`\n=== WSS client ${peer} ===`);
  let pipe = null;
  let desktop = null;
  let pingTimer = null;

  // Per-connection wire stats for the GEEKS overlay + /debug/stats.
  const stats = {
    protocol: null,
    transport: "WSS",
    state: "connecting",
    sessionStart: Date.now(),
    hello: null,
    config: null,
    in: { Hello: 0, Ping: 0, Pong: 0, Touch: 0, MouseButton: 0, Scroll: 0, Key: 0, Orientation: 0, Bye: 0 },
    out: { Video: 0, Audio: 0, Config: 0, Overlay: 0, Ping: 0, Pong: 0 },
    video: { aus: 0, keyframes: 0, bytes: 0 },
    audio: { chunks: 0, bytes: 0, lastAt: 0, maxGapMs: 0 },
    ping: { lastRttMs: 0, sum: 0, n: 0 },
  };

  const getStats = () => {
    const now = Date.now();
    const upS = Math.max(0.001, (now - stats.sessionStart) / 1000);
    const v = stats.video;
    const fps = Math.round(v.aus / upS);
    const gopMs = v.keyframes > 0 && fps > 0
      ? Math.round((v.aus / v.keyframes) * (1000 / fps))
      : 0;
    return {
      active: true,
      protocol: stats.protocol,
      transport: stats.transport,
      state: stats.state,
      uptimeMs: now - stats.sessionStart,
      hello: stats.hello,
      config: stats.config,
      in: stats.in,
      out: stats.out,
      video: { aus: v.aus, keyframes: v.keyframes, fps, kbps: Math.round((v.bytes * 8) / upS / 1000), gopMs },
      audio: {
        chunks: stats.audio.chunks,
        chunkMs: 20,
        kbps: Math.round((stats.audio.bytes * 8) / upS / 1000),
        maxGapMs: stats.audio.maxGapMs,
      },
      ping: {
        lastRttMs: stats.ping.lastRttMs,
        avgRttMs: stats.ping.n ? Math.round(stats.ping.sum / stats.ping.n) : 0,
        samples: stats.ping.n,
      },
      lipsync: pipe?.lipsync ?? null,
    };
  };

  const stopMedia = () => {
    pipe?.stop();
    pipe = null;
    if (pingTimer) {
      clearInterval(pingTimer);
      pingTimer = null;
    }
    if (desktop && lastDesktop === desktop) lastDesktop = null;
    if (lastStatsFn === getStats) lastStatsFn = null;
    desktop = null;
    stats.state = "closed";
    if (activeSession?.ws === ws) activeSession = null;
  };

  ws.on("message", (data, isBinary) => {
    if (!isBinary && !(data instanceof Buffer)) return;
    const parsed = parseFrame(data);
    if (!parsed) return;
    const { type, body } = parsed;

    if (type === MsgType.Hello) {
      const hello = decodeHello(body);
      console.log("HELLO", hello);
      stats.in.Hello++;
      stats.protocol = hello?.version ?? null;
      stats.hello = hello;
      stats.state = "streaming";
      stats.sessionStart = Date.now();

      stopMedia();
      if (activeSession) {
        console.log("preempting previous session");
        activeSession.stop();
        try {
          activeSession.ws.close();
        } catch {
          /* ignore */
        }
        activeSession = null;
      }
      stats.state = "streaming";
      const dims = probeMp4(SAMPLE_MP4);
      desktop = createMockDesktop(dims.width, dims.height, { getStats });
      lastDesktop = desktop;
      lastStatsFn = getStats;

      pipe = startOverlayStream({
        mp4: SAMPLE_MP4,
        desktop,
        onVideo: ({ keyframe, nal, ptsUs }) => {
          if (ws.readyState !== ws.OPEN) return;
          stats.video.aus++;
          if (keyframe) stats.video.keyframes++;
          stats.video.bytes += nal.length;
          stats.out.Video++;
          // PTS is the media clock from the source frame, not wall time.
          ws.send(frame(MsgType.Video, encodeVideo(ptsUs ?? 0n, keyframe, nal)));
        },
        onAudio: (pcm) => {
          if (ws.readyState !== ws.OPEN) return;
          const now = Date.now();
          if (stats.audio.lastAt) {
            stats.audio.maxGapMs = Math.max(stats.audio.maxGapMs, now - stats.audio.lastAt);
          }
          stats.audio.lastAt = now;
          stats.audio.chunks++;
          stats.audio.bytes += pcm.length;
          stats.out.Audio++;
          ws.send(frame(MsgType.Audio, pcm));
        },
        onError: (e) => console.warn("ffmpeg:", e),
      });
      activeSession = { ws, stop: stopMedia };

      ws.send(
        frame(MsgType.Config, encodeConfig(pipe.width, pipe.height, pipe.fps)),
      );
      stats.out.Config++;
      stats.config = { width: pipe.width, height: pipe.height, fps: pipe.fps };
      ws.send(frame(MsgType.Overlay, encodeOverlay(1)));
      stats.out.Overlay++;
      console.log(
        `CONFIG ${pipe.width}x${pipe.height}@${pipe.fps} from ${path.basename(SAMPLE_MP4)}`,
      );

      // Server-driven ping to measure RTT (client echoes the 8-byte stamp).
      pingTimer = setInterval(() => {
        if (ws.readyState !== ws.OPEN) return;
        const b = Buffer.alloc(8);
        b.writeBigUInt64BE(BigInt(Date.now()), 0);
        stats.out.Ping++;
        ws.send(frame(MsgType.Ping, b));
      }, 1000);
      return;
    }

    if (type === MsgType.Ping) {
      stats.in.Ping++;
      stats.out.Pong++;
      ws.send(frame(MsgType.Pong, body));
      return;
    }
    if (type === MsgType.Pong) {
      stats.in.Pong++;
      if (body.length >= 8) {
        const dv = new DataView(body.buffer, body.byteOffset, 8);
        const sent = Number(dv.getBigUint64(0));
        const rtt = Date.now() - sent;
        if (rtt >= 0 && rtt < 60000) {
          stats.ping.lastRttMs = rtt;
          stats.ping.sum += rtt;
          stats.ping.n++;
        }
      }
      return;
    }
    if (type === MsgType.Bye) {
      console.log("BYE");
      stats.in.Bye++;
      stopMedia();
      ws.close();
      return;
    }
    if (type === MsgType.Touch) {
      stats.in.Touch++;
      const contacts = decodeTouch(body);
      logInput("Touch", contacts.length ? contacts : "(all up)");
      desktop?.onTouch(contacts);
      return;
    }
    if (type === MsgType.Scroll) {
      stats.in.Scroll++;
      const s = decodeScroll(body);
      logInput("Scroll", s);
      desktop?.onScroll(s);
      return;
    }
    if (type === MsgType.MouseButton) {
      stats.in.MouseButton++;
      const m = decodeMouseButton(body);
      const names = { 1: "right", 2: "middle" };
      logInput("MouseButton", {
        ...m,
        button: names[m?.button] || m?.button,
        action: m?.action === 1 ? "down" : "up",
      });
      desktop?.onMouseButton(m);
      return;
    }
    if (type === MsgType.Key) {
      stats.in.Key++;
      const k = decodeKey(body);
      logInput("Key", {
        ...k,
        action: ["up", "down", "repeat"][k?.action] ?? k?.action,
      });
      desktop?.onKey(k);
      return;
    }
    if (type === MsgType.Orientation) {
      stats.in.Orientation++;
      logInput("Orientation", body[0]);
      return;
    }
    console.log(`← ${TYPE_NAME[type] || type} (${body.length} B)`);
  });

  ws.on("close", () => {
    console.log("=== disconnected ===\n");
    stopMedia();
  });
  ws.on("error", (e) => console.warn("ws error", e.message));
});

server.listen(PORT, "0.0.0.0", () => {
  console.log(`
droppix web-mock-host (MP4 + server overlay)
  URL:  https://localhost:${PORT}/
  PIN:  ${PAIRING}
  MP4:  ${SAMPLE_MP4}

Chromium → Connect. Movie + audio looped realtime; server burns click marks +
event log INTO the H.264 stream. Starts muted (unmute to hear audio).
Override: DROPPIX_MOCK_MP4=/path/to/file.mp4
`);
});
