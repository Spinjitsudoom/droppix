# WiFi transport + device discovery + Android client GUI

**Status:** Shipped on master.
directions; implemented in two parts (Part 1 then Part 2).

## Goal

Let the tablet and PC connect over the **local network** (not just USB), with a real
**Connect screen** on the Android app and **mDNS discovery** so neither end has to type
IP addresses. Both ends can discover the other and initiate; the video always streams
tablet→PC. Gate WiFi connections with an **approve-on-host** prompt (remembered).

## Architecture

### Connection model (unchanged direction)
The video socket is **always opened by the tablet to the PC** (the PC stays the TCP
server; it already binds `INADDR_ANY`, so WiFi works today once the tablet dials the
PC's LAN IP instead of `127.0.0.1`). "Who initiates" is layered on top of this.

### Discovery (mDNS)
- **PC advertises** `_droppix._tcp` on its stream port via the system **Avahi** daemon
  (`avahi-publish-service`), managed by the host GUI as a `QProcess` while the streamer
  is running. The host GUI **browses** `_droppix-client._tcp` via `avahi-browse -rptk`
  (parseable, resolved) on a timer to list tablets.
- **Tablet advertises** `_droppix-client._tcp` (carrying its **wake port** in a TXT
  record) and **browses** `_droppix._tcp`, both via Android's built-in **NSD**
  (`NsdManager`, API 16+).
- If Avahi/`avahi-browse` is missing, discovery degrades gracefully to **manual IP**
  (both ends still work by typing an address).

### Part 1 — tablet initiates
Tablet Connect screen lists discovered PCs → tap → opens the stream socket to that PC.

### Part 2 — PC initiates
Host GUI lists discovered tablets → **Connect** → (Start the streamer if needed and)
send a one-shot **wake** datagram to the tablet's wake port. The tablet receives it and
opens the stream socket back to the PC. Streaming direction is unchanged.

### Security — approve on host
The tablet's `HELLO` carries a device **name** + a stable **id**. The streamer
auto-allows `127.0.0.1` (USB/adb-reverse) but gates non-localhost (WiFi) peers:
- The streamer prints `approve-request id=<id> name=<name> ip=<ip>` to stderr and waits
  (timeout) for an `approve <id>` / `deny <id>` reply.
- **Unified stdin control channel.** The streamer's stdin reader (added for Stop, where
  EOF = quit) is extended to parse **line commands**: `approve <id>` / `deny <id>` route
  to the approval logic (a mutex-guarded result map the accept path waits on); a closed
  stdin (EOF) still sets `g_stop`. So one reader serves both Stop and approval — no
  second consumer of stdin.
