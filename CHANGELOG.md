# Changelog

All notable changes to **Universal Game Launcher** are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Planned
- Milestone M15: Cloud gaming streaming hooks (GeForce NOW, Xbox Cloud Gaming, Moonlight/Sunshine).
- Gamepad / handheld Big Picture overlay mode (Steam Deck, ROG Ally, Legion Go).
- Automated standalone emulator presets (RetroArch, Dolphin, PCSX2, RPCS3).

## [1.4.1] - 2026-10-16

### Added
- **HELIX Gaming DB Three-Tier REST API Integration**: Full support for root gateway (`/api/manifest.json`), v2 relational discovery, and v3 dedicated adult tier.
- **Dynamic Shard Cycling**: Client discovery across volume shards (`ugl-db-00` through `03` and future sequential shards) with automatic failover.
- **Persistent Serverless KV Offline Fallback**: High-speed in-memory dual-tier index caching and persistent KV fallback (`api.kvSetJson`, `api.kvGetJson`).

### Changed
- **Hand-Rolled Metadata Purge**: Deleted 28 redundant static developer files in favor of dynamic API resolution; streamlined `developers/index.ts`.

## [1.4.0] - 2026-10-14

### Added
- **Universal Core Plugins**: Modular consolidated architecture (`platform-universal`, `metadata-universal`, `utility-title-normalizer`, `media-universal`, `utility-gamerpower`, `utility-storage`).
- **Authentic Vector SVG Platform Icons**: Custom vector SVG variants (`<PlatformIcon />`) for all major storefronts and emulation consoles.
- **Per-Game Customization (`⚙ Manage` Modal)**: In-library editing of title, descriptions, tags, and artwork overrides protected by `userEnhanced: true`.
- **Automatic Metadata Engine**: Multi-source background cascade (Local Engine, Libretro Thumbnails, Steam, GOG, Wikipedia/Wikidata, PCGamingWiki, ProtonDB).

## [1.3.3] - 2026-10-10

### Changed
- Standardized project scripts to `main:sub:option` format.
- Manifest schema validation upgraded to schema 1.12.0.

## [1.3.2] - 2026-10-06

### Fixed
- Resolved all cross-platform compiler warnings on POSIX and Windows targets (`RUSTFLAGS="-D warnings"`).

## [1.3.1] - 2026-10-02

### Added
- Multi-platform free game checker and store deals engine (`utility-gamerpower`).
- Inline settings workspace with five categories and real-time connection verification.

## [1.0.0] - 2026-09-15

### Added
- Initial public release of Universal Game Launcher on Tauri v2.
- Multi-platform owned game detection (Steam, GOG, Epic, Xbox, EA, Ubisoft, Battle.net).
- SQLite database persistence (`database.sqlite`) and obsidian dark SCSS theme.

[Unreleased]: https://github.com/HELIX-Origin/universal-game-launcher/compare/v1.4.1...HEAD
[1.4.1]: https://github.com/HELIX-Origin/universal-game-launcher/releases/tag/v1.4.1
[1.4.0]: https://github.com/HELIX-Origin/universal-game-launcher/releases/tag/v1.4.0
[1.3.3]: https://github.com/HELIX-Origin/universal-game-launcher/releases/tag/v1.3.3
[1.3.2]: https://github.com/HELIX-Origin/universal-game-launcher/releases/tag/v1.3.2
[1.3.1]: https://github.com/HELIX-Origin/universal-game-launcher/releases/tag/v1.3.1
[1.0.0]: https://github.com/HELIX-Origin/universal-game-launcher/releases/tag/v1.0.0
