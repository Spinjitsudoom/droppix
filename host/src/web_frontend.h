#pragma once
#include <csignal>
#include <memory>
#include <string>
#include <vector>
#include "byte_channel.h"
#include "transport_server.h"

namespace droppix {

// Serves static files from web_root over the TransportServer TLS listener and waits
// until a streaming client is ready: either a WebSocket upgrade to /ws, or a native
// (non-HTTP) TLS client that speaks the length-prefixed protocol.
class WebFrontend {
 public:
  // ca_cert_path: local CA public cert (PEM), served unauthenticated at /ca.crt so a
  // browser can install it once and trust every future leaf cert this host signs. Empty
  // string disables the route (404) — e.g. an older/portless build without a CertManager.
  static bool serve_until_stream(TransportServer& tx,
                                 const std::string& web_root,
                                 const std::string& ca_cert_path,
                                 const std::string& pairing_code,
                                 std::unique_ptr<ByteChannel>& out_channel,
                                 std::string& out_peer,
                                 volatile std::sig_atomic_t& stop);
};

std::vector<unsigned char> load_cert_der_pem(const std::string& cert_path);

}  // namespace droppix
