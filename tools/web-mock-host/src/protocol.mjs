/** WSS binding: one binary frame = [type u8][body…] (no TCP length prefix). */

export const MsgType = {
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
  Pen: 15,
};

export const TYPE_NAME = Object.fromEntries(
  Object.entries(MsgType).map(([k, v]) => [v, k]),
);

function putU16(v, out) {
  out.push((v >>> 8) & 0xff, v & 0xff);
}
function putU32(v, out) {
  out.push((v >>> 24) & 0xff, (v >>> 16) & 0xff, (v >>> 8) & 0xff, v & 0xff);
}
function putU64(v, out) {
  const bi = typeof v === "bigint" ? v : BigInt(v);
  putU32(Number((bi >> 32n) & 0xffffffffn), out);
  putU32(Number(bi & 0xffffffffn), out);
}
function getU16(b, o) {
  return (b[o] << 8) | b[o + 1];
}
function getU32(b, o) {
  return ((b[o] << 24) | (b[o + 1] << 16) | (b[o + 2] << 8) | b[o + 3]) >>> 0;
}
function getI16(b, o) {
  const u = getU16(b, o);
  return u > 0x7fff ? u - 0x10000 : u;
}

export function frame(type, body = new Uint8Array()) {
  const out = new Uint8Array(1 + body.length);
  out[0] = type;
  out.set(body, 1);
  return out;
}

export function parseFrame(buf) {
  const u = buf instanceof Uint8Array ? buf : new Uint8Array(buf);
  if (u.length < 1) return null;
  return { type: u[0], body: u.subarray(1) };
}

export function encodeConfig(w, h, fps) {
  const out = [];
  putU32(w, out);
  putU32(h, out);
  putU32(fps, out);
  return new Uint8Array(out);
}

export function encodeVideo(ptsUs, keyframe, nal) {
  const out = [];
  putU64(ptsUs, out);
  out.push(keyframe ? 1 : 0);
  const body = new Uint8Array(out.length + nal.length);
  body.set(out, 0);
  body.set(nal, out.length);
  return body;
}

export function encodeOverlay(show) {
  return new Uint8Array([show ? 1 : 0]);
}

export function decodeHello(body) {
  if (body.length < 16) return null;
  let o = 0;
  const version = getU32(body, o); o += 4;
  const width = getU32(body, o); o += 4;
  const height = getU32(body, o); o += 4;
  const density = getU32(body, o); o += 4;
  let fps = 0, audioWanted = 0, orientation = 0, bitrate = 0;
  let name = "", id = "";
  if (body.length >= 16 + 4 + 2) {
    fps = getU32(body, o); o += 4;
    if (o < body.length) audioWanted = body[o++];
    if (o < body.length) orientation = body[o++];
    if (o + 4 <= body.length) {
      bitrate = getU32(body, o); o += 4;
    }
    if (o + 2 <= body.length) {
      const nlen = getU16(body, o); o += 2;
      name = new TextDecoder().decode(body.subarray(o, o + nlen));
      o += nlen;
    }
    if (o + 2 <= body.length) {
      const ilen = getU16(body, o); o += 2;
      id = new TextDecoder().decode(body.subarray(o, o + ilen));
    }
  }
  return { version, width, height, density, fps, audioWanted, orientation, bitrate, name, id };
}

export function decodeTouch(body) {
  if (body.length < 1) return [];
  const count = body[0];
  const contacts = [];
  let o = 1;
  for (let i = 0; i < count && o + 7 <= body.length; i++) {
    const id = body[o++];
    const x = getU16(body, o); o += 2;
    const y = getU16(body, o); o += 2;
    const pressure = getU16(body, o); o += 2;
    contacts.push({ id, x, y, pressure });
  }
  return contacts;
}

export function decodeScroll(body) {
  if (body.length < 8) return null;
  return {
    dx: getI16(body, 0),
    dy: getI16(body, 2),
    x: getU16(body, 4),
    y: getU16(body, 6),
  };
}

export function decodeMouseButton(body) {
  if (body.length < 6) return null;
  return {
    button: body[0],
    action: body[1],
    x: getU16(body, 2),
    y: getU16(body, 4),
  };
}

export function decodeKey(body) {
  if (body.length < 3) return null;
  return { keycode: getU16(body, 0), action: body[2] };
}
