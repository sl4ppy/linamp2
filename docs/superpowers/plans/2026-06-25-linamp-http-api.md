# Linamp HTTP Control API — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a LAN-reachable, browser-clickable HTTP API to control transport/audio and the screensaver/clock faces.

**Architecture:** A single self-contained `ApiServer` (`QObject` owning a `QTcpServer`) runs on the Qt GUI event loop, parses simple `GET` requests, and calls existing player slots directly. Transport/audio is routed through new forwarding slots on `AudioSourceCoordinator`; screensaver/clock control goes through new thin public methods on `MainWindow`.

**Tech Stack:** C++17, Qt 6 (Core, Network, Gui, Widgets — all already linked). No new dependencies.

## Global Constraints

- Qt 6, C++ (match existing style: 4-space indent, `m_`-prefixed members in new classes, `camelCase` methods).
- **No new build or runtime dependencies.** Use `QTcpServer`/`QTcpSocket` from the already-linked `Network` module. Do **not** add `QHttpServer`.
- All request handlers run on the GUI thread (event loop) — call slots directly, no threads/locks.
- Primary command verb is `GET`; `POST` is accepted on the same paths; `OPTIONS` returns `200`.
- All responses are `application/json`: success → `{"ok":true}` (plus payload where noted); failure → `{"ok":false,"error":"..."}`.
- A malformed request must never crash the player. A bind failure at startup is logged and non-fatal.
- Config lives under the `[api]` group via `QSettings settings;` (org `Rod`, app `Linamp`, set in `src/main.cpp`): keys `api/enabled` (default `true`), `api/port` (default `8080`), `api/bindAddress` (default `0.0.0.0`), `api/token` (default `""`).
- Build target is `player` in `CMakeLists.txt`; new source files must be added there manually.
- **Build/verify environment:** the app only builds/runs on the Raspberry Pi at `root@10.10.0.204` in `/opt/linamp2` (DietPi Bookworm, Qt 6.4). There is no local/automated test harness. Verification = build on the box (`make -j4`) + `curl` smoke tests from the dev machine. Do development on branch `feature/http-api`; merge to `main` at the end.

---

### Task 1: Coordinator transport-forwarding slots

Add public slots to `AudioSourceCoordinator` that forward transport commands to the currently active source, reusing its existing `currentSource` tracking.

**Files:**
- Modify: `src/audiosource-coordinator/audiosourcecoordinator.h`
- Modify: `src/audiosource-coordinator/audiosourcecoordinator.cpp`

**Interfaces:**
- Consumes: existing private members `QList<AudioSource*> sources;` and `int currentSource;`.
- Produces: public slots `void play(); void pause(); void stop(); void next(); void previous(); void seek(int ms); void shuffle(); void repeat();` and private helper `AudioSource* activeSource() const;`.

- [ ] **Step 1: Add declarations to the header**

In `src/audiosource-coordinator/audiosourcecoordinator.h`, extend the `public slots:` block and add a private helper:

```cpp
public slots:
    void setSource(int source);
    void setVolume(int volume);
    void setBalance(int balance);

    // Transport forwarding to the active source (used by the HTTP API)
    void play();
    void pause();
    void stop();
    void next();
    void previous();
    void seek(int ms);
    void shuffle();
    void repeat();

private:
    AudioSource *activeSource() const;
```

(Keep the existing `private:` members below; a second `private:` label is fine in C++.)

- [ ] **Step 2: Implement in the .cpp**

Append to `src/audiosource-coordinator/audiosourcecoordinator.cpp`:

```cpp
AudioSource *AudioSourceCoordinator::activeSource() const
{
    if (currentSource < 0 || currentSource >= sources.size())
        return nullptr;
    return sources.at(currentSource);
}

void AudioSourceCoordinator::play()     { if (auto *s = activeSource()) s->handlePlay(); }
void AudioSourceCoordinator::pause()    { if (auto *s = activeSource()) s->handlePause(); }
void AudioSourceCoordinator::stop()     { if (auto *s = activeSource()) s->handleStop(); }
void AudioSourceCoordinator::next()     { if (auto *s = activeSource()) s->handleNext(); }
void AudioSourceCoordinator::previous() { if (auto *s = activeSource()) s->handlePrevious(); }
void AudioSourceCoordinator::seek(int ms) { if (auto *s = activeSource()) s->handleSeek(ms); }
void AudioSourceCoordinator::shuffle()  { if (auto *s = activeSource()) s->handleShuffle(); }
void AudioSourceCoordinator::repeat()   { if (auto *s = activeSource()) s->handleRepeat(); }
```

