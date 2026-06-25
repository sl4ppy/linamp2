# Linamp Web UI — Phase 1 (Foundation) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stand up the backend foundation for the remote web UI: live state aggregation, an SSE push channel, a status endpoint, and an embedded minimal page that shows the device's now-playing state updating live in a browser.

**Architecture:** A new `WebStateHub` (QObject) subscribes to the same audio signals `PlayerView` consumes and holds a JSON snapshot. A new `SseBroker` fans that state out to connected browsers as Server-Sent Events. `ApiServer` gains `GET /api/status` (snapshot), `GET /api/events` (SSE stream), and static serving of an embedded vanilla page from a new `webui.qrc`. All on the existing `QTcpServer`/GUI event loop — no new dependencies.

**Tech Stack:** C++17, Qt 6 (Core, Network, Gui, Multimedia — all already linked), vanilla HTML/CSS/JS embedded via Qt resource.

## Global Constraints

- Qt 6, C++. Match existing style (4-space indent; `m_`-prefixed members in the new `src/api` classes).
- **No new build/runtime dependencies.** SSE is implemented on the existing `QTcpServer`/`QTcpSocket`. Do NOT add `qt6-websockets` or any module.
- All handlers run on the Qt GUI event loop — direct calls, no threads/locks.
- Live push is **SSE** (`text/event-stream`); controls remain GET. Browser uses `EventSource` (auto-reconnect).
- Web assets are **embedded** via a new `webui.qrc` (`CMAKE_AUTORCC` is ON; add the `.qrc` to the `player` target like `uiassets.qrc`). No Node/build toolchain.
- Config is the `[api]` group via `QSettings settings;` (org `Rod`, app `Linamp`). New key: `api/maxSseClients` (default `8`).
- SSE snapshot JSON keys (exact): `state, artist, title, album, bitrate, codec, sampleRateHz, channels, durationMs, positionMs, volume, balance, eq, pl, shuffle, repeat, source, view`.
- A malformed request must never crash the player; SSE handling must never block.
- Build target is `player` in `CMakeLists.txt`; new source files and the `.qrc` must be added there manually.
- **Build/verify environment:** builds and runs ONLY on the Raspberry Pi at `root@10.10.0.204:/opt/linamp2` (DietPi Bookworm, Qt 6.4). No local compiler, no automated test harness. Verification = build on the box (`make -j4`) + `curl`/browser checks. Develop on branch `feature/web-ui-phase1`; integration (merge/deploy) happens via the finishing skill after review.
- Restarting the player to test runs `/root/restart_player.sh` on the box (relaunches as the `dietpi` user with the X/D-Bus env). This is the controller's step.

---

### Task 1: Coordinator read access for volume/balance/source

`WebStateHub` needs to observe volume/balance changes and read initial values + the active source label. The coordinator owns `SystemAudioControl` (private). Add re-emit signals and const getters.

**Files:**
- Modify: `src/audiosource-coordinator/audiosourcecoordinator.h`
- Modify: `src/audiosource-coordinator/audiosourcecoordinator.cpp`

**Interfaces:**
- Consumes: existing private `SystemAudioControl *system_audio` (has `getVolume()`, `getBalance()`, signals `volumeChanged(int)`, `balanceChanged(int)`), and private `QList<QString> sourceLabels`, `int currentSource`.
- Produces: signals `void volumeChanged(int)`, `void balanceChanged(int)`; methods `int currentVolume() const`, `int currentBalance() const`, `QString currentSourceLabel() const`.

- [ ] **Step 1: Header additions**

In `src/audiosource-coordinator/audiosourcecoordinator.h`, extend the `signals:` and add public getters. The existing `signals:` block has `void sourceChanged(int source);` — add two more; and add the getters near `addSource`:

```cpp
public:
    explicit AudioSourceCoordinator(QObject *parent = nullptr, PlayerView *playerView = nullptr);

    void addSource(AudioSource *source, QString label, bool activate = false);

    int currentVolume() const;
    int currentBalance() const;
    QString currentSourceLabel() const;

signals:
    void sourceChanged(int source);
    void volumeChanged(int volume);
    void balanceChanged(int balance);
```

- [ ] **Step 2: Re-emit system-audio changes in the constructor**

In `src/audiosource-coordinator/audiosourcecoordinator.cpp`, inside the constructor (after the existing `system_audio` connects, before the closing brace), add signal-to-signal forwards:

```cpp
    connect(system_audio, &SystemAudioControl::volumeChanged,
            this, &AudioSourceCoordinator::volumeChanged);
    connect(system_audio, &SystemAudioControl::balanceChanged,
            this, &AudioSourceCoordinator::balanceChanged);
```

- [ ] **Step 3: Implement the getters**

Append to `src/audiosource-coordinator/audiosourcecoordinator.cpp`:

```cpp
int AudioSourceCoordinator::currentVolume() const
{
    return system_audio->getVolume();
}

int AudioSourceCoordinator::currentBalance() const
{
    return system_audio->getBalance();
}

QString AudioSourceCoordinator::currentSourceLabel() const
{
    if (currentSource < 0 || currentSource >= sourceLabels.size())
        return QString();
    return sourceLabels.at(currentSource);
}
```

- [ ] **Step 4: Build on the box**

