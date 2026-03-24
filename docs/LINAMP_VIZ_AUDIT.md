# Linamp Visualization Audit

> Audit of Linamp's current architecture, capabilities, and integration points
> for adding a Geiss-style visualization system.

---

## 1. Current Audio Pipeline

### 1.1 Audio Sources

Linamp has four audio source implementations, all inheriting from a common `AudioSource` base:

| Source | Class | Playback Tech | Viz Data Access |
|--------|-------|--------------|-----------------|
| **File** | `AudioSourceFile` → `MediaPlayer` | QAudioDecoder → QAudioSink | `MediaPlayer::newData` signal emits raw PCM at playback time |
| **CD** | `AudioSourceCD` | Python bridge (`cdplayer` module) | Inherits `AudioSourceWSpectrumCapture` (PipeWire capture) |
| **Bluetooth** | `AudioSourcePython` | Python bridge (generic) | Inherits `AudioSourceWSpectrumCapture` (PipeWire capture) |
| **Spotify** | `AudioSourcePython` | Python bridge (generic) | Inherits `AudioSourceWSpectrumCapture` (PipeWire capture) |

### 1.2 Audio Data Flow to Visualization

**File source path:**
```
QAudioDecoder → QBuffer → QAudioSink (playback)
                        ↘ MediaPlayer::newData signal
                          → AudioSourceFile::handleSpectrumData()
                            → dataEmitted(QByteArray, QAudioFormat) signal
```

**PipeWire source path (CD, Bluetooth, Spotify):**
```
System audio (PipeWire graph)
  → pw_stream capture (44100 Hz, S16, stereo)
    → on_process() callback writes to QByteArray
      → Timer (33ms) triggers dataEmitted(QByteArray, QAudioFormat) signal
```

**Common path (all sources):**
```
AudioSource::dataEmitted(QByteArray, QAudioFormat)
  → AudioSourceCoordinator routes to active view
    → PlayerView::setSpectrumData()
      → SpectrumWidget::setData(QByteArray, QAudioFormat)
```

### 1.3 Audio Data Format

| Parameter | Value |
|-----------|-------|
| Sample rate | 44100 Hz |
| Channels | 2 (stereo, interleaved) |
| Sample format | qint16 (signed 16-bit) |
| Bytes per frame | 4 (2 channels × 2 bytes) |
| Minimum chunk | DFT_SIZE × 4 = 2048 bytes (512 stereo frames) |

### 1.4 Available Audio Data

At the `dataEmitted` signal, we receive a `QByteArray` of raw interleaved stereo PCM. This gives us:

- **Time-domain waveform**: Direct access to L/R samples — exactly what Geiss waveform effects consume
- **Frequency-domain**: Via the existing 512-point Cooley-Tukey FFT (`calc_freq` in `fft.cpp`), which produces 256 magnitude bins
- **No dedicated beat detection or band-energy extraction** exists yet — would need to be added

---

## 2. Current Rendering Stack

### 2.1 Framework

- **Qt Widgets** (QWidget hierarchy) with **QPainter** software rendering
- **No OpenGL**: Qt::OpenGL and Qt::OpenGLWidgets are not linked
- **No QML/Qt Quick**: Pure C++ widget-based UI
- **Qt version**: Auto-detects Qt5 or Qt6 (`find_package(QT NAMES Qt5 Qt6)`)

### 2.2 Existing Visualization: SpectrumWidget

**File**: `src/view-player/spectrumwidget.cpp` (263 lines)

**Rendering**: QPainter in `paintEvent()`:
1. Receives raw PCM via `setData()`
2. Converts stereo int16 to mono float
3. Runs 512-point FFT → 256 frequency bins
4. Maps to 19 logarithmic display bands
5. Paints gradient bars (3px wide) and peak markers via QPainter

**Dimensions**: Fills its container within PlayerView. Base grid is 38 columns × 8 rows; bar width = 3px × UI_SCALE.

**Update rate**: ~30 FPS (33ms QTimer)

### 2.3 Screensaver View

**File**: `src/view-screensaver/screensaverview.cpp`

