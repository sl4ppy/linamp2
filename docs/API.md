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

Available faces: Luxury, Aviator, Diver, Minimalist, Chronograph, Neon Retro, Bauhaus, Mondaine, Orbital, Guilloche, Digital, Seven Segment, Split Flap, Nixie, Terminal (or GET `/api/clock/list`).

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
