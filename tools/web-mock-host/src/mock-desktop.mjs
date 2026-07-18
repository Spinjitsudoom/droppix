/**
 * Fake remote desktop for E2E: receives wire input, updates scene, and either
 * renders a full RGB24 framebuffer (render) or paints a server-authored overlay
 * on top of a real movie frame (overlayFrame). Either way the marks/log are
 * encoded INTO the H.264 stream - not a client-side cosmetic.
 */

function clamp(v, a, b) {
  return Math.max(a, Math.min(b, v));
}

function normToPx(nx, ny, W, H) {
  return {
    x: Math.round((clamp(nx, 0, 65535) / 65535) * (W - 1)),
    y: Math.round((clamp(ny, 0, 65535) / 65535) * (H - 1)),
  };
}

function fillRect(buf, W, H, x, y, w, h, r, g, b) {
  const x0 = clamp(Math.floor(x), 0, W - 1);
  const y0 = clamp(Math.floor(y), 0, H - 1);
  const x1 = clamp(Math.ceil(x + w), 0, W);
  const y1 = clamp(Math.ceil(y + h), 0, H);
  for (let py = y0; py < y1; py++) {
    let o = (py * W + x0) * 3;
    for (let px = x0; px < x1; px++) {
      buf[o] = r;
      buf[o + 1] = g;
      buf[o + 2] = b;
      o += 3;
    }
  }
}

function fillRectA(buf, W, H, x, y, w, h, r, g, b, a) {
  const x0 = clamp(Math.floor(x), 0, W - 1);
  const y0 = clamp(Math.floor(y), 0, H - 1);
  const x1 = clamp(Math.ceil(x + w), 0, W);
  const y1 = clamp(Math.ceil(y + h), 0, H);
  const ia = 1 - a;
  for (let py = y0; py < y1; py++) {
    let o = (py * W + x0) * 3;
    for (let px = x0; px < x1; px++) {
      buf[o] = (buf[o] * ia + r * a) | 0;
      buf[o + 1] = (buf[o + 1] * ia + g * a) | 0;
      buf[o + 2] = (buf[o + 2] * ia + b * a) | 0;
      o += 3;
    }
  }
}

function fillCircle(buf, W, H, cx, cy, radius, r, g, b, a = 1) {
  const rad = Math.ceil(radius);
  const x0 = clamp(cx - rad, 0, W - 1);
  const y0 = clamp(cy - rad, 0, H - 1);
  const x1 = clamp(cx + rad, 0, W - 1);
  const y1 = clamp(cy + rad, 0, H - 1);
  const rr = radius * radius;
  for (let py = y0; py <= y1; py++) {
    for (let px = x0; px <= x1; px++) {
      const dx = px - cx;
      const dy = py - cy;
      if (dx * dx + dy * dy > rr) continue;
      const o = (py * W + px) * 3;
      buf[o] = Math.round(buf[o] * (1 - a) + r * a);
      buf[o + 1] = Math.round(buf[o + 1] * (1 - a) + g * a);
      buf[o + 2] = Math.round(buf[o + 2] * (1 - a) + b * a);
    }
  }
}

function strokeCircle(buf, W, H, cx, cy, radius, r, g, b, thickness = 3) {
  const rad = Math.ceil(radius + thickness);
  const x0 = clamp(cx - rad, 0, W - 1);
  const y0 = clamp(cy - rad, 0, H - 1);
  const x1 = clamp(cx + rad, 0, W - 1);
  const y1 = clamp(cy + rad, 0, H - 1);
  const rOut = radius + thickness / 2;
  const rIn = Math.max(0, radius - thickness / 2);
  const rOut2 = rOut * rOut;
  const rIn2 = rIn * rIn;
  for (let py = y0; py <= y1; py++) {
    for (let px = x0; px <= x1; px++) {
      const d2 = (px - cx) ** 2 + (py - cy) ** 2;
      if (d2 > rOut2 || d2 < rIn2) continue;
      const o = (py * W + px) * 3;
      buf[o] = r;
      buf[o + 1] = g;
      buf[o + 2] = b;
    }
  }
}

function drawCross(buf, W, H, cx, cy, arm, r, g, b) {
  fillRect(buf, W, H, cx - arm, cy - 2, arm * 2, 4, r, g, b);
  fillRect(buf, W, H, cx - 2, cy - arm, 4, arm * 2, r, g, b);
}

function drawText(buf, W, H, x, y, text, r, g, b, scale = 2) {
  let cx = x;
  for (const ch of String(text)) {
    drawGlyph(buf, W, H, cx, y, ch, r, g, b, scale);
    cx += 6 * scale;
  }
}

