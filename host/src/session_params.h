#pragma once
#include <cstdint>
namespace droppix {
// The effective per-session display params after reconciling the client's HELLO with the
// host's fallback defaults. A v4 client is authoritative; older clients use the defaults.
struct SessionParams { int fps; bool audio; int orientation; int bitrate; };
SessionParams select_session_params(uint32_t client_version, uint32_t hello_fps,
                                    uint8_t hello_audio, uint8_t hello_orientation,
                                    uint32_t hello_bitrate,
                                    int default_fps, bool default_audio, int default_orientation,
                                    int default_bitrate);
}  // namespace droppix
