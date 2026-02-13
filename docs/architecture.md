# System Architecture

## High-Level Component Diagram

```
┌─────────────────────────────────────────────────────────────────────┐
│                           main.cpp                                  │
│  QApplication → MainWindow → Py_Initialize() → app.exec()          │
└────────────────────────────────┬────────────────────────────────────┘
                                 │
┌────────────────────────────────▼────────────────────────────────────┐
│                          MainWindow                                 │
│  Owns all views, all audio sources, the coordinator, and playlist   │
│                                                                     │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │              QStackedLayout (viewStack)                      │    │
│  │  Index 0: PlayerView     (main playback UI)                 │    │
│  │  Index 1: PlaylistView   (file browser + playlist)          │    │
│  │  Index 2: MainMenuView   (source selection)                 │    │
│  │  Index 3: ScreenSaverView (idle clock display)              │    │
│  └─────────────────────────────────────────────────────────────┘    │
│                                                                     │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │              AudioSourceCoordinator                          │    │
│  │  Manages source switching, signal wiring, volume/balance     │    │
│  │                                                              │    │
│  │  ┌──────────────┬──────────────┬──────────────┬───────────┐  │    │
│  │  │AudioSourceFile│AudioSourcePython│AudioSourceCD│AudioSourcePython│  │
│  │  │  (FILE)      │  (BT)        │  (CD)       │  (SPOT)   │  │    │
│  │  └──────────────┴──────────────┴──────────────┴───────────┘  │    │
│  └─────────────────────────────────────────────────────────────┘    │
│                                                                     │
│  SystemAudioControl (ALSA mixer)                                    │
└─────────────────────────────────────────────────────────────────────┘
```

## Audio Source Plugin System

All audio sources inherit from the `AudioSource` abstract base class, which defines a uniform signal/slot interface for playback control and metadata. The `AudioSourceCoordinator` manages which source is active and dynamically connects/disconnects signals between the active source and the `PlayerView`.

Inheritance hierarchy:

```
QObject
  └── AudioSource (abstract base)
        ├── AudioSourceFile (direct, for local files)
        └── AudioSourceWSpectrumCapture (adds PipeWire spectrum capture)
              ├── AudioSourcePython (generic Python wrapper)
              │     ├── BT instance (module="linamp", class="BTPlayer")
              │     └── Spotify instance (module="linamp", class="SpotifyPlayer")
              └── AudioSourceCD (CD playback, also Python-backed)
```

Sources are registered with the coordinator in `MainWindow` constructor:

```cpp
coordinator->addSource(fileSource, "FILE", true);  // index 0, activated by default
coordinator->addSource(btSource, "BT", false);      // index 1
coordinator->addSource(cdSource, "CD", false);       // index 2
coordinator->addSource(spotSource, "SPOT", false);   // index 3
```

See [audio-sources.md](audio-sources.md) for full details.

## View Stack and Navigation

Navigation is managed by switching `QStackedLayout` indices:

```
PlayerView (0) ←──── showPlayer() ────── PlaylistView (1)
      │                                        ▲
      │ showMenu()                              │ showPlaylist()
      ▼                                        │
MainMenuView (2) ──── source selected ────► PlayerView (0)

ScreenSaverView (3) ←── 5min idle timeout ── Any view
                    ──── any user input ────► PlayerView (0)
```

Key navigation connections:
- `controlButtons->logoClicked` → `MainWindow::showMenu()`
- `menu->sourceSelected` → `coordinator->setSource()` + `showPlayer()`
- `fileSource->showPlaylistRequested` → `MainWindow::showPlaylist()`
- `playlist->showPlayerClicked` → `MainWindow::showPlayer()`
- `screenSaver->userActivityDetected` → `MainWindow::deactivateScreenSaver()`

## Signal/Slot Wiring Between Layers

### Coordinator ↔ Active Source ↔ PlayerView

When a source is activated, the coordinator connects these signal pairs:

**PlayerView → AudioSource (user actions):**
| PlayerView Signal | AudioSource Slot |
|---|---|
| `positionChanged(qint64)` | `handleSeek(int)` |
| `previousClicked()` | `handlePrevious()` |
| `playClicked()` | `handlePlay()` |
| `pauseClicked()` | `handlePause()` |
| `stopClicked()` | `handleStop()` |
| `nextClicked()` | `handleNext()` |
| `openClicked()` | `handleOpen()` |
| `shuffleClicked()` | `handleShuffle()` |
| `repeatClicked()` | `handleRepeat()` |
| `plClicked()` | `handlePl()` |

**AudioSource → PlayerView (state updates):**
| AudioSource Signal | PlayerView Slot |
|---|---|
| `playbackStateChanged(PlaybackState)` | `setPlaybackState()` |
| `positionChanged(qint64)` | `setPosition()` |
| `dataEmitted(QByteArray, QAudioFormat)` | `setSpectrumData()` |
| `metadataChanged(QMediaMetaData)` | `setMetadata()` |
| `durationChanged(qint64)` | `setDuration()` |
| `eqEnabledChanged(bool)` | `setEqEnabled()` |
| `plEnabledChanged(bool)` | `setPlEnabled()` |
| `shuffleEnabledChanged(bool)` | `setShuffleEnabled()` |
| `repeatEnabledChanged(bool)` | `setRepeatEnabled()` |
| `messageSet(QString, qint64)` | `setMessage()` |
| `messageClear()` | `clearMessage()` |

