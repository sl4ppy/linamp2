# Linamp Web UI — Phase 3 (Playlist + File Browser) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Let the web UI view/control the current playlist (list, play a track, remove, clear) and browse the device's music folder to add tracks — with the filesystem access strictly sandboxed to a configured music root.

**Architecture:** New thin `MainWindow` methods expose the playlist (`m_playlistModel`/`m_playlist`) and a sandboxed file browser over `api/musicRoot`; `ApiServer` adds playlist + browse/add routes; the frontend gains Player/Playlist/Files tabs. Re-fetches on tab open and after actions/track-changes (no new SSE event needed).

**Tech Stack:** C++17, Qt 6, vanilla JS embedded via the existing `webui.qrc`.

## Global Constraints

- Qt 6, C++; no new deps; GUI event loop only.
- **File browsing is sandboxed (hard requirement):** all `/api/browse` and `/api/add` paths are resolved relative to a configured root `api/musicRoot` (default `~/Music`, falling back to `~`), canonicalized, and REJECTED unless the canonical result equals the canonical root or is under it (`root + "/"` prefix). No `..` escape, no symlink-out, no absolute paths outside the root. Only audio files (mp3/flac/wav/ogg/m4a/aac/opus) are listed/added.
- Reuse existing source slots: `AudioSourceFile::jump(const QModelIndex&)`, `AudioSourceFile::addToPlaylist(const QList<QUrl>&)`. `QMediaPlaylist`: `mediaCount()`, `media(int)`, `currentIndex()`, `removeMedia(int)`, `clear()`. `PlaylistModel` columns: `Title`, `Artist`, `Duration`.
- New endpoints (all GET; JSON): `/api/playlist`, `/api/playlist/play?index=N`, `/api/playlist/remove?index=N`, `/api/playlist/clear`, `/api/browse?path=REL`, `/api/add?path=REL`.
- Config additions to the `[api]` group: `musicRoot`.
- Never crash on malformed input; reject bad paths with `400`.
- Build target `player`; Pi-only build/verify (`make -j4` + browser). Develop on `feature/web-ui-phase3`; integrate via finishing skill. Controller owns device builds/restarts.

---

### Task 1: Backend — playlist + sandboxed file browser

**Files:**
- Modify: `src/view-basewindow/mainwindow.h`
- Modify: `src/view-basewindow/mainwindow.cpp`
- Modify: `src/api/apiserver.h`
- Modify: `src/api/apiserver.cpp`

**Interfaces:**
- Consumes: `m_playlist` (`QMediaPlaylist*`), `m_playlistModel` (`PlaylistModel*`), `fileSource` (`AudioSourceFile*`).
- Produces: `MainWindow` methods `QJsonArray apiPlaylist() const`, `int apiPlaylistCurrent() const`, `bool apiPlaylistPlay(int)`, `bool apiPlaylistRemove(int)`, `void apiPlaylistClear()`, `QJsonObject apiBrowse(const QString&) const`, `QJsonObject apiAddPath(const QString&)`. `ApiServer::handlePlaylist(...)` route group.

- [ ] **Step 1: MainWindow header**

In `src/view-basewindow/mainwindow.h`, add the includes `#include <QJsonArray>` and `#include <QJsonObject>` near the top. Add to the `public:` section (after the existing api hooks):
```cpp
    // Web API: playlist + file browser
    QJsonArray apiPlaylist() const;
    int  apiPlaylistCurrent() const;
    bool apiPlaylistPlay(int index);
    bool apiPlaylistRemove(int index);
    void apiPlaylistClear();
    QJsonObject apiBrowse(const QString &rel) const;
    QJsonObject apiAddPath(const QString &rel);
```
In the `private:` section add:
```cpp
    QString m_musicRoot;
    bool resolveSandboxed(const QString &rel, QString &outAbs) const;
```

- [ ] **Step 2: MainWindow implementation**

