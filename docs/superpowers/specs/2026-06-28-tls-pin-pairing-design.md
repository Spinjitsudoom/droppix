# TLS encryption + PIN pairing

**Status:** Shipped on master.
the PC to the tablet via a 6-digit pairing code derived from the PC's self-signed
certificate (cert pinning). Keeps Part-1 approve-on-host for the PC→tablet direction.

## Goal

Make the WiFi stream confidential and man-in-the-middle-resistant: wrap the streaming TCP
in **TLS**, and on first WiFi connect have the tablet verify the PC's identity by a short
**PIN** (entered on the tablet, shown on the PC) that is bound to the PC's certificate, then
**pin** that cert. USB/localhost stays friction-free (auto-trusted).

## Trust model (asymmetric, by design)

- **PIN = the tablet verifies the PC.** The PC owns a persistent self-signed cert. Its
  **pairing code** = 6 digits derived from `SHA-256(cert DER)`, shown in the host GUI. On a
  first WiFi connect the tablet completes the TLS handshake, derives the same code from the
  cert it *actually received*, and asks the user to enter the PC's code. Match → the tablet
  **pins** that cert's fingerprint (per host). A MITM presents a different cert → different
  code → mismatch → rejected. Later connects only check the pinned cert; a changed cert →
  "PC identity changed — re-pair?".
- **approve-on-host = the PC verifies the tablet** (Part 1, unchanged).
- **localhost (`127.0.0.1`) is auto-trusted** by the tablet (USB/adb-reverse is physically
  secure) — TLS still wraps it, but no PIN and no pin-store entry. Mirrors the host already
  auto-allowing `127.0.0.1`.

So first pairing has one action on each side (PIN on tablet, Allow on PC); afterwards both
are silent.

## Architecture

The entire streaming TCP connection (HELLO/CONFIG/VIDEO/INPUT/ORIENTATION) is wrapped in TLS
— host = **OpenSSL** server, tablet = **`SSLSocket`** client + a custom pinning
`X509TrustManager`. mDNS advertise/browse and the UDP wake are unchanged (they carry no
screen/input content). TLS is **opt-in via `--tls`** on the streamer so the existing host
unit tests (in-process plaintext clients) keep working; the GUI always passes `--tls`, and
the Android app always uses TLS for real connections.

## Pairing-code + fingerprint (shared, pure, byte-identical)

- `derive_pairing_code(der: bytes) -> "NNNNNN"`: `SHA-256(der)`, take the first 4 bytes as a
  big-endian u32, `% 1000000`, zero-pad to 6 digits. Implemented identically host (C++) and
  Kotlin; unit-tested against a fixed DER input so both produce the same string.
- Cert fingerprint (for pinning) = `SHA-256(der)` hex.

## Components

### Host
- **`host/src/pairing_code.h`** (new, pure, header-only) — `std::string derive_pairing_code(const std::vector<unsigned char>& der)`. Uses OpenSSL `SHA256`. Unit-tested.
- **Cert generation (GUI):** on first run the GUI creates `~/.config/droppix_gui/cert.pem` +
  `key.pem` if absent, via `openssl req -x509 -newkey rsa:2048 -nodes -keyout key.pem -out
  cert.pem -days 3650 -subj "/CN=droppix"`. It reads the cert (DER) to show the pairing code.
- **GUI display:** a "Pairing code: `NNNNNN`" label (in the Settings/Status area), derived from
  the cert. A small "Re-generate identity" affordance is out of scope (delete the files to reset).
- **`args_builder.cpp`:** the evdi/streamer command gains `--tls --cert <certPath> --key <keyPath>`.
- **`transport_server.{h,cpp}`:** a `bool tls` + cert/key paths; when enabled, after `accept`,
  `SSL_accept` on an `SSL*` bound to the client fd (a process-wide `SSL_CTX` loaded with the
  cert+key); `read_hello`/`send_config`/`send_video`/`poll_control` route byte I/O through
  `SSL_read`/`SSL_write`. `wait_readable(fd)` still gates timeouts on the raw fd; `SSL_pending()`
  is checked so buffered TLS records aren't missed by `poll`. Plaintext path unchanged when `tls=false`.
