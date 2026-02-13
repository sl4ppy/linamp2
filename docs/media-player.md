# MediaPlayer Internals

**Files:** `src/audiosourcefile/mediaplayer.h`, `mediaplayer.cpp`

## Overview

`MediaPlayer` is a custom audio player built on `QIODevice`. It decodes audio files (MP3, FLAC, etc.) using `QAudioDecoder`, buffers the decoded PCM data, and plays it through `QAudioSink`. This design was chosen because Qt 6 removed the built-in `QMediaPlayer` playlist/decode pipeline that Qt 5 had.

## Class Design

```
QIODevice
  └── MediaPlayer
        ├── QAudioDecoder (m_decoder)  -- decodes files to PCM
        ├── QBuffer m_input            -- write end of buffer
        ├── QByteArray m_data          -- shared backing storage
        ├── QBuffer m_output           -- read end of buffer
        └── QAudioSink (m_audioOutput) -- plays PCM to speakers
```

`MediaPlayer` is a `QIODevice` because `QAudioSink::start()` takes a `QIODevice*` and pulls data from it by calling `readData()`.

## Buffer Pipeline

```
Audio File (MP3/FLAC/etc.)
    │
    ▼
QAudioDecoder (m_decoder)
    │ bufferReady() signal
    ▼
m_input.write(data, length)    ← QBuffer, write-only
    │
    ▼
m_data (QByteArray)            ← shared backing array
    │
    ▼
m_output.read(data, maxlen)    ← QBuffer, read-only
    │ called from readData()
    ▼
QAudioSink (m_audioOutput)     → speakers
    │
    emit newData(QByteArray)   → spectrum visualization
```

Key point: `m_input` and `m_output` are both `QBuffer` objects backed by the **same** `QByteArray m_data`. The input writes decoded data to one end; the output reads from the current position. This means the entire decoded file is held in memory.

## Enums

### PlaybackState
```cpp
enum PlaybackState { StoppedState, PlayingState, PausedState };
```

### MediaStatus
```cpp
enum MediaStatus {
    NoMedia,        // No source set
    LoadingMedia,   // Source set, decoder starting
    LoadedMedia,    // Metadata loaded
    StalledMedia,   // (unused)
    BufferingMedia, // Decoder actively decoding
    BufferedMedia,  // Decoding complete
    EndOfMedia,     // Playback reached end
    InvalidMedia    // (unused)
};
```

### Error
```cpp
enum Error { NoError, ResourceError, FormatError, NetworkError, AccessDeniedError };
```

## Playback State Machine

```
                    setSource()
    NoMedia ──────────────────────► LoadingMedia
                                        │
                                        │ bufferReady()
                                        ▼
                                   BufferingMedia ◄─── bufferReady() ───┐
                                        │                               │
                                        │ finished()                    │
                                        ▼                               │
                                   BufferedMedia ───────────────────────┘
                                        │
                                        │ atEnd() in readData()
                                        ▼
                                    EndOfMedia
                                        │
                                        │ (AudioSourceFile handles next track)
                                        ▼
                                   LoadingMedia (new track)


    PlaybackState transitions:

    StoppedState ──── play() ────► PlayingState
         ▲                              │
         │                              │ pause()
         │ stop()                       ▼
         └──────────────────── PausedState
                                        │
                                play()  │
         PlayingState ◄─────────────────┘
```

## Buffer Underrun Recovery

