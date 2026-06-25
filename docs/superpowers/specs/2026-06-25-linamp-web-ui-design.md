# Linamp Web Interface — Design

**Date:** 2026-06-25
**Status:** Approved (brainstorming) — architecture spec; Phase 1 goes to implementation planning first.

## Goal

A full remote web interface, served by the device itself, that mirrors the
on-device Linamp UI and can control everything over the LAN from a phone or
desktop browser — plus a few web-only extras (notably a clock-face picker).

The on-device UI today spans four surfaces this web UI mirrors:
- **Player** — now-playing (artist/title), bitrate, sample rate, mono/stereo,
  real-time spectrum analyzer, transport (prev/play/pause/stop/next/eject),
  EQ/PL toggles, shuffle/repeat, volume + balance, seek bar.
- **Playlist** — the current queue + a filesystem browser to add tracks.
- **Menu** — audio source selection (File/CD/Bluetooth/Spotify) + VBAN toggle.
- **Screensaver/clocks** — trigger screensaver; (web extra) pick a clock face.

The existing HTTP API (`src/api/ApiServer`) is **control-only** (GET commands,
no status). This feature adds the missing **status + live-update** layer and
the **frontend**, reusing the existing API for commands.

## Decisions (from brainstorming)

| Topic | Decision | Rationale |
|---|---|---|
| Scope | Full mirror: player + live spectrum + playlist/file-browser + sources/VBAN + screensaver/clock picker | User selected all surfaces |
| Visual style | Hybrid retro-modern, responsive/touch-first | Controlled mostly from a phone; keep Winamp identity, stay usable |
| Build/serve | Vanilla HTML/CSS/JS embedded via a Qt resource (`webui.qrc`), served by `ApiServer` | No build toolchain; self-contained in the binary/.deb; matches existing asset embedding (`uiassets.qrc`) |
| Live updates | Server-Sent Events (SSE) on the existing `QTcpServer` | One-way push fits (controls are GET); no new dependency; browser auto-reconnect; spectrum is small enough for JSON |
| Controls | Reuse/extend the existing GET API | Already built and verified |
| Delivery | One architecture spec; a separate implementation plan per phase | Too large for a single plan |

