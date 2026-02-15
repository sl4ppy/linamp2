# Audio Source System

![Source selection menu](../screenshots/menu-view.png)

## AudioSource Abstract Base Class

**File:** `src/audiosource-base/audiosource.h`

All audio sources inherit from `AudioSource`, which defines the interface contract between sources and the rest of the application.

### Signals

| Signal | Description |
|---|---|
| `playbackStateChanged(MediaPlayer::PlaybackState state)` | Emitted when playback state changes (Playing, Paused, Stopped) |
| `positionChanged(qint64 progress)` | Current playback position in milliseconds |
| `dataEmitted(const QByteArray& data, QAudioFormat format)` | Raw audio data for spectrum visualization |
| `metadataChanged(QMediaMetaData metadata)` | Track metadata (title, artist, album, duration, etc.) |
| `durationChanged(qint64 duration)` | Track duration in milliseconds |
| `eqEnabledChanged(bool enabled)` | EQ button state |
| `plEnabledChanged(bool enabled)` | Playlist button state |
| `shuffleEnabledChanged(bool enabled)` | Shuffle toggle state |
| `repeatEnabledChanged(bool enabled)` | Repeat toggle state |
| `messageSet(QString message, qint64 timeout)` | Display a temporary message in PlayerView |
| `messageClear()` | Clear any displayed message |
| `requestActivation()` | Ask the coordinator to switch to this source (coordinator may ignore) |

### Pure Virtual Slots

Every audio source must implement these:

| Slot | Description |
|---|---|
| `activate()` | Called when this source becomes the active source |
| `deactivate()` | Called when switching away from this source |
| `handlePl()` | Playlist button pressed |
| `handlePrevious()` | Previous track |
| `handlePlay()` | Play |
| `handlePause()` | Pause |
| `handleStop()` | Stop |
| `handleNext()` | Next track |
| `handleOpen()` | Open/eject button (context-dependent) |
| `handleShuffle()` | Toggle shuffle mode |
| `handleRepeat()` | Toggle repeat mode |
| `handleSeek(int mseconds)` | Seek to position in milliseconds |

## AudioSourceWSpectrumCapture Mixin

**Files:** `src/audiosource-base/audiosourcewspectrumcapture.h`, `.cpp`

Extends `AudioSource` with PipeWire-based audio capture for spectrum visualization. Used by sources that don't produce audio data directly (Bluetooth, Spotify, CD) -- these play audio through the system audio pipeline, and PipeWire captures the output for the spectrum analyzer.

### How It Works

1. `startSpectrum()` launches a PipeWire capture stream in a background thread via `QtConcurrent::run()`
2. PipeWire's `on_process()` callback copies audio samples into a `QByteArray` buffer, protected by `QMutex`
3. A 33ms timer (`dataEmitTimer`, ~30 fps) reads the buffer and emits `dataEmitted()` with the captured PCM data
4. `stopSpectrum()` quits the PipeWire loop and stops the timer

### PipeWire Configuration

- Captures from the system audio sink (`PW_KEY_STREAM_CAPTURE_SINK = "true"`)
- Format: Int16, 44100 Hz, 2 channels (stereo)
- Buffer size: `DFT_SIZE * 4` bytes (512 samples * 4 bytes/sample)
- Only one global instance can run at a time (enforced by `globalAudioSourceWSpectrumCaptureInstanceIsRunning`)

### Key Methods

| Method | Description |
|---|---|
| `startSpectrum()` | Start PipeWire capture and data emission timer |
| `stopSpectrum()` | Stop capture and timer |

## AudioSourceCoordinator

**Files:** `src/audiosource-coordinator/audiosourcecoordinator.h`, `.cpp`

Manages which audio source is active and handles the wiring between sources and the PlayerView.

### Source Registration

```cpp
void addSource(AudioSource *source, QString label, bool activate = false);
```

Sources are registered with a label (e.g., "FILE", "BT", "CD", "SPOT") and an optional flag to activate immediately. The coordinator also connects each source's `requestActivation` signal to auto-switch when a source requests focus.

### Source Switching Protocol

When `setSource(int newSource)` is called:

