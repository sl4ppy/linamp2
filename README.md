# Linamp

Your favorite music player of the 90s, but in real life.

Linamp is a retro Winamp-inspired music player for Linux/Raspberry Pi, built with C++/Qt 6. It features a classic skin-based UI with spectrum visualizer, and supports multiple audio sources through a pluggable architecture.

## Features

- **Local file playback** — MP3, FLAC, WAV, OGG, and other formats via Qt Multimedia with TagLib metadata
- **CD audio** — Disc playback with MusicBrainz track/album lookup, powered by VLC and libdiscid
- **Bluetooth receiver** — Act as a Bluetooth audio sink for phones/tablets
- **Spotify Connect** — Appear as a Spotify Connect device on the local network
- **VBAN network streaming** — Send audio output over the network via VBAN protocol to Voicemeeter Banana or other VBAN-compatible receivers
- **Spectrum visualizer** — Real-time FFT-based visualization captured from PipeWire system audio output
- **Screensaver** — Neon digital or glow-in-the-dark analog clock after 5 minutes of idle (randomly selected)
- **Scalable UI** — 1x through 4x DPI scaling with per-scale stylesheets

## Architecture

### Audio Sources

All audio sources inherit from `AudioSource` (`src/audiosource-base/audiosource.h`), which defines the signal/slot interface for playback control, metadata, spectrum data, and state changes. `AudioSourceCoordinator` (`src/audiosource-coordinator/`) manages switching between sources and routes volume/balance control.

| Source | Directory | Description |
|---|---|---|
| File | `src/audiosourcefile/` | Local file playback via Qt Multimedia. Custom `QMediaPlaylist`, TagLib for metadata |
| CD | `src/audiosourcecd/` | CD playback via embedded Python. VLC decoding, libdiscid + MusicBrainz metadata |
| Bluetooth | `src/audiosourcepython/` | Bluetooth A2DP sink via embedded Python (`python/linamp/btplayer/`) |
| Spotify | `src/audiosourcepython/` | Spotify Connect via embedded Python (`python/linamp/spotifyplayer/`) |

### VBAN Streaming

The VBAN sender (`src/vban/`) is independent of the audio source system — it captures system audio output via its own PipeWire stream (the same approach used for the spectrum visualizer) and packetizes it as VBAN UDP packets. This means it works with all audio sources simultaneously.

Configuration is stored in `~/.config/Rod/Linamp.conf` under the `[vban]` group:

| Key | Default | Description |
|---|---|---|
| `enabled` | `false` | Auto-start VBAN on launch |
| `destinationIp` | `255.255.255.255` | Target IP address (broadcast by default) |
| `port` | `6980` | UDP port |
| `streamName` | `Linamp` | VBAN stream name visible in Voicemeeter |

Toggle VBAN from the Sources menu in the app. The button turns green when active. State is persisted across restarts.

### View System

Views are managed via `QStackedLayout` in `MainWindow` (`src/view-basewindow/mainwindow.h`):

| Index | View | Description |
|---|---|---|
| 0 | `PlayerView` | Main playback UI: spectrum visualizer, track info, transport controls |
| 1 | `PlaylistView` | File browser and playlist management |
| 2 | `MainMenuView` | Audio source selection and VBAN toggle |
| 3 | `ScreenSaverView` | Neon digital or analog clock, activates after 5 min idle |
| 4 | `AvsView` | AVS-style visualization (click spectrum to activate) |

Two base window variants: `DesktopBaseWindow` (windowed with title bar) and `EmbeddedBaseWindow` (fullscreen for Raspberry Pi).

### Python Integration

`AudioSourcePython` (`src/audiosourcepython/`) manages the embedded Python interpreter and uses the Python C API to call into modules in `python/linamp/`. The CD, Bluetooth, and Spotify sources are all Python-backed. Mock implementations for testing without hardware are in `python/linamp-mock/`.

Key concern: proper reference counting (`Py_INCREF`/`Py_DECREF`) at the C/Python boundary — memory leaks here have been a recurring issue.

