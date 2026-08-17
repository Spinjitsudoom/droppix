#pragma once
#include "theme.h"   // ../host/gui — the shared Theme enum

namespace droppix {

// Theme preference for the desktop client.
//
// The host keeps its own in a file under its config dir; the client stores everything else
// (hosts, settings) in QSettings, so it follows its own convention rather than importing the
// host's file-based helper. The Theme ENUM and the palette are shared — only where the
// choice is written differs.
Theme loadClientTheme();          // defaults to Dark, matching the host
void saveClientTheme(Theme t);

}  // namespace droppix