When the audio sink runs out of data (the decoder hasn't filled the buffer fast enough), the audio output enters `QAudio::IdleState` with `QAudio::UnderrunError`. The recovery algorithm:

```cpp
#define MP_MAX_BUFFER_UNDERRUN_RETRIES 10

void MediaPlayer::onOutputStateChanged(QAudio::State newState) {
    if (newState == QAudio::IdleState && error == QAudio::UnderrunError) {
        if (bufferUnderrunRetries > MP_MAX_BUFFER_UNDERRUN_RETRIES) {
            stop();  // Give up after 10 retries
            return;
        }
        bufferUnderrunRetries++;
        qint64 pos = position();
        stop();
        // Wait 200ms for buffer to fill, then retry
        QTimer::singleShot(200, [=]() {
            play();
            setPosition(pos);
        });
    }
}
```

This handles the race condition where `play()` is called before the decoder has buffered enough data. The retry count resets when `clear()` is called (on new track).

## Position Tracking and Seeking

### Position Calculation

Position is calculated from the output buffer's byte offset:

```cpp
m_position = m_output.pos()
             / m_format.sampleFormat()     // bytes per sample (e.g., 2 for Int16)
             / (m_format.sampleRate() / 1000)  // samples per ms
             / m_format.channelCount();    // channels (e.g., 2 for stereo)
```

This converts byte offset → milliseconds.

### Seeking

```cpp
void MediaPlayer::setPosition(qint64 position) {
    // Block seeking while still buffering (prevents noise)
    if (m_status == BufferingMedia) return;

    qint64 target = position
                    * m_format.sampleFormat()
                    * (m_format.sampleRate() / 1000)
                    * m_format.channelCount();

    // Clamp to buffer bounds
    if (target >= m_data.size()) target = m_data.size() - 1;
    if (target < 0) target = 0;

    m_output.seek(target);
}
```

Seeking is only possible after the file is fully decoded (`BufferedMedia`), since it works by seeking within the in-memory buffer.

## Audio Format Configuration

When `setSource()` is called:

1. Query the default audio output device for its preferred format
2. Fall back to defaults if the format is invalid:
   - Sample format: Int16
   - Sample rate: 44100 Hz (from `DEFAULT_SAMPLE_RATE`)
   - Channel config: Stereo
   - Channel count: 2
3. Pass this format to both the decoder (decode target) and the audio sink

## Metadata Loading

Metadata is loaded via TagLib (not Qt's metadata system):

```cpp
void MediaPlayer::loadMetaData() {
    m_metaData = parseMetaData(m_source);  // Uses TagLib via util.h
    setMediaStatus(LoadedMedia);
    emit metaDataChanged();
}
```

The `parseMetaData()` function in `src/shared/util.h` reads tags using TagLib's `FileRef` and `Tag` classes.

## Threading Considerations

### Audio Sink Thread → `readData()`

`QAudioSink` runs its own internal thread that calls `readData()` to pull PCM data. This is protected by `readMutex`:

```cpp
qint64 MediaPlayer::readData(char* data, qint64 maxlen) {
    QMutexLocker l(&readMutex);
    // ... read from m_output buffer ...
}
```

`readData()` also emits `newData()` with the audio data for spectrum visualization.

### Decoder Thread → `bufferReady()`

`QAudioDecoder` runs decoding in its own thread and emits `bufferReady()` when a chunk is ready. The slot writes to `m_input`, which shares `m_data` with `m_output`.

### Initialization Mutex

`initMutex` protects the `init()` method from being called concurrently.

## Signals

| Signal | Description |
|---|---|
| `playbackStateChanged(PlaybackState)` | State transition |
| `mediaStatusChanged(MediaStatus)` | Media loading status |
| `newData(const QByteArray&)` | Decoded PCM data (for spectrum) |
| `durationChanged(qint64)` | Track duration from decoder |
| `positionChanged(qint64)` | Playback position in ms |
| `bufferProgressChanged(float)` | Decode progress 0-100% |
| `volumeChanged(float)` | Volume change (0.0-1.0) |
| `metaDataChanged()` | Metadata loaded from TagLib |
| `errorChanged()` | Error state changed |

## Public Slots

| Slot | Description |
|---|---|
| `setSource(const QUrl&)` | Load a new audio file |
| `clearSource()` | Remove current source and reset |
| `setPosition(qint64)` | Seek to position in ms |
| `setVolume(float)` | Set volume (0.0-1.0) |

## Constants

| Constant | Value | Location |
|---|---|---|
| `MP_MAX_BUFFER_UNDERRUN_RETRIES` | 10 | `mediaplayer.cpp` |
| `DEFAULT_SAMPLE_RATE` | 44100 | `util.h` |
| `MAX_AUDIO_STREAM_SAMPLE_SIZE` | 4096 | `util.h` |
