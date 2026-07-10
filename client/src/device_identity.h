#pragma once
#include <string>

namespace droppix {

// Port of android/.../net/DeviceIdentity.kt: a stable id (random UUID, generated once
// and persisted) plus a human display name, sent in HELLO and reused if this client
// ever advertises itself for discovery.
namespace DeviceIdentity {
std::string displayName();  // hostname/machine model string
std::string stableId();     // persisted UUID (QSettings-backed), generated once
}  // namespace DeviceIdentity

}  // namespace droppix