In `src/view-basewindow/mainwindow.cpp`, add includes near the top:
```cpp
#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <QSettings>
#include <QJsonArray>
#include <QJsonObject>
#include "playlistmodel.h"
```
In the constructor (anywhere after the members are constructed), initialise the music root:
```cpp
    m_musicRoot = QSettings().value("api/musicRoot", QDir::homePath() + "/Music").toString();
    if (!QDir(m_musicRoot).exists())
        m_musicRoot = QDir::homePath();
```
Append the implementations to the file:
```cpp
static const QStringList kAudioExt = { "mp3","flac","wav","ogg","m4a","aac","opus" };

int MainWindow::apiPlaylistCurrent() const
{
    return m_playlist ? m_playlist->currentIndex() : -1;
}

QJsonArray MainWindow::apiPlaylist() const
{
    QJsonArray arr;
    if (!m_playlist || !m_playlistModel) return arr;
    const int cur = m_playlist->currentIndex();
    const int n = m_playlist->mediaCount();
    for (int i = 0; i < n; ++i) {
        QJsonObject o;
        o["index"] = i;
        QString title = m_playlistModel->data(m_playlistModel->index(i, PlaylistModel::Title)).toString();
        const QUrl u = m_playlist->media(i);
        if (title.isEmpty()) title = u.fileName();
        o["title"]    = title;
        o["artist"]   = m_playlistModel->data(m_playlistModel->index(i, PlaylistModel::Artist)).toString();
        o["duration"] = m_playlistModel->data(m_playlistModel->index(i, PlaylistModel::Duration)).toString();
        o["current"]  = (i == cur);
        arr.append(o);
    }
    return arr;
}

bool MainWindow::apiPlaylistPlay(int index)
{
    if (!m_playlist || !m_playlistModel || index < 0 || index >= m_playlist->mediaCount())
        return false;
    fileSource->jump(m_playlistModel->index(index, 0));
    return true;
}

bool MainWindow::apiPlaylistRemove(int index)
{
    if (!m_playlist || index < 0 || index >= m_playlist->mediaCount())
        return false;
    return m_playlist->removeMedia(index);
}

void MainWindow::apiPlaylistClear()
{
    if (m_playlist) m_playlist->clear();
}

bool MainWindow::resolveSandboxed(const QString &rel, QString &outAbs) const
{
    const QString rootCanon = QDir(m_musicRoot).canonicalPath();
    if (rootCanon.isEmpty()) return false;
    const QString candidate = QDir(rootCanon).filePath(rel.isEmpty() ? QStringLiteral(".") : rel);
    const QString canon = QFileInfo(candidate).canonicalFilePath();
    if (canon.isEmpty()) return false;                // does not exist
    if (canon != rootCanon && !canon.startsWith(rootCanon + "/")) return false; // escaped root
    outAbs = canon;
    return true;
}

QJsonObject MainWindow::apiBrowse(const QString &rel) const
{
    QJsonObject res;
    QString abs;
    if (!resolveSandboxed(rel, abs)) { res["ok"] = false; res["error"] = "invalid path"; return res; }
    QFileInfo fi(abs);
    if (!fi.isDir()) { res["ok"] = false; res["error"] = "not a directory"; return res; }

    const QString rootCanon = QDir(m_musicRoot).canonicalPath();
    QString relPath = QDir(rootCanon).relativeFilePath(abs);
    if (relPath == ".") relPath = "";

    res["ok"] = true;
    res["path"] = relPath;

    QJsonArray entries;
    QDir dir(abs);
    const auto list = dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot,
                                        QDir::Name | QDir::DirsFirst);
    for (const QFileInfo &e : list) {
        const QString childRel = relPath.isEmpty() ? e.fileName() : relPath + "/" + e.fileName();
        if (e.isDir()) {
            QJsonObject o; o["name"] = e.fileName(); o["type"] = "dir"; o["path"] = childRel;
            entries.append(o);
        } else if (kAudioExt.contains(e.suffix().toLower())) {
            QJsonObject o; o["name"] = e.fileName(); o["type"] = "file"; o["path"] = childRel;
            o["size"] = static_cast<double>(e.size());
            entries.append(o);
        }
    }
    res["entries"] = entries;
    return res;
}

QJsonObject MainWindow::apiAddPath(const QString &rel)
{
    QJsonObject res;
    QString abs;
    if (!resolveSandboxed(rel, abs)) { res["ok"] = false; res["error"] = "invalid path"; return res; }
    QFileInfo fi(abs);
    QList<QUrl> urls;
    if (fi.isDir()) {
        QDir dir(abs);
        const auto files = dir.entryInfoList(QDir::Files, QDir::Name);
        for (const QFileInfo &f : files)
            if (kAudioExt.contains(f.suffix().toLower()))
                urls << QUrl::fromLocalFile(f.absoluteFilePath());
    } else if (kAudioExt.contains(fi.suffix().toLower())) {
        urls << QUrl::fromLocalFile(abs);
    }
    if (urls.isEmpty()) { res["ok"] = false; res["error"] = "no audio files"; return res; }
    fileSource->addToPlaylist(urls);
    res["ok"] = true;
    res["added"] = urls.size();
    return res;
}
```

