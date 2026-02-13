# Linamp2 Raspberry Pi Deployment Guide

Step-by-step instructions for deploying Linamp2 on a Raspberry Pi running DietPi (Debian Bookworm).

## Prerequisites

- Raspberry Pi (tested on aarch64/arm64)
- DietPi with Debian Bookworm (or standard Debian Bookworm)
- Openbox window manager configured as the desktop environment
- X11 display server running on `:0`
- PipeWire or PulseAudio audio stack

## 1. Remove any existing Linamp installation

If a previous version of Linamp is installed as a deb package:

```bash
# Stop the running player
pkill linamp-player

# Remove the package
sudo apt-get remove --purge linamp -y
sudo apt autoremove -y

# Remove the old start script directory if it exists
sudo rm -rf /opt/linamp
```

## 2. Install build dependencies

```bash
sudo apt-get update

sudo apt-get install -y \
  build-essential \
  git \
  cmake \
  qt6-base-dev \
  qt6-base-dev-tools \
  qt6-multimedia-dev \
  libtag1-dev \
  libasound2-dev \
  libpulse-dev \
  libpipewire-0.3-dev \
  libspa-0.2-dev \
  libdbus-1-dev \
  vlc \
  libiso9660-dev \
  libcdio-dev \
  libcdio-utils \
  swig \
  python3-pip \
  python3-full \
  python3-dev \
  python3-dbus-next \
  libdiscid0 \
  libdiscid-dev
```

## 3. Clone the repository

```bash
sudo git clone https://github.com/sl4ppy/linamp2.git /opt/linamp2
sudo chown -R dietpi:dietpi /opt/linamp2
```

## 4. Fix Python version in CMakeLists.txt (if needed)

The repo's `CMakeLists.txt` may reference a different Python version than what's installed. Check your version and update if necessary:

```bash
python3 --version
```

If you're on Python 3.11 (Debian Bookworm default) and the CMakeLists.txt references `python3.12`, edit `/opt/linamp2/CMakeLists.txt`:

- Change `/usr/include/python3.12` to `/usr/include/python3.11` in `target_include_directories`
- Change `python3.12` to `python3.11` in `target_link_libraries`

## 5. Build

```bash
cd /opt/linamp2

# Generate build configuration
cmake CMakeLists.txt

# Set up the Python virtual environment and install Python dependencies
./setup.sh

# Compile (uses all available CPU cores)
make -j$(nproc)
```

The output binary will be at `./build/player`.

## 6. Verify the build

```bash
file /opt/linamp2/build/player
# Should show: ELF 64-bit LSB pie executable, ARM aarch64 ...
```

Quick smoke test (will fail without a display, but confirms the binary loads):

```bash
DISPLAY=:0 /opt/linamp2/start.sh &
sleep 3
pgrep -f "build/player" && echo "Running OK"
```

## 7. Configure autostart

Linamp2 is started via the Openbox autostart script. Edit `~/.config/openbox/autostart.sh`:

```bash
#!/bin/bash

DISPLAY=:0 xrandr -o right
/opt/linamp2/start.sh
```

The `xrandr -o right` line rotates the display 90 degrees (for portrait-oriented screens). Remove or adjust this line to match your display configuration.

## 8. Reboot and verify

```bash
sudo reboot
```

After reboot, verify the player is running:

```bash
ps aux | grep "build/player"
```

## Configuration

### VBAN network streaming

VBAN settings are stored in `~/.config/Rod/Linamp.conf`. This file is created automatically when VBAN is first toggled from the app menu. To configure manually:

```ini
[vban]
enabled=true
destinationIp=192.168.1.100
port=6980
streamName=Linamp
```

| Key | Default | Description |
|---|---|---|
| `enabled` | `false` | Start VBAN automatically on launch |
| `destinationIp` | `255.255.255.255` | Target IP (broadcast by default) |
| `port` | `6980` | UDP port |
| `streamName` | `Linamp` | Stream name visible in VBAN receivers |

VBAN can also be toggled on/off from the Sources menu inside the app.

### Display scaling

Edit the `UI_SCALE` constant in `src/shared/scale.h` and rebuild to change DPI scaling (1x-4x).

### Windowed mode

To run in a window instead of fullscreen, comment out this line in `src/main.cpp`:

```cpp
window.setWindowState(Qt::WindowFullScreen);
```

Then rebuild with `make`.

## Updating

To update to a newer version:

```bash
# Stop the running player
pkill -f "build/player"

# Pull latest changes
cd /opt/linamp2
git pull

# Check if CMakeLists.txt Python version needs fixing again
# (see step 4 above)

# Rebuild
make clean
cmake CMakeLists.txt
make -j$(nproc)

# Restart
/opt/linamp2/start.sh &
# Or reboot: sudo reboot
```

## Troubleshooting

### Player won't start

Check that X11 is running on DISPLAY :0:

```bash
DISPLAY=:0 xdpyinfo | head -5
```

### No audio

Verify PipeWire or PulseAudio is running:

```bash
pactl info
```

### Python sources crash (CD/Bluetooth/Spotify)

The player must be launched via `start.sh` so the Python venv is activated and `PYTHONPATH` is set. Running the `build/player` binary directly will cause Python-backed sources to fail.

### Build fails with Python errors

Ensure the Python version in `CMakeLists.txt` matches your system:

```bash
python3 --version
ls /usr/include/python3.*
```

Update `target_include_directories` and `target_link_libraries` in `CMakeLists.txt` to match.

## Reference

- **Install location:** `/opt/linamp2`
- **Binary:** `/opt/linamp2/build/player`
- **Start script:** `/opt/linamp2/start.sh`
- **Autostart:** `~/.config/openbox/autostart.sh`
- **App config:** `~/.config/Rod/Linamp.conf`
- **Python venv:** `/opt/linamp2/venv/`
- **Host:** `linamp.local` (via Avahi/mDNS)
