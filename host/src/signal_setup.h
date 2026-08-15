#pragma once
#include <csignal>

namespace droppix {

// Ignore SIGPIPE process-wide.
//
// Writing to a socket whose peer has gone away raises SIGPIPE, whose default action is
// to TERMINATE the process — silently, with no log line (exit status 141). The streamer
// writes to client sockets constantly, and the web path writes via SSL_write (OpenSSL
// has no MSG_NOSIGNAL equivalent), so a browser tab closing/reloading mid-frame killed
// the whole server: the listener died and every later client got connection-refused.
//
// With SIGPIPE ignored, those writes instead fail with EPIPE, which the channels already
// handle as an ordinary write failure (send_all -> false -> session ends, reconnect loop
// keeps serving). Ignored dispositions survive fork/exec, so this also covers children.
inline void ignore_sigpipe() { std::signal(SIGPIPE, SIG_IGN); }

}  // namespace droppix