- [ ] **Step 3: Build on the box**

Sync the `feature/http-api` branch to the box and build:

Run (on the box): `cd /opt/linamp2 && make -j4`
Expected: `Built target player` (rc 0). No behavior change yet.

- [ ] **Step 4: Commit**

```bash
git add src/audiosource-coordinator/audiosourcecoordinator.h src/audiosource-coordinator/audiosourcecoordinator.cpp
git commit -m "Add transport-forwarding slots to AudioSourceCoordinator"
```

---

### Task 2: ScreenSaverView face selection by index/name

Let callers start the screensaver with a specific clock face and enumerate the available faces.

**Files:**
- Modify: `src/view-screensaver/screensaverview.h`
- Modify: `src/view-screensaver/screensaverview.cpp`

**Interfaces:**
- Consumes: existing `getAllClockThemes()` (returns `QVector<ClockTheme>`; each `ClockTheme` has `const char *name`), and existing members `m_themeIndex`, `m_currentTheme`, `m_clockMode`, `m_hue`, `m_posX/m_posY`.
- Produces: `void start(int themeIndex);` (slot), `static QStringList faceNames();`, `static int faceIndexForName(const QString &name);`.

- [ ] **Step 1: Add declarations to the header**

In `src/view-screensaver/screensaverview.h`, add `#include <QStringList>` near the other includes, and update the `public slots:` / `public:` sections:

```cpp
public:
    enum ClockMode { Digital, Analog };

    explicit ScreenSaverView(QWidget *parent = nullptr);
    ~ScreenSaverView();

    // Face enumeration for the HTTP API
    static QStringList faceNames();
    static int faceIndexForName(const QString &name); // -1 if not found

public slots:
    void start();
    void start(int themeIndex); // start with a specific theme (-1 = random)
```

- [ ] **Step 2: Refactor `start()` and add the overload + helpers**

In `src/view-screensaver/screensaverview.cpp`, replace the existing `start()` implementation:

```cpp
void ScreenSaverView::start()
{
    start(-1);
}

void ScreenSaverView::start(int themeIndex)
{
    m_hue = QRandomGenerator::global()->bounded(360);
    m_posX = -1;
    m_posY = -1;

    auto themes = getAllClockThemes();
    if (themeIndex < 0 || themeIndex >= themes.size())
        themeIndex = QRandomGenerator::global()->bounded(themes.size());

    m_themeIndex = themeIndex;
    m_currentTheme = themes[m_themeIndex];
    m_clockMode = m_currentTheme.isDigital ? Digital : Analog;
}

QStringList ScreenSaverView::faceNames()
{
    QStringList names;
    for (const ClockTheme &t : getAllClockThemes())
        names << QString::fromUtf8(t.name);
    return names;
}

int ScreenSaverView::faceIndexForName(const QString &name)
{
    auto themes = getAllClockThemes();
    for (int i = 0; i < themes.size(); ++i)
        if (name.compare(QString::fromUtf8(themes[i].name), Qt::CaseInsensitive) == 0)
            return i;
    return -1;
}
```

- [ ] **Step 3: Build on the box**

Run (on the box): `cd /opt/linamp2 && make -j4`
Expected: `Built target player` (rc 0). Existing random screensaver still works.

- [ ] **Step 4: Commit**

```bash
git add src/view-screensaver/screensaverview.h src/view-screensaver/screensaverview.cpp
git commit -m "Add specific-face start and face enumeration to ScreenSaverView"
```

---

### Task 3: MainWindow API hooks

Expose thin public methods the API will call, wrapping the existing (private) screensaver activation logic, plus a playback-state getter for the play/pause toggle.

**Files:**
- Modify: `src/view-basewindow/mainwindow.h`
- Modify: `src/view-basewindow/mainwindow.cpp`

**Interfaces:**
- Consumes: existing members `screenSaver`, `geissActive`, `screenSaverActive`, `viewStack`, `currentPlaybackState`; existing `resetScreenSaverTimer()`, `deactivateScreenSaver()`; `ScreenSaverView::faceNames()` (Task 2).
- Produces: `void apiTriggerScreensaver(); void apiDismissScreensaver(); bool apiShowClockFace(int index); MediaPlayer::PlaybackState playbackState() const;` and private `void showClockScreensaver(int themeIndex);`.

- [ ] **Step 1: Add declarations to the header**

In `src/view-basewindow/mainwindow.h`, add to the `public:` section (e.g. just after `~MainWindow();`):