- The **GUI** reads the streamer's stderr and owns its stdin: it auto-writes `approve
  <id>` for ids in its remembered set, else shows *"Allow `<name>` (`<ip>`) to
  connect?"*; **Allow** persists the id and writes `approve <id>`. CONFIG (and thus
  video) is withheld until the reply arrives.
- Remembered ids persist in `~/.config/droppix_gui/approved` (one id per line),
  read/written by the GUI; "Forget devices" clears it.
- Part-2 connections are auto-approved (the PC chose the tablet). The tablet shows a
  lightweight *"Connect to `<PC>`?"* confirm on a wake it didn't initiate.
- Without the GUI (CLI), non-localhost peers are auto-allowed unless `--approve` is
  passed; the GUI always passes `--approve` for the evdi/WiFi path.

## Components

### Wire protocol (`host/src/protocol.*` + Kotlin mirror), version → 3
- `HELLO` body gains, after the existing `version,width,height,density` (16 bytes):
  `u16 name_len + name(UTF-8)` then `u16 id_len + id(UTF-8)`. `decode_hello` stays
  back-compatible: a 16-byte body (old v2/USB client) yields empty name/id. Round-trip
  + wire-layout tests both ends.
- **Wake datagram** (separate UDP channel, not the stream socket): `magic "DPXW"
  (4 bytes) + u16 port`. The tablet connects to `(sender_ip, port)`. `encode_wake` /
  `decode_wake` with tests both ends.
- Approval uses no new stream message — it's the stderr/stdin handshake above.

### Host
- **`mdns.{h,cpp}` (new, pure):** parse `avahi-browse -rptk` output → a list of
  `{name, address, port, txt}` (unit-tested); build the `avahi-publish-service`
  argv. No libavahi dependency — shell out to the CLI tools.
- **`stream_main` / `stream_daemon`:** `--approve` flag; on a non-localhost accept,
  do the approve handshake (read HELLO name/id, emit `approve-request`, await stdin
  reply, with a timeout) before `send_config`. `--wake-tablet <ip:port>` is **not**
  needed here; the wake is sent by the GUI.
- **Host GUI:**
  - Advertise: start/stop an `avahi-publish-service "<host> (droppix)" _droppix._tcp
    <port>` `QProcess` alongside the streamer.
  - **Devices panel:** a list of discovered tablets (timer-driven `avahi-browse`), each
    with **Connect** → Start (if needed) + send the wake datagram (a small `QUdpSocket`
    write of `DPXW`+port to the tablet's resolved `ip:wakePort`).
  - **Approval:** parse `approve-request` from the streamer log; remembered ids in
    `~/.config/droppix_gui/approved` (via ProfileStore-style storage); `QMessageBox`
    for new ids; reply on the streamer's stdin.

### Android client
- **Refactor to two activities:** `ConnectActivity` (new launcher, Material dark/teal
  theme) and `StreamActivity` (the existing fullscreen surface, now `fullSensor`,
  started with `host`/`port` extras). On disconnect, `StreamActivity` finishes back to
  Connect.
- **`ConnectActivity`:** a discovered-PCs list (NSD browse `_droppix._tcp`, name + IP),
  a manual `IP[:port]` field, a "reconnect to last" shortcut (persisted), and status.
  Material Views (`com.google.android.material`), dark theme matching the host.
- **`net/Discovery.kt`:** NSD wrappers — browse PCs, register `_droppix-client._tcp`,
  and a UDP **wake listener** that, on a valid `DPXW` packet, prompts/launches a
  connection to the sender. Pure parsing/encoding (wake bytes, host:port) unit-tested;
  NSD glue verified manually.
- **Manifest:** add `ACCESS_NETWORK_STATE`, `ACCESS_WIFI_STATE`; `ConnectActivity` as
  launcher; keep USB working (localhost is just another target the user can type/tap).
- `TransportClient.run` already takes `host`/`port`; `StreamActivity` passes the chosen
  endpoint instead of the hardcoded `127.0.0.1`.

## Data flow

- **Part 1:** Tablet NSD-browses → user taps PC → `StreamActivity(host,port)` →
  `TransportClient` connects → host accepts, reads `HELLO(name,id)` → (non-localhost)
  approve handshake → on Allow, `CONFIG` + video. USB path: dial `127.0.0.1`, auto-allowed.
- **Part 2:** Host GUI `avahi-browse` lists tablets → Connect → Start + UDP wake to the
  tablet → tablet wake-listener → confirm → `StreamActivity(pc_ip, pc_port)` → connects
  → auto-approved (PC-initiated).

## Error handling
- Avahi/tools absent → hide discovery lists, keep manual IP / "reconnect to last".
- Connect failure / refused / timeout → status message, stay on Connect screen.
- Approval timeout or Deny → streamer closes the socket; tablet shows "rejected".
- Wake to an offline tablet → no-op (UDP, best-effort); the list refreshes.

## Testing
- **Host unit tests:** `HELLO` v3 round-trip + 16-byte back-compat + wire layout; wake
  encode/decode; `avahi-browse` parser; `approve-request` line format.
- **Kotlin unit tests:** `encodeHello` v3 byte-match with the host; wake encode/decode;
  host:port parsing.
- **Manual e2e:** WiFi connect via tablet-scan (Part 1) and PC-scan (Part 2); approval
  dialog + remember; manual IP; USB/`adb reverse` still works; touch + orientation intact.

## Out of scope / deferred
- PIN pairing and TLS/encryption (the H.264 stream stays plaintext on the LAN — a later
  security phase). Multi-client. Auto-reconnect heuristics beyond "reconnect to last".

## Implementation order
1. **Part 1:** protocol v3 (name/id) → host advertise + `--approve` handshake + GUI
   approval/persistence → Android two-activity split + `ConnectActivity` + NSD browse +
   manual IP + WiFi connect.
2. **Part 2:** tablet advertise + wake listener → host `avahi-browse` parser + Devices
   panel + wake send.
