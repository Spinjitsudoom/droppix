#pragma once
#include <QtGlobal>

namespace droppix {

// Below this run time, a server session that never had a client is treated as a failed
// start (e.g. pkexec denied, port clash) rather than a real session that ended.
inline constexpr qint64 kServerMinRunMs = 2000;

// Decide whether an ended primary-server session should be re-armed (kept listening).
// Re-arm iff the server is still enabled AND it did real work — it had a client at some
// point, or it ran at least kServerMinRunMs. A fast exit with no client is a failed start.
bool shouldRearm(bool enabled, qint64 elapsedMs, bool everConnected);

}  // namespace droppix