```cpp
    // HTTP API hooks (called on the GUI thread by ApiServer)
    void apiTriggerScreensaver();
    void apiDismissScreensaver();
    bool apiShowClockFace(int index); // false if index out of range
    MediaPlayer::PlaybackState playbackState() const { return currentPlaybackState; }
```

And in the `private:` section, add the helper declaration:

```cpp
    void showClockScreensaver(int themeIndex); // themeIndex < 0 = random
```

- [ ] **Step 2: Implement in the .cpp**

Append to `src/view-basewindow/mainwindow.cpp` (after `resetScreenSaverTimer()`):

```cpp
void MainWindow::showClockScreensaver(int themeIndex)
{
    // Force the clock screensaver regardless of playback/Geiss state.
    geissActive = false;
    screenSaverActive = true;
    screenSaver->start(themeIndex);
    viewStack->setCurrentIndex(3); // ScreenSaverView
    resetScreenSaverTimer();
}

void MainWindow::apiTriggerScreensaver()
{
    showClockScreensaver(-1); // random face
}

void MainWindow::apiDismissScreensaver()
{
    deactivateScreenSaver();
}

bool MainWindow::apiShowClockFace(int index)
{
    if (index < 0 || index >= ScreenSaverView::faceNames().size())
        return false;
    showClockScreensaver(index);
    return true;
}
```

- [ ] **Step 3: Build on the box**

Run (on the box): `cd /opt/linamp2 && make -j4`
Expected: `Built target player` (rc 0).

- [ ] **Step 4: Commit**

```bash
git add src/view-basewindow/mainwindow.h src/view-basewindow/mainwindow.cpp
git commit -m "Add MainWindow API hooks for screensaver/clock control"
```

---

### Task 4: ApiServer core (server, parser, config, auth, meta routes) + wiring

Create the `ApiServer` class with HTTP parsing, the `QTcpServer`, config loading, token auth, error responses, and the meta routes (`/api/health`, `/api/clock/list`). Add it to the build and instantiate it in `MainWindow`. After this task the server is live and answering.

**Files:**
- Create: `src/api/apiserver.h`
- Create: `src/api/apiserver.cpp`
- Modify: `CMakeLists.txt`
- Modify: `src/view-basewindow/mainwindow.h`
- Modify: `src/view-basewindow/mainwindow.cpp`

**Interfaces:**
- Consumes: `AudioSourceCoordinator*`, `MainWindow*` (forward-declared), `ScreenSaverView::faceNames()`.
- Produces: `struct HttpRequest`, free function `HttpRequest parseRequest(const QByteArray&)`, `class ApiServer` with ctor `ApiServer(AudioSourceCoordinator*, MainWindow*, QObject* parent)`, and private member `Response`/`route`/`handleMeta`/`authorized`/`sendResponse`. Later tasks add `handleTransport` (Task 5) and `handleScreensaver` (Task 6).

- [ ] **Step 1: Create `src/api/apiserver.h`**

```cpp
#ifndef APISERVER_H
#define APISERVER_H

#include <QObject>
#include <QByteArray>
#include <QHash>
#include <QString>

class AudioSourceCoordinator;
class MainWindow;
class QTcpServer;
class QTcpSocket;

// Parsed HTTP request (request line + headers; body is ignored).
struct HttpRequest {
    QString method;                   // "GET", "POST", "OPTIONS", ...
    QString path;                     // "/api/play"
    QHash<QString, QString> query;    // decoded query parameters
    QHash<QString, QString> headers;  // lowercased header name -> value
    bool valid = false;
};

// Parse a raw HTTP request buffer. Free function so it is testable in isolation.
HttpRequest parseRequest(const QByteArray &raw);

class ApiServer : public QObject
{
    Q_OBJECT
public:
    explicit ApiServer(AudioSourceCoordinator *coordinator,
                       MainWindow *window,
                       QObject *parent = nullptr);

private slots:
    void onNewConnection();
    void onReadyRead();

private:
    struct Response {
        int status;
        QByteArray json;
    };

    Response route(const HttpRequest &req);
    bool handleMeta(const QString &path, const HttpRequest &req, Response &out);
    bool handleTransport(const QString &path, const HttpRequest &req, Response &out);   // Task 5
    bool handleScreensaver(const QString &path, const HttpRequest &req, Response &out); // Task 6
    bool authorized(const HttpRequest &req) const;
    void sendResponse(QTcpSocket *socket, const Response &resp);

    QTcpServer *m_server = nullptr;
    AudioSourceCoordinator *m_coordinator = nullptr;
    MainWindow *m_window = nullptr;
    QHash<QTcpSocket *, QByteArray> m_buffers;

    bool m_enabled = true;
    quint16 m_port = 8080;
    QString m_bindAddress = "0.0.0.0";
    QString m_token;
};

#endif // APISERVER_H
```

