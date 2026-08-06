#include "version.h"

// Generated at build time into the target's binary dir (see CMakeLists +
// cmake/GenerateVersion.cmake). This is the ONLY translation unit that includes it,
// so a version change relinks just this file instead of the whole GUI.
#include "droppix_version.h"

namespace droppix {

const char* app_version() { return DROPPIX_VERSION; }

}  // namespace droppix