Run (on the box): `cd /opt/linamp2 && make -j4`
Expected: `Built target player` (rc 0). No behavior change yet.

- [ ] **Step 5: Commit**

```bash
git add src/audiosource-coordinator/audiosourcecoordinator.h src/audiosource-coordinator/audiosourcecoordinator.cpp
git commit -m "Coordinator: expose volume/balance signals + getters for web state"
```

---

### Task 2: WebStateHub + GET /api/status

Aggregate live state and expose a JSON snapshot via a new endpoint.

**Files:**
- Create: `src/api/webstatehub.h`
- Create: `src/api/webstatehub.cpp`
- Modify: `src/api/apiserver.h`
- Modify: `src/api/apiserver.cpp`
- Modify: `src/view-basewindow/mainwindow.h`
- Modify: `src/view-basewindow/mainwindow.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `AudioSource` signals (`playbackStateChanged(MediaPlayer::PlaybackState)`, `metadataChanged(QMediaMetaData)`, `durationChanged(qint64)`, `positionChanged(qint64)`, `dataEmitted(const QByteArray&, QAudioFormat)`, `eqEnabledChanged(bool)`, `plEnabledChanged(bool)`, `shuffleEnabledChanged(bool)`, `repeatEnabledChanged(bool)`); `AudioSourceCoordinator` (`volumeChanged`, `balanceChanged`, `sourceChanged`, `currentVolume`, `currentBalance`, `currentSourceLabel`) from Task 1.
- Produces: `class WebStateHub` with `WebStateHub(AudioSourceCoordinator*, const QList<AudioSource*>&, QObject*)`, `QJsonObject snapshot() const`, slot `void setView(const QString&)`, signals `void stateChanged()`, `void positionChanged(qint64)`. `ApiServer` gains a `WebStateHub*` ctor arg and a `/api/status` route.

- [ ] **Step 1: Create `src/api/webstatehub.h`**

```cpp
#ifndef WEBSTATEHUB_H
#define WEBSTATEHUB_H

#include <QObject>
#include <QJsonObject>
#include <QMediaMetaData>
#include <QAudioFormat>
#include "mediaplayer.h"

class AudioSource;
class AudioSourceCoordinator;

// Aggregates the live player state the web UI mirrors. Connects to all sources
// (inactive sources don't emit) plus the coordinator, and holds the current
// snapshot. Emits stateChanged() on any non-position change, positionChanged()
// on playback position updates.
class WebStateHub : public QObject
{
    Q_OBJECT
public:
    WebStateHub(AudioSourceCoordinator *coordinator,
                const QList<AudioSource *> &sources,
                QObject *parent = nullptr);

    QJsonObject snapshot() const;

public slots:
    void setView(const QString &view);

signals:
    void stateChanged();
    void positionChanged(qint64 positionMs);

private slots:
    void onPlaybackState(MediaPlayer::PlaybackState state);
    void onMetadata(const QMediaMetaData &md);
    void onDuration(qint64 ms);
    void onPosition(qint64 ms);
    void onFormat(const QByteArray &data, QAudioFormat format);
    void onVolume(int v);
    void onBalance(int b);
    void onEq(bool e);
    void onPl(bool p);
    void onShuffle(bool s);
    void onRepeat(bool r);
    void onSourceChanged(int index);

private:
    AudioSourceCoordinator *m_coord;

    QString m_state = "stopped";
    QString m_artist, m_title, m_album, m_codec, m_source;
    QString m_view = "player";
    int m_bitrateKbps = 0;
    int m_sampleRateHz = 0;
    int m_channels = 0;
    qint64 m_durationMs = 0;
    qint64 m_positionMs = 0;
    int m_volume = 0;
    int m_balance = 0;
    bool m_eq = false, m_pl = false, m_shuffle = false, m_repeat = false;
};

#endif // WEBSTATEHUB_H
```

- [ ] **Step 2: Create `src/api/webstatehub.cpp`**

```cpp
#include "webstatehub.h"
#include "audiosource.h"
#include "audiosourcecoordinator.h"

static QString stateString(MediaPlayer::PlaybackState s)
{
    switch (s) {
    case MediaPlayer::PlayingState: return QStringLiteral("playing");
    case MediaPlayer::PausedState:  return QStringLiteral("paused");
    default:                        return QStringLiteral("stopped");
    }
}

WebStateHub::WebStateHub(AudioSourceCoordinator *coordinator,
                         const QList<AudioSource *> &sources, QObject *parent)
    : QObject(parent), m_coord(coordinator)
{
    for (AudioSource *s : sources) {
        connect(s, &AudioSource::playbackStateChanged, this, &WebStateHub::onPlaybackState);
        connect(s, &AudioSource::metadataChanged,      this, &WebStateHub::onMetadata);
        connect(s, &AudioSource::durationChanged,      this, &WebStateHub::onDuration);
        connect(s, &AudioSource::positionChanged,      this, &WebStateHub::onPosition);
        connect(s, &AudioSource::dataEmitted,          this, &WebStateHub::onFormat);
        connect(s, &AudioSource::eqEnabledChanged,     this, &WebStateHub::onEq);
        connect(s, &AudioSource::plEnabledChanged,     this, &WebStateHub::onPl);
        connect(s, &AudioSource::shuffleEnabledChanged,this, &WebStateHub::onShuffle);
        connect(s, &AudioSource::repeatEnabledChanged, this, &WebStateHub::onRepeat);
    }
    connect(coordinator, &AudioSourceCoordinator::volumeChanged,  this, &WebStateHub::onVolume);
    connect(coordinator, &AudioSourceCoordinator::balanceChanged, this, &WebStateHub::onBalance);
    connect(coordinator, &AudioSourceCoordinator::sourceChanged,  this, &WebStateHub::onSourceChanged);

    m_volume  = coordinator->currentVolume();
    m_balance = coordinator->currentBalance();
    m_source  = coordinator->currentSourceLabel();
}

