# Linamp Web UI — Phase 2 (Player UI) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Turn the Phase 1 status page into a full hybrid retro-modern player: transport, volume/balance sliders, shuffle/repeat toggles, seek bar, EQ/PL state indicators, and a live `<canvas>` spectrum analyzer — all driven by SSE + the existing control API.

**Architecture:** Add a `spectrum` SSE event: `WebStateHub` computes the 19-band spectrum from the PCM it already receives (`dataEmitted`), gated on whether any SSE client is connected (`SseBroker` toggles it); `SseBroker` broadcasts it. The frontend is rebuilt as a responsive player using the existing GET control endpoints plus the new spectrum stream.

**Tech Stack:** C++17, Qt 6 (Core/Network/Gui/Multimedia, all linked), vanilla HTML/CSS/JS embedded via the existing `webui.qrc`.

## Global Constraints

- Qt 6, C++; `m_`-prefixed members in `src/api`. No new dependencies. GUI event loop only (no threads/locks).
- Spectrum is computed from PCM in `WebStateHub` (reusing `fft.h`'s `calc_freq`), throttled to ~30 fps, and ONLY when `SseBroker` reports ≥1 client (avoid FFT work when nobody is watching).
- Reuse the EXISTING control endpoints: `/api/play /api/pause /api/playpause /api/stop /api/next /api/previous /api/seek?ms=N /api/shuffle /api/repeat /api/volume?level=0..100 /api/balance?value=-100..100`. Do NOT add new control routes in this phase. `eq`/`pl` are display-only indicators.
- New SSE event: `event: spectrum\r\ndata: [v,v,...]` where each `v` is an int 0..40 (19 values). Sent only while a client is connected and audio is flowing.
- Web assets stay embedded in `webui.qrc` (already wired). No new qrc entries unless a new file is added (then add it to `webui.qrc`).
- Never crash on malformed input; SSE must never block.
- Build target `player`; build/verify ONLY on the Pi (`root@10.10.0.204:/opt/linamp2`, `make -j4`) + browser. No local compiler / test harness. Develop on branch `feature/web-ui-phase2`; integration via the finishing skill after review. Controller owns device builds/restarts (`/root/restart_player.sh`).

---

### Task 1: Spectrum backend (compute + SSE event + client gating)

**Files:**
- Modify: `src/api/webstatehub.h`
- Modify: `src/api/webstatehub.cpp`
- Modify: `src/api/ssebroker.h`
- Modify: `src/api/ssebroker.cpp`

**Interfaces:**
- Consumes: `AudioSource::dataEmitted(const QByteArray&, QAudioFormat)` (already connected in `WebStateHub::onFormat`); `fft.h` `calc_freq(const float[512], float[256])`.
- Produces: `WebStateHub` signal `void spectrum(const QVector<int> &bars)` (19 ints 0..40) and slot `void setSpectrumActive(bool)`. `SseBroker` broadcasts a `spectrum` event and toggles `WebStateHub::setSpectrumActive` on first/last client.

- [ ] **Step 1: WebStateHub header additions**

In `src/api/webstatehub.h`: add `#include <QVector>` and `#include <QElapsedTimer>` near the other includes. Add to the class:
- In `public slots:` add `void setSpectrumActive(bool active);`
- In `signals:` add `void spectrum(const QVector<int> &bars);`
- In `private:` add:
```cpp
    // Spectrum (computed from PCM, gated on SSE clients)
    bool m_spectrumActive = false;
    float m_xscale[20] = {0};
    QElapsedTimer m_spectrumClock;
    void computeSpectrum(const QByteArray &data, const QAudioFormat &format);
```

- [ ] **Step 2: WebStateHub spectrum implementation**

In `src/api/webstatehub.cpp`, add includes at the top (after the existing includes):
```cpp
#include "fft.h"
#include <algorithm>
#include <cmath>
#include <cstring>
```
Add these file-static DSP helpers (adapted from `spectrumwidget.cpp`) above the `WebStateHub` constructor:
```cpp
static float pcmToFloatSample(qint16 pcm) { return float(pcm) / 32768.0f; }

static void computeLogXscaleBands(float *xscale, int bands)
{
    for (int i = 0; i <= bands; i++)
        xscale[i] = powf(256, (float)i / bands) - 0.5f;
}

static float computeOneBand(const float *freq, const float *xscale, int band, int bands)
{
    int a = ceilf(xscale[band]);
    int b = floorf(xscale[band + 1]);
    float n = 0;
    if (b < a) {
        n += freq[b] * (xscale[band + 1] - xscale[band]);
    } else {
        if (a > 0) n += freq[a - 1] * (a - xscale[band]);
        for (; a < b; a++) n += freq[a];
        if (b < 256) n += freq[b] * (xscale[band + 1] - b);
    }
    n *= (float)bands / 12;
    return 20 * log10f(n);
}
```
In the constructor, initialise the scale and clock (add after the existing initial-value reads):
```cpp
    computeLogXscaleBands(m_xscale, 19);
    m_spectrumClock.start();
```
Implement `setSpectrumActive` and `computeSpectrum`, and call the latter from `onFormat`. Replace the existing `onFormat` body with:
```cpp
void WebStateHub::onFormat(const QByteArray &data, QAudioFormat format)
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

    if (m_spectrumActive && m_spectrumClock.elapsed() >= 33) {
        m_spectrumClock.restart();
        computeSpectrum(data, format);
    }
}

void WebStateHub::setSpectrumActive(bool active)
{
    m_spectrumActive = active;
}

void WebStateHub::computeSpectrum(const QByteArray &data, const QAudioFormat &format)
{
    const int channels = format.channelCount();
    if (channels < 1 || format.sampleFormat() != QAudioFormat::Int16)
        return;
    const int bytesPerFrame = 2 * channels;
    if (data.size() < N * bytesPerFrame)   // N = 512 from fft.h
        return;

    float mono[N];
    const char *ptr = data.constData();
    for (int i = 0; i < N; ++i) {
        if (channels == 1) {
            mono[i] = pcmToFloatSample(*reinterpret_cast<const qint16 *>(ptr));
        } else {
            const qint16 l = *reinterpret_cast<const qint16 *>(ptr);
            const qint16 r = *reinterpret_cast<const qint16 *>(ptr + 2);
            mono[i] = (pcmToFloatSample(l) + pcmToFloatSample(r)) / 2.0f;
        }
        ptr += bytesPerFrame;
    }

    float freq[N / 2];
    calc_freq(mono, freq);

    QVector<int> bars(19);
    for (int i = 0; i < 19; ++i) {
        int x = 40 + static_cast<int>(computeOneBand(freq, m_xscale, i, 19));
        bars[i] = std::clamp(x, 0, 40);
    }
    emit spectrum(bars);
}
```

- [ ] **Step 3: SseBroker spectrum broadcast + gating**

In `src/api/ssebroker.h`: add a private slot `void onSpectrum(const QVector<int> &bars);` and `#include <QVector>`.

In `src/api/ssebroker.cpp`: in the constructor, connect the new signal:
```cpp
    connect(hub, &WebStateHub::spectrum, this, &SseBroker::onSpectrum);
```
In `addClient`, after `m_clients.insert(socket);`, activate spectrum when the first client connects:
```cpp
    if (m_clients.size() == 1) m_hub->setSpectrumActive(true);
```
In `removeClient`, deactivate when the last client leaves:
```cpp
void SseBroker::removeClient(QTcpSocket *socket)
{
    m_clients.remove(socket);
    if (m_clients.isEmpty()) m_hub->setSpectrumActive(false);
}
```
Add the `onSpectrum` slot:
```cpp
void SseBroker::onSpectrum(const QVector<int> &bars)
{
    QByteArray data = "[";
    for (int i = 0; i < bars.size(); ++i) {
        if (i) data += ',';
        data += QByteArray::number(bars[i]);
    }
    data += "]";
    broadcast("spectrum", data);
}
```

- [ ] **Step 4: Build on the box**

Run (on the box): `cd /opt/linamp2 && make -j4`
Expected: `Built target player` (rc 0).

- [ ] **Step 5: Restart and verify the spectrum event**

Restart the player. With a track playing, from the box:
Run: `timeout 4 curl -s -N localhost:8080/api/events | grep -c "event: spectrum"`
Expected: a non-zero count (multiple spectrum events per second) while audio plays; `0` when stopped. Confirm `event: status` and `event: position` still appear.

- [ ] **Step 6: Commit**

```bash
git add src/api/webstatehub.h src/api/webstatehub.cpp src/api/ssebroker.h src/api/ssebroker.cpp
git commit -m "Web UI: compute + stream spectrum over SSE (client-gated)"
```

---

### Task 2: Full player frontend

Rebuild the embedded page into the hybrid retro-modern player. Replaces the Phase 1 minimal page.

**Files:**
- Modify: `webui/index.html`
- Modify: `webui/app.css`
- Modify: `webui/app.js`
- Create: `webui/api.js`
- Modify: `webui.qrc`

**Interfaces:**
- Consumes: SSE events `status` (18-key snapshot), `position` (`{positionMs}`), `spectrum` (`[19 ints 0..40]`); control endpoints listed in Global Constraints.
- Produces: a working player UI.

- [ ] **Step 1: Create `webui/api.js`**

```javascript
// Thin control-call wrapper. All controls are GET; token appended if present.
export function apiToken() {
  return new URLSearchParams(location.search).get("token")
      || localStorage.getItem("linamp_token") || "";
}

export function call(path) {
  const t = apiToken();
  const url = path + (path.includes("?") ? "&" : "?") + (t ? "token=" + encodeURIComponent(t) : "_=1");
  return fetch(url, { method: "GET" }).catch(() => {});
}

export function eventsUrl() {
  const t = apiToken();
  return "/api/events" + (t ? "?token=" + encodeURIComponent(t) : "");
}
```

- [ ] **Step 2: Create `webui/index.html`**

```html
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1">
<title>Linamp Remote</title>
<link rel="stylesheet" href="/app.css">
</head>
<body>
  <main class="wrap">
    <header>
      <h1>LINAMP <span class="dim">REMOTE</span></h1>
      <div id="conn" class="conn off">connecting…</div>
    </header>

    <section class="screen">
      <canvas id="spectrum" class="spectrum" width="234" height="80"></canvas>
      <div class="screen-info">
        <div id="track" class="track">—</div>
        <div class="meta">
          <span id="bitrate">—</span> kbps · <span id="srate">—</span> kHz ·
          <span id="chan">—</span>
        </div>
        <div class="flags">
          <span id="f-eq" class="flag">EQ</span>
          <span id="f-pl" class="flag">PL</span>
          <span id="f-shuffle" class="flag">SHUF</span>
          <span id="f-repeat" class="flag">REP</span>
        </div>
      </div>
    </section>

    <section class="seek">
      <input id="seek" type="range" min="0" max="1000" value="0">
      <div class="time"><span id="pos">0:00</span><span id="dur">0:00</span></div>
    </section>

    <section class="transport">
      <button id="prev"  class="tbtn" title="Previous">⏮</button>
      <button id="play"  class="tbtn" title="Play">⏵</button>
      <button id="pause" class="tbtn" title="Pause">⏸</button>
      <button id="stop"  class="tbtn" title="Stop">⏹</button>
      <button id="next"  class="tbtn" title="Next">⏭</button>
      <button id="shuffle" class="tbtn tog" title="Shuffle">🔀</button>
      <button id="repeat"  class="tbtn tog" title="Repeat">🔁</button>
    </section>

    <section class="rows">
      <div class="row"><span class="lbl">vol</span>
        <input id="vol" type="range" min="0" max="100" value="0"><span id="volv" class="val">0</span></div>
      <div class="row"><span class="lbl">bal</span>
        <input id="bal" type="range" min="-100" max="100" value="0"><span id="balv" class="val">C</span></div>
      <div class="row"><span class="lbl">src</span><span id="source" class="val">—</span>
        <span class="lbl">state</span><span id="state" class="val">stopped</span></div>
    </section>
  </main>
  <script type="module" src="/app.js"></script>
</body>
</html>
```

- [ ] **Step 3: Create `webui/app.css`**

```css
:root{ --bg:#0b0d12; --panel:#11151c; --ink:#e7eaf0; --dim:#7c8597;
  --lcd:#06120d; --green:#33ff88; --amber:#ffcc33; --line:#1c2330; --accent:#2ee6a0; }
*{ box-sizing:border-box; -webkit-tap-highlight-color:transparent; }
body{ margin:0; background:var(--bg); color:var(--ink);
  font-family:"Segoe UI",system-ui,sans-serif; }
.wrap{ max-width:560px; margin:0 auto; padding:16px 14px 40px; }
header{ display:flex; align-items:center; justify-content:space-between; }
h1{ font-size:18px; letter-spacing:.12em; margin:.2em 0; }
.dim{ color:var(--dim); }
.conn{ font-size:11px; padding:3px 9px; border-radius:999px; border:1px solid var(--line); color:var(--dim); }
.conn.on{ color:var(--green); border-color:#1f4; }

.screen{ display:flex; gap:12px; margin:12px 0; padding:12px; background:var(--lcd);
  border:1px solid var(--line); border-radius:10px;
  font-family:"DejaVu Sans Mono",ui-monospace,monospace; color:var(--green); }
.spectrum{ width:160px; height:80px; image-rendering:pixelated; flex:0 0 auto;
  background:#020805; border:1px solid #15241b; border-radius:4px; }
.screen-info{ flex:1; min-width:0; }
.track{ font-weight:700; white-space:nowrap; overflow:hidden; text-overflow:ellipsis;
  text-shadow:0 0 8px rgba(51,255,136,.4); }
.meta{ font-size:12px; color:#6fd6a0; margin-top:6px; }
.flags{ display:flex; gap:8px; margin-top:10px; }
.flag{ font-size:11px; letter-spacing:.08em; color:#2a5a44; border:1px solid #163a2b;
  padding:2px 6px; border-radius:4px; }
.flag.on{ color:var(--amber); border-color:#5a4a14; text-shadow:0 0 6px rgba(255,204,51,.4); }

.seek{ margin-top:6px; }
.seek input{ width:100%; }
.time{ display:flex; justify-content:space-between; font-size:12px; color:var(--dim);
  font-family:"DejaVu Sans Mono",monospace; margin-top:2px; }

.transport{ display:flex; gap:8px; margin-top:14px; flex-wrap:wrap; }
.tbtn{ flex:1; min-width:48px; height:52px; font-size:20px; color:var(--ink);
  background:var(--panel); border:1px solid var(--line); border-radius:8px; cursor:pointer; }
.tbtn:active{ transform:translateY(1px); }
.tbtn.on{ color:var(--accent); border-color:var(--accent);
  box-shadow:0 0 0 1px var(--accent) inset; }

.rows{ display:flex; flex-direction:column; gap:10px; margin-top:14px; }
.row{ display:flex; align-items:center; gap:10px; padding:10px 12px;
  background:var(--panel); border:1px solid var(--line); border-radius:8px; }
.lbl{ color:var(--dim); font-size:12px; text-transform:uppercase; letter-spacing:.1em; }
.row input[type=range]{ flex:1; }
.val{ font-family:"DejaVu Sans Mono",monospace; font-size:13px; min-width:34px; text-align:right; }
input[type=range]{ accent-color:var(--green); height:22px; }
@media (max-width:380px){ .spectrum{ width:120px; } .tbtn{ font-size:18px; height:48px; } }
```

- [ ] **Step 4: Create `webui/app.js`**

```javascript
import { call, eventsUrl } from "/api.js";

const $ = (id) => document.getElementById(id);
const fmt = (ms) => {
  if (!ms || ms < 0) ms = 0;
  const s = Math.floor(ms / 1000);
  return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0");
};

let durationMs = 0;
let seeking = false;     // user dragging the seek bar
let volTouching = false; // user dragging volume
let balTouching = false; // user dragging balance

// ---- spectrum canvas ----
const canvas = $("spectrum");
const ctx = canvas.getContext("2d");
let bars = new Array(19).fill(0);
let smooth = new Array(19).fill(0);
function drawSpectrum() {
  const w = canvas.width, h = canvas.height, n = bars.length;
  ctx.clearRect(0, 0, w, h);
  const bw = w / n;
  for (let i = 0; i < n; i++) {
    smooth[i] = Math.max(bars[i], smooth[i] - 2); // falloff
    const bh = (smooth[i] / 40) * h;
    const hue = 120 - (smooth[i] / 40) * 120;     // green→red
    ctx.fillStyle = `hsl(${hue} 90% 50%)`;
    ctx.fillRect(i * bw + 1, h - bh, bw - 1, bh);
  }
  requestAnimationFrame(drawSpectrum);
}
requestAnimationFrame(drawSpectrum);

// ---- status rendering ----
function setFlag(id, on) { $(id).classList.toggle("on", !!on); }
function applyStatus(s) {
  const t = [s.artist, s.album, s.title].filter(Boolean).join(" — ");
  $("track").textContent = t || "—";
  $("bitrate").textContent = s.bitrate || "—";
  $("srate").textContent = s.sampleRateHz ? Math.round(s.sampleRateHz / 1000) : "—";
  $("chan").textContent = s.channels === 1 ? "mono" : (s.channels === 2 ? "stereo" : "—");
  setFlag("f-eq", s.eq); setFlag("f-pl", s.pl);
  setFlag("f-shuffle", s.shuffle); setFlag("f-repeat", s.repeat);
  $("shuffle").classList.toggle("on", !!s.shuffle);
  $("repeat").classList.toggle("on", !!s.repeat);
  durationMs = s.durationMs || 0;
  $("dur").textContent = fmt(durationMs);
  if (!seeking) {
    $("pos").textContent = fmt(s.positionMs || 0);
    $("seek").value = durationMs ? Math.round((s.positionMs || 0) / durationMs * 1000) : 0;
  }
  if (!volTouching) { $("vol").value = s.volume || 0; $("volv").textContent = s.volume || 0; }
  if (!balTouching) {
    $("bal").value = s.balance || 0;
    $("balv").textContent = s.balance === 0 ? "C" : (s.balance < 0 ? Math.abs(s.balance) + "L" : s.balance + "R");
  }
  $("source").textContent = s.source || "—";
  $("state").textContent = s.state || "stopped";
}
function applyPosition(ms) {
  if (seeking) return;
  $("pos").textContent = fmt(ms);
  $("seek").value = durationMs ? Math.round(ms / durationMs * 1000) : 0;
}

// ---- controls ----
$("prev").onclick  = () => call("/api/previous");
$("play").onclick  = () => call("/api/play");
$("pause").onclick = () => call("/api/pause");
$("stop").onclick  = () => call("/api/stop");
$("next").onclick  = () => call("/api/next");
$("shuffle").onclick = () => call("/api/shuffle");
$("repeat").onclick  = () => call("/api/repeat");

const seek = $("seek");
seek.addEventListener("input", () => { seeking = true;
  if (durationMs) $("pos").textContent = fmt(seek.value / 1000 * durationMs); });
seek.addEventListener("change", () => {
  if (durationMs) call("/api/seek?ms=" + Math.round(seek.value / 1000 * durationMs));
  seeking = false;
});

let volTimer = null;
const vol = $("vol");
vol.addEventListener("input", () => { volTouching = true; $("volv").textContent = vol.value;
  clearTimeout(volTimer); volTimer = setTimeout(() => call("/api/volume?level=" + vol.value), 80); });
vol.addEventListener("change", () => { call("/api/volume?level=" + vol.value);
  setTimeout(() => volTouching = false, 300); });

let balTimer = null;
const bal = $("bal");
bal.addEventListener("input", () => { balTouching = true;
  const v = parseInt(bal.value, 10);
  $("balv").textContent = v === 0 ? "C" : (v < 0 ? Math.abs(v) + "L" : v + "R");
  clearTimeout(balTimer); balTimer = setTimeout(() => call("/api/balance?value=" + v), 80); });
bal.addEventListener("change", () => { call("/api/balance?value=" + bal.value);
  setTimeout(() => balTouching = false, 300); });

// ---- SSE ----
function connect() {
  const es = new EventSource(eventsUrl());
  es.addEventListener("open",  () => { $("conn").textContent = "connected"; $("conn").className = "conn on"; });
  es.addEventListener("error", () => { $("conn").textContent = "reconnecting…"; $("conn").className = "conn off"; });
  es.addEventListener("status",   (e) => applyStatus(JSON.parse(e.data)));
  es.addEventListener("position", (e) => applyPosition(JSON.parse(e.data).positionMs));
  es.addEventListener("spectrum", (e) => { bars = JSON.parse(e.data); });
}
connect();
```

- [ ] **Step 5: Ensure `webui.qrc` includes `api.js`**

In `webui.qrc`, add the new file inside the `<qresource prefix="/">` block:
```xml
        <file>webui/api.js</file>
```

- [ ] **Step 6: Serve `api.js` from ApiServer**

`api.js` is loaded as an ES module (`import`), so it must be served. In `src/api/apiserver.cpp`, add an entry to the `handleStatic` asset table:
```cpp
        { "/api.js",     ":/webui/api.js",     "application/javascript; charset=utf-8" },
```

- [ ] **Step 7: Build on the box**

Run (on the box): `cd /opt/linamp2 && make -j4`
Expected: `Built target player` (rc 0).

- [ ] **Step 8: Verify in a browser (Phase 2 gate)**

Restart the player. Open `http://10.10.0.204:8080/` on a phone or desktop. With a track playing:
- The spectrum canvas animates (green→red bars) in sync with the audio.
- Track/bitrate/kHz/channels show; the seek bar and position advance; duration shows.
- Tapping play/pause/stop/next/prev controls the device (audible).
- Dragging volume/balance moves the device's level; shuffle/repeat buttons toggle and reflect state (the EQ/PL/SHUF/REP flags light per device state).
Confirm `curl -s localhost:8080/api.js` returns the JS (200) and `/api/status` still works.

- [ ] **Step 9: Commit**

```bash
git add webui/index.html webui/app.css webui/app.js webui/api.js webui.qrc src/api/apiserver.cpp
git commit -m "Web UI Phase 2: full player UI with live spectrum and controls"
```

---

## Self-Review

**Spec coverage (Phase 2):** full player surface (transport, sliders, shuffle/repeat toggles, seek, EQ/PL state) → Task 2; live `<canvas>` spectrum from the `spectrum` SSE event → Task 1 (backend) + Task 2 (render); reuse existing control endpoints, EQ display-only → Task 2. ✓

**Placeholder scan:** none. The deferral of `pl`/`eject` control routes (display-only instead) is a deliberate, documented scope decision (eject is ambiguous remotely; transport coverage is complete via existing endpoints).

**Type consistency:** `WebStateHub::spectrum(const QVector<int>&)`, `setSpectrumActive(bool)`, `computeSpectrum(const QByteArray&, const QAudioFormat&)`, `m_xscale[20]`, `m_spectrumClock`; `SseBroker::onSpectrum(const QVector<int>&)` + first/last-client gating; `handleStatic` table gains `/api.js`. Frontend event names (`status`/`position`/`spectrum`) and control paths match the backend. ✓
