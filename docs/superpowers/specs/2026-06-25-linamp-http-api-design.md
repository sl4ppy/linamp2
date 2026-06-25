# Linamp HTTP Control API — Design

**Date:** 2026-06-25
**Status:** Approved (brainstorming) — ready for implementation planning

## Goal

Add a lightweight HTTP REST-style API to the Linamp player so it can be
controlled over the LAN with simple, browser-clickable `GET` requests from
automations (Home Assistant, scripts, phone shortcuts, browser bookmarks).

Scope is deliberately limited to two capability groups:

1. **Transport + audio** — play, pause, play/pause toggle, stop, next,
   previous, seek, shuffle, repeat, volume, balance.
2. **Screensaver + clocks** — trigger/dismiss the screensaver and select a
   specific clock face.

Explicitly **out of scope** (can be added later): source switching, view
navigation, and status read-back (now-playing JSON). A trivial `clock/list`
and a `health` endpoint are included for discoverability/liveness only.

## Decisions

| Decision | Choice | Rationale |
|---|---|---|
| Consumer | Home-LAN automation | Drives binding + auth posture |
| Binding | `0.0.0.0` (LAN), optional token | Trusted home network |
| API style | `GET` with query params (browser-clickable) | Easiest for curl/HA/shortcuts/bookmarks |
| Server tech | `QTcpServer` (hand-rolled HTTP) | Zero new deps; `Network` already linked; Qt 6.4 on Bookworm has `QHttpServer` only as Technology Preview, a deploy risk; self-contained `.deb` |
| Threading | GUI event loop | Handlers call player slots directly — no marshaling |
| Testing | curl smoke-test + docs | No existing test harness; parser kept testable as a free function |

## Architecture

A new self-contained module `src/api/` with one class, **`ApiServer`**
(`QObject` owning a `QTcpServer`).

- **Lifecycle & ownership:** constructed by `MainWindow` after the coordinator
  and views exist; owned by `MainWindow`. On construction it reads the `[api]`
  config and, if enabled, calls `listen(bindAddress, port)`. Bind failure
  (e.g. port in use) is logged to stderr and the app continues normally — the
  API is best-effort, never fatal.
- **Threading:** `QTcpServer` runs on the GUI event loop, so request handlers
  call player slots directly. No threads, no locks.
- **Collaborators (constructor-injected; no new globals):**
  - `AudioSourceCoordinator*` — transport + audio
  - `MainWindow*` — screensaver/clock control and the `currentPlaybackState()`
    getter used by the play/pause toggle
- **Request flow:** `newConnection` → read until end-of-headers (`\r\n\r\n`;
  GET commands carry no body) → `parseRequest(QByteArray)` produces an
  `HttpRequest` struct (method, path, query map, headers) → optional token
  check → route lookup in a command table → execute → write JSON response with
  `Connection: close`, then close the socket.
- **Testability:** `parseRequest()` is a free function with no Qt-GUI
  dependencies so it can be unit-tested in isolation later.

### Files

```
src/api/
  apiserver.h     # ApiServer class, HttpRequest struct, parseRequest() decl
  apiserver.cpp   # server, routing table, handlers, response writing
```

Both added to the `player` target in `CMakeLists.txt`. No new `find_package`.

## Endpoints

All respond with `Content-Type: application/json`. Success → `{"ok":true}`
(plus payload where noted); failure → `{"ok":false,"error":"..."}`. All accept
`GET`; `POST` is accepted on the same paths.

### Transport + audio

| Endpoint | Action |
|---|---|
| `/api/play` | play active source (`AudioSource::handlePlay`) |
| `/api/pause` | pause (`handlePause`) |
| `/api/playpause` | toggle using internally-cached playback state |
| `/api/stop` | stop (`handleStop`) |
| `/api/next` | next track (`handleNext`) |
| `/api/previous` (alias `/api/prev`) | previous (`handlePrevious`) |
| `/api/seek?ms=N` | seek to N ms (`handleSeek`) |
| `/api/shuffle` | toggle shuffle (`handleShuffle`) |
| `/api/repeat` | toggle repeat (`handleRepeat`) |
| `/api/volume?level=0..100` | set volume (`coordinator.setVolume`) |
| `/api/balance?value=-100..100` | set balance, 0 = center (`coordinator.setBalance`) |