**Rendering**: QPainter in `paintEvent()`:
- Fills entire widget with black
- Draws centered clock text (48pt Arial, green #00FF00)
- Draws date text (16pt, darker green #00C800)
- Updates every 1 second

**Precedent**: This demonstrates full-widget QPainter painting at the view-stack level — exactly the pattern a Geiss visualizer would follow.

---

## 3. UI Architecture and Integration Points

### 3.1 View Stack

MainWindow manages a `QStackedLayout` with 4 views:

| Index | View | Content |
|-------|------|---------|
| 0 | DesktopBaseWindow / EmbeddedBaseWindow | Player (with SpectrumWidget) |
| 1 | PlaylistView | Playlist browser |
| 2 | MainMenuView | Settings/source selection |
| 3 | ScreenSaverView | Clock screensaver |

A Geiss visualizer could be:
- **Option A**: A new view at index 4 (or replacing the screensaver at index 3)
- **Option B**: A replacement/enhancement of the SpectrumWidget within PlayerView
- **Option C**: An overlay mode that takes over the screensaver view during playback

### 3.2 Display Dimensions

```
Base resolution:  320 × 100 (embedded) or 277 × 117 (desktop)
UI_SCALE:         4
Actual pixels:    1280 × 400 (embedded) or 1108 × 468 (desktop)
Window:           Fixed size, not resizable
```

**For Geiss rendering**, the effective resolution should be the **base resolution** (320×100 or similar) with the result scaled up via QPainter's built-in image scaling. This is both:
- Performant (fewer pixels to warp per frame)
- Authentic (original Geiss ran at 320×240)

### 3.3 Display Aspect Ratio

The embedded display is 320:100 = 3.2:1 — very wide and short. This is unusual and will affect warp mode aesthetics. Modes designed for ~4:3 aspect ratios will need parameter adjustment. The radial/circular waveform mode will appear as an ellipse unless the coordinate system compensates.

---

## 4. Build System and Dependencies

### 4.1 Current CMake Configuration

```cmake
find_package(QT NAMES Qt5 Qt6 REQUIRED COMPONENTS
    Core Concurrent DBus Gui Multimedia MultimediaWidgets Network Widgets)

target_link_libraries(player PRIVATE
    Qt::Core Qt::Concurrent Qt::DBus Qt::Gui
    Qt::Multimedia Qt::MultimediaWidgets
    Qt::Network Qt::Widgets
    asound pipewire-0.3 pulse pulse-simple python3.11 tag)
```

### 4.2 What Adding OpenGL Would Require

```cmake
# Add to find_package:
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS ... OpenGL OpenGLWidgets)

# Add to target_link_libraries:
target_link_libraries(player PRIVATE ... Qt::OpenGL Qt::OpenGLWidgets)
```

On Raspberry Pi, OpenGL ES 2.0/3.0 is available via Mesa (VideoCore VI on Pi 4/5). However:
- Driver support quality varies
- Qt's QOpenGLWidget has overhead for context management
- The effective render resolution is tiny (320×100) — GPU acceleration may add complexity without meaningful perf gain

### 4.3 Dependencies for CPU Software Rendering

**None needed** — QPainter + QImage provide everything:
- `QImage(width, height, Format_RGB32)` for the framebuffer
- Direct pixel access via `QImage::bits()` or `QImage::scanLine()`
- Scaling via `QPainter::drawImage()` with `Qt::SmoothTransformation`

---

## 5. Platform and Threading Constraints

### 5.1 Target Platform

| Aspect | Value |
|--------|-------|
| Primary target | Raspberry Pi (ARM, DietPi) |
| Dev platform | Debian Bookworm (x86_64) |
| Display | Fixed embedded screen, framebuffer |
| GPU | VideoCore VI (Pi 4) — OpenGL ES 3.1 capable but not currently used |
| CPU | ARM Cortex-A72 (Pi 4), quad-core |

### 5.2 Threading Model

| Thread | Purpose | Access |
|--------|---------|--------|
| Main/UI | QPainter rendering, event loop, signal/slot dispatch | SpectrumWidget::paintEvent, timers |
| Audio decode | QAudioDecoder internal thread | Feeds QBuffer |
| Audio sink | QAudioSink pull thread | Calls MediaPlayer::readData(), emits newData |
| PipeWire | pw_main_loop_run() for capture | Writes to QByteArray via QMutex |
| QtConcurrent | Python bridges, disc detection | Background tasks |

**Key constraint**: All QPainter rendering must happen on the main/UI thread. Warp map generation and audio analysis can be offloaded to a worker thread, but the final pixel writes and paint must be on the UI thread.

**Thread-safe data passing**: Already established via Qt signals/slots (queued connections for cross-thread) and QMutex for shared buffers.

---

## 6. Rendering Approach Recommendation

### Recommended: CPU Software Rendering via QImage + QPainter

**Justification:**

1. **Matches the codebase**: Linamp is 100% QPainter today. A QImage-based visualizer slots in natively with zero new dependencies.

2. **Matches the resolution**: At 320×100 base resolution, we're warping ~32,000 pixels per frame. Even at 8 bytes per pixel of warp computation, that's 256KB of work — trivially fast on any modern CPU, including ARM.

3. **Matches the original**: Geiss was a CPU software renderer. The warp inner loop was hand-optimized x86 assembly because GPUs of 1998 couldn't do this. A modern ARM core at 1.5+ GHz can easily handle the same algorithm at the same resolution.

4. **Matches the platform**: OpenGL ES on Raspberry Pi works but adds driver dependency risk, Qt OpenGL module overhead, and context management complexity — all for a render target that's 32K pixels.

5. **Enables the error diffusion trick**: The carry-forward rounding technique (Geiss's grain) requires sequential pixel processing with inter-pixel state. This is natural in a CPU loop but awkward in a GPU shader. Software rendering preserves this visual signature.

6. **Lower risk**: No driver compatibility issues, no shader compilation failures, no GL context loss handling.

**Implementation approach:**
- Two `QImage` objects (Format_RGB32) as ping-pong framebuffers
- `QImage::bits()` for direct pixel access in the warp loop
- Warp map generation on a background QThread
- Audio analysis (FFT, beat detection) on the UI thread (cheap at this resolution)
- Final display via `QPainter::drawImage()` scaling the small QImage to the full widget size

**Fallback consideration**: If Raspberry Pi performance proves insufficient at 30fps with software warp, a QOpenGLWidget path could be added later as an optimization. But this is unlikely to be needed at 320×100 resolution.

---

## 7. Existing Code Reuse

| Existing Asset | Reusable For |
|----------------|-------------|
| `fft.cpp` / `fft.h` | Frequency analysis for band-energy extraction and spectrum-mode waveform |
| `AudioSource::dataEmitted` signal | Audio data delivery to the visualizer (all sources) |
| `ScreenSaverView` pattern | Model for a full-view QPainter widget in the view stack |
| `QStackedLayout` navigation | Integration point for adding the visualizer view |
| `MainWindow::screenSaverTimer` | Idle timeout pattern for auto-switching to visualizer |
| `scale.h` / UI_SCALE | Coordinate scaling convention |
| `AudioSourceCoordinator` | Routing audio data to the active visualization |
