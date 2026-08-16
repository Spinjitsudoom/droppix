# G-2026-08-10-web-local-ca-trust: browser-trusted HTTPS for a LAN-only self-signed host

- **ID:** `G-2026-08-10-web-local-ca-trust`
- **Tags:** `host`, `tls`, `gotcha`
- **Date:** 2026-08-10
- **Related:** *(none)*

## Context

The web PWA client always showed a browser "connection not private" warning:
`CertManager` generated a **bare self-signed** leaf cert (`openssl req -x509
... /CN=droppix`), which no browser/OS trust store can ever validate, since
nothing vouches for it. There's no public domain to get a Let's Encrypt cert
for a LAN-only device with a dynamic IP.

## Fix: a persistent local CA, leaf certs signed by it

`CertManager` now generates two tiers, both in the same config dir:

- `ca.pem`/`ca-key.pem` — generated **once**, `CA:true`, 20-year validity,
  **never rotated** by `regenerate()`. Rotating it would silently untrust
  every device that already installed it.
- `cert.pem`/`key.pem` — the leaf, still rotated every launch (unchanged
  per-restart pairing-code behavior), but now a CSR **signed by the CA**
  instead of self-signed, with `subjectAltName` covering every current LAN
  IPv4 (`lan_ipv4_ifaces()`) + the mDNS hostname — a bare CN is not enough
  for modern browser validation.

`host/src/web_frontend.cpp` serves the CA's public cert, unauthenticated
(same tier as `/config.json`/static files — the CA cert grants no access, it
only lets a browser *validate* future leaf certs), at `GET /ca.crt` with
`Content-Type: application/x-x509-ca-cert` — the MIME type that makes
Android's browser offer "install certificate" instead of rendering raw PEM
text. `web/public/index.html` links to it from the connect card.

## The unavoidable bootstrap step

The very first page load from any new device still shows one warning
interstitial — `serve_until_stream` requires a completed TLS handshake
before it can route *any* path, including `/ca.crt` itself, so there's no
way to fetch the CA over an already-trusted channel on the first visit. This
is the same one-time step every local-CA tool (mkcert, etc.) requires. After
that single "proceed anyway" + installing the CA, every future connection —
including to a rotated leaf cert, or a different LAN IP once SAN covers it —
is fully trusted with no warning.

## How to detect this in the future

- `openssl verify -CAfile ca.pem cert.pem` must print `OK` — if a leaf cert
  generation path changes, this is the one command that would have caught a
  broken chain (also exercised by `CertManager` gtest, `test_cert_manager.cpp`).
- If the CA is ever accidentally regenerated (e.g. a future refactor merges
  it back into `regenerate()`'s delete step), every already-trusted device
  silently starts showing the warning again — `RegenerateKeepsTheSameCa`
  guards this by fingerprint-comparing `ca.pem` across two `ensure()` calls.