- [ ] **Step 3: ApiServer playlist/browse routes**

In `src/api/apiserver.h`, add a private method declaration:
```cpp
    bool handlePlaylist(const QString &path, const HttpRequest &req, Response &out);
```
In `src/api/apiserver.cpp`, add includes `#include <QJsonArray>` and `#include <QJsonObject>` (QJsonDocument already included). Add the handler:
```cpp
bool ApiServer::handlePlaylist(const QString &path, const HttpRequest &req, Response &out)
{
    if (path == "/api/playlist") {
        QJsonObject o;
        o["ok"] = true;
        o["current"] = m_window->apiPlaylistCurrent();
        o["items"] = m_window->apiPlaylist();
        out = {200, QJsonDocument(o).toJson(QJsonDocument::Compact)};
        return true;
    }
    if (path == "/api/playlist/play") {
        int index;
        if (!parseIntParam(req.query.value("index"), index)) { out = {400, errJson("index required")}; return true; }
        out = m_window->apiPlaylistPlay(index) ? Response{200, okJson()} : Response{400, errJson("bad index")};
        return true;
    }
    if (path == "/api/playlist/remove") {
        int index;
        if (!parseIntParam(req.query.value("index"), index)) { out = {400, errJson("index required")}; return true; }
        out = m_window->apiPlaylistRemove(index) ? Response{200, okJson()} : Response{400, errJson("bad index")};
        return true;
    }
    if (path == "/api/playlist/clear") {
        m_window->apiPlaylistClear();
        out = {200, okJson()};
        return true;
    }
    if (path == "/api/browse") {
        QJsonObject o = m_window->apiBrowse(req.query.value("path"));
        out = {o.value("ok").toBool() ? 200 : 400, QJsonDocument(o).toJson(QJsonDocument::Compact)};
        return true;
    }
    if (path == "/api/add") {
        QJsonObject o = m_window->apiAddPath(req.query.value("path"));
        out = {o.value("ok").toBool() ? 200 : 400, QJsonDocument(o).toJson(QJsonDocument::Compact)};
        return true;
    }
    return false;
}
```
Call it in `route()` after `handleStatic` (before `handleTransport`):
```cpp
    if (handleStatic(path, out))           return out;
    if (handlePlaylist(path, req, out))    return out;
    if (handleTransport(path, req, out))   return out;
```

- [ ] **Step 4: Build on the box**

Run (on the box): `cd /opt/linamp2 && make -j4`
Expected: `Built target player` (rc 0).

- [ ] **Step 5: Restart and verify**

Restart the player. From the box:
- `curl -s "localhost:8080/api/browse?path="` → `{"ok":true,"path":"",...,"entries":[...]}` listing the music root (dirs + audio files).
- `curl -s "localhost:8080/api/browse?path=../../etc"` → `400 {"ok":false,"error":"invalid path"}` (sandbox rejects traversal).
- `curl -s "localhost:8080/api/add?path=<a real audio file rel path from the browse output>"` → `{"ok":true,"added":1}`.
- `curl -s localhost:8080/api/playlist` → `{"ok":true,"current":...,"items":[{"index":0,"title":...}]}` now showing the added track.
- `curl -s "localhost:8080/api/playlist/play?index=0"` → `{"ok":true}` and audio starts.

- [ ] **Step 6: Commit**

```bash
git add src/view-basewindow/mainwindow.h src/view-basewindow/mainwindow.cpp src/api/apiserver.h src/api/apiserver.cpp
git commit -m "Web UI Phase 3: playlist + sandboxed file-browser API"
```

---

### Task 2: Frontend — Player/Playlist/Files tabs

**Files:**
- Modify: `webui/index.html`
- Modify: `webui/app.css`
- Modify: `webui/app.js`

**Interfaces:**
- Consumes: `/api/playlist`, `/api/playlist/play?index=`, `/api/playlist/remove?index=`, `/api/playlist/clear`, `/api/browse?path=`, `/api/add?path=`, plus existing SSE.
- Produces: a three-tab UI.

- [ ] **Step 1: index.html — add tab bar + two sections**