1. **Guard checks:** Bounds check, already-selected check
2. **Deactivate old source:**
   - Call `sources[oldSource]->deactivate()`
   - Disconnect all 21 signal/slot connections (10 PlayerView→Source, 11 Source→PlayerView)
3. **Connect new source:**
   - Connect all 21 signal/slot pairs
   - Update source label in PlayerView
4. **Activate new source:**
   - Call `sources[newSource]->activate()`

### Volume and Balance

The coordinator owns a `SystemAudioControl` instance and mediates between the PlayerView sliders and ALSA:

```
PlayerView::volumeChanged → Coordinator::setVolume → SystemAudioControl::setVolume
```

Volume changes also trigger a temporary message in PlayerView (e.g., "VOLUME: 75%"). Balance displays directional messages (e.g., "BALANCE: 30% LEFT", "BALANCE: CENTER").

## AudioSourceFile

**Files:** `src/audiosourcefile/audiosourcefile.h`, `.cpp`

Handles local file playback using a custom `MediaPlayer` class and `QMediaPlaylist`.

### Key Members

- `MediaPlayer *m_player` -- Custom QIODevice-based player (see [media-player.md](media-player.md))
- `QMediaPlaylist *m_playlist` -- Track list with shuffle/repeat support
- `PlaylistModel *m_playlistModel` -- Qt model for the playlist view

### Behavior

- **activate():** Emits initial state, enables PL button, restores shuffle/repeat state
- **deactivate():** Stops playback
- **handlePlay():** Plays current track, or starts from beginning if stopped
- **handleOpen():** Emits `showPlaylistRequested()` signal (MainWindow navigates to PlaylistView)
- **handlePl():** Same as handleOpen()
- **handleShuffle/Repeat():** Toggles mode and updates playlist's playback mode

### Playlist Integration

- `jump(QModelIndex)`: Jump to specific track in playlist
- `addToPlaylist(QList<QUrl>)`: Add files/folders to playlist, auto-expanding directories
- When a track ends (`EndOfMedia`), advances to next track based on playback mode

### Spectrum Data

Unlike Python sources, `AudioSourceFile` gets spectrum data directly from `MediaPlayer::newData()` signal (the decoded PCM data), not from PipeWire capture.

### Extra Signal

- `showPlaylistRequested()` -- Tells MainWindow to navigate to PlaylistView

## AudioSourceCD

**Files:** `src/audiosourcecd/audiosourcecd.h`, `.cpp`

CD audio playback. Inherits from `AudioSourceWSpectrumCapture` (for PipeWire spectrum capture). Uses Python for disc detection (libdiscid), metadata lookup (MusicBrainz), and VLC for audio playback.

### Python Module

Uses `python/linamp/cdplayer.py` which manages:
- CD disc detection via libdiscid
- Track metadata via MusicBrainz
- Audio playback via python-vlc
- Eject functionality

### Operation

Similar to `AudioSourcePython` but with CD-specific handling. Polls for disc insertion/removal and auto-requests activation when a disc is detected.

## AudioSourcePython (Generic Python Wrapper)

**Files:** `src/audiosourcepython/audiosourcepython.h`, `.cpp`

A generic C++ wrapper that can load any Python class that implements the `BasePlayer` interface. Used for both Bluetooth and Spotify sources.

### Construction

```cpp
AudioSourcePython(QString module, QString className, QObject *parent);
// Example: new AudioSourcePython("linamp", "BTPlayer", this)
// Example: new AudioSourcePython("linamp", "SpotifyPlayer", this)
```

The constructor:
1. Acquires the GIL
2. Imports the Python module
3. Instantiates the Python class
4. Starts the 1-second poll timer
5. Launches `run_loop()` in a background thread

### Polling Architecture

```
pollEventsTimer (1s) → pollEvents()
  → QtConcurrent::run(doPollEvents)     [background thread, holds GIL]
    → player.poll_events()              [Python call]
    → returns bool
  → handlePollResult()                  [main thread]
    → if true: requestActivation + doLoad()
    → if false: refreshStatus()
    → refreshMessage()
```

### Progress Tracking

Two timers work together:

