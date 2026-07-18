/** droppix wire protocol v5 bodies. WSS frames are [type][body] (no TCP u32 length). */

export const kProtocolVersion = 5;

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
} as const;
export type MsgType = (typeof MsgType)[keyof typeof MsgType];

export interface TouchContact {
  id: number;
  x: number;
  y: number;
  pressure: number;
}

function putU16(v: number, out: number[]) {
  out.push((v >>> 8) & 0xff, v & 0xff);
}
function putU32(v: number, out: number[]) {
  out.push((v >>> 24) & 0xff, (v >>> 16) & 0xff, (v >>> 8) & 0xff, v & 0xff);
}
function putU64(v: bigint, out: number[]) {
  const hi = Number((v >> 32n) & 0xffffffffn);
  const lo = Number(v & 0xffffffffn);
  putU32(hi, out);
  putU32(lo, out);
}
function getU16(b: Uint8Array, o: number) {
  return (b[o]! << 8) | b[o + 1]!;
}
function getU32(b: Uint8Array, outOff: number) {
  return (
    ((b[outOff]! << 24) | (b[outOff + 1]! << 16) | (b[outOff + 2]! << 8) | b[outOff + 3]!) >>> 0
  );
}
function getU64(b: Uint8Array, o: number): bigint {
  return (BigInt(getU32(b, o)) << 32n) | BigInt(getU32(b, o + 4));
}
function getI16(b: Uint8Array, o: number) {
  const u = getU16(b, o);
  return u > 0x7fff ? u - 0x10000 : u;
}

export function frameMessage(type: MsgType, body: Uint8Array): Uint8Array {
  const out = new Uint8Array(1 + body.length);
  out[0] = type;
  out.set(body, 1);
  return out;
}

export function parseFrame(data: ArrayBuffer | Uint8Array): { type: MsgType; body: Uint8Array } | null {
  const u = data instanceof Uint8Array ? data : new Uint8Array(data);
  if (u.length < 1) return null;
  return { type: u[0]! as MsgType, body: u.subarray(1) };
}

export function encodeHello(
  version: number,
  width: number,
  height: number,
  density: number,
  name: string,
  id: string,
  fps = 0,
  audioWanted = 0,
  orientationCode = 0,
  bitrateKbps = 0,
): Uint8Array {
  const nameBytes = new TextEncoder().encode(name);
  const idBytes = new TextEncoder().encode(id);
  const out: number[] = [];
  putU32(version, out);
  putU32(width, out);
  putU32(height, out);
  putU32(density, out);
  putU32(fps, out);
  out.push(audioWanted & 0xff, orientationCode & 0xff);
  putU32(bitrateKbps, out);
  putU16(nameBytes.length, out);
  for (const c of nameBytes) out.push(c);
  putU16(idBytes.length, out);
  for (const c of idBytes) out.push(c);
  return new Uint8Array(out);
}

export function encodeTouch(contacts: TouchContact[]): Uint8Array {
  const n = Math.min(contacts.length, 10);
  const out: number[] = [n];
  for (let i = 0; i < n; i++) {
    const c = contacts[i]!;
    out.push(c.id & 0xff);
    putU16(c.x, out);
    putU16(c.y, out);
    putU16(c.pressure, out);
  }
  return new Uint8Array(out);
}

export function encodeScroll(dx: number, dy: number, x: number, y: number): Uint8Array {
  const out: number[] = [];
  putU16(dx & 0xffff, out);
  putU16(dy & 0xffff, out);
  putU16(x, out);
  putU16(y, out);
  return new Uint8Array(out);
}

export function encodeMouseButton(button: number, action: number, x: number, y: number): Uint8Array {
  const out: number[] = [button & 0xff, action & 0xff];
  putU16(x, out);
  putU16(y, out);
  return new Uint8Array(out);
}

export function encodeKey(keycode: number, action: number): Uint8Array {
  const out: number[] = [];
  putU16(keycode, out);
  out.push(action & 0xff);
  return new Uint8Array(out);
}

export function decodeConfig(body: Uint8Array): {
  width: number;
  height: number;
  fps: number;
  extradata: Uint8Array;
} | null {
  if (body.length < 12) return null;
  return {
    width: getU32(body, 0),
    height: getU32(body, 4),
    fps: getU32(body, 8),
    extradata: body.subarray(12),
  };
}

export function decodeVideo(body: Uint8Array): {
  ptsUs: bigint;
  keyframe: boolean;
  nal: Uint8Array;
} | null {
  if (body.length < 9) return null;
  return {
    ptsUs: getU64(body, 0),
    keyframe: body[8] !== 0,
    nal: body.subarray(9),
  };
}

export function decodeOverlay(body: Uint8Array): number {
  return body.length > 0 ? body[0]! : 0;
}

/** Locked vectors from host/tests/test_protocol.cpp (payload only, no TCP length). */
export function encodeMessageTcp(type: MsgType, body: Uint8Array): Uint8Array {
  const len = 1 + body.length;
  const out = new Uint8Array(4 + len);
  out[0] = (len >>> 24) & 0xff;
  out[1] = (len >>> 16) & 0xff;
  out[2] = (len >>> 8) & 0xff;
  out[3] = len & 0xff;
  out[4] = type;
  out.set(body, 5);
  return out;
}

export { getU16, getI16 };