- **`stream_main.cpp`:** `--tls`, `--cert <path>`, `--key <path>` flags → `TransportServer`.
- **CMake:** `find_package(OpenSSL REQUIRED)`; link `OpenSSL::SSL OpenSSL::Crypto` into `droppix_core`.

### Android
- **`net/PairingCode.kt`** (new, pure) — `fun derive(der: ByteArray): String` (same algorithm). Unit-tested.
- **`net/TlsTrust.kt`** (new) — a pinning store (SharedPreferences `host -> certFingerprintHex`)
  plus an `SSLSocketFactory`/`X509TrustManager` that captures the server chain without throwing
  (trust is decided by the pinning logic, not CA validation). Helpers: `isPaired(host)`,
  `pinnedFingerprint(host)`, `pin(host, fp)`, `clear(host)`.
- **Pairing happens in `ConnectActivity.connectTo(host, port)`** (the single funnel for both
  tablet-initiated and wake-initiated connects):
  - `host == 127.0.0.1` → launch StreamActivity directly (auto-trust).
  - host already pinned → launch StreamActivity (it will verify the pin).
  - else (new host) → run a short **pairing probe**: open an `SSLSocket` to `host:port`, do the
    handshake, read `session.peerCertificates[0]`, derive the code, show a dialog "Enter the
    6-digit code shown on the PC"; on a matching entry, `TlsTrust.pin(host, fp)` and launch
    StreamActivity; on mismatch/cancel, abort with a status message. (The probe socket is closed;
    the real stream reconnects.)
- **`TransportClient.run`** uses an `SSLSocket` (via `TlsTrust`) instead of a plain `Socket`;
  after handshake it verifies the server cert fingerprint == the pinned one for that host
  (localhost exempt). A mismatch aborts the session with a distinct "PC identity changed" error
  surfaced to the user as a re-pair prompt (clear the pin to re-pair).

## Data flow
- **First WiFi pair:** tap PC → probe handshake → derive code → PIN dialog → match → pin →
  StreamActivity TLS-connects (verifies pin) → HELLO → approve-on-host → stream.
- **Reconnect:** tap PC → pinned → StreamActivity TLS verify pin → stream (no PIN; host
  auto-approves the remembered id).
- **Cert changed:** TLS verify fingerprint mismatch → "PC identity changed" → user clears the
  pin and re-pairs.
- **USB:** `connectTo(127.0.0.1)` → auto-trust → TLS connect accepting the localhost cert → stream.

## Error handling
- Handshake failure (host not running `--tls`, or unreachable) → status message, stay on Connect.
- PIN mismatch → dialog lets the user retry or cancel (cert is not pinned).
- Pinned-cert mismatch on a streaming connect → abort + "PC identity changed, re-pair" (offer to clear the pin).
- Missing/unreadable cert on the host → the streamer logs and (without `--tls`) falls back to plaintext only in CLI; the GUI always supplies a generated cert.

## Testing
- **Host unit:** `derive_pairing_code(fixed DER) == "NNNNNN"`; loads a generated cert.
- **Kotlin unit:** `PairingCode.derive(same fixed DER)` == the same string (byte-identical with host); pin-store round-trip.
- **Manual e2e:** pair over WiFi (PIN), reconnect (silent), regenerate the host cert → tablet
  warns/re-pairs, USB connects with no PIN, and a deliberately-wrong PIN is rejected.

## Out of scope / deferred
- CA-signed certificates; mutual TLS (client certs); PAKE (SPAKE2). Encrypting the mDNS/wake
  side channels. A GUI "forget paired devices" manager beyond delete-the-prefs. Stylus/pressure
  (the next, separate feature).
