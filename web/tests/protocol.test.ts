import { test } from "node:test";
import assert from "node:assert/strict";
import {
  MsgType,
  encodeMessageTcp,
  encodeKey,
  encodeTouch,
  encodeHello,
  frameMessage,
  kProtocolVersion,
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

test("protocol version is 6 (HELLO v6 wall)", () => {
  assert.equal(kProtocolVersion, 6);
});

test("web pairing MsgType values match the host (web_pin.h kMsgPair/kMsgPairResult)", () => {
  // Must stay byte-identical to host/src/web_pin.h: kMsgPair=20, kMsgPairResult=21.
  assert.equal(MsgType.Pair, 20);
  assert.equal(MsgType.PairResult, 21);
});

test("HELLO v6 carries wall_col/wall_row after bitrate (col=2,row=1)", () => {
  // Byte-identical to the C++ (test_protocol) and Kotlin (encodeHelloV6Wall) vectors:
  // wall_col at bytes 26-27, wall_row at 28-29 (big-endian u16), strings start at 30.
  const body = encodeHello(6, 1280, 800, 160, "", "", 30, 0, 0, 8000, 2, 1);
  assert.deepEqual([...body.subarray(26, 30)], [0x00, 0x02, 0x00, 0x01]);
  // name_len (u16) = 0 immediately after the wall fields, at offset 30.
  assert.deepEqual([...body.subarray(30, 32)], [0x00, 0x00]);
});

test("HELLO v5 body omits wall fields (back-compat)", () => {
  // A v5-versioned encode writes nothing after bitrate but the strings.
  const body = encodeHello(5, 1280, 800, 160, "", "", 30, 0, 0, 8000, 2, 1);
  // name_len (u16) = 0 sits right after bitrate (offset 26), no wall bytes.
  assert.deepEqual([...body.subarray(26, 28)], [0x00, 0x00]);
});
