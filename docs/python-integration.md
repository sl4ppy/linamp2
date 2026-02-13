# Python/C++ Bridge

## Python Interpreter Lifecycle

### Initialization

Python is initialized in the `MainWindow` constructor (`src/view-basewindow/mainwindow.cpp`):

```cpp
Py_Initialize();
PyEval_SaveThread();  // Release GIL immediately
```

`PyEval_SaveThread()` releases the GIL after initialization so that background threads (poll, load, run_loop) can acquire it independently. The main thread never holds the GIL after this point unless explicitly acquiring it.

### Module Loading

`AudioSourcePython` instances load their Python modules in their constructors:

```cpp
// Import "linamp" module
PyObject *pModuleName = PyUnicode_DecodeFSDefault("linamp");
playerModule = PyImport_Import(pModuleName);
Py_DECREF(pModuleName);

// Get class (e.g., "BTPlayer") and instantiate
PyObject *PlayerClass = PyObject_GetAttrString(playerModule, "BTPlayer");
player = PyObject_CallNoArgs(PlayerClass);
```

The module path is set via `PYTHONPATH` environment variable in `start.sh`:
```bash
PYTHONPATH=$SCRIPT_DIR/python $SCRIPT_DIR/build/player
```

### Shutdown

The `MainWindow` destructor acquires the GIL and finalizes Python:
```cpp
PyGILState_Ensure();
Py_Finalize();
```

### Qt/Python Header Conflict

The `slots` keyword conflicts between Qt and Python. Headers that include `<Python.h>` must use this pattern:

```cpp
#define PY_SSIZE_T_CLEAN
#undef slots
#include <Python.h>
#define slots Q_SLOTS
```

## AudioSourcePython C++ Class

**Files:** `src/audiosourcepython/audiosourcepython.h`, `.cpp`

### Construction

```cpp
AudioSourcePython(QString module, QString className, QObject *parent);
```

The constructor:
1. Acquires the GIL
2. Imports the Python module by name
3. Gets the class attribute and instantiates it
4. Sets up timers: `pollEventsTimer` (1s), `progressRefreshTimer` (1s), `progressInterpolateTimer` (100ms)
5. Connects `QFutureWatcher` instances for async result handling
6. Releases the GIL
7. Launches `run_loop()` in a background thread

### Key Members

```cpp
PyObject *playerModule;   // Imported Python module
PyObject *player;         // Instantiated Python player object

QTimer *pollEventsTimer;           // 1s poll for events
QTimer *progressRefreshTimer;      // 1s position refresh from Python
QTimer *progressInterpolateTimer;  // 100ms position interpolation

QFutureWatcher<bool> pollResultWatcher;  // Async poll result
QFutureWatcher<void> loadWatcher;        // Async load completion
QFutureWatcher<void> ejectWatcher;       // Async eject completion
```

### Polling Architecture

The polling system runs continuously (even when the source is not active) to detect events like Bluetooth connections or CD insertions:

```
Main Thread                    Background Thread
──────────                    ─────────────────
pollEventsTimer (1s)
    │
    ▼
pollEvents()
    │ (guard: skip if poll/load in progress)
    │
    ├──── QtConcurrent::run(doPollEvents) ──► doPollEvents()
    │                                          GIL acquire
    │                                          player.poll_events()
    │                                          GIL release
    │                                          return bool
    │
    ▼
handlePollResult()  ◄──────── QFutureWatcher::finished
    │
    ├── if true:  emit requestActivation()
    │             QtConcurrent::run(doLoad) ──► doLoad()
    │                                           GIL acquire
    │                                           player.load()
    │                                           GIL release
    │                ◄── handleLoadEnd()
    │
    └── if false: refreshStatus()
    │
    └── refreshMessage()
```

### Progress Tracking

Two complementary timers:

1. **progressRefreshTimer (1000ms):** Calls `refreshProgress()` which:
   - Calls `refreshStatus()` (gets playback state, shuffle/repeat)
   - Calls Python's `get_postition()` (note the typo)
   - Only updates `currentProgress` if the difference is >1000ms (avoids small jumps from Python call latency)

