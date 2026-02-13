# Build, Package & Deploy

## Development Environment Setup

Target platform: Debian Bookworm (matches Raspberry Pi with DietPi).

### Install System Dependencies

```bash
# Qt 6, Qt Multimedia, Qt Creator, CMake
sudo apt-get install build-essential qt6-base-dev qt6-base-dev-tools \
    qt6-multimedia-dev qtcreator cmake -y

# Third-party library dependencies
sudo apt-get install libtag1-dev libasound2-dev libpulse-dev \
    libpipewire-0.3-dev libdbus-1-dev -y

# Python dependencies (for CD, Bluetooth, Spotify sources)
sudo apt-get install vlc libiso9660-dev libcdio-dev libcdio-utils swig \
    python3-pip python3-full python3-dev python3-dbus-next \
    libdiscid0 libdiscid-dev -y
```

### Set Up Python Virtual Environment

```bash
python3 -m venv venv
source venv/bin/activate
pip install -r python/requirements.txt
```

Or use the setup script which does both apt-get and venv setup:
```bash
./setup.sh
```

### Python Requirements

From `python/requirements.txt`:
```
python-libdiscid==2.0.3   # CD disc ID reading
musicbrainzngs==0.7.1     # MusicBrainz metadata lookup
pycdio==2.1.1             # CD-ROM control
python-vlc==3.0.20123     # VLC media playback
dbus-next==0.2.3          # D-Bus async client (Bluetooth)
```

## CMake Configuration

**File:** `CMakeLists.txt`

### Build Target

Single target: `player` (executable output to `./build/player`)

```cmake
cmake_minimum_required(VERSION 3.16)
project(player VERSION 1.0 LANGUAGES C CXX)
```

### Key CMake Settings

```cmake
set(CMAKE_AUTOUIC ON)    # Auto-process .ui files
set(CMAKE_AUTORCC ON)    # Auto-process .qrc resource files
set(CMAKE_AUTOMOC ON)    # Auto-process Q_OBJECT macros
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)
```

### Qt Dependencies

```cmake
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS
    Concurrent DBus Gui Multimedia MultimediaWidgets Network Widgets)
```

### Include Directories

Each source module has its own include directory:
```cmake
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/audiosource-base)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/audiosource-coordinator)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/audiosourcepython)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/audiosourcecd)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/audiosourcefile)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/shared)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/view-basewindow)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/view-menu)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/view-player)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/view-playlist)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/view-screensaver)
```

### External Include Paths (hardcoded)

```cmake
target_include_directories(player PRIVATE
    /usr/include/pipewire-0.3
    /usr/include/python3.11
    /usr/include/spa-0.2
)
```

Note: Python 3.11 headers are hardcoded. If using a different Python version, update this path.

### Linked Libraries

```cmake
target_link_libraries(player PRIVATE
    Qt::Concurrent Qt::Core Qt::DBus Qt::Gui
    Qt::Multimedia Qt::MultimediaWidgets Qt::Network Qt::Widgets
    asound          # ALSA
    pipewire-0.3    # PipeWire spectrum capture
    pulse           # PulseAudio
    pulse-simple    # PulseAudio simple API
    python3.11      # Python C API
    tag             # TagLib metadata
)
```

### Adding New Source Files

All source files are explicitly listed in `qt_add_executable()`. When adding a new file:

1. Create the `.h` and `.cpp` files
2. Add both to the `qt_add_executable(player ...)` block in `CMakeLists.txt`
3. If in a new directory, add `include_directories()` for it
4. Re-run `cmake CMakeLists.txt`

### Build Commands

```bash
# First-time: generate build configuration
cmake CMakeLists.txt

# Build
make

# Build with parallel jobs
make -j$(nproc)
```

## Python Venv and PYTHONPATH

The Python-backed audio sources require:
1. A Python venv with dependencies installed
2. `PYTHONPATH` set to `python/` directory so Python can find the `linamp` module

The `start.sh` script handles both:
```bash
source $SCRIPT_DIR/venv/bin/activate
DISPLAY=:0 PYTHONPATH=$SCRIPT_DIR/python $SCRIPT_DIR/build/player
```

Running the player without `start.sh` (e.g., directly from Qt Creator) will cause Python sources to crash.

## Shell Scripts

### `setup.sh` -- First-Time Setup

Installs system dependencies and creates the Python venv:
```bash
sudo apt-get install build-essential vlc libiso9660-dev ...
python3 -m venv venv
source venv/bin/activate
pip install -r python/requirements.txt
```

### `start.sh` -- Run the Application

Activates the venv, sets `PYTHONPATH` and `DISPLAY`, then runs the player:
```bash
source $SCRIPT_DIR/venv/bin/activate
DISPLAY=:0 PYTHONPATH=$SCRIPT_DIR/python $SCRIPT_DIR/build/player
```

### `install.sh` -- Deploy to linamp-os

Copies built files to `~/linamp/` on the target device. Only for use on the Raspberry Pi:
```bash
rm -rf ~/linamp/*
cp -r build ~/linamp/
cp shutdown.sh ~/linamp/
cp start.sh ~/linamp/
cp -r venv ~/linamp/
cp -r python ~/linamp/
```

### `shutdown.sh` -- System Shutdown

Simple wrapper:
```bash
sudo shutdown now
```

Called from the MainMenuView's shutdown option.

### `scale-skin.sh` -- Asset Scaling

Scales skin PNG files from `skin/` directory to 400% (4x) and places them in `assets/`:
```bash
for filePath in skin/*.png; do
    convert -scale 400% "$filePath" "assets/$fileName"
done
```

Requires ImageMagick's `convert` command.

## Debian Packaging

### Package Structure

