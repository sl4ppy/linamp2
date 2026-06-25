# Linamp Web UI — Phase 4 (Sources/VBAN + Clocks) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Finish the web UI: switch audio source (File/Bluetooth/CD/Spotify), toggle VBAN streaming, and pick a screensaver clock face from a gallery (the web-only extra) plus screensaver on/off — all from the browser.

**Architecture:** New backend routes for source switching + VBAN (thin MainWindow methods over the coordinator and `VbanSender`); the clock endpoints already exist (`/api/clock`, `/api/clock/list`, `/api/screensaver/on|off`). The frontend gains Sources and Clocks tabs.

**Tech Stack:** C++17, Qt 6, vanilla JS embedded via `webui.qrc`.

## Global Constraints

- Qt 6, C++; no new deps; GUI event loop only.
- Source order in the coordinator: index 0 FILE, 1 BT, 2 CD, 3 SPOT. Switch via `AudioSourceCoordinator::setSource(int)`.
- VBAN via `VbanSender::setEnabled(bool)` / `isEnabled()` (MainWindow member `vbanSender`).
- New endpoints (GET, JSON): `/api/sources` (list + current + vban), `/api/source?name=FILE|BT|CD|SPOT` (or `?index=N`), `/api/vban?on=1|0` (omit `on` to just read state). Clock endpoints already exist and are reused.
- Frontend vanilla JS; server text via textContent; token forwarded.
- Never crash on malformed input; unknown source → 400.
- Build target `player`; Pi-only build/verify. Develop on `feature/web-ui-phase4`; integrate via finishing skill. Controller owns device builds/restarts.

---

### Task 1: Backend — source switching + VBAN routes

**Files:**
- Modify: `src/audiosource-coordinator/audiosourcecoordinator.h`
- Modify: `src/audiosource-coordinator/audiosourcecoordinator.cpp`
- Modify: `src/view-basewindow/mainwindow.h`
- Modify: `src/view-basewindow/mainwindow.cpp`
- Modify: `src/api/apiserver.h`
- Modify: `src/api/apiserver.cpp`

**Interfaces:**
- Consumes: `AudioSourceCoordinator::setSource(int)`, `currentSourceLabel()`, private `sourceLabels` (`QList<QString>`); `VbanSender` (`setEnabled`/`isEnabled`) via `MainWindow::vbanSender`.
- Produces: `AudioSourceCoordinator::sourceLabelList() const`; `MainWindow::apiSources()/apiSetSource(QString)/apiVban(bool)/apiVbanState()`; `ApiServer::handleSources(...)`.

- [ ] **Step 1: Coordinator — expose the label list**

In `src/audiosource-coordinator/audiosourcecoordinator.h`, add `#include <QStringList>` and a public method:
```cpp
    QStringList sourceLabelList() const;   // all source labels in index order
```
In `src/audiosource-coordinator/audiosourcecoordinator.cpp`, append:
```cpp
QStringList AudioSourceCoordinator::sourceLabelList() const
{
    return sourceLabels;   // QList<QString> is QStringList in Qt 6
}
```

- [ ] **Step 2: MainWindow — sources + vban methods**

In `src/view-basewindow/mainwindow.h`, add to `public:`:
```cpp
    QJsonObject apiSources() const;
    bool apiSetSource(const QString &nameOrIndex);
    void apiVban(bool on);
    bool apiVbanState() const;
```
In `src/view-basewindow/mainwindow.cpp` (QJsonObject/QJsonArray already included from Phase 3), append:
```cpp
QJsonObject MainWindow::apiSources() const
{
    QJsonObject o;
    o["ok"] = true;
    o["current"] = coordinator->currentSourceLabel();
    QJsonArray arr;
    for (const QString &l : coordinator->sourceLabelList())
        arr.append(l);
    o["sources"] = arr;
    o["vban"] = apiVbanState();
    return o;
}

bool MainWindow::apiSetSource(const QString &nameOrIndex)
{
    const QStringList labels = coordinator->sourceLabelList();
    int idx = -1;
    for (int i = 0; i < labels.size(); ++i)
        if (labels[i].compare(nameOrIndex, Qt::CaseInsensitive) == 0) { idx = i; break; }
    if (idx < 0) {
        bool ok = false;
        const int n = nameOrIndex.toInt(&ok);
        if (ok && n >= 0 && n < labels.size()) idx = n;
    }
    if (idx < 0) return false;
    coordinator->setSource(idx);
    return true;
}

void MainWindow::apiVban(bool on)
{
    if (vbanSender) vbanSender->setEnabled(on);
}

bool MainWindow::apiVbanState() const
{
    return vbanSender ? vbanSender->isEnabled() : false;
}
```

- [ ] **Step 3: ApiServer — sources/vban routes**