In `webui/index.html`, add a tab bar right after `<header>...</header>` (before the `<section class="screen">`):
```html
    <nav class="tabs">
      <button class="tab on" data-tab="player">Player</button>
      <button class="tab" data-tab="playlist">Playlist</button>
      <button class="tab" data-tab="files">Files</button>
    </nav>
```
Wrap the existing player sections (`.screen`, `.seek`, `.transport`, `.rows`) in `<div id="tab-player" class="panel">…</div>`. Then add the two new panels after it (before `</main>`):
```html
    <div id="tab-playlist" class="panel hidden">
      <div class="pl-head"><span id="pl-count">0 tracks</span>
        <button id="pl-clear" class="minibtn">Clear</button></div>
      <ul id="pl-list" class="pl-list"></ul>
    </div>
    <div id="tab-files" class="panel hidden">
      <div class="fb-head"><button id="fb-up" class="minibtn">⬆ Up</button>
        <span id="fb-path" class="fb-path">/</span></div>
      <ul id="fb-list" class="fb-list"></ul>
    </div>
```

- [ ] **Step 2: app.css — tabs + lists**

Append to `webui/app.css`:
```css
.tabs{ display:flex; gap:6px; margin:10px 0 4px; }
.tab{ flex:1; padding:9px; font-size:13px; color:var(--dim); background:var(--panel);
  border:1px solid var(--line); border-radius:8px; cursor:pointer; }
.tab.on{ color:var(--green); border-color:#1f4; }
.panel.hidden{ display:none; }
.pl-head,.fb-head{ display:flex; align-items:center; justify-content:space-between;
  gap:10px; margin:10px 0; color:var(--dim); font-size:13px; }
.fb-path{ font-family:"DejaVu Sans Mono",monospace; color:var(--green); overflow:hidden;
  text-overflow:ellipsis; white-space:nowrap; }
.minibtn{ padding:6px 10px; font-size:12px; color:var(--ink); background:var(--panel);
  border:1px solid var(--line); border-radius:6px; cursor:pointer; }
.pl-list,.fb-list{ list-style:none; margin:0; padding:0; display:flex; flex-direction:column; gap:6px; }
.pl-list li,.fb-list li{ display:flex; align-items:center; gap:10px; padding:11px 12px;
  background:var(--panel); border:1px solid var(--line); border-radius:8px; }
.pl-list li.cur{ border-color:var(--green); }
.pl-row{ flex:1; min-width:0; cursor:pointer; }
.pl-row .t{ white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
.pl-row .a{ font-size:12px; color:var(--dim); }
.pl-dur{ font-family:"DejaVu Sans Mono",monospace; font-size:12px; color:var(--dim); }
.pl-x{ color:#c66; background:none; border:none; font-size:16px; cursor:pointer; }
.fb-list li{ cursor:pointer; }
.fb-ico{ width:18px; text-align:center; }
.fb-list .add{ margin-left:auto; color:var(--accent); }
```

- [ ] **Step 3: app.js — tabs, playlist, files**