**Source package:** `linamp`
**Binary package:** `linamp`

Key files:
| File | Purpose |
|---|---|
| `debian/control` | Package metadata and dependencies |
| `debian/rules` | Build rules (uses dh with cmake) |
| `debian/changelog` | Version history |
| `debian/copyright` | License info (GPL-3) |
| `debian/source/format` | Source format: `3.0 (native)` |

### Build Dependencies (`debian/control`)

```
Build-Depends:
  debhelper-compat (= 13), qt6-base-dev, qt6-base-dev-tools,
  qt6-multimedia-dev, qtcreator, cmake, libtag1-dev, libasound2-dev,
  libpulse-dev, libpipewire-0.3-dev, libdbus-1-dev, libiso9660-dev,
  libcdio-dev, libcdio-utils, swig, python3-dev, dh-python
```

### Runtime Dependencies

```
Depends:
  vlc, python3-full, python3-libdiscid, python3-musicbrainzngs,
  python3-cdio, python3-vlc, python3-dbus-next
```

### Build Rules (`debian/rules`)

```makefile
%:
    dh $@ --with python3 --buildsystem=cmake

override_dh_auto_install:
    # Install binary
    mkdir -p debian/linamp/usr/bin
    cp build/player debian/linamp/usr/bin/linamp-player

    # Install Python modules (system-wide, no venv)
    mkdir -p debian/linamp/usr/lib/python3/dist-packages/linamp
    cp -r python/linamp debian/linamp/usr/lib/python3/dist-packages/

    # Install desktop file
    mkdir -p debian/linamp/usr/share/applications/
    cp linamp.desktop debian/linamp/usr/share/applications/

    # Install icon
    mkdir -p debian/linamp/usr/share/icons/hicolor/72x72/apps/
    cp assets/logoButton.png debian/linamp/usr/share/icons/hicolor/72x72/apps/linamp-player.png
```

Note: The Debian package installs Python modules to the system `dist-packages`, not a venv. Runtime Python dependencies come from Debian packages.

### Building the Package

#### Prerequisites

Install sbuild:
```bash
sudo apt-get install sbuild schroot debootstrap apt-cacher-ng \
    devscripts piuparts dh-python dh-cmake
```

Set up sbuild (see `README.md` for full `.sbuildrc` config and chroot creation).

#### Build Command

```bash
sbuild --no-run-piuparts \
    --lintian-opt="--suppress-tags=bad-distribution-in-changes-file"
```

Output `.deb` files go to the home directory.

#### Install

```bash
sudo apt install ./linamp_<version>_<arch>.deb
```

### Desktop Entry

**File:** `linamp.desktop`

```ini
[Desktop Entry]
Name=Linamp
Exec=/usr/bin/linamp-player %U
Icon=/usr/share/icons/hicolor/72x72/apps/linamp-player.png
Type=Application
Categories=Audio
```

## CI/CD Pipeline

**File:** `.github/workflows/build-deb.yml`

### Triggers

- Push to tags matching `v*` (e.g., `v1.0`, `v2.0.1`)
- Manual `workflow_dispatch`

### Jobs

#### `build-x86_64`

- Runner: `ubuntu-22.04`
- Docker image: `debian:bookworm-slim`
- Extra build deps: `dh-python dh-cmake`
- Uses: `jtdor/build-deb-action@v1.8.0`
- Uploads: `debian/artifacts/*` as `build-x86_64` artifact

#### `build-arm64`

- Runner: `ubuntu-22.04` with QEMU (`docker/setup-qemu-action@v3`)
- Docker image: `debian:bookworm-slim` (emulated ARM64)
- Extra docker args: `--platform linux/arm64`
- Same build deps and action
- Uploads: `debian/artifacts/*` as `build-arm64` artifact

Both jobs run in parallel. ARM64 build is slower due to QEMU emulation.

## Qt Resource System

**File:** `uiassets.qrc`

All assets (images, fonts, stylesheets) are embedded into the executable at compile time via Qt's resource compiler (`rcc`, handled automatically by `CMAKE_AUTORCC`).

### Adding New Assets

1. Place the file in `assets/` (for images/fonts) or `styles/` (for QSS)
2. Add a `<file>` entry to `uiassets.qrc`:
   ```xml
   <file>assets/my-new-icon.png</file>
   ```
3. Access in code: `":/assets/my-new-icon.png"`
4. Rebuild (`make`)

### Adding Scale-Dependent Stylesheets

For a new widget that needs per-scale styling:

1. Create 4 files: `styles/mywidget.1x.qss` through `styles/mywidget.4x.qss`
2. Add all 4 to `uiassets.qrc`
3. Load in code: `widget->setStyleSheet(getStylesheet("mywidget"));`

## Running in Development

### Fullscreen vs Windowed

The `IS_EMBEDDED` define in `src/shared/scale.h` controls fullscreen behavior:

```cpp
#define IS_EMBEDDED  // Comment out for windowed mode
```

When `IS_EMBEDDED` is defined:
- `main.cpp` calls `window.setWindowState(Qt::WindowFullScreen)`
- `EmbeddedBaseWindow` is used (no title bar)
- Window size: 320x100 (base)

When commented out:
- Window appears in normal windowed mode
- `DesktopBaseWindow` is used (with title bar)
- Window size: 277x117 (base)

### Changing UI Scale

Edit `src/shared/scale.h`:
```cpp
#define UI_SCALE 4  // Change to 1, 2, or 3 for different scales
```

Rebuild after changing. All stylesheet variants (1x-4x) are included in the resource file.

### Debugging

```bash
# Run with Qt debug output
QT_LOGGING_RULES="*.debug=true" ./start.sh

# Memory leak detection
valgrind --leak-check=full ./build/player
```