1. **progressRefreshTimer (1s):** Calls Python's `get_postition()` for accurate position
2. **progressInterpolateTimer (100ms):** Interpolates between refreshes by adding elapsed time

The interpolation avoids visible position jumps. Jumps >1s from the Python source override the interpolated value.

### GIL Pattern

Every Python C API call follows:
```cpp
auto state = PyGILState_Ensure();
PyObject *result = PyObject_CallMethod(player, "method_name", NULL);
if(result) Py_DECREF(result);
PyGILState_Release(state);
```

### Status Mapping

Python status strings map to Qt playback states:

| Python Status | Qt PlaybackState | Additional Actions |
|---|---|---|
| `"idle"` | StoppedState | Reset position/duration, stop spectrum |
| `"stopped"` | StoppedState | Reset position, stop spectrum |
| `"playing"` | PlayingState | Start spectrum, start progress interpolation |
| `"paused"` | PausedState | Stop spectrum, stop interpolation |
| `"loading"` | StoppedState | Show "LOADING..." message |
| `"error"` | StoppedState | Show "ERROR" message |

### Track Info Extraction

`refreshTrackInfo()` calls Python's `get_track_info()` and maps the 8-tuple to `QMediaMetaData`:

| Tuple Index | Field | QMediaMetaData Key |
|---|---|---|
| 0 | Track number | `TrackNumber` |
| 1 | Artist | `AlbumArtist` |
| 2 | Album | `AlbumTitle` |
| 3 | Title | `Title` |
| 4 | Duration (ms) | `Duration` |
| 5 | Codec | `Description` (repurposed) |
| 6 | Bitrate (bps) | `AudioBitRate` |
| 7 | Sample rate (Hz) | `Comment` (repurposed) |

## How to Add a New Audio Source

### Step 1: Decide on Source Type

- **C++ native source:** Subclass `AudioSource` directly (like `AudioSourceFile`)
- **Python-backed source:** Implement a Python `BasePlayer` subclass and use `AudioSourcePython` wrapper
- **Python-backed with spectrum:** Subclass `AudioSourceWSpectrumCapture` (like `AudioSourceCD`)

### Step 2: For a Python-backed source (simplest path)

1. **Create Python module** in `python/linamp/yoursource/`:

```python
# python/linamp/yoursource/__init__.py
from .yoursource import YourPlayer

# python/linamp/yoursource/yoursource.py
from linamp.baseplayer import BasePlayer, PlayerStatus

class YourPlayer(BasePlayer):
    def load(self):
        # Called when source is selected or needs refresh
        pass

    def play(self):
        # Start playback
        pass

    def stop(self):
        # Stop playback
        pass

    def pause(self):
        # Pause playback
        pass

    def get_status(self):
        return PlayerStatus.Idle.value

    def get_track_info(self):
        return (1, 'Artist', 'Album', 'Title', 180000, 'MP3', 320000, 44100)

    def get_postition(self):  # Note: typo must be preserved!
        return 0

    def poll_events(self):
        # Return True to request activation (e.g., device connected)
        return False

    def run_loop(self):
        # Optional: run async event loop (e.g., asyncio)
        pass
```

2. **Export from linamp package** -- add import to `python/linamp/__init__.py`:
```python
from .yoursource import YourPlayer
```

3. **Register in MainWindow** (`src/view-basewindow/mainwindow.cpp`):
```cpp
AudioSourcePython *yourSource = new AudioSourcePython(LINAMP_PY_MODULE, "YourPlayer", this);
coordinator->addSource(yourSource, "YOUR", false);  // index 4
```

4. **Add menu button** in `src/view-menu/mainmenuview.cpp` to emit `sourceSelected(4)`

5. **Add source icon** to `assets/source-icon-yoursource.png` and `uiassets.qrc`

### Step 3: For a C++ native source

1. Create directory `src/audiosourceyour/`
2. Create class inheriting from `AudioSource` (or `AudioSourceWSpectrumCapture`)
3. Implement all 12 pure virtual slots
4. Add `.h` and `.cpp` files to `CMakeLists.txt`
5. Add `include_directories()` entry in `CMakeLists.txt`
6. Instantiate and register in `MainWindow` constructor
