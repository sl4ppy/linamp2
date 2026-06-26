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
| `musicRoot` | `~/Music` (else `~`) | Sandbox root for the file browser (`/api/browse`, `/api/add`) |
| `maxSseClients` | `8` | Max simultaneous `/api/events` (SSE) connections |

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

### Status + live updates
| Method | Path | Notes |
|---|---|---|
| GET | `/api/status` | full now-playing snapshot (JSON, 18 keys: state, artist, title, album, bitrate, codec, sampleRateHz, channels, durationMs, positionMs, volume, balance, eq, pl, shuffle, repeat, source, view) |
| GET | `/api/events` | Server-Sent Events stream. Events: `status` (full snapshot on connect + on change), `position` (`{positionMs}`, ~1 Hz). Use `EventSource` in the browser. |

### Playlist + file browser
| Method | Path | Notes |
|---|---|---|
| GET | `/api/playlist` | `{ok,current,items:[{index,title,artist,duration,current}]}` |
| GET | `/api/playlist/play?index=N` | play queue item N |
| GET | `/api/playlist/remove?index=N` | remove item N |
| GET | `/api/playlist/clear` | empty the queue |
| GET | `/api/browse?path=REL` | list a directory under `musicRoot` (sandboxed; audio files + folders). `{ok,path,entries:[{name,type,path,size?}]}` |
| GET | `/api/add?path=REL` | add an audio file (or all audio in a folder) to the queue. `{ok,added}` |

Paths for `/api/browse` and `/api/add` are relative to `musicRoot` and strictly sandboxed — `..` traversal, absolute paths and symlinks escaping the root are rejected with `400`.

### Sources + VBAN
| Method | Path | Notes |
|---|---|---|
| GET | `/api/sources` | `{ok,current,sources:["FILE","BT","CD","SPOT"],vban}` |
| GET | `/api/source?name=FILE\|BT\|CD\|SPOT` | switch active source (also `?index=N`) |
| GET | `/api/vban?on=1\|0` | enable/disable VBAN; omit `on` to read state. `{ok,vban}` |

### Screensaver + clocks
| Method | Path | Notes |
|---|---|---|
| GET | `/api/screensaver/on` | random face |
| GET | `/api/screensaver/off` | dismiss |
| GET | `/api/clock?face=NAME` | case-insensitive (see `/api/clock/list`) |
| GET | `/api/clock?index=N` | by theme index |
| GET | `/api/clock/list` | list face names |

Available faces: Luxury, Aviator, Diver, Minimalist, Chronograph, Neon Retro, Bauhaus, Mondaine, Orbital, Guilloche, Digital, Seven Segment, Split Flap, Nixie, Terminal, VFD, Wandering Hours, Regulator, Word Clock, Berlin Uhr, Pong, Binary, Fibonacci, Sundial, Flip Dot (or GET `/api/clock/list`).

### Meta
| Method | Path | Notes |
|---|---|---|
| GET | `/api/health` (`/`) | liveness |

## Responses
- Success: `{"ok":true}` (plus payload for `clock/list`).
- Failure: `{"ok":false,"error":"..."}` with status `400/401/404/405`.

## Examples
```bash
curl http://localhost:8080/api/play
curl "http://localhost:8080/api/volume?level=60"
curl "http://localhost:8080/api/clock?face=Nixie"
```

Or just open `http://localhost:8080/api/play` in a browser / bookmark it.