QJsonObject WebStateHub::snapshot() const
{
    QJsonObject o;
    o["state"]        = m_state;
    o["artist"]       = m_artist;
    o["title"]        = m_title;
    o["album"]        = m_album;
    o["bitrate"]      = m_bitrateKbps;
    o["codec"]        = m_codec;
    o["sampleRateHz"] = m_sampleRateHz;
    o["channels"]     = m_channels;
    o["durationMs"]   = static_cast<double>(m_durationMs);
    o["positionMs"]   = static_cast<double>(m_positionMs);
    o["volume"]       = m_volume;
    o["balance"]      = m_balance;
    o["eq"]           = m_eq;
    o["pl"]           = m_pl;
    o["shuffle"]      = m_shuffle;
    o["repeat"]       = m_repeat;
    o["source"]       = m_source;
    o["view"]         = m_view;
    return o;
}

void WebStateHub::setView(const QString &view)
{
    if (m_view == view) return;
    m_view = view;
    emit stateChanged();
}

void WebStateHub::onPlaybackState(MediaPlayer::PlaybackState s)
{
    const QString ns = stateString(s);
    if (ns == m_state) return;
    m_state = ns;
    emit stateChanged();
}

void WebStateHub::onMetadata(const QMediaMetaData &md)
{
    m_artist = md.value(QMediaMetaData::AlbumArtist).toString();
    m_album  = md.value(QMediaMetaData::AlbumTitle).toString();
    m_title  = md.value(QMediaMetaData::Title).toString();
    m_codec  = md.value(QMediaMetaData::Description).toString();

    const int br = md.value(QMediaMetaData::AudioBitRate).toInt();
    m_bitrateKbps = br > 0 ? br / 1000 : 0;

    const int sr = md.value(QMediaMetaData::Comment).toString().toInt();
    if (sr > 0) m_sampleRateHz = sr;

    const qint64 dur = md.value(QMediaMetaData::Duration).toLongLong();
    if (dur > 0) m_durationMs = dur;

    emit stateChanged();
}

void WebStateHub::onDuration(qint64 ms)
{
    if (ms == m_durationMs) return;
    m_durationMs = ms;
    emit stateChanged();
}

void WebStateHub::onPosition(qint64 ms)
{
    m_positionMs = ms;
    emit positionChanged(ms);
}

void WebStateHub::onFormat(const QByteArray &, QAudioFormat format)
{
    bool changed = false;
    if (format.channelCount() > 0 && format.channelCount() != m_channels) {
        m_channels = format.channelCount();
        changed = true;
    }
    if (format.sampleRate() > 0 && format.sampleRate() != m_sampleRateHz) {
        m_sampleRateHz = format.sampleRate();
        changed = true;
    }
    if (changed) emit stateChanged();
}

void WebStateHub::onVolume(int v)   { if (v != m_volume)  { m_volume = v;  emit stateChanged(); } }
void WebStateHub::onBalance(int b)  { if (b != m_balance) { m_balance = b; emit stateChanged(); } }
void WebStateHub::onEq(bool e)      { if (e != m_eq)      { m_eq = e;      emit stateChanged(); } }
void WebStateHub::onPl(bool p)      { if (p != m_pl)      { m_pl = p;      emit stateChanged(); } }
void WebStateHub::onShuffle(bool s) { if (s != m_shuffle) { m_shuffle = s; emit stateChanged(); } }
void WebStateHub::onRepeat(bool r)  { if (r != m_repeat)  { m_repeat = r;  emit stateChanged(); } }

void WebStateHub::onSourceChanged(int)
{
    m_source = m_coord->currentSourceLabel();
    emit stateChanged();
}
```

- [ ] **Step 3: Add `WebStateHub*` to `ApiServer` and a `/api/status` route**

In `src/api/apiserver.h`: add a forward declaration `class WebStateHub;` (near the other forward decls), add a `WebStateHub *` parameter to the constructor, and a member. Update the constructor signature:

```cpp
    explicit ApiServer(AudioSourceCoordinator *coordinator,
                       MainWindow *window,
                       WebStateHub *webState,
                       QObject *parent = nullptr);
```
And in the private members, after `MainWindow *m_window = nullptr;` add:
```cpp
    WebStateHub *m_webState = nullptr;