- [ ] **Step 2: Create `src/api/apiserver.cpp`**

```cpp
#include "apiserver.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>
#include <QSettings>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStringList>
#include <QDebug>

#include "audiosourcecoordinator.h"
#include "mainwindow.h"
#include "screensaverview.h"

// --- small helpers ---

static QByteArray okJson()
{
    return QByteArrayLiteral("{\"ok\":true}");
}

static QByteArray errJson(const QString &msg)
{
    QJsonObject o;
    o["ok"] = false;
    o["error"] = msg;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

static QByteArray reasonPhrase(int status)
{
    switch (status) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    default:  return "OK";
    }
}

static bool parseIntParam(const QString &s, int &out)
{
    bool ok = false;
    int v = s.toInt(&ok);
    if (ok) out = v;
    return ok;
}

// --- request parsing ---

HttpRequest parseRequest(const QByteArray &raw)
{
    HttpRequest req;
    int headerEnd = raw.indexOf("\r\n\r\n");
    QByteArray head = headerEnd >= 0 ? raw.left(headerEnd) : raw;

    QList<QByteArray> lines = head.split('\n');
    if (lines.isEmpty())
        return req;

    QByteArray reqLine = lines.first().trimmed();
    QList<QByteArray> parts = reqLine.split(' ');
    if (parts.size() < 2)
        return req;

    req.method = QString::fromLatin1(parts[0]).toUpper();

    QByteArray target = parts[1];
    int q = target.indexOf('?');
    QByteArray pathPart  = q >= 0 ? target.left(q)   : target;
    QByteArray queryPart = q >= 0 ? target.mid(q + 1) : QByteArray();

    req.path = QString::fromUtf8(QByteArray::fromPercentEncoding(pathPart));

    const QList<QByteArray> pairs = queryPart.split('&');
    for (QByteArray pair : pairs) {
        if (pair.isEmpty())
            continue;
        int eq = pair.indexOf('=');
        QByteArray k = eq >= 0 ? pair.left(eq)   : pair;
        QByteArray v = eq >= 0 ? pair.mid(eq + 1) : QByteArray();
        k.replace('+', ' ');
        v.replace('+', ' ');
        QString key = QString::fromUtf8(QByteArray::fromPercentEncoding(k));
        QString val = QString::fromUtf8(QByteArray::fromPercentEncoding(v));
        req.query.insert(key, val);
    }

    for (int i = 1; i < lines.size(); ++i) {
        QByteArray line = lines[i].trimmed();
        if (line.isEmpty())
            continue;
        int colon = line.indexOf(':');
        if (colon < 0)
            continue;
        QString name  = QString::fromLatin1(line.left(colon)).trimmed().toLower();
        QString value = QString::fromLatin1(line.mid(colon + 1)).trimmed();
        req.headers.insert(name, value);
    }

    req.valid = true;
    return req;
}

// --- ApiServer ---

ApiServer::ApiServer(AudioSourceCoordinator *coordinator, MainWindow *window, QObject *parent)
    : QObject(parent), m_coordinator(coordinator), m_window(window)
{
    QSettings settings;
    m_enabled     = settings.value("api/enabled", true).toBool();
    m_port        = static_cast<quint16>(settings.value("api/port", 8080).toInt());
    m_bindAddress = settings.value("api/bindAddress", "0.0.0.0").toString();
    m_token       = settings.value("api/token", "").toString();

    if (!m_enabled) {
        qInfo() << "[api] disabled via config";
        return;
    }

    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &ApiServer::onNewConnection);

    QHostAddress addr = (m_bindAddress == "0.0.0.0")
                            ? QHostAddress(QHostAddress::Any)
                            : QHostAddress(m_bindAddress);

    if (!m_server->listen(addr, m_port))
        qWarning() << "[api] failed to listen on" << m_bindAddress << m_port
                   << ":" << m_server->errorString();
    else
        qInfo() << "[api] listening on" << m_bindAddress << m_port;
}

void ApiServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, this, &ApiServer::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            m_buffers.remove(socket);
            socket->deleteLater();
        });
    }
}

void ApiServer::onReadyRead()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket)
        return;

    QByteArray &buf = m_buffers[socket];
    buf += socket->readAll();

    if (buf.size() > 64 * 1024) {
        sendResponse(socket, {400, errJson("request too large")});
        socket->disconnectFromHost();
        return;
    }
    if (!buf.contains("\r\n\r\n"))
        return; // wait for the full header block

    HttpRequest req = parseRequest(buf);
    m_buffers.remove(socket);

    Response resp;
    if (!req.valid)
        resp = {400, errJson("bad request")};
    else if (!authorized(req))
        resp = {401, errJson("unauthorized")};
    else
        resp = route(req);

    sendResponse(socket, resp);
    socket->disconnectFromHost();
}

bool ApiServer::authorized(const HttpRequest &req) const
{
    if (m_token.isEmpty())
        return true;
    if (req.query.value("token") == m_token)
        return true;
    if (req.headers.value("authorization") == "Bearer " + m_token)
        return true;
    return false;
}

ApiServer::Response ApiServer::route(const HttpRequest &req)
{
    if (req.method == "OPTIONS")
        return {200, okJson()};
    if (req.method != "GET" && req.method != "POST")
        return {405, errJson("method not allowed")};

    QString path = req.path;
    if (path.size() > 1 && path.endsWith('/'))
        path.chop(1);

    Response out;
    if (handleMeta(path, req, out))        return out;
    if (handleTransport(path, req, out))   return out;
    if (handleScreensaver(path, req, out)) return out;
    return {404, errJson("unknown endpoint")};
}

bool ApiServer::handleMeta(const QString &path, const HttpRequest &req, Response &out)
{
    Q_UNUSED(req);
    if (path == "/" || path == "/api/health") {
        out = {200, QByteArrayLiteral("{\"ok\":true,\"service\":\"linamp\"}")};
        return true;
    }
    if (path == "/api/clock/list") {
        QJsonObject o;
        o["ok"] = true;
        o["faces"] = QJsonArray::fromStringList(ScreenSaverView::faceNames());
        out = {200, QJsonDocument(o).toJson(QJsonDocument::Compact)};
        return true;
    }
    return false;
}

// Stubs filled in by later tasks.
bool ApiServer::handleTransport(const QString &, const HttpRequest &, Response &)
{
    return false;
}

bool ApiServer::handleScreensaver(const QString &, const HttpRequest &, Response &)
{
    return false;
}

void ApiServer::sendResponse(QTcpSocket *socket, const Response &resp)
{
    QByteArray body = resp.json;
    QByteArray out;
    out += "HTTP/1.1 " + QByteArray::number(resp.status) + " " + reasonPhrase(resp.status) + "\r\n";
    out += "Content-Type: application/json\r\n";
    out += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    out += "Access-Control-Allow-Origin: *\r\n";
    out += "Connection: close\r\n\r\n";
    out += body;
    socket->write(out);
    socket->flush();
}
```