### Shared Utilities

`src/shared/` contains:
- `fft.h` — FFT for spectrum analysis
- `systemaudiocontrol.h` — ALSA volume/balance control
- `scale.h` — `UI_SCALE` constant for DPI scaling
- `linampslider.h` — Custom slider widget
- `util.h` — General utilities (audio file filters, etc.)

## Requirements

### C++ (build-time)

- Qt 6 (Core, Gui, Widgets, Multimedia, MultimediaWidgets, Concurrent, DBus, Network)
- TagLib (`libtag1-dev`)
- ALSA (`libasound2-dev`)
- PipeWire 0.3 (`libpipewire-0.3-dev`, `libspa-0.2-dev`)
- PulseAudio (`libpulse-dev`)
- Python 3 C API (`python3-dev`)
- CMake 3.16+

### Python (runtime, for CD/Bluetooth/Spotify)

- python-vlc
- python-libdiscid
- musicbrainzngs
- pycdio
- dbus-next

### System (runtime, for CD)

- VLC
- libdiscid0
- libcdio, libiso9660

## Development

### Setup

The target deployment environment is Raspberry Pi with DietPi (Debian Bookworm). These instructions are for Debian/Ubuntu-based systems:

```bash
# Install Qt 6, build tools
sudo apt-get install build-essential qt6-base-dev qt6-base-dev-tools qt6-multimedia-dev qtcreator cmake -y

# Install C++ library dependencies
sudo apt-get install libtag1-dev libasound2-dev libpulse-dev libpipewire-0.3-dev libspa-0.2-dev libdbus-1-dev -y

# Install Python dependencies (for CD/Bluetooth/Spotify)
sudo apt-get install vlc libiso9660-dev libcdio-dev libcdio-utils swig python3-pip python3-full python3-dev python3-dbus-next libdiscid0 libdiscid-dev -y

# Create Python venv and install Python packages
# IMPORTANT: Run from the repository root
python3 -m venv venv
source venv/bin/activate
pip install -r python/requirements.txt
```

Or use the setup script after cmake:

```bash
cmake CMakeLists.txt
./setup.sh
```

### Build and Run

**From the console:**

```bash
# Generate build config (first time only)
cmake CMakeLists.txt

# Build
make

# Run (activates Python venv and sets PYTHONPATH)
./start.sh
```

**Using Qt Creator:**

1. Open `CMakeLists.txt` in Qt Creator
2. Configure the kit when prompted
3. Click the green Play button to build and run

**Tip:** To run windowed instead of fullscreen, comment out `window.setWindowState(Qt::WindowFullScreen)` in `src/main.cpp`.

### CMake Structure

Build target is `player`. Output binary: `./build/player`. CMake auto-handles MOC/UIC/RCC. All source modules are listed explicitly in `CMakeLists.txt` — new source files must be added there manually.

Note: `target_include_directories` in CMakeLists.txt hardcodes Python 3.11 include paths for the Raspberry Pi target. If building locally with a different Python version (e.g. 3.12 on Ubuntu 24.04), you'll need to adjust the path temporarily.

### Building a Debian Package