```

In `src/api/apiserver.cpp`: add `#include "webstatehub.h"` and `#include <QJsonDocument>` (already included). Update the constructor definition signature and initializer list to take and store `webState`:
```cpp
ApiServer::ApiServer(AudioSourceCoordinator *coordinator, MainWindow *window,
                     WebStateHub *webState, QObject *parent)
    : QObject(parent), m_coordinator(coordinator), m_window(window), m_webState(webState)
{
```
Then in `handleMeta`, add a `/api/status` branch (place it after the `/api/health` branch, before `/api/clock/list`):
```cpp
    if (path == "/api/status") {
        out = {200, QJsonDocument(m_webState->snapshot()).toJson(QJsonDocument::Compact)};
        return true;
    }
```

- [ ] **Step 4: Construct `WebStateHub` in `MainWindow` and pass it to `ApiServer`**

In `src/view-basewindow/mainwindow.h`: add a forward declaration `class WebStateHub;` and a private member `WebStateHub *webState = nullptr;`.

In `src/view-basewindow/mainwindow.cpp`: add `#include "webstatehub.h"`. Replace the existing `apiServer = new ApiServer(coordinator, this, this);` line at the end of the constructor with:
```cpp
    // Web state aggregation + HTTP/SSE API
    webState = new WebStateHub(coordinator,
                               { fileSource, btSource, cdSource, spotSource }, this);
    apiServer = new ApiServer(coordinator, this, webState, this);
```

- [ ] **Step 5: Wire view changes into `WebStateHub::setView`**

In `src/view-basewindow/mainwindow.cpp`, add `webState->setView(...)` calls so the snapshot reflects the on-screen view. Add to the existing methods (guard each with `if (webState)` since `setView` may be called during construction before `webState` exists — though these are user-triggered post-construction, the guard is cheap and safe):

- In `showPlayer()` (after `viewStack->setCurrentIndex(0);`): `if (webState) webState->setView("player");`
- In `showPlaylist()` (after `setCurrentIndex(1)`): `if (webState) webState->setView("playlist");`
- In `showMenu()` (after `setCurrentIndex(2)`): `if (webState) webState->setView("menu");`
- In `showClockScreensaver(int)` (after `viewStack->setCurrentIndex(3);`): `if (webState) webState->setView("screensaver");`
- In `deactivateScreenSaver()` and `deactivateAvs()` (where it returns to the player, after the `fadeTo(0)`/`setCurrentIndex(0)`): `if (webState) webState->setView("player");`
- In `showAvs()`: `if (webState) webState->setView("avs");`
- In `showGeiss()` / where Geiss activates (`activateScreenSaver()` Geiss branch): `if (webState) webState->setView("geiss");`