> Note: `handleTransport`/`handleScreensaver` are defined here as `return false` stubs so the core compiles and routes meta endpoints. Task 5 and Task 6 **replace** these stub definitions with real implementations.

- [ ] **Step 3: Add the module to `CMakeLists.txt`**

After line 31 (the `include_directories(... src/view-geiss/effects)` block), add:

```cmake
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/api)
```

And inside the `qt_add_executable(player ...)` source list (e.g. right after the `src/vban/vbansender.h` line), add:

```cmake
    src/api/apiserver.cpp
    src/api/apiserver.h
```

- [ ] **Step 4: Instantiate in MainWindow**

In `src/view-basewindow/mainwindow.h`, add a forward declaration near the top (after the includes, before `class MainWindow`):

```cpp
class ApiServer;
```

and add a member in the `private:` section:

```cpp
    ApiServer *apiServer = nullptr;
```

In `src/view-basewindow/mainwindow.cpp`, add the include near the top:

```cpp
#include "apiserver.h"
```

and at the **end** of the `MainWindow` constructor (after the source `playbackStateChanged` connects, ~line 205), add:

```cpp
    // HTTP control API (best-effort; never fatal)
    apiServer = new ApiServer(coordinator, this, this);
```

- [ ] **Step 5: Build on the box**

Run (on the box): `cd /opt/linamp2 && make -j4`
Expected: `Built target player` (rc 0).

- [ ] **Step 6: Restart the player and verify meta endpoints**

Restart the app (see project restart procedure — relaunch `start.sh` as the `dietpi` user with the X/D-Bus environment). Then from the dev machine:

Run: `curl -s http://10.10.0.204:8080/api/health`
Expected: `{"ok":true,"service":"linamp"}`