In `src/api/apiserver.h`, add:
```cpp
    bool handleSources(const QString &path, const HttpRequest &req, Response &out);
```
In `src/api/apiserver.cpp`, add the handler:
```cpp
bool ApiServer::handleSources(const QString &path, const HttpRequest &req, Response &out)
{
    if (path == "/api/sources") {
        out = {200, QJsonDocument(m_window->apiSources()).toJson(QJsonDocument::Compact)};
        return true;
    }
    if (path == "/api/source") {
        QString sel = req.query.value("name");
        if (sel.isEmpty()) sel = req.query.value("index");
        if (sel.isEmpty()) { out = {400, errJson("name or index required")}; return true; }
        out = m_window->apiSetSource(sel) ? Response{200, okJson()} : Response{400, errJson("unknown source")};
        return true;
    }
    if (path == "/api/vban") {
        if (req.query.contains("on")) {
            const QString v = req.query.value("on");
            const bool on = (v == "1" || v.compare("true", Qt::CaseInsensitive) == 0);
            m_window->apiVban(on);
        }
        QJsonObject o;
        o["ok"] = true;
        o["vban"] = m_window->apiVbanState();
        out = {200, QJsonDocument(o).toJson(QJsonDocument::Compact)};
        return true;
    }
    return false;
}
```
Call it in `route()` after `handlePlaylist` (before `handleTransport`):
```cpp
    if (handlePlaylist(path, req, out))    return out;
    if (handleSources(path, req, out))     return out;
    if (handleTransport(path, req, out))   return out;
```

- [ ] **Step 4: Build on the box**

Run (on the box): `cd /opt/linamp2 && make -j4`
Expected: `Built target player` (rc 0).

- [ ] **Step 5: Restart and verify**

Restart the player. From the box:
- `curl -s localhost:8080/api/sources` → `{"ok":true,"current":"FILE","sources":["FILE","BT","CD","SPOT"],"vban":false}`.
- `curl -s "localhost:8080/api/source?name=BT"` → `{"ok":true}`; then `/api/sources` shows `"current":"BT"`. Switch back: `?name=FILE`.
- `curl -s "localhost:8080/api/source?name=NOPE"` → `400`.
- `curl -s "localhost:8080/api/vban?on=1"` → `{"ok":true,"vban":true}`; `?on=0` → `vban:false`.

- [ ] **Step 6: Commit**

```bash
git add src/audiosource-coordinator/audiosourcecoordinator.h src/audiosource-coordinator/audiosourcecoordinator.cpp src/view-basewindow/mainwindow.h src/view-basewindow/mainwindow.cpp src/api/apiserver.h src/api/apiserver.cpp
git commit -m "Web UI Phase 4: source switching + VBAN API"
```

---

### Task 2: Frontend — Sources + Clocks tabs

**Files:**
- Modify: `webui/index.html`
- Modify: `webui/app.css`
- Modify: `webui/app.js`

**Interfaces:**
- Consumes: `/api/sources`, `/api/source?name=`, `/api/vban?on=`, `/api/clock/list`, `/api/clock?face=`, `/api/screensaver/on`, `/api/screensaver/off`.
- Produces: Sources + Clocks tabs.

- [ ] **Step 1: index.html — add two tabs + panels**

In `webui/index.html`, add two buttons to the `<nav class="tabs">`:
```html
      <button class="tab" data-tab="sources">Sources</button>
      <button class="tab" data-tab="clocks">Clocks</button>
```
Add two panels before `</main>` (after the files panel):
```html
    <div id="tab-sources" class="panel hidden">
      <div class="sec-head">Audio source</div>
      <div id="src-list" class="src-list"></div>
      <div class="sec-head">VBAN streaming</div>
      <button id="vban-btn" class="bigtog">VBAN: —</button>
    </div>
    <div id="tab-clocks" class="panel hidden">
      <div class="sec-head">Screensaver</div>
      <div class="ss-row">
        <button id="ss-on" class="minibtn">Show (random)</button>
        <button id="ss-off" class="minibtn">Dismiss</button>
      </div>
      <div class="sec-head">Clock faces</div>
      <div id="clock-grid" class="clock-grid"></div>
    </div>
```

- [ ] **Step 2: app.css — sources/clocks styling**

Append to `webui/app.css`:
```css
.tabs{ flex-wrap:wrap; }
.tab{ min-width:72px; }
.sec-head{ color:var(--dim); font-size:12px; text-transform:uppercase; letter-spacing:.1em;
  margin:14px 0 8px; }
.src-list{ display:grid; grid-template-columns:repeat(2,1fr); gap:8px; }
.src-btn{ padding:14px; font-size:14px; color:var(--ink); background:var(--panel);
  border:1px solid var(--line); border-radius:8px; cursor:pointer; }
.src-btn.on{ color:var(--green); border-color:var(--green); box-shadow:0 0 0 1px var(--green) inset; }
.bigtog{ width:100%; padding:14px; font-size:14px; color:var(--ink); background:var(--panel);
  border:1px solid var(--line); border-radius:8px; cursor:pointer; }
.bigtog.on{ color:var(--green); border-color:var(--green); }
.ss-row{ display:flex; gap:8px; }
.ss-row .minibtn{ flex:1; }
.clock-grid{ display:grid; grid-template-columns:repeat(3,1fr); gap:8px; }
.clock-cell{ padding:14px 8px; font-size:13px; text-align:center; color:var(--ink);
  background:var(--panel); border:1px solid var(--line); border-radius:8px; cursor:pointer; }
.clock-cell:active{ border-color:var(--green); color:var(--green); }
```

