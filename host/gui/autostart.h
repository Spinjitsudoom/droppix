#pragma once
#include <string>

namespace droppix {

// Inputs used to resolve the command that should launch droppix at login.
struct AutostartEnv {
  std::string appimage;    // $APPIMAGE, if running as an AppImage.
  std::string flatpak_id;  // $FLATPAK_ID, if running inside a Flatpak sandbox.
  std::string app_path;    // QCoreApplication::applicationFilePath() fallback.
};

// Resolve the exec command for the autostart .desktop entry: prefers the
// AppImage path (the in-sandbox applicationFilePath() is gone after the
// AppImage's mount is unmounted), then Flatpak (`flatpak run <id>`, since
// applicationFilePath() is an in-sandbox path the host session can't launch),
// else falls back to app_path. A value containing a space is double-quoted;
// the Flatpak `flatpak run <id>` command is left unquoted.
std::string autostart_exec_command(const AutostartEnv& env);

// Build the full ~/.config/autostart/droppix.desktop text for the given exec
// command, appending ` --minimized` so login autostart doesn't pop a window.
std::string autostart_desktop(const std::string& exec_cmd);

}  // namespace droppix
