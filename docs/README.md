# Linamp Developer Documentation

Documentation for the Linamp retro music player. These docs cover the internal architecture and implementation details for developers working on the codebase.

## Documents

| Document | Description |
|---|---|
| [architecture.md](architecture.md) | High-level system architecture, component diagram, signal/slot wiring, threading model, and data flow diagrams |
| [audio-sources.md](audio-sources.md) | Audio source plugin system: base class interface, coordinator, per-source details, and how to add a new source |
| [python-integration.md](python-integration.md) | Python/C++ bridge: interpreter lifecycle, GIL management, BasePlayer interface, polling architecture, and adding Python plugins |
| [media-player.md](media-player.md) | Custom MediaPlayer internals: QIODevice-based design, buffer pipeline, state machine, underrun recovery, seeking |
| [ui-system.md](ui-system.md) | UI and view system: window hierarchy, all views, scaling system, custom widgets, ALSA audio control, Qt resources |
| [build-and-deploy.md](build-and-deploy.md) | Development setup, CMake configuration, Debian packaging, CI/CD pipeline, shell scripts |
| [SCREENSAVER.md](SCREENSAVER.md) | Screensaver feature: idle detection, clock rendering, configuration, customization |
