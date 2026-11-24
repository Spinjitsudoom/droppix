#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace droppix {

// Fixed UDP port the tablet listens on for tether-discovery probes.
constexpr uint16_t kTetherDiscoveryPort = 27010;

// Probe (host -> tablet): ASCII "DPXQ", 4 bytes, no payload.
inline std::vector<unsigned char> encode_probe() { return {'D','P','X','Q'}; }
inline bool is_probe(const std::vector<unsigned char>& b) {
  return b.size() == 4 && b[0]=='D' && b[1]=='P' && b[2]=='X' && b[3]=='Q';
}

// Reply (tablet -> host): "DPXR" u16 wakePort(BE) u8 idLen id[] u8 nameLen name[].
struct TetherReply { uint16_t wake_port = 0; std::string id; std::string name; };
// encode_reply clamps id and name to 255 bytes each (the single-byte length prefix), so the
// emitted payload always matches the declared length. Real ids/names (a UUID + Build.MODEL)
// are far shorter; the Kotlin codec (TetherProbe) clamps identically to stay byte-compatible.
std::vector<unsigned char> encode_reply(const TetherReply& r);
bool decode_reply(const std::vector<unsigned char>& b, TetherReply& out);

// True for a network adapter that is a USB tether: its device (the resolved
// /sys/class/net/<if>/device path, empty for virtual interfaces) sits on a USB bus and it
// is not wireless (a USB Wi-Fi stick is a LAN, not a tether).
bool is_usb_tether_netdev(const std::string& device_path, bool wireless);

}  // namespace droppix