2. **progressInterpolateTimer (100ms):** Calls `interpolateProgress()` which:
   - Adds elapsed wall-clock time to `currentProgress`
   - Emits `positionChanged()` for smooth UI updates
   - Ignores intervals >200ms (handles timer drift)

### Method Dispatch Pattern

All playback control methods follow the same pattern:

```cpp
void AudioSourcePython::handlePlay() {
    if(player == nullptr) return;
    auto state = PyGILState_Ensure();
    PyObject *pyResult = PyObject_CallMethod(player, "play", NULL);
    if(pyResult == nullptr) {
        PyErr_Print();
    } else {
        Py_DECREF(pyResult);
    }
    PyGILState_Release(state);
    refreshStatus();
}
```

## BasePlayer Python Interface

**File:** `python/linamp/baseplayer/baseplayer.py`

### PlayerStatus Enum

```python
class PlayerStatus(Enum):
    Idle = 'idle'
    Playing = 'playing'
    Stopped = 'stopped'
    Paused = 'paused'
    Error = 'error'
    Loading = 'loading'
```

### Methods

#### Lifecycle

| Method | Signature | Description |
|---|---|---|
| `load()` | `() -> None` | Called when source is selected or track info needs refresh |
| `unload()` | `() -> None` | Called when source is deselected |

#### Playback Control

| Method | Signature | Description |
|---|---|---|
| `play()` | `() -> None` | Start/resume playback |
| `stop()` | `() -> None` | Stop playback |
| `pause()` | `() -> None` | Pause playback |
| `next()` | `() -> None` | Skip to next track |
| `prev()` | `() -> None` | Go to previous track |
| `seek(ms)` | `(int) -> None` | Seek to position in milliseconds |
| `eject()` | `() -> None` | Eject media (e.g., CD eject) |

#### Mode Control

| Method | Signature | Description |
|---|---|---|
| `set_shuffle(enabled)` | `(bool) -> None` | Enable/disable shuffle |
| `set_repeat(enabled)` | `(bool) -> None` | Enable/disable repeat |

#### Status Getters

| Method | Signature | Description |
|---|---|---|
| `get_postition()` | `() -> int` | Current position in ms. **Note: the typo `get_postition` must be preserved** -- the C++ side calls this exact method name. |
| `get_shuffle()` | `() -> bool` | Current shuffle state |
| `get_repeat()` | `() -> bool` | Current repeat state |
| `get_status()` | `() -> str` | One of: `'idle'`, `'playing'`, `'stopped'`, `'paused'`, `'error'`, `'loading'` |
| `get_track_info()` | `() -> tuple[int, str, str, str, int, str, int, int]` | Returns `(track_number, artist, album, title, duration_ms, codec, bitrate_bps, samplerate_hz)` |
| `get_message()` | `() -> tuple[bool, str, int]` | Returns `(show_message, message, timeout_ms)` |
| `clear_message()` | `() -> None` | Called when UI wants to dismiss message |

#### Event Loop

| Method | Signature | Description |
|---|---|---|
| `poll_events()` | `() -> bool` | Called every 1s. Return `True` to request focus/activation. |
| `run_loop()` | `() -> None` | Called once in a background thread at startup. Use for asyncio or other event loops. |

### The `get_postition()` Typo

The method name `get_postition()` (missing an 'i') is a known typo that **must be preserved**. The C++ side in `AudioSourcePython::refreshProgress()` calls:

```cpp
PyObject *pyPosition = PyObject_CallMethod(player, "get_postition", NULL);
```

Renaming the Python method without updating the C++ call would silently break position tracking. If you want to fix this, you must update both sides simultaneously.

## Threading: `run_loop()` and `poll_events()`

### `run_loop()` -- Background Event Loop

Called once at `AudioSourcePython` construction in a background thread:

```cpp
QFuture<void> pyLoopFuture = QtConcurrent::run(&AudioSourcePython::runPythonLoop, this);
```

This is used by sources that need a persistent event loop:

- **Bluetooth (`btplayer`):** Runs an asyncio event loop for D-Bus monitoring (detecting paired devices, media player connections)
- **Spotify (`spotifyplayer`):** Runs an event loop for librespot event handling