const GLYPHS = {
  "0": ["01110", "10001", "10001", "10001", "10001", "10001", "01110"],
  "1": ["00100", "01100", "00100", "00100", "00100", "00100", "01110"],
  "2": ["01110", "10001", "00001", "00110", "01000", "10000", "11111"],
  "3": ["01110", "10001", "00001", "00110", "00001", "10001", "01110"],
  "4": ["00010", "00110", "01010", "10010", "11111", "00010", "00010"],
  "5": ["11111", "10000", "11110", "00001", "00001", "10001", "01110"],
  "6": ["01110", "10000", "11110", "10001", "10001", "10001", "01110"],
  "7": ["11111", "00001", "00010", "00100", "01000", "01000", "01000"],
  "8": ["01110", "10001", "10001", "01110", "10001", "10001", "01110"],
  "9": ["01110", "10001", "10001", "01111", "00001", "00001", "01110"],
  " ": ["00000", "00000", "00000", "00000", "00000", "00000", "00000"],
  "-": ["00000", "00000", "00000", "11111", "00000", "00000", "00000"],
  ":": ["00000", "00100", "00100", "00000", "00100", "00100", "00000"],
  ",": ["00000", "00000", "00000", "00000", "00100", "00100", "01000"],
  ".": ["00000", "00000", "00000", "00000", "00000", "00100", "00100"],
  "%": ["11001", "11010", "00100", "01000", "10110", "00110", "00000"],
  "S": ["01111", "10000", "10000", "01110", "00001", "00001", "11110"],
  "E": ["11111", "10000", "10000", "11110", "10000", "10000", "11111"],
  "R": ["11110", "10001", "10001", "11110", "10100", "10010", "10001"],
  "V": ["10001", "10001", "10001", "10001", "10001", "01010", "00100"],
  "C": ["01110", "10001", "10000", "10000", "10000", "10001", "01110"],
  "L": ["10000", "10000", "10000", "10000", "10000", "10000", "11111"],
  "I": ["01110", "00100", "00100", "00100", "00100", "00100", "01110"],
  "K": ["10001", "10010", "10100", "11000", "10100", "10010", "10001"],
  "T": ["11111", "00100", "00100", "00100", "00100", "00100", "00100"],
  "O": ["01110", "10001", "10001", "10001", "10001", "10001", "01110"],
  "U": ["10001", "10001", "10001", "10001", "10001", "10001", "01110"],
  "P": ["11110", "10001", "10001", "11110", "10000", "10000", "10000"],
  "N": ["10001", "11001", "10101", "10011", "10001", "10001", "10001"],
  "D": ["11100", "10010", "10001", "10001", "10001", "10010", "11100"],
  "W": ["10001", "10001", "10001", "10101", "10101", "10101", "01010"],
  "M": ["10001", "11011", "10101", "10001", "10001", "10001", "10001"],
  "B": ["11110", "10001", "10001", "11110", "10001", "10001", "11110"],
  "G": ["01110", "10001", "10000", "10111", "10001", "10001", "01110"],
  "Y": ["10001", "10001", "01010", "00100", "00100", "00100", "00100"],
  "X": ["10001", "01010", "00100", "00100", "00100", "01010", "10001"],
  "A": ["01110", "10001", "10001", "11111", "10001", "10001", "10001"],
  "H": ["10001", "10001", "10001", "11111", "10001", "10001", "10001"],
  "F": ["11111", "10000", "10000", "11110", "10000", "10000", "10000"],
  "J": ["00111", "00010", "00010", "00010", "10010", "10010", "01100"],
  "Q": ["01110", "10001", "10001", "10001", "10101", "10010", "01101"],
  "Z": ["11111", "00001", "00010", "00100", "01000", "10000", "11111"],
  "(": ["00100", "01000", "10000", "10000", "10000", "01000", "00100"],
  ")": ["00100", "00010", "00001", "00001", "00001", "00010", "00100"],
  "@": ["01110", "10001", "10111", "10101", "10111", "10000", "01110"],
  "/": ["00001", "00010", "00100", "00100", "01000", "10000", "10000"],
  "=": ["00000", "00000", "11111", "00000", "11111", "00000", "00000"],
};

function drawGlyph(buf, W, H, x, y, ch, r, g, b, scale) {
  const rows = GLYPHS[ch] || GLYPHS[ch.toUpperCase()] || GLYPHS["-"];
  for (let row = 0; row < 7; row++) {
    const line = rows[row];
    for (let col = 0; col < 5; col++) {
      if (line[col] !== "1") continue;
      fillRect(buf, W, H, x + col * scale, y + row * scale, scale, scale, r, g, b);
    }
  }
}