- [ ] **Step 3: app.js — sources + clocks logic**

Append to `webui/app.js`:
```javascript
// ---- sources + vban ----
async function loadSources() {
  let data;
  try { data = await (await fetch("/api/sources" + tokenQS())).json(); } catch { return; }
  const wrap = $("src-list");
  wrap.innerHTML = "";
  for (const label of data.sources || []) {
    const b = document.createElement("button");
    b.className = "src-btn" + (label === data.current ? " on" : "");
    b.textContent = label;
    b.onclick = () => call("/api/source?name=" + encodeURIComponent(label)).then(() => setTimeout(loadSources, 200));
    wrap.appendChild(b);
  }
  const vb = $("vban-btn");
  vb.textContent = "VBAN: " + (data.vban ? "ON" : "OFF");
  vb.classList.toggle("on", !!data.vban);
  vb.onclick = () => call("/api/vban?on=" + (data.vban ? 0 : 1)).then(() => setTimeout(loadSources, 200));
}

// ---- clocks ----
let clocksLoaded = false;
async function loadClocks() {
  if (clocksLoaded) return;
  let data;
  try { data = await (await fetch("/api/clock/list" + tokenQS())).json(); } catch { return; }
  const grid = $("clock-grid");
  grid.innerHTML = "";
  for (const face of data.faces || []) {
    const cell = document.createElement("div");
    cell.className = "clock-cell";
    cell.textContent = face;
    cell.onclick = () => call("/api/clock?face=" + encodeURIComponent(face));
    grid.appendChild(cell);
  }
  clocksLoaded = true;
}
$("ss-on").onclick = () => call("/api/screensaver/on");
$("ss-off").onclick = () => call("/api/screensaver/off");
```
And extend the existing tab-switch handler so the new tabs load their data. Find the `btn.onclick` block added in Phase 3 (the one with `if (activeTab === "playlist") loadPlaylist();`) and add two lines inside it:
```javascript
    if (activeTab === "sources") loadSources();
    if (activeTab === "clocks") loadClocks();
```
Also add `sources` and `clocks` to the `panels` map (find `const panels = { player: ..., playlist: ..., files: ... };` and add):
```javascript
const panels = { player: $("tab-player"), playlist: $("tab-playlist"), files: $("tab-files"),
  sources: $("tab-sources"), clocks: $("tab-clocks") };
```

- [ ] **Step 4: Build on the box**

Run (on the box): `cd /opt/linamp2 && make -j4`
Expected: `Built target player` (rc 0).

- [ ] **Step 5: Verify in a browser (Phase 4 gate)**

Restart the player. Open `http://10.10.0.204:8080/`.
- **Sources** tab: four source buttons (FILE/BT/CD/SPOT) with the current one highlighted; tapping one switches the device source (the Player tab's SRC updates); the VBAN button toggles ON/OFF and reflects state.
- **Clocks** tab: Show/Dismiss screensaver buttons work; a 3-column gallery of the 15 face names; tapping a face shows that clock on the device screen.
- Player/Playlist/Files tabs still work.

- [ ] **Step 6: Commit**

```bash
git add webui/index.html webui/app.css webui/app.js
git commit -m "Web UI Phase 4: Sources + Clocks tabs"
```

---

## Self-Review

**Spec coverage (Phase 4):** switch source (File/CD/Bluetooth/Spotify) + VBAN toggle → Task 1 (API) + Task 2 (Sources tab); clock-face picker gallery + screensaver on/off → Task 2 (Clocks tab, reusing existing `/api/clock*` endpoints). ✓

**Placeholder scan:** none. The Clocks tab reuses the already-built `/api/clock`, `/api/clock/list`, `/api/screensaver/on|off` endpoints (no new backend needed for clocks) — intentional.

**Type consistency:** `AudioSourceCoordinator::sourceLabelList()`; `MainWindow::apiSources()/apiSetSource(QString)/apiVban(bool)/apiVbanState()`; `ApiServer::handleSources(...)` added to `route()` after `handlePlaylist`. Frontend endpoint paths/params (`name`, `on`, `face`) match the backend; the `panels` map and tab-switch handler extended for `sources`/`clocks`. ✓
