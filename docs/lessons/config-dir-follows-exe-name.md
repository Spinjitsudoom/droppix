# L-2026-11-24-config-dir-follows-exe-name: every AppImage filename started with empty settings

- **ID:** `L-2026-11-24-config-dir-follows-exe-name`
- **Tags:** `gui`, `packaging`, `data-loss`, `silent-failure`, `high`
- **Date:** 2026-11-24

## Symptom

A freshly built AppImage started streaming a test pattern instead of creating a monitor.
Known AOA tablets, approved devices, profiles and the theme were all gone.

## Root cause

`droppix_gui` never called `setApplicationName`, so Qt derived it from the executable's
filename, and `QStandardPaths::AppConfigLocation` followed it. The dev binary used
`~/.config/droppix_gui`, but each AppImage got its own folder named after the file:
`~/.config/droppix-20261123-2346-104199a-x86_64.AppImage`, `~/.config/droppix-x86_64.AppImage`,
and so on. Each new build therefore started from defaults, where `Settings::source` is
`TestPattern`.

Nothing logged it: the GUI just saw an empty config dir, which looks like a first run.

## Fix

`app.setApplicationName("droppix_gui")` in `host/gui/main.cpp`, so every build (raw binary or
any AppImage filename) shares `~/.config/droppix_gui`. The stray per-filename folders can be
deleted. Their settings were never read back.