Run: `curl -s http://10.10.0.204:8080/api/clock/list`
Expected: `{"ok":true,"faces":["Luxury","Aviator","Diver","Minimalist","Chronograph","Neon Retro","Bauhaus","Mondaine","Orbital","Guilloche","Digital","Seven Segment","Split Flap","Nixie","Terminal"]}`

Run: `curl -s -o /dev/null -w "%{http_code}\n" http://10.10.0.204:8080/api/nope`
Expected: `404`

- [ ] **Step 7: Commit**

```bash
git add src/api/apiserver.h src/api/apiserver.cpp CMakeLists.txt src/view-basewindow/mainwindow.h src/view-basewindow/mainwindow.cpp
git commit -m "Add ApiServer core: HTTP parsing, config, auth, meta routes"
```

---

### Task 5: Transport + audio routes

Replace the `handleTransport` stub with real routing for play/pause/stop/next/previous/seek/shuffle/repeat/volume/balance.

**Files:**
- Modify: `src/api/apiserver.cpp`

**Interfaces:**
- Consumes: `AudioSourceCoordinator` slots from Task 1 (`play/pause/stop/next/previous/seek/shuffle/repeat`, plus existing `setVolume/setBalance`); `MainWindow::playbackState()` from Task 3; `MediaPlayer::PlaybackState`.
- Produces: working `handleTransport`.

- [ ] **Step 1: Add the MediaPlayer include**

In `src/api/apiserver.cpp`, add to the include block:

```cpp
#include "mediaplayer.h"
```

- [ ] **Step 2: Replace the `handleTransport` stub**

Replace the stub definition of `ApiServer::handleTransport` with:

```cpp
bool ApiServer::handleTransport(const QString &path, const HttpRequest &req, Response &out)
{
    if (path == "/api/play")    { m_coordinator->play();    out = {200, okJson()}; return true; }
    if (path == "/api/pause")   { m_coordinator->pause();   out = {200, okJson()}; return true; }
    if (path == "/api/stop")    { m_coordinator->stop();    out = {200, okJson()}; return true; }
    if (path == "/api/next")    { m_coordinator->next();    out = {200, okJson()}; return true; }
    if (path == "/api/previous" || path == "/api/prev") {
        m_coordinator->previous(); out = {200, okJson()}; return true;
    }
    if (path == "/api/shuffle") { m_coordinator->shuffle(); out = {200, okJson()}; return true; }
    if (path == "/api/repeat")  { m_coordinator->repeat();  out = {200, okJson()}; return true; }

    if (path == "/api/playpause") {
        if (m_window->playbackState() == MediaPlayer::PlayingState)
            m_coordinator->pause();
        else
            m_coordinator->play();
        out = {200, okJson()};
        return true;
    }
    if (path == "/api/seek") {
        int ms;
        if (!parseIntParam(req.query.value("ms"), ms) || ms < 0) {
            out = {400, errJson("seek requires ms>=0")};
            return true;
        }
        m_coordinator->seek(ms);
        out = {200, okJson()};
        return true;
    }
    if (path == "/api/volume") {
        int level;
        if (!parseIntParam(req.query.value("level"), level) || level < 0 || level > 100) {
            out = {400, errJson("volume level must be 0..100")};
            return true;
        }
        m_coordinator->setVolume(level);
        out = {200, okJson()};
        return true;
    }
    if (path == "/api/balance") {
        int value;
        if (!parseIntParam(req.query.value("value"), value) || value < -100 || value > 100) {
            out = {400, errJson("balance value must be -100..100")};
            return true;
        }
        m_coordinator->setBalance(value);
        out = {200, okJson()};
        return true;
    }
    return false;
}
```

- [ ] **Step 3: Build + restart on the box**

Run (on the box): `cd /opt/linamp2 && make -j4` → `Built target player`, then restart the app.

- [ ] **Step 4: Verify transport endpoints**

With a track loaded, from the dev machine:

Run: `curl -s http://10.10.0.204:8080/api/play`   → `{"ok":true}` (audio starts)
Run: `curl -s http://10.10.0.204:8080/api/pause`  → `{"ok":true}` (audio pauses)
Run: `curl -s http://10.10.0.204:8080/api/volume?level=40` → `{"ok":true}` (volume slider moves to 40)
Run: `curl -s "http://10.10.0.204:8080/api/balance?value=-50"` → `{"ok":true}`
Run: `curl -s -o /dev/null -w "%{http_code}\n" "http://10.10.0.204:8080/api/volume?level=999"` → `400`

- [ ] **Step 5: Commit**

```bash
git add src/api/apiserver.cpp
git commit -m "Add transport + audio routes to ApiServer"
```

---

