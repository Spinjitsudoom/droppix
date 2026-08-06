#pragma once

namespace droppix {

// Human-readable build version, e.g. "v0.1.0-121-ge6bd3ca" (git describe) or the
// static fallback "0.1.0" for tarball builds. Resolved at build time; see
// cmake/GenerateVersion.cmake. Declared here (no version string) so callers don't
// recompile when the version changes — only version.cpp does.
const char* app_version();

}  // namespace droppix
