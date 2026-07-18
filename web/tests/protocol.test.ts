import { test } from "node:test";
import assert from "node:assert/strict";
import {
  MsgType,
  encodeMessageTcp,
  encodeKey,
  encodeTouch,
  frameMessage,
} from "../src/protocol.ts";

test("TCP encode VIDEO matches locked hex prefix", () => {
  const m = encodeMessageTcp(MsgType.Video, new Uint8Array([0xaa, 0xbb]));
  assert.deepEqual(
    [...m],
    [0x00, 0x00, 0x00, 0x03, 0x03, 0xaa, 0xbb],
  );
});

test("TCP encode AUDIO body", () => {
  const m = encodeMessageTcp(MsgType.Audio, new Uint8Array([0xde, 0xad, 0xbe, 0xef]));
  assert.deepEqual(
    [...m],
    [0x00, 0x00, 0x00, 0x05, 0x09, 0xde, 0xad, 0xbe, 0xef],
  );
});

test("KEY(300,2) body", () => {
  const body = encodeKey(300, 2);
  assert.deepEqual([...body], [0x01, 0x2c, 0x02]);
});

test("TOUCH one contact body", () => {
  const body = encodeTouch([{ id: 2, x: 0x0102, y: 0x0304, pressure: 0x0506 }]);
  assert.deepEqual([...body], [0x01, 0x02, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06]);
});

test("WSS frame is type+body without length", () => {
  const f = frameMessage(MsgType.Ping, new Uint8Array([1, 2, 3]));
  assert.deepEqual([...f], [0x04, 1, 2, 3]);
});