Append to `webui/app.js` (after the existing code; it can reference `call` already imported):
```javascript
// ---- tabs ----
const panels = { player: $("tab-player"), playlist: $("tab-playlist"), files: $("tab-files") };
let activeTab = "player";
document.querySelectorAll(".tab").forEach((btn) => {
  btn.onclick = () => {
    activeTab = btn.dataset.tab;
    document.querySelectorAll(".tab").forEach((b) => b.classList.toggle("on", b === btn));
    for (const k in panels) panels[k].classList.toggle("hidden", k !== activeTab);
    if (activeTab === "playlist") loadPlaylist();
    if (activeTab === "files") browse(fbPath);
  };
});

// ---- playlist ----
async function loadPlaylist() {
  let data;
  try { data = await (await fetch("/api/playlist" + tokenQS())).json(); } catch { return; }
  const items = data.items || [];
  $("pl-count").textContent = items.length + (items.length === 1 ? " track" : " tracks");
  const ul = $("pl-list");
  ul.innerHTML = "";
  for (const it of items) {
    const li = document.createElement("li");
    if (it.current) li.classList.add("cur");
    const row = document.createElement("div");
    row.className = "pl-row";
    const t = document.createElement("div"); t.className = "t"; t.textContent = it.title || "—";
    const a = document.createElement("div"); a.className = "a"; a.textContent = it.artist || "";
    row.append(t, a);
    row.onclick = () => call("/api/playlist/play?index=" + it.index).then(() => setTimeout(loadPlaylist, 250));
    const dur = document.createElement("span"); dur.className = "pl-dur"; dur.textContent = it.duration || "";
    const x = document.createElement("button"); x.className = "pl-x"; x.textContent = "✕";
    x.onclick = (e) => { e.stopPropagation();
      call("/api/playlist/remove?index=" + it.index).then(() => setTimeout(loadPlaylist, 150)); };
    li.append(row, dur, x);
    ul.appendChild(li);
  }
}
$("pl-clear").onclick = () => call("/api/playlist/clear").then(() => setTimeout(loadPlaylist, 150));

// ---- file browser ----
let fbPath = "";
function tokenQS() {
  const t = new URLSearchParams(location.search).get("token") || localStorage.getItem("linamp_token") || "";
  return t ? "?token=" + encodeURIComponent(t) : "";
}
async function browse(path) {
  let data;
  try {
    const q = "/api/browse?path=" + encodeURIComponent(path);
    const t = new URLSearchParams(location.search).get("token") || localStorage.getItem("linamp_token") || "";
    data = await (await fetch(q + (t ? "&token=" + encodeURIComponent(t) : "")).then((r) => r)).json();
  } catch { return; }
  if (!data.ok) return;
  fbPath = data.path || "";
  $("fb-path").textContent = "/" + fbPath;
  const ul = $("fb-list");
  ul.innerHTML = "";
  for (const e of data.entries || []) {
    const li = document.createElement("li");
    const ico = document.createElement("span"); ico.className = "fb-ico";
    ico.textContent = e.type === "dir" ? "📁" : "🎵";
    const name = document.createElement("span"); name.textContent = e.name;
    name.style.cssText = "flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap";
    li.append(ico, name);
    if (e.type === "dir") {
      li.onclick = () => browse(e.path);
    } else {
      const add = document.createElement("span"); add.className = "add"; add.textContent = "+ add";
      add.onclick = (ev) => { ev.stopPropagation();
        call("/api/add?path=" + encodeURIComponent(e.path)).then(() => { add.textContent = "added"; }); };
      li.appendChild(add);
    }
    ul.appendChild(li);
  }
}
$("fb-up").onclick = () => {
  if (!fbPath) return;
  const parent = fbPath.includes("/") ? fbPath.slice(0, fbPath.lastIndexOf("/")) : "";
  browse(parent);
};

// refresh the playlist highlight when the track changes (status events)
window.addEventListener("linamp-status", () => { if (activeTab === "playlist") loadPlaylist(); });
```
And in the existing `applyStatus(s)` function, add one line at the end so the playlist tab refreshes the current-track highlight:
```javascript
  window.dispatchEvent(new CustomEvent("linamp-status"));
```

- [ ] **Step 4: Build on the box**

Run (on the box): `cd /opt/linamp2 && make -j4`
Expected: `Built target player` (rc 0). (Only `qrc_webui` regenerates.)

- [ ] **Step 5: Verify in a browser (Phase 3 gate)**

Restart the player. Open `http://10.10.0.204:8080/`.
- The **Files** tab lists the music root; tapping a folder navigates in, **Up** goes back; tapping **+ add** on a file adds it (button shows "added").
- The **Playlist** tab shows the queue with title/artist/duration; tapping a row plays that track (and the Player tab then shows it playing with the live spectrum); the ✕ removes a track; **Clear** empties it; the current track is highlighted.
- The **Player** tab still works (transport, sliders, spectrum).

- [ ] **Step 6: Commit**

```bash
git add webui/index.html webui/app.css webui/app.js
git commit -m "Web UI Phase 3: Player/Playlist/Files tabs"
```

---

## Self-Review

**Spec coverage (Phase 3):** view/reorder*/remove/clear the queue + play a track → Task 1 (API) + Task 2 (UI); sandboxed filesystem browse + add → Task 1 (`resolveSandboxed`, audio-only) + Task 2 (Files tab). (*reorder deferred — not in this phase's endpoints; view/play/remove/clear delivered.) ✓

**Placeholder scan:** none. Reorder is explicitly out of this phase (the spec lists view/play/remove/clear + browse/add; drag-reorder is a later nicety).

**Type consistency:** `MainWindow::apiPlaylist()/apiPlaylistCurrent()/apiPlaylistPlay(int)/apiPlaylistRemove(int)/apiPlaylistClear()/apiBrowse(QString)/apiAddPath(QString)/resolveSandboxed(QString,QString&)`; `ApiServer::handlePlaylist(...)` added to `route()` after `handleStatic`. Frontend endpoint paths and params (`index`, `path`) match the backend. `PlaylistModel::Title/Artist/Duration` columns used as defined. ✓
