# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Linamp is a retro Winamp-inspired music player for Linux/Raspberry Pi, built with C++/Qt 6. It supports local file playback, CD audio, Bluetooth receiver, and Spotify Connect via a pluggable audio source architecture. Python is embedded (via Python C API) for CD, Bluetooth, and Spotify sources.

## Build Commands

```bash
# First-time setup (generates build config + installs Python venv)
cmake CMakeLists.txt
./setup.sh

# Build
make

# Run (must use start.sh to activate Python venv)
./start.sh

# Build Debian package (requires sbuild setup, see README.md)
sbuild --no-run-piuparts --lintian-opt="--suppress-tags=bad-distribution-in-changes-file"
```

The executable is output to `./build/player`. There are no automated tests or linters configured.

To run windowed instead of fullscreen, comment out `window.setWindowState(Qt::WindowFullScreen)` in `src/main.cpp`.

## Architecture

### Audio Source System

All audio sources inherit from `AudioSource` (abstract base in `src/audiosource-base/audiosource.h`), which defines the signal/slot interface for playback control, metadata, spectrum data, and state changes.

`AudioSourceCoordinator` (`src/audiosource-coordinator/`) manages switching between sources and routes volume/balance control.

Sources:
- **`AudioSourceFile`** (`src/audiosourcefile/`) — Local file playback via Qt Multimedia. Has a custom `QMediaPlaylist` implementation and uses TagLib for metadata.
- **`AudioSourcePython`** (`src/audiosourcepython/`) — Generic wrapper that embeds Python modules via the Python C API. Used for Bluetooth (`btplayer`) and Spotify (`spotifyplayer`).
- **`AudioSourceCD`** (`src/audiosourcecd/`) — CD playback, also Python-backed. Uses VLC for decoding, libdiscid + MusicBrainz for disc/track metadata.

### View System

Views are managed via `QStackedLayout` in `MainWindow` (`src/view-basewindow/mainwindow.h`):
- Index 0: `PlayerView` — main playback UI with spectrum visualizer, track info, controls
- Index 1: `PlaylistView` — file browser and playlist management
- Index 2: `MainMenuView` — settings and audio source selection
- Index 3: `ScreenSaverView` — themed clock faces (7 themes: 6 analog watch styles + digital) after 5 min idle (`SCREENSAVER_TIMEOUT_MS`)

Two base window variants exist: `DesktopBaseWindow` (windowed with title bar) and `EmbeddedBaseWindow` (fullscreen for Raspberry Pi).

### Python Integration

`AudioSourcePython` manages the Python interpreter lifecycle and uses the Python C API to call into modules in `python/linamp/`. Key concern: proper reference counting (`Py_INCREF`/`Py_DECREF`) at the C/Python boundary — memory leaks here have been a recurring issue.

Python modules: `python/linamp/cdplayer.py`, `python/linamp/btplayer/`, `python/linamp/spotifyplayer/`. Mock implementations for testing without hardware are in `python/linamp-mock/`.

### UI Scaling

`src/shared/scale.h` provides a `UI_SCALE` constant. Stylesheets in `styles/` have variants for 1x–4x scaling. Assets in `assets/` are embedded via `uiassets.qrc`.

### Shared Utilities

`src/shared/` contains: FFT for spectrum analysis (`fft.h`), ALSA volume/balance control (`systemaudiocontrol.h`), DPI scaling (`scale.h`), custom slider widget (`linampslider.h`), and general utilities (`util.h`).

## Key Dependencies

**C++:** Qt 6 (Core, Gui, Widgets, Multimedia, MultimediaWidgets, Concurrent, DBus, Network), TagLib, ALSA (libasound2), PipeWire 0.3, PulseAudio, Python 3.11 (C API)

**Python:** python-vlc, python-libdiscid, musicbrainzngs, pycdio, dbus-next

## CMake Structure

Build target is `player`. CMake auto-handles MOC/UIC/RCC. All source modules are listed explicitly in `CMakeLists.txt` — new source files must be added there manually. Python 3.11 headers are hardcoded in `target_include_directories`.