When switching sources, the coordinator disconnects all of the above from the old source and reconnects them to the new source.

### Volume/Balance (always connected)

```
PlayerView::volumeChanged → Coordinator::setVolume → SystemAudioControl::setVolume
SystemAudioControl::volumeChanged → PlayerView::setVolume
(same pattern for balance)
```

### Screensaver (all sources connected permanently)

```
fileSource::playbackStateChanged  ─┐
btSource::playbackStateChanged    ─┤→ MainWindow::onPlaybackStateChanged
cdSource::playbackStateChanged    ─┤
spotSource::playbackStateChanged  ─┘
```

## Threading Model

```
┌──────────────────────────────────────────────────────────────┐
│ Main Thread (Qt GUI)                                         │
│  - All UI rendering and event handling                       │
│  - Signal/slot delivery (queued connections from other       │
│    threads are delivered here)                                │
│  - Timer callbacks (poll, progress refresh, screensaver)     │
│  - AudioSourceCoordinator logic                              │
└──────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────┐
│ Audio Sink Thread (QAudioSink internal)                      │
│  - Calls MediaPlayer::readData() to pull decoded PCM data    │
│  - Protected by readMutex                                    │
│  - Emits newData() signal for spectrum visualization         │
└──────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────┐
│ Audio Decoder Thread (QAudioDecoder internal)                │
│  - Decodes MP3/FLAC/etc. to PCM                             │
│  - Calls MediaPlayer::bufferReady() when data is decoded     │
│  - Writes decoded data to m_input buffer                     │
└──────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────┐
│ PipeWire Thread (QtConcurrent::run)                          │
│  - Runs pw_main_loop_run() for spectrum audio capture        │
│  - on_process() callback copies audio samples                │
│  - Protected by pwData.sampleMutex                           │
│  - One global instance enforced                              │
└──────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────┐
│ Python Event Loop Thread (QtConcurrent::run)                 │
│  - Calls player.run_loop() for async Python sources          │
│  - Used by Bluetooth (D-Bus async) and Spotify (librespot)   │
│  - Acquires/releases GIL                                     │
└──────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────┐
│ Python Poll/Load/Eject Threads (QtConcurrent::run)           │
│  - Short-lived threads for GIL-holding Python calls          │
│  - doPollEvents(), doLoad(), doEject() each run off-thread   │
│  - Results handled via QFutureWatcher on main thread         │
└──────────────────────────────────────────────────────────────┘
```

### GIL Management

Python is initialized in `MainWindow` constructor:
```cpp
Py_Initialize();
PyEval_SaveThread();  // Release GIL so other threads can acquire it
```

All Python C API calls follow this pattern:
```cpp
auto state = PyGILState_Ensure();
// ... Python API calls ...
PyGILState_Release(state);
```

## Data Flow: File Playback

```
1. User clicks play
   PlayerView::playClicked() ──signal──► AudioSourceFile::handlePlay()

2. AudioSourceFile tells MediaPlayer to play
   AudioSourceFile::handlePlay() → m_player->play()

3. MediaPlayer starts audio output
   m_player->play() → m_audioOutput->start(this)  [this = QIODevice]

4. Audio sink thread pulls data
   QAudioSink thread → MediaPlayer::readData()
     → reads from m_output buffer (decoded PCM)
     → emits newData(QByteArray) for spectrum

5. Decoder fills buffer (parallel)
   QAudioDecoder thread → MediaPlayer::bufferReady()
     → writes decoded PCM to m_input buffer

6. Spectrum visualization
   MediaPlayer::newData() → AudioSourceFile → SpectrumWidget::setData()
```

## Data Flow: Source Switching

```
1. User selects "BT" in MainMenuView
   MainMenuView::sourceSelected(1) → AudioSourceCoordinator::setSource(1)

2. Coordinator deactivates old source
   sources[0]->deactivate()          // AudioSourceFile stops playback
   disconnect(all signals from FILE)  // 21 disconnect calls

3. Coordinator activates new source
   connect(all signals to BT)         // 21 connect calls
   view->setSourceLabel("BT")
   sources[1]->activate()             // AudioSourcePython starts polling
```

## Data Flow: Python Source (Bluetooth/Spotify)

```
1. Background: pollEventsTimer fires every 1s
   AudioSourcePython::pollEvents()
     → QtConcurrent::run(doPollEvents)  [acquires GIL]
       → PyObject_CallMethod(player, "poll_events")
       → returns bool (change detected?)

2. If change detected (e.g., phone connects to BT):
   handlePollResult()
     → emit requestActivation()         // Ask coordinator to switch to us
     → QtConcurrent::run(doLoad)        // Load track info from Python
       → PyObject_CallMethod(player, "load")

3. Progress tracking (every 1s when active):
   refreshProgress()
     → PyObject_CallMethod(player, "get_postition")  // Note: typo preserved
     → emit positionChanged(position)

4. Progress interpolation (every 100ms):
   interpolateProgress()
     → currentProgress += elapsed time
     → emit positionChanged(currentProgress)
```