The `run_loop()` method acquires the GIL and may hold it for extended periods (the entire duration of the event loop). Other Python calls from other threads will block on GIL acquisition during this time, which is handled by the asynchronous polling architecture.

### `poll_events()` -- Periodic Status Check

Called every 1 second by `pollEventsTimer`. Runs in a background thread via `QtConcurrent::run()`. The result is processed on the main thread via `QFutureWatcher`.

Guard conditions prevent concurrent operations:
- If a poll is already in progress (`pollInProgress`), skip
- If the load watcher is running, skip
- If the poll result watcher is running, skip

## Python Module Structure

```
python/
├── linamp/
│   ├── __init__.py              # Exports BTPlayer, CDPlayer, SpotifyPlayer
│   ├── baseplayer/
│   │   ├── __init__.py          # Exports BasePlayer, PlayerStatus
│   │   └── baseplayer.py        # Abstract base class
│   ├── btplayer/
│   │   ├── __init__.py          # Exports BTPlayer
│   │   ├── btplayer.py          # Bluetooth player implementation
│   │   └── btadapter.py         # D-Bus Bluetooth adapter
│   ├── cdplayer.py              # CD player (VLC + libdiscid + MusicBrainz)
│   └── spotifyplayer/
│       ├── __init__.py          # Exports SpotifyPlayer
│       ├── spotifyplayer.py     # Spotify Connect via librespot
│       ├── spotifyadapter.py    # librespot event adapter
│       └── librespot-event-handler.sh  # Shell hook for librespot events
├── linamp-mock/
│   ├── __init__.py
│   └── mock_cdplayer.py         # CD player mock for testing without hardware
└── requirements.txt             # Python dependencies
```

### Mock Modules

`python/linamp-mock/` provides mock implementations for development without hardware. To use mocks, set `PYTHONPATH` to include the mock directory first, so Python resolves the mock modules before the real ones.

## How to Add a New Python Audio Source Plugin

### 1. Create the Python module

```bash
mkdir -p python/linamp/mysource
```

Create `python/linamp/mysource/__init__.py`:
```python
from .mysource import MyPlayer
```

Create `python/linamp/mysource/mysource.py`:
```python
from linamp.baseplayer import BasePlayer, PlayerStatus

class MyPlayer(BasePlayer):
    def __init__(self):
        self._status = PlayerStatus.Idle
        self._position = 0

    def poll_events(self) -> bool:
        # Check if your source has become available
        # Return True to request activation
        return False

    def load(self) -> None:
        # Initialize your source, load track info
        pass

    def play(self) -> None:
        self._status = PlayerStatus.Playing

    def stop(self) -> None:
        self._status = PlayerStatus.Stopped
        self._position = 0

    def pause(self) -> None:
        self._status = PlayerStatus.Paused

    def get_status(self) -> str:
        return self._status.value

    def get_postition(self) -> int:  # Note: typo is required!
        return self._position

    def get_track_info(self) -> tuple:
        return (1, 'Artist', 'Album', 'Title', 180000, 'MP3', 320000, 44100)

    def get_message(self) -> tuple:
        return (False, '', 0)
```

### 2. Export from the linamp package

Edit `python/linamp/__init__.py`:
```python
from .mysource import MyPlayer
```

### 3. Register in C++

In `src/view-basewindow/mainwindow.cpp`, add in the constructor:
```cpp
AudioSourcePython *mySource = new AudioSourcePython(LINAMP_PY_MODULE, "MyPlayer", this);
coordinator->addSource(mySource, "MY", false);
```

### 4. Add a menu entry

In `src/view-menu/mainmenuview.cpp`, add a button that emits `sourceSelected()` with the correct index.

### 5. Connect screensaver monitoring

In `mainwindow.cpp`, connect the new source's playback state to the screensaver handler:
```cpp
connect(mySource, &AudioSource::playbackStateChanged,
        this, &MainWindow::onPlaybackStateChanged);
```

### 6. Test

Run with:
```bash
PYTHONPATH=./python ./build/player
```