**Explicitly NOT chosen:** WebSocket (would add `qt6-websockets` dep), polling
(can't carry a smooth ~30fps spectrum), a JS framework (adds a Node build step).

## Architecture

```
 AudioSource / Coordinator / SystemAudio / SpectrumWidget  (existing signals)
        │  playbackState, metadata, position, duration, volume, balance,
        │  eq/pl/shuffle/repeat, spectrum bars, source, view
        ▼
   WebStateHub  ──snapshot(JSON)──┐
   (holds current state,          │
    emits changed/position/spectrum)
        │                          │
        ▼                          ▼
   SseBroker  ───SSE events───►  Browser (EventSource)  ──renders──► DOM
   (fan-out to open clients)        │
        ▲                           │ control: GET /api/...
   ApiServer ◄────────────────────-─┘
   (routes /api/*, serves webui.qrc, hands /api/events to SseBroker)
```

All handlers run on the Qt GUI event loop (single-threaded); no locks.

### New backend components

- **`WebStateHub`** (`src/api/webstatehub.{h,cpp}`, `QObject`): the single
  source of truth for web state. Connects to the same signals `PlayerView`
  consumes (rebinding the active source's signals on source switch, the way
  `MainWindow` already wires them). Keeps a struct of current values, exposes
  `QJsonObject snapshot()`, and emits:
  - `stateChanged()` — any of: playback state, metadata, duration, volume,
    balance, eq/pl/shuffle/repeat, source, view.
  - `positionChanged(qint64 ms)` — playback position (≈1 Hz).
  - `spectrum(QVector<float> bars)` — throttled to ~20–30 fps.
- **`SseBroker`** (`src/api/ssebroker.{h,cpp}`, `QObject`): owns the list of
  open SSE `QTcpSocket*`s. API hands off a `/api/events` socket via
  `addClient(QTcpSocket*)` (broker writes the SSE response headers and an
  initial `status` snapshot). Subscribes to `WebStateHub` and broadcasts
  `status`/`position`/`spectrum` events. Removes clients on disconnect; sends a
  periodic `: heartbeat` comment to keep connections alive. Never blocks.
- **`ApiServer` extensions** (`src/api/apiserver.{h,cpp}`):
  - Static serving: `GET /` → embedded `index.html`; `GET /app.css`, `/app.js`,
    `/<module>.js` → assets from `:/webui/...` (new `webui.qrc`). Correct
    `Content-Type`; `404` for unknown asset paths.
  - `GET /api/status` → `WebStateHub::snapshot()` as JSON.
  - `GET /api/events` → keep socket open, register with `SseBroker` (do NOT
    close it like normal requests). Counts against a max-clients cap.
  - New control routes (per phase, below).
- **Spectrum tap**: add a signal to `SpectrumWidget`
  (`spectrumBars(const QVector<float>&)`) emitted each frame with the already-
  computed bar magnitudes (the FFT is already done there); `WebStateHub`
  connects to it and throttles. Avoids recomputing FFT or shipping PCM.

### Frontend (vanilla, embedded)

Files under `webui/` (compiled into `webui.qrc`):
- `index.html` — shell + tab bar (Player · Playlist · Sources · Clocks).
- `app.css` — hybrid retro-modern theme (dark base, LCD-green readouts, retro
  accents), responsive (phone-first single column; wider multi-pane ≥ tablet).
- `sse.js` — opens `EventSource('/api/events'[?token])`, dispatches events,
  shows a connection indicator, relies on built-in auto-reconnect.
- `api.js` — thin wrapper over the control GET calls (adds token if set).
- `player.js`, `playlist.js`, `sources.js`, `clocks.js` — per-surface render +
  control logic. `player.js` includes a `<canvas>` spectrum renderer driven by
  the `spectrum` SSE event.

### SSE event model

`GET /api/events` streams `text/event-stream`:
```
event: status
data: {"state":"playing","artist":"...","title":"...","album":"...",
       "bitrate":265,"sampleRateHz":44100,"channels":2,"durationMs":203000,
       "positionMs":84000,"volume":50,"balance":0,
       "eq":false,"pl":true,"shuffle":true,"repeat":false,
       "source":"file","view":"player"}

event: position
data: {"positionMs":85000}

event: spectrum
data: [12,40,33,...]      // 0..255 bar magnitudes, ~20–30/sec
```
Initial `status` is sent on connect. `position` fires ~1 Hz; full `status` on
any non-position change; `spectrum` only while playing.

## Security

- **Auth:** reuse `api/token`. If non-empty, the page prompts once and includes
  it as `?token=` on the EventSource URL and on every control call (the API
  already accepts `?token=` or `Authorization: Bearer`). Default empty = open
  on LAN, unchanged from today.
- **File-browser sandbox (hard requirement):** remote filesystem browsing is
  restricted to a configurable root `api/musicRoot` (default: the user's
  `~/Music`, falling back to `~`). Every `/api/browse` and `/api/add` path is
  canonicalized (`QDir::canonicalPath`) and **rejected unless it stays under
  the canonical root** — no `..` escapes, no following symlinks outside the
  root, no absolute paths outside it. Listings expose only name/type/size, not
  full system paths beyond the root-relative path.
- **Carry-over notes (not expanded here):** `Access-Control-Allow-Origin: *`
  means same-LAN pages can *trigger* (not read) commands without a token; token
  compare is not constant-time. Acceptable for a LAN device; documented.

## Configuration (`[api]` group, additive)

| Key | Default | Meaning |
|---|---|---|
| `musicRoot` | `~/Music` (else `~`) | Sandbox root for the web file browser |
| `maxSseClients` | `8` | Cap on simultaneous SSE connections |

(`enabled`, `port`, `bindAddress`, `token` already exist.)

## Phasing (each phase = its own implementation plan, shippable on its own)

**Phase 1 — Foundation.** `webui.qrc` + static serving from `ApiServer`;
`WebStateHub` (state aggregation + snapshot); `SseBroker`; `GET /api/status`
and `GET /api/events`; a minimal page that connects via SSE and displays live
now-playing + position + state. *Gate: open the page in a browser, see state
update live as the device plays.*

**Phase 2 — Player UI.** Full hybrid player surface: transport buttons, volume
+ balance sliders, shuffle/repeat + PL toggles (reflecting live state), seek
bar bound to `position`/`duration`, and a `<canvas>` spectrum driven by the
`spectrum` event. Adds the `pl` and `eject` (open) control routes. **EQ:** the
`AudioSource` interface exposes `handlePl` but no EQ handler, so EQ is treated
as **display-only** (the `eq` flag is shown but not toggleable) unless a device
EQ action is found during implementation — confirm before wiring a control.

**Phase 3 — Playlist + file browser.** `GET /api/playlist`,
`/api/playlist/play?index=N`, `remove?index=N`, `clear`; sandboxed
`GET /api/browse?path=…` and `add?path=…`. Playlist + Files tabs in the UI.

**Phase 4 — Sources/VBAN + Clocks.** `GET /api/source?name=…`,
`GET /api/vban?on=1`; Sources tab. Clocks tab: gallery of the 15 faces (from
`/api/clock/list`) that push via the existing `/api/clock` endpoint, plus
screensaver on/off.

## Testing

No automated harness (consistent with the rest of the project). Each phase is
build-verified on the Raspberry Pi (`/opt/linamp2`, `make`) and exercised in a
real browser: load the served page, confirm SSE events update the DOM, and
confirm controls actually move the device (audible/visible). Phase gates are
the "Gate" lines above.

## Out of scope (future)

- WebSocket upgrade (only if bidirectional/binary ever needed).
- HTTPS (LAN-only; reverse-proxy if needed).
- Album-art delivery (the device UI doesn't show art; revisit if desired).
- Multi-user accounts / per-client state.
- Editing EQ bands (the device EQ is a toggle today).