Install and setup [sbuild](https://wiki.debian.org/sbuild):

```bash
sudo apt-get install sbuild schroot debootstrap apt-cacher-ng devscripts piuparts dh-python dh-cmake
sudo tee ~/.sbuildrc << "EOF"
##############################################################################
# PACKAGE BUILD RELATED (additionally produce _source.changes)
##############################################################################
# -d
$distribution = 'bookworm';
# -A
$build_arch_all = 1;
# -s
$build_source = 1;
# --source-only-changes (applicable for dput. irrelevant for dgit push-source).
$source_only_changes = 1;
# -v
$verbose = 1;
# parallel build
$ENV{'DEB_BUILD_OPTIONS'} = 'parallel=5';
##############################################################################
# POST-BUILD RELATED (turn off functionality by setting variables to 0)
##############################################################################
$run_lintian = 1;
$lintian_opts = ['-i', '-I'];
$run_piuparts = 1;
$piuparts_opts = ['--schroot', '%r-%a-sbuild', '--no-eatmydata'];
$run_autopkgtest = 1;
$autopkgtest_root_args = '';
$autopkgtest_opts = [ '--', 'schroot', '%r-%a-sbuild' ];
##############################################################################
# PERL MAGIC
##############################################################################
1;
EOF
sudo sbuild-adduser $LOGNAME
newgrp sbuild
sudo ln -sf ~/.sbuildrc /root/.sbuildrc
sudo sbuild-createchroot --include=eatmydata,ccache bookworm /srv/chroot/bookworm-amd64-sbuild http://127.0.0.1:3142/ftp.us.debian.org/debian
```

Then build:

```bash
sbuild --no-run-piuparts --lintian-opt="--suppress-tags=bad-distribution-in-changes-file"
```

Install the resulting package:

```bash
sudo apt install ./linamp_[version]_[arch].deb
```

## Project Structure

```
linamp2/
  assets/              # Icons, images, fonts (embedded via uiassets.qrc)
  debian/              # Debian packaging files
  python/
    linamp/            # Python audio source modules
      baseplayer/      # Base class for Python audio sources
      btplayer/        # Bluetooth A2DP sink
      cdplayer.py      # CD playback
      spotifyplayer/   # Spotify Connect
    linamp-mock/       # Mock implementations for testing without hardware
    requirements.txt   # Python pip dependencies
  src/
    audiosource-base/  # AudioSource abstract base class + PipeWire spectrum capture
    audiosource-coordinator/  # Source switching and volume routing
    audiosourcecd/     # CD audio source
    audiosourcefile/   # Local file audio source + custom MediaPlayer
    audiosourcepython/ # Python-backed audio source wrapper
    shared/            # FFT, ALSA control, scaling, utilities
    vban/              # VBAN network audio streaming
    view-basewindow/   # MainWindow, desktop/embedded base windows
    view-menu/         # Source selection menu + VBAN toggle
    view-player/       # Main playback UI, spectrum widget, scrolling text
    view-playlist/     # File browser, playlist model, media playlist
    view-screensaver/  # Idle clock display
    main.cpp           # Application entry point
  styles/              # Scale-variant QSS stylesheets (1x-4x)
  uiassets.qrc         # Qt resource file
  CMakeLists.txt       # CMake build configuration
  setup.sh             # First-time Python venv setup
  start.sh             # Run script (activates venv, sets PYTHONPATH)
```

## Known Issues

- **Mouse input in file browser/playlist:** Clicks are not detected (touch works fine). This is a side effect of a fix for a touch input bug. **Workaround:** Click and hold for about one second to trigger the click event.
- **Qt Creator and Python venv:** Qt Creator runs the executable directly, outside the Python venv, which causes CD/Bluetooth/Spotify sources to crash. Configure Qt Creator to run `start.sh` instead, or set up the venv environment variables manually in the run configuration.

## Debugging

### Memory leaks (Valgrind)

1. Install valgrind
2. In Qt Creator: Analyze > Valgrind Memory Analyzer
3. Use the app, then close it to see results

### Useful references

- https://taglib.org/
- https://github.com/Znurre/QtMixer/blob/b222c49c8f202981e5104bd65c8bf49e73b229c1/QAudioDecoderStream.cpp#L153
- https://github.com/audacious-media-player/audacious/blob/master/src/libaudcore/visualization.cc#L110
- https://github.com/audacious-media-player/audacious/blob/master/src/libaudcore/fft.cc
- https://github.com/qt/qtmultimedia/blob/4fafcd6d2c164472ce63d5f09614b7e073c74bea/src/spatialaudio/qaudioengine.cpp
- https://github.com/audacious-media-player/audacious-plugins/blob/master/src/qt-spectrum/qt-spectrum.cc
- https://github.com/captbaritone/webamp/blob/master/packages/webamp/js/components/Visualizer.tsx