(These are best-effort labels; exact set isn't load-bearing for Phase 1. If a method name differs, place the call right after its `viewStack->setCurrentIndex(...)`/`fadeTo(...)`.)

- [ ] **Step 6: Add the new sources to `CMakeLists.txt`**

In `CMakeLists.txt`, inside the `qt_add_executable(player ...)` source list, right after the existing `src/api/apiserver.h` line, add:
```cmake
    src/api/webstatehub.cpp
    src/api/webstatehub.h
```

- [ ] **Step 7: Build on the box**

Run (on the box): `cd /opt/linamp2 && make -j4`
Expected: `Built target player` (rc 0).

- [ ] **Step 8: Restart and verify `/api/status`**

Restart the player (controller step), then from the box:
Run: `curl -s localhost:8080/api/status`
Expected: a JSON object with all snapshot keys, e.g. `{"album":"",...,"state":"stopped","volume":<n>,...}`. Then trigger playback (e.g. `curl -s localhost:8080/api/play` with a track loaded) and re-run `/api/status`: `state` becomes `"playing"`, and `positionMs`/`title` reflect the track.

- [ ] **Step 9: Commit**

```bash
git add src/api/webstatehub.h src/api/webstatehub.cpp src/api/apiserver.h src/api/apiserver.cpp src/view-basewindow/mainwindow.h src/view-basewindow/mainwindow.cpp CMakeLists.txt
git commit -m "Add WebStateHub state aggregation and GET /api/status"
```

---

### Task 3: SseBroker + GET /api/events

Push live state to browsers as Server-Sent Events.

**Files:**
- Create: `src/api/ssebroker.h`
- Create: `src/api/ssebroker.cpp`
- Modify: `src/api/apiserver.h`
- Modify: `src/api/apiserver.cpp`
- Modify: `src/view-basewindow/mainwindow.h`
- Modify: `src/view-basewindow/mainwindow.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `WebStateHub` (`snapshot()`, signals `stateChanged()`, `positionChanged(qint64)`); `QTcpSocket`.
- Produces: `class SseBroker` with `SseBroker(WebStateHub*, QObject*)`, `void addClient(QTcpSocket*)`, `void removeClient(QTcpSocket*)`, `int clientCount() const`. `ApiServer` gains an `SseBroker*` ctor arg, an `m_maxSseClients` config value, SSE handoff in `onReadyRead`, and SSE removal in the disconnect handler.

- [ ] **Step 1: Create `src/api/ssebroker.h`**

```cpp
#ifndef SSEBROKER_H
#define SSEBROKER_H

#include <QObject>
#include <QSet>
#include <QByteArray>

class QTcpSocket;
class QTimer;
class WebStateHub;

// Fans WebStateHub updates out to connected SSE clients. ApiServer hands a
// freshly-accepted /api/events socket to addClient() (which writes the SSE
// response headers + an initial status event) and notifies removeClient() when
// the socket disconnects. The broker never owns socket lifetime — it only holds
// pointers and writes to them.
class SseBroker : public QObject
{
    Q_OBJECT
public:
    explicit SseBroker(WebStateHub *hub, QObject *parent = nullptr);

    void addClient(QTcpSocket *socket);
    void removeClient(QTcpSocket *socket);
    int clientCount() const { return m_clients.size(); }

private slots:
    void onStateChanged();
    void onPositionChanged(qint64 ms);
    void sendHeartbeat();

private:
    void writeEvent(QTcpSocket *s, const char *event, const QByteArray &data);
    void broadcast(const char *event, const QByteArray &data);

    WebStateHub *m_hub;
    QSet<QTcpSocket *> m_clients;
    QTimer *m_heartbeat;
    qint64 m_lastPosSec = -1;
};

#endif // SSEBROKER_H
```

- [ ] **Step 2: Create `src/api/ssebroker.cpp`**

```cpp
#include "ssebroker.h"
#include "webstatehub.h"

#include <QTcpSocket>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>

SseBroker::SseBroker(WebStateHub *hub, QObject *parent)
    : QObject(parent), m_hub(hub)
{
    connect(hub, &WebStateHub::stateChanged,    this, &SseBroker::onStateChanged);
    connect(hub, &WebStateHub::positionChanged, this, &SseBroker::onPositionChanged);

    m_heartbeat = new QTimer(this);
    m_heartbeat->setInterval(15000);
    connect(m_heartbeat, &QTimer::timeout, this, &SseBroker::sendHeartbeat);
    m_heartbeat->start();
}

void SseBroker::addClient(QTcpSocket *socket)
{
    static const QByteArray headers =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: keep-alive\r\n"
        "\r\n";
    socket->write(headers);
    socket->flush();
    m_clients.insert(socket);

    const QByteArray data = QJsonDocument(m_hub->snapshot()).toJson(QJsonDocument::Compact);
    writeEvent(socket, "status", data);
}

void SseBroker::removeClient(QTcpSocket *socket)
{
    m_clients.remove(socket);
}

void SseBroker::writeEvent(QTcpSocket *s, const char *event, const QByteArray &data)
{
    if (s->state() != QAbstractSocket::ConnectedState)
        return;
    QByteArray frame;
    frame += "event: ";
    frame += event;
    frame += "\r\ndata: ";
    frame += data;
    frame += "\r\n\r\n";
    s->write(frame);
    s->flush();
}

void SseBroker::broadcast(const char *event, const QByteArray &data)
{
    // Iterate a copy: a failed write can disconnect a socket, which removes it
    // from m_clients mid-iteration.
    const auto clients = m_clients;
    for (QTcpSocket *s : clients)
        writeEvent(s, event, data);
}

void SseBroker::onStateChanged()
{
    const QByteArray data = QJsonDocument(m_hub->snapshot()).toJson(QJsonDocument::Compact);
    broadcast("status", data);
}

void SseBroker::onPositionChanged(qint64 ms)
{
    const qint64 sec = ms / 1000;
    if (sec == m_lastPosSec)
        return; // throttle to ~1 Hz
    m_lastPosSec = sec;
    const QByteArray data = "{\"positionMs\":" + QByteArray::number(ms) + "}";
    broadcast("position", data);
}

void SseBroker::sendHeartbeat()
{
    const auto clients = m_clients;
    for (QTcpSocket *s : clients) {
        if (s->state() == QAbstractSocket::ConnectedState) {
            s->write(": ping\r\n\r\n");
            s->flush();
        }
    }
}
```

- [ ] **Step 3: Add `SseBroker*` + `maxSseClients` to `ApiServer`**

In `src/api/apiserver.h`: add `class SseBroker;` forward decl; add an `SseBroker *` ctor parameter (after `webState`); add members:
```cpp
    SseBroker *m_sseBroker = nullptr;
    int m_maxSseClients = 8;
```
Updated constructor signature:
```cpp
    explicit ApiServer(AudioSourceCoordinator *coordinator,
                       MainWindow *window,
                       WebStateHub *webState,
                       SseBroker *sseBroker,
                       QObject *parent = nullptr);
```

In `src/api/apiserver.cpp`: add `#include "ssebroker.h"` and `#include <QTcpSocket>` (already included). Update the constructor definition to accept and store `sseBroker`, and read the cap from config (alongside the other `settings.value(...)` reads):
```cpp
ApiServer::ApiServer(AudioSourceCoordinator *coordinator, MainWindow *window,
                     WebStateHub *webState, SseBroker *sseBroker, QObject *parent)
    : QObject(parent), m_coordinator(coordinator), m_window(window),
      m_webState(webState), m_sseBroker(sseBroker)
{
    QSettings settings;
    m_enabled       = settings.value("api/enabled", true).toBool();
    m_port          = static_cast<quint16>(settings.value("api/port", 8080).toInt());
    m_bindAddress   = settings.value("api/bindAddress", "0.0.0.0").toString();
    m_token         = settings.value("api/token", "").toString();
    m_maxSseClients = settings.value("api/maxSseClients", 8).toInt();
    // ... (rest of constructor unchanged)
```

- [ ] **Step 4: SSE handoff in `onReadyRead` + disconnect cleanup**

In `src/api/apiserver.cpp`, update the `disconnected` lambda in `onNewConnection` to also drop the socket from the broker:
```cpp
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            if (m_sseBroker) m_sseBroker->removeClient(socket);
            m_buffers.remove(socket);
            socket->deleteLater();
        });
```

In `onReadyRead`, after `HttpRequest req = parseRequest(buf); m_buffers.remove(socket);`, and after computing validity/auth, special-case `/api/events` BEFORE the normal `route()`/`sendResponse()`/`disconnectFromHost()` path. Replace the tail of `onReadyRead` (from `HttpRequest req = parseRequest(buf);` to the end) with:
```cpp
    HttpRequest req = parseRequest(buf);
    m_buffers.remove(socket);

    if (req.valid && authorized(req) && req.path == "/api/events") {
        if (!m_sseBroker || m_sseBroker->clientCount() >= m_maxSseClients) {
            sendResponse(socket, {503, errJson("too many SSE clients")});
            socket->disconnectFromHost();
            return;
        }
        // Hand the socket to the broker: stop parsing further reads and keep it
        // open. The disconnected handler removes it from the broker and frees it.
        disconnect(socket, &QTcpSocket::readyRead, this, &ApiServer::onReadyRead);
        m_sseBroker->addClient(socket);
        return;
    }

    Response resp;
    if (!req.valid)
        resp = {400, errJson("bad request")};
    else if (!authorized(req))
        resp = {401, errJson("unauthorized")};
    else
        resp = route(req);

    sendResponse(socket, resp);
    socket->disconnectFromHost();
```

- [ ] **Step 5: Construct `SseBroker` in `MainWindow` and pass it to `ApiServer`**

In `src/view-basewindow/mainwindow.h`: add `class SseBroker;` forward decl and member `SseBroker *sseBroker = nullptr;`.

In `src/view-basewindow/mainwindow.cpp`: add `#include "ssebroker.h"`. Update the construction block from Task 2 to:
```cpp
    // Web state aggregation + HTTP/SSE API
    webState  = new WebStateHub(coordinator,
                                { fileSource, btSource, cdSource, spotSource }, this);
    sseBroker = new SseBroker(webState, this);
    apiServer = new ApiServer(coordinator, this, webState, sseBroker, this);
```

- [ ] **Step 6: Add the new sources to `CMakeLists.txt`**

After the `src/api/webstatehub.h` line in the `qt_add_executable(player ...)` list, add:
```cmake
    src/api/ssebroker.cpp
    src/api/ssebroker.h
```

- [ ] **Step 7: Build on the box**

Run (on the box): `cd /opt/linamp2 && make -j4`
Expected: `Built target player` (rc 0).

- [ ] **Step 8: Restart and verify the SSE stream**

Restart the player, then from the box stream events for a few seconds while a track plays:
Run: `timeout 5 curl -s -N localhost:8080/api/events`
Expected: an initial `event: status` with the full snapshot, then `event: position` lines about once per second as playback advances (and another `event: status` if you change volume/track during the window). Also confirm the cap: opening more than `maxSseClients` concurrent streams returns `503`.

- [ ] **Step 9: Commit**

```bash
git add src/api/ssebroker.h src/api/ssebroker.cpp src/api/apiserver.h src/api/apiserver.cpp src/view-basewindow/mainwindow.h src/view-basewindow/mainwindow.cpp CMakeLists.txt
git commit -m "Add SseBroker and GET /api/events live stream"
```

---

### Task 4: Embedded web page + static serving (Phase 1 gate)

Serve a minimal embedded page that connects via SSE and shows the device's live state.

**Files:**
- Create: `webui/index.html`
- Create: `webui/app.css`
- Create: `webui/app.js`
- Create: `webui.qrc`
- Modify: `src/api/apiserver.h`
- Modify: `src/api/apiserver.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `GET /api/status` (Task 2), `GET /api/events` (Task 3).
- Produces: `ApiServer` serves `GET /`, `/index.html`, `/app.css`, `/app.js` from `:/webui/...`; `Response` gains a `contentType` field; `/` no longer returns health JSON (health stays at `/api/health`).

- [ ] **Step 1: Create `webui/index.html`**

```html
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Linamp Remote</title>
<link rel="stylesheet" href="/app.css">
</head>
<body>
  <main class="wrap">
    <header><h1>LINAMP <span class="dim">REMOTE</span></h1><div id="conn" class="conn off">connecting…</div></header>
    <section class="lcd">
      <div id="track" class="track">—</div>
      <div class="meta">
        <span id="bitrate">— kbps</span> · <span id="srate">— kHz</span> · <span id="chan">—</span>
      </div>
      <div class="time"><span id="pos">0:00</span> / <span id="dur">0:00</span></div>
      <div id="state" class="state">stopped</div>
    </section>
    <section class="rows">
      <div class="row"><span class="lbl">vol</span><progress id="vol" max="100" value="0"></progress><span id="volv">0</span></div>
      <div class="row"><span class="lbl">bal</span><span id="balv">center</span></div>
      <div class="row"><span class="lbl">source</span><span id="source">—</span></div>
    </section>
    <p class="note">Phase 1 — read-only live status. Controls arrive in Phase 2.</p>
  </main>
  <script src="/app.js"></script>
</body>
</html>
```

- [ ] **Step 2: Create `webui/app.css`**

```css
:root{ --bg:#0c0e13; --panel:#11151c; --ink:#e7eaf0; --dim:#7c8597;
  --lcd:#0a1410; --green:#33ff88; --line:#1c2330; }
*{ box-sizing:border-box; }
body{ margin:0; background:var(--bg); color:var(--ink);
  font-family:"Segoe UI",system-ui,sans-serif; }
.wrap{ max-width:560px; margin:0 auto; padding:18px 16px 40px; }
header{ display:flex; align-items:center; justify-content:space-between; }
h1{ font-size:18px; letter-spacing:.12em; margin:.2em 0; }
.dim{ color:var(--dim); }
.conn{ font-size:12px; padding:3px 9px; border-radius:999px; border:1px solid var(--line); }
.conn.on{ color:var(--green); border-color:#1f4; }
.conn.off{ color:var(--dim); }
.lcd{ margin:14px 0; padding:14px 16px; background:var(--lcd);
  border:1px solid var(--line); border-radius:10px;
  font-family:"DejaVu Sans Mono",ui-monospace,monospace; color:var(--green); }
.track{ font-size:16px; font-weight:700; white-space:nowrap; overflow:hidden;
  text-overflow:ellipsis; text-shadow:0 0 8px rgba(51,255,136,.4); }
.meta{ font-size:12px; color:#6fd6a0; margin-top:6px; }
.time{ font-size:13px; margin-top:8px; }
.state{ font-size:11px; text-transform:uppercase; letter-spacing:.18em;
  color:#8de8b6; margin-top:6px; }
.rows{ display:flex; flex-direction:column; gap:10px; margin-top:8px; }
.row{ display:flex; align-items:center; gap:12px; padding:10px 12px;
  background:var(--panel); border:1px solid var(--line); border-radius:8px; }
.lbl{ width:64px; color:var(--dim); font-size:12px; text-transform:uppercase;
  letter-spacing:.1em; }
progress{ flex:1; height:10px; accent-color:var(--green); }
.note{ color:var(--dim); font-size:12px; margin-top:18px; }
```

- [ ] **Step 3: Create `webui/app.js`**

```javascript
const $ = (id) => document.getElementById(id);

function fmt(ms) {
  if (!ms || ms < 0) return "0:00";
  const s = Math.floor(ms / 1000);
  return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0");
}

function applyStatus(s) {
  const t = [s.artist, s.album, s.title].filter(Boolean).join(" — ");
  $("track").textContent = t || "—";
  $("bitrate").textContent = (s.bitrate ? s.bitrate : "—") + " kbps";
  $("srate").textContent = (s.sampleRateHz ? Math.round(s.sampleRateHz / 1000) : "—") + " kHz";
  $("chan").textContent = s.channels === 1 ? "mono" : (s.channels === 2 ? "stereo" : "—");
  $("dur").textContent = fmt(s.durationMs || 0);
  $("pos").textContent = fmt(s.positionMs || 0);
  $("state").textContent = s.state || "stopped";
  $("vol").value = s.volume || 0;
  $("volv").textContent = s.volume || 0;
  $("balv").textContent = s.balance === 0 ? "center"
    : (s.balance < 0 ? Math.abs(s.balance) + "% L" : s.balance + "% R");
  $("source").textContent = s.source || "—";
}

function token() {
  return new URLSearchParams(location.search).get("token")
      || localStorage.getItem("linamp_token") || "";
}

function connect() {
  const t = token();
  const url = "/api/events" + (t ? "?token=" + encodeURIComponent(t) : "");
  const es = new EventSource(url);
  es.addEventListener("open", () => {
    $("conn").textContent = "connected";
    $("conn").className = "conn on";
  });
  es.addEventListener("error", () => {
    $("conn").textContent = "reconnecting…";
    $("conn").className = "conn off";
  });
  es.addEventListener("status", (e) => applyStatus(JSON.parse(e.data)));
  es.addEventListener("position", (e) => {
    $("pos").textContent = fmt(JSON.parse(e.data).positionMs);
  });
}

connect();
```

- [ ] **Step 4: Create `webui.qrc`**

```xml
<RCC>
    <qresource prefix="/">
        <file>webui/index.html</file>
        <file>webui/app.css</file>
        <file>webui/app.js</file>
    </qresource>
</RCC>
```

- [ ] **Step 5: Give `Response` a content type and use it in `sendResponse`**

In `src/api/apiserver.h`, change the `Response` struct (rename `json` → `body`, add `contentType`):
```cpp
    struct Response {
        int status;
        QByteArray body;
        QByteArray contentType = "application/json";
    };
```
Add a private method declaration `bool handleStatic(const QString &path, Response &out);`.

In `src/api/apiserver.cpp`, update `sendResponse` to use the new fields:
```cpp
void ApiServer::sendResponse(QTcpSocket *socket, const Response &resp)
{
    const QByteArray body = resp.body;
    QByteArray out;
    out += "HTTP/1.1 " + QByteArray::number(resp.status) + " " + reasonPhrase(resp.status) + "\r\n";
    out += "Content-Type: " + resp.contentType + "\r\n";
    out += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    out += "Access-Control-Allow-Origin: *\r\n";
    out += "Connection: close\r\n\r\n";
    out += body;
    socket->write(out);
    socket->flush();
}
```
(The `{status, bytes}` aggregate initializers used throughout still work — they set `status` and `body`, with `contentType` defaulting to JSON.)

- [ ] **Step 6: Serve static assets; move `/` off the health route**

In `src/api/apiserver.cpp`, add `#include <QFile>`.

Change `handleMeta` so `/` no longer returns health — health stays only at `/api/health`. Replace the health branch:
```cpp
    if (path == "/api/health") {
        out = {200, QByteArrayLiteral("{\"ok\":true,\"service\":\"linamp\"}")};
        return true;
    }
```
(Remove `"/" ||` from that condition if present.)

Add `handleStatic`:
```cpp
bool ApiServer::handleStatic(const QString &path, Response &out)
{
    struct Asset { const char *route; const char *res; const char *type; };
    static const Asset assets[] = {
        { "/",           ":/webui/index.html", "text/html; charset=utf-8" },
        { "/index.html", ":/webui/index.html", "text/html; charset=utf-8" },
        { "/app.css",    ":/webui/app.css",    "text/css; charset=utf-8" },
        { "/app.js",     ":/webui/app.js",     "application/javascript; charset=utf-8" },
    };
    for (const Asset &a : assets) {
        if (path == a.route) {
            QFile f(QString::fromLatin1(a.res));
            if (!f.open(QIODevice::ReadOnly)) {
                out = {404, errJson("asset not found")};
                return true;
            }
            out = {200, f.readAll(), a.type};
            return true;
        }
    }
    return false;
}
```

Call it in `route()` right after `handleMeta`:
```cpp
    if (handleMeta(path, req, out))        return out;
    if (handleStatic(path, out))           return out;
    if (handleTransport(path, req, out))   return out;
    if (handleScreensaver(path, req, out)) return out;
    return {404, errJson("unknown endpoint")};
```

- [ ] **Step 7: Add `webui.qrc` to `CMakeLists.txt`**

In `CMakeLists.txt`, in the `qt_add_executable(player ...)` source list, right after the existing `uiassets.qrc` line, add:
```cmake
    webui.qrc
```

- [ ] **Step 8: Build on the box**

Run (on the box): `cd /opt/linamp2 && make -j4`
Expected: `Built target player` (rc 0).

- [ ] **Step 9: Verify in a browser (Phase 1 gate)**

Restart the player. From the dev machine, open `http://10.10.0.204:8080/` in a browser. Expected:
- The page loads (LINAMP REMOTE header, LCD panel).
- The connection chip shows **connected**.
- With a track playing on the device, the track line, bitrate/kHz/channels, volume, balance, source, and state all reflect the device, and the position counter advances ~once per second.
- Changing volume on the device (or via `/api/volume?level=…`) updates the page within a second.

Also confirm `curl -s localhost:8080/api/health` still returns `{"ok":true,"service":"linamp"}` and `curl -s localhost:8080/` returns the HTML.

- [ ] **Step 10: Commit**

```bash
git add webui/index.html webui/app.css webui/app.js webui.qrc src/api/apiserver.h src/api/apiserver.cpp CMakeLists.txt
git commit -m "Serve embedded web UI; minimal live-status page (Phase 1 gate)"
```

---

## Self-Review

**Spec coverage (Phase 1 scope):**
- `webui.qrc` + static serving from `ApiServer` → Task 4. ✓
- `WebStateHub` (state aggregation + snapshot, rebind-free via all-sources connect) → Task 2 (+ Task 1 for volume/balance/source access). ✓
- `SseBroker` (fan-out, heartbeat, throttled position, max-clients) → Task 3. ✓
- `GET /api/status` → Task 2; `GET /api/events` (SSE, keep-open, no disconnect) → Task 3. ✓
- Minimal page connecting via SSE showing live now-playing + position + state → Task 4 (gate). ✓
- Config `api/maxSseClients` default 8 → Task 3. ✓
- No new deps; SSE on existing QTcpServer; GUI-thread; never crash (handoff guarded, broadcast iterates a copy, writes guarded by socket state) → Tasks 3–4. ✓
- Snapshot keys match the spec's exact list → Task 2 `snapshot()`. ✓

**Placeholder scan:** No TBD/TODO. The Task 5 view-wiring note ("if a method name differs, place the call after its setCurrentIndex") is guidance for a known-variable insertion point, not a placeholder for code — the calls themselves are fully specified.

**Type consistency:** `WebStateHub(AudioSourceCoordinator*, const QList<AudioSource*>&, QObject*)`, `snapshot()`, `stateChanged()`, `positionChanged(qint64)`, `setView(const QString&)`; `SseBroker(WebStateHub*, QObject*)`, `addClient/removeClient(QTcpSocket*)`, `clientCount()`; `ApiServer` ctor grows by `WebStateHub*` (Task 2) then `SseBroker*` (Task 3), with the MainWindow call site updated in each; `Response{int status; QByteArray body; QByteArray contentType}` (Task 4) — all consistent across tasks. Coordinator additions (`volumeChanged`/`balanceChanged` signals, `currentVolume`/`currentBalance`/`currentSourceLabel`) defined in Task 1 and consumed in Task 2. ✓