### Task 6: Screensaver + clock routes

Replace the `handleScreensaver` stub with real routing for screensaver on/off and clock face selection.

**Files:**
- Modify: `src/api/apiserver.cpp`

**Interfaces:**
- Consumes: `MainWindow::apiTriggerScreensaver/apiDismissScreensaver/apiShowClockFace` (Task 3); `ScreenSaverView::faceIndexForName` (Task 2). `screensaverview.h` is already included (Task 4).
- Produces: working `handleScreensaver`.

- [ ] **Step 1: Replace the `handleScreensaver` stub**

Replace the stub definition of `ApiServer::handleScreensaver` with:

```cpp
bool ApiServer::handleScreensaver(const QString &path, const HttpRequest &req, Response &out)
{
    if (path == "/api/screensaver/on") {
        m_window->apiTriggerScreensaver();
        out = {200, okJson()};
        return true;
    }
    if (path == "/api/screensaver/off") {
        m_window->apiDismissScreensaver();
        out = {200, okJson()};
        return true;
    }
    if (path == "/api/clock") {
        int index = -1;
        if (req.query.contains("index")) {
            if (!parseIntParam(req.query.value("index"), index)) {
                out = {400, errJson("index must be an integer")};
                return true;
            }
        } else if (req.query.contains("face")) {
            index = ScreenSaverView::faceIndexForName(req.query.value("face"));
            if (index < 0) {
                out = {400, errJson("unknown face name")};
                return true;
            }
        } else {
            out = {400, errJson("clock requires face or index")};
            return true;
        }
        if (!m_window->apiShowClockFace(index)) {
            out = {400, errJson("index out of range")};
            return true;
        }
        out = {200, okJson()};
        return true;
    }
    return false;
}
```

- [ ] **Step 2: Build + restart on the box**

Run (on the box): `cd /opt/linamp2 && make -j4` → `Built target player`, then restart the app.

- [ ] **Step 3: Verify screensaver + clock endpoints**

From the dev machine (watch the device screen):

Run: `curl -s "http://10.10.0.204:8080/api/clock?face=Nixie"` → `{"ok":true}` (Nixie tube face appears)
Run: `curl -s "http://10.10.0.204:8080/api/clock?face=Mondaine"` → `{"ok":true}` (Mondaine face appears)
Run: `curl -s "http://10.10.0.204:8080/api/clock?index=8"` → `{"ok":true}` (Orbital face appears)
Run: `curl -s http://10.10.0.204:8080/api/screensaver/off` → `{"ok":true}` (returns to player)
Run: `curl -s "http://10.10.0.204:8080/api/clock?face=Nope"` → `{"ok":false,"error":"unknown face name"}`

- [ ] **Step 4: Commit**

```bash
git add src/api/apiserver.cpp
git commit -m "Add screensaver + clock routes to ApiServer"
```

---

### Task 7: Documentation + full smoke test + merge

Document the API and provide a runnable example script; run the full smoke test; merge to `main`.

**Files:**
- Create: `docs/API.md`
- Create: `docs/api-examples.sh`
- Modify: `README.md`

- [ ] **Step 1: Write `docs/API.md`**

```markdown
# Linamp HTTP API

A lightweight LAN control API. All commands are plain `GET` requests (also
accept `POST`) and return JSON. Default: enabled on port `8080`, bound to all
interfaces, no auth.

Base URL: `http://<linamp-ip>:8080`

## Configuration (`~/.config/Rod/Linamp.conf`, `[api]` group)

| Key | Default | Meaning |
|---|---|---|
| `enabled` | `true` | Start the API server on launch |
| `port` | `8080` | TCP port |
| `bindAddress` | `0.0.0.0` | Interface to bind (use `127.0.0.1` for localhost only) |
| `token` | (empty) | If set, send `?token=…` or `Authorization: Bearer …` |

## Endpoints

### Transport + audio
| Method | Path | Notes |
|---|---|---|
| GET | `/api/play` | |
| GET | `/api/pause` | |
| GET | `/api/playpause` | toggle |
| GET | `/api/stop` | |
| GET | `/api/next` | |
| GET | `/api/previous` (`/api/prev`) | |
| GET | `/api/seek?ms=N` | absolute position in ms |
| GET | `/api/shuffle` | toggle |
| GET | `/api/repeat` | toggle |
| GET | `/api/volume?level=0..100` | |
| GET | `/api/balance?value=-100..100` | 0 = center |