### Screensaver + clocks

| Endpoint | Action |
|---|---|
| `/api/screensaver/on` | activate screensaver (random face) |
| `/api/screensaver/off` | dismiss screensaver |
| `/api/clock?face=NAME` | activate screensaver showing a specific face (case-insensitive) |
| `/api/clock?index=N` | same, by theme index |
| `/api/clock/list` | `{"ok":true,"faces":[...]}` — static list from `getAllClockThemes()` |

### Meta

| Endpoint | Action |
|---|---|
| `/api/health` (and `/`) | `{"ok":true,"service":"linamp"}` liveness check |

## Configuration & security

New `[api]` group in `~/.config/Rod/Linamp.conf` (read via `QSettings`,
consistent with the existing `[vban]` group):

| Key | Default | Meaning |
|---|---|---|
| `enabled` | `true` | Start the server on launch |
| `port` | `8080` | TCP port |
| `bindAddress` | `0.0.0.0` | Listen on all interfaces (LAN) |
| `token` | `""` | If non-empty, every request must present it via `?token=…` or `Authorization: Bearer …`; empty = no auth |

Default is open on the LAN with no token, matching the browser-clickable goal.

## Code touchpoints (small, additive)

- **`AudioSourceCoordinator`** — add public slots forwarding to the *active*
  source: `play/pause/stop/next/previous/seek(int ms)/shuffle/repeat`. Reuses
  the coordinator's existing current-source tracking; the API never touches
  `MainWindow` wiring. `setVolume`/`setBalance` already exist.
- **`ScreenSaverView`** — add `void start(int themeIndex)` (existing `start()`
  remains the random path), a `static QStringList faceNames()` over
  `getAllClockThemes()`, and a case-insensitive name→index lookup.
- **`MainWindow`** — add thin public methods the API calls:
  `apiTriggerScreensaver()`, `apiDismissScreensaver()`,
  `apiShowClockFace(int index)` (wrapping the existing private
  `activateScreenSaver`/`deactivateScreenSaver`), and a
  `currentPlaybackState()` getter.
- **`ApiServer`** — owns the `QTcpServer`, routing table, handlers, response
  writer, and token check. For `/api/playpause` it reads
  `MainWindow::currentPlaybackState()` (Playing → pause, otherwise → play).
- **`CMakeLists.txt`** — add `src/api/apiserver.cpp` (+ header) to `player`.

## Error handling

| Condition | Response |
|---|---|
| Unknown path | `404` `{"ok":false,"error":"unknown endpoint"}` |
| Bad/out-of-range param (volume, seek, unknown face) | `400` with a specific message |
| Missing/invalid token (when configured) | `401` |
| Method other than GET/POST | `405` |
| Bind failure at startup | logged to stderr; app runs without the API |

Handlers are wrapped so a malformed request can never crash the player; every
socket gets a response and is closed.

## Testing

- `parseRequest()` is written as a pure, GUI-free function so a unit test can
  be added later; for now it is verifiable by inspection.
- Primary verification: build on the Pi, then run a curl smoke-test script
  (`docs/api-examples.sh`) hitting every endpoint and confirm real player
  behavior (e.g. `/api/clock?face=Nixie` actually shows the Nixie face).
- Docs: new `docs/API.md` with the endpoint reference and copy-paste
  `curl`/browser examples.

## Out of scope (future)

- Source switching (`/api/source?name=…`) and view navigation.
- Status read-back (`GET /api/status` with now-playing metadata, position,
  volume, source, view).
- WebSocket/event stream for push updates.
- HTTPS (LAN-only for now; put behind a reverse proxy if needed).
