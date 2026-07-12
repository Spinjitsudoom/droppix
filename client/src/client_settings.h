#pragma once
namespace droppix {
// Per-device display prefs the client sends to the host in HELLO. width/height == 0 means
// "use this device's native screen resolution" (resolved at connect time by the caller).
struct ClientSettings { int width = 0, height = 0; int fps = 60; bool audio = false; int rotation = 0; int bitrate_kbps = 8000; bool flip_horizontal = false; };
int rotation_to_code(int degrees);   // 0/90/180/270 -> 0/1/2/3; else 0
struct ClientSettingsStore { static ClientSettings load(); static void save(const ClientSettings&); };
}  // namespace droppix