function fmtEvent(e) {
  const k = String(e.kind || "").toUpperCase();
  if (e.kind === "scroll") return `SCROLL DY ${e.dy} ${e.x},${e.y}`;
  if (e.kind === "key") return `KEY ${e.ch || ""}`;
  if (e.kind === "touch-up") return "TOUCH UP";
  if (e.kind === "mouse-up") return "MOUSE UP";
  if (e.x != null) return `${k} ${e.x},${e.y}`;
  return k;
}

export function createMockDesktop(width = 1280, height = 720, opts = {}) {
  const W = width;
  const H = height;
  const getStats = typeof opts.getStats === "function" ? opts.getStats : () => ({});
  const marks = []; // {x,y,born,kind} for fading rings
  let cursor = { x: W / 2, y: H / 2, down: false };
  let lastMsg = "waiting for input";
  let clickCount = 0;
  let keys = "";
  let geeks = false;
  const recentServer = []; // {t,kind,...} for /debug + log panel

  // Server-side GEEKS button (top-right), burned into the video. Clicking it
  // (via wire input) toggles the full stats overlay.
  const GEEKS_BTN = { x: W - 150, y: 12, w: 138, h: 36 };
  const inGeeksBtn = (x, y) =>
    x >= GEEKS_BTN.x && x < GEEKS_BTN.x + GEEKS_BTN.w &&
    y >= GEEKS_BTN.y && y < GEEKS_BTN.y + GEEKS_BTN.h;

  function pushServer(ev) {
    recentServer.push({ t: Date.now(), ...ev });
    if (recentServer.length > 100) recentServer.shift();
  }

  function applyClick(x, y, kind) {
    cursor = { x, y, down: true };
    if (inGeeksBtn(x, y)) {
      geeks = !geeks;
      lastMsg = `GEEKS ${geeks ? "ON" : "OFF"}`;
      pushServer({ kind: "geeks", x, y, on: geeks });
      return;
    }
    clickCount++;
    marks.push({ x, y, born: Date.now(), kind });
    if (marks.length > 40) marks.shift();
    lastMsg = `SERVER ${kind} @ ${x},${y}`;
    pushServer({ kind, x, y, clickCount });
  }

  function padR(s, n) {
    s = String(s);
    return s.length >= n ? s.slice(0, n) : s + " ".repeat(n - s.length);
  }

  function logLines() {
    const lines = [`EVENTS ${clickCount}   KEYS ${keys || "-"}`];
    const recent = recentServer.slice(-7);
    for (const e of recent) lines.push(fmtEvent(e).slice(0, 40));
    return lines;
  }

  function statsLines() {
    const s = getStats() || {};
    const h = s.hello || {};
    const c = s.config || {};
    const iN = s.in || {};
    const oN = s.out || {};
    const v = s.video || {};
    const a = s.audio || {};
    const p = s.ping || {};
    const ls = s.lipsync || {};
    const upS = Math.round((s.uptimeMs || 0) / 1000);
    const left = [
      `WIRE PROTOCOL V${s.protocol ?? "-"}`,
      `TRANSPORT ${s.transport || "WSS"}`,
      `STATE ${(s.state || "-").toUpperCase()}`,
      `UPTIME ${upS}S`,
      "",
      "HELLO NEGOTIATION",
      ` VER ${h.version ?? "-"}`,
      ` SIZE ${h.width ?? "-"}X${h.height ?? "-"} @${h.fps ?? "-"}`,
      ` DENSITY ${h.density ?? "-"}`,
      ` AUDIO ${h.audioWanted ? "ON" : "OFF"}`,
      ` BITRATE ${h.bitrate ?? "-"} KBPS`,
      ` NAME ${h.name || "-"}`,
      ` ID ${h.id || "-"}`,
      "",
      "CONFIG (SERVER)",
      ` SIZE ${c.width ?? "-"}X${c.height ?? "-"} @${c.fps ?? "-"}`,
    ];
    const right = [
      "MSGS IN",
      ` HELLO ${padR(iN.Hello ?? 0, 5)} PING ${iN.Ping ?? 0}`,
      ` TOUCH ${padR(iN.Touch ?? 0, 5)} MOUSE ${iN.MouseButton ?? 0}`,
      ` SCROLL ${padR(iN.Scroll ?? 0, 4)} KEY ${iN.Key ?? 0}`,
      ` PONG ${iN.Pong ?? 0}`,
      "",
      "MSGS OUT",
      ` VIDEO ${padR(oN.Video ?? 0, 5)} AUDIO ${oN.Audio ?? 0}`,
      ` CONFIG ${padR(oN.Config ?? 0, 4)} OVERLAY ${oN.Overlay ?? 0}`,
      ` PING ${padR(oN.Ping ?? 0, 5)} PONG ${oN.Pong ?? 0}`,
      "",
      "VIDEO H264",
      ` AUS ${padR(v.aus ?? 0, 6)} KEY ${v.keyframes ?? 0}`,
      ` FPS ${padR(v.fps ?? 0, 6)} GOP ${v.gopMs ?? 0}MS`,
      ` BITRATE ${v.kbps ?? 0} KBPS`,
      "",
      "AUDIO PCM S16 48K",
      ` CHUNKS ${padR(a.chunks ?? 0, 5)} ${a.chunkMs ?? 20}MS`,
      ` BITRATE ${padR(a.kbps ?? 0, 4)} MAXGAP ${a.maxGapMs ?? 0}MS`,
      "",
      "LIPSYNC MEDIA CLOCK",
      ` VPTS ${Math.round(Number(ls.lastVideoPtsUs || 0) / 1000)}MS`,
      ` APTS ${Math.round(Number(ls.lastAudioPtsUs || 0) / 1000)}MS`,
      ` SKEW ${ls.skewMs ?? 0}MS`,
      ` DROP V ${ls.droppedVideo ?? 0} A ${ls.droppedAudio ?? 0}`,
      "",
      "PING RTT",
      ` LAST ${p.lastRttMs ?? 0}MS AVG ${p.avgRttMs ?? 0}MS`,
    ];
    return { left, right };
  }

  function renderStats(out) {
    fillRectA(out, W, H, 0, 0, W, H, 4, 6, 12, 0.82);
    fillRect(out, W, H, 0, 0, W, 4, 90, 200, 255);
    drawText(out, W, H, 24, 16, "GEEKS DEBUG - DROPPIX WIRE PROTOCOL", 120, 220, 255, 3);
    drawText(out, W, H, 24, 44, "CLICK GEEKS AGAIN TO CLOSE", 150, 160, 180, 2);
    const { left, right } = statsLines();
    const lineH = 8 * 2 + 3;
    let ly = 72;
    for (const ln of left) {
      drawText(out, W, H, 24, ly, ln, 220, 235, 245, 2);
      ly += lineH;
    }
    let ry = 72;
    for (const ln of right) {
      drawText(out, W, H, Math.floor(W / 2) + 10, ry, ln, 220, 235, 245, 2);
      ry += lineH;
    }
  }

  return {
    width: W,
    height: H,
    get geeks() {
      return geeks;
    },
    setGeeks(on) {
      geeks = !!on;
    },
    get marks() {
      return recentServer.slice();
    },
    clearMarksDebug() {
      recentServer.length = 0;
      marks.length = 0;
    },
    onTouch(contacts) {
      if (!contacts.length) {
        cursor.down = false;
        lastMsg = "SERVER touch up";
        pushServer({ kind: "touch-up" });
        return;
      }
      const c = contacts[0];
      const { x, y } = normToPx(c.x, c.y, W, H);
      const wasUp = !cursor.down;
      cursor = { x, y, down: true };
      if (wasUp) applyClick(x, y, "touch");
      else lastMsg = `SERVER drag @ ${x},${y}`;
    },
    onMouseButton(m) {
      if (!m) return;
      const { x, y } = normToPx(m.x, m.y, W, H);
      if (m.action === 1) {
        const kind = m.button === 1 ? "right" : m.button === 2 ? "middle" : "left";
        applyClick(x, y, kind);
      } else {
        cursor = { x, y, down: false };
        lastMsg = `SERVER mouse up @ ${x},${y}`;
        pushServer({ kind: "mouse-up", x, y });
      }
    },
    onScroll(s) {
      if (!s) return;
      const { x, y } = normToPx(s.x, s.y, W, H);
      cursor = { x, y, down: cursor.down };
      lastMsg = `SERVER scroll dy=${s.dy} @ ${x},${y}`;
      pushServer({ kind: "scroll", x, y, dy: s.dy });
    },
    onKey(k) {
      if (!k || k.action === 0) return;
      const ch = keycodeToChar(k.keycode);
      // 'G' toggles the GEEKS stats overlay (same as clicking the button).
      if (ch === "G") {
        geeks = !geeks;
        lastMsg = `GEEKS ${geeks ? "ON" : "OFF"}`;
        pushServer({ kind: "geeks", on: geeks });
        return;
      }
      if (ch) {
        keys = (keys + ch).slice(-24);
        lastMsg = `SERVER key ${ch}`;
        pushServer({ kind: "key", ch, keycode: k.keycode });
      }
    },

    /**
     * Paint the server overlay ON TOP of an existing RGB24 movie frame:
     * a translucent event-log panel, fading click rings, and the live cursor.
     */
    overlayFrame(out) {
      const now = Date.now();
      const lines = logLines();
      const scale = 2;
      const lineH = 9 * scale;
      const panelW = Math.min(W - 24, 470);
      const panelH = 14 + lineH * lines.length + 8;
      fillRectA(out, W, H, 12, 12, panelW, panelH, 8, 10, 18, 0.6);
      fillRect(out, W, H, 12, 12, panelW, 3, 90, 200, 255);
      let ty = 12 + 8;
      let first = true;
      for (const ln of lines) {
        const col = first ? [120, 220, 255] : [235, 235, 240];
        drawText(out, W, H, 20, ty, ln, col[0], col[1], col[2], scale);
        ty += lineH;
        first = false;
      }

      for (const m of marks) {
        const age = (now - m.born) / 1000;
        if (age > 3) continue;
        const a = Math.max(0, 1 - age / 3);
        const rad = 16 + age * 44;
        strokeCircle(out, W, H, m.x, m.y, rad, 255, 40, 80, 4);
        fillCircle(out, W, H, m.x, m.y, 9, 255, 220, 40, a);
        drawCross(out, W, H, m.x, m.y, 20, 20, 20, 20);
        drawText(out, W, H, m.x + 14, m.y - 26, `${m.kind} ${m.x},${m.y}`.toUpperCase(), 255, 240, 120, 2);
      }

      drawCross(out, W, H, cursor.x, cursor.y, 14, 255, 255, 255);
      if (cursor.down) fillCircle(out, W, H, cursor.x, cursor.y, 6, 255, 80, 80, 1);

      // Server-side GEEKS button (click toggles the stats overlay).
      const bg = geeks ? [40, 150, 90] : [40, 60, 110];
      fillRectA(out, W, H, GEEKS_BTN.x, GEEKS_BTN.y, GEEKS_BTN.w, GEEKS_BTN.h, bg[0], bg[1], bg[2], 0.85);
      fillRect(out, W, H, GEEKS_BTN.x, GEEKS_BTN.y, GEEKS_BTN.w, 2, 120, 220, 255);
      drawText(out, W, H, GEEKS_BTN.x + 14, GEEKS_BTN.y + 11, "GEEKS", 240, 245, 255, 2);

      fillRectA(out, W, H, 0, H - 26, W, 26, 8, 10, 18, 0.6);
      drawText(out, W, H, 12, H - 20, "SERVER OVERLAY BURNED INTO H264 STREAM", 150, 200, 255, 2);

      if (geeks) renderStats(out);
    },

    /** Standalone full desktop framebuffer (no movie); used by desktop encoder. */
    render(out, tSec) {
      const pulse = 0.5 + 0.5 * Math.sin(tSec * 2);
      const bandW = Math.max(1, Math.floor(W / 8));
      for (let band = 0; band < 8; band++) {
        const r = Math.min(255, Math.floor(30 + band * 20 * pulse));
        const g = Math.min(255, Math.floor(40 + ((band + 2) % 5) * 16));
        const b = Math.min(255, Math.floor(70 + ((band + 4) % 7) * 14));
        fillRect(out, W, H, band * bandW, 0, bandW, H, r, g, b);
      }
      fillRect(out, W, H, 0, 0, W, 56, 28, 28, 40);
      drawText(out, W, H, 16, 16, "DROPPIX MOCK DESKTOP", 230, 230, 240, 3);
      this.overlayFrame(out);
    },
  };
}

/** Minimal evdev-ish map for a-z / 0-9 from web keymap codes used by client. */
function keycodeToChar(code) {
  const map = {
    16: "Q", 17: "W", 18: "E", 19: "R", 20: "T", 21: "Y", 22: "U", 23: "I",
    24: "O", 25: "P", 30: "A", 31: "S", 32: "D", 33: "F", 34: "G", 35: "H",
    36: "J", 37: "K", 38: "L", 44: "Z", 45: "X", 46: "C", 47: "V", 48: "B",
    49: "N", 50: "M", 2: "1", 3: "2", 4: "3", 5: "4", 6: "5", 7: "6", 8: "7",
    9: "8", 10: "9", 11: "0", 57: " ",
  };
  return map[code] || "";
}