### Screensaver + clocks
| Method | Path | Notes |
|---|---|---|
| GET | `/api/screensaver/on` | random face |
| GET | `/api/screensaver/off` | dismiss |
| GET | `/api/clock?face=NAME` | case-insensitive (see `/api/clock/list`) |
| GET | `/api/clock?index=N` | by theme index |
| GET | `/api/clock/list` | list face names |

### Meta
| Method | Path | Notes |
|---|---|---|
| GET | `/api/health` (`/`) | liveness |

## Responses
- Success: `{"ok":true}` (plus payload for `clock/list`).
- Failure: `{"ok":false,"error":"..."}` with status `400/401/404/405`.

## Examples
```bash
curl http://10.10.0.204:8080/api/play
curl "http://10.10.0.204:8080/api/volume?level=60"
curl "http://10.10.0.204:8080/api/clock?face=Nixie"
```
Or just open `http://10.10.0.204:8080/api/play` in a browser / bookmark it.
```

- [ ] **Step 2: Write `docs/api-examples.sh`**

```bash
#!/usr/bin/env bash
# Smoke-test the Linamp HTTP API. Usage: ./api-examples.sh [host:port]
set -u
BASE="http://${1:-10.10.0.204:8080}"

check() {
  local path="$1" expect="$2"
  local code
  code=$(curl -s -o /dev/null -w "%{http_code}" "$BASE$path")
  printf "%-40s -> %s (want %s)\n" "$path" "$code" "$expect"
}

check "/api/health"               200
check "/api/clock/list"           200
check "/api/play"                 200
check "/api/pause"                200
check "/api/volume?level=50"      200
check "/api/balance?value=0"      200
check "/api/clock?face=Nixie"     200
check "/api/screensaver/off"      200
check "/api/volume?level=999"     400
check "/api/clock?face=Nope"      400
check "/api/nope"                 404
```

- [ ] **Step 3: Reference the API in `README.md`**

Add a bullet to the fork-highlights callout near the top of `README.md`:

```markdown
> - 🔌 **HTTP API** — control transport, audio and the screensaver/clocks over the LAN with simple GET requests (see [docs/API.md](docs/API.md))
```

- [ ] **Step 4: Run the full smoke test on the box**

Run: `bash docs/api-examples.sh 10.10.0.204:8080`
Expected: every line shows the wanted code (200s and the three 4xx checks).

- [ ] **Step 5: Commit and merge to main**

```bash
git add docs/API.md docs/api-examples.sh README.md
git commit -m "Document HTTP API and add smoke-test script"
git checkout main
git merge --no-ff feature/http-api -m "Merge HTTP control API"
git push origin main
```

- [ ] **Step 6: Deploy on the box**

On the box: `cd /opt/linamp2 && git pull --ff-only origin main && make -j4`, then restart the player and re-run the smoke test to confirm the deployed build.

---

## Self-Review

**Spec coverage:**
- Transport+audio (play/pause/playpause/stop/next/prev/seek/shuffle/repeat/volume/balance) → Task 1 (slots) + Task 5 (routes). ✓
- Screensaver+clocks (on/off, clock by face/index, list) → Task 2 (face selection) + Task 3 (MainWindow hooks) + Task 6 (routes) + Task 4 (`clock/list`). ✓
- GET (browser-clickable), POST accepted, OPTIONS 200 → Task 4 `route()`. ✓
- QTcpServer, no new deps → Task 4. ✓
- Config `[api]` (enabled/port/bindAddress/token) → Task 4 ctor. ✓
- Token auth (query or Bearer) → Task 4 `authorized()`. ✓
- Error handling (404/400/401/405, non-fatal bind, never crash) → Task 4. ✓
- Health endpoint → Task 4. ✓
- Docs + curl smoke test → Task 7. ✓

**Placeholder scan:** The `handleTransport`/`handleScreensaver` "stubs" in Task 4 are intentional, compilable `return false` definitions, explicitly replaced in Tasks 5/6 — not placeholders. No `TBD`/`TODO` remain.

**Type consistency:** `HttpRequest`, `Response{int status; QByteArray json;}`, `parseRequest`, `route`, `handleMeta/handleTransport/handleScreensaver(const QString&, const HttpRequest&, Response&)`, `authorized`, `sendResponse`, `parseIntParam`, `okJson/errJson/reasonPhrase` are used consistently across tasks. Coordinator slots (`play/pause/stop/next/previous/seek/shuffle/repeat`), `MainWindow::playbackState()/apiTriggerScreensaver()/apiDismissScreensaver()/apiShowClockFace(int)`, and `ScreenSaverView::start(int)/faceNames()/faceIndexForName()` match their definitions. ✓
