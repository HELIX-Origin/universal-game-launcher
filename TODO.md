# Universal Game Launcher — Task Checklist & Workstream Tracking

> 📋 **Living Source of Truth**: Actionable task ledger for **Universal Game Launcher**.
> Statuses: ⬜ backlog · 🚧 in progress · ✅ done (tracked as `- [ ]` and `- [x]` checkboxes).
> High-level direction lives in [`ROADMAP.md`](./ROADMAP.md), active sprints in [`PLAN.md`](./PLAN.md), issues in [`BUGS.md`](./BUGS.md).

---

## 📜 Tracking Rules

- **100% Active Direction Accuracy**: Task items and checklists must reflect the current state of Universal Game Launcher with zero contradictions.
- **Normalized Directives**: Rewrite user directives into clean, self-contained engineering requirements.
- **Done Moves Down**: Completed items are marked with `- [x]` and archived when workstreams finish.
- **Zero-Warning Gate**: All builds and tests must pass with 0 errors and 0 warnings (`cargo test`, `svelte-check`).

---

## 🔥 Active Workstreams

### 🎯 [universal-game-launcher] Workstream W.55: Cloud Gaming Streaming Hooks, Gamepad Handheld Mode & Performance Tuning
- **Project Identifier:** `[universal-game-launcher]`
- **Repository Path:** `projects/universal-game-launcher`
- **Target Component:** Core Launcher & UI

```mermaid
flowchart TD
    W55_1["[universal-game-launcher] Cloud Provider Hooks"] --> W55_2["[universal-game-launcher] Controller Big Picture Mode"]
    W55_2 --> W55_3["[universal-game-launcher] SQLite Index Optimization"]
```

**Implementation checklist:**
- [ ] **[universal-game-launcher] 1. Cloud Gaming URI Integration**:
  - [ ] `[universal-game-launcher]` Implement launch handlers and URI schemes for GeForce NOW, Xbox Cloud Gaming, and Moonlight/Sunshine.
  - [ ] `[universal-game-launcher]` Add cloud provider pills and status badges to library game cards.
- [ ] **[universal-game-launcher] 2. Gamepad & Big Picture Overlay**:
  - [ ] `[universal-game-launcher]` Implement gamepad input driver supporting standard controllers (Xbox, DualShock/DualSense, Switch Pro).
  - [ ] `[universal-game-launcher]` Design full-screen Big Picture layout tailored for handheld displays (Steam Deck, ROG Ally, Legion Go).
- [ ] **[universal-game-launcher] 3. Large-Library Performance & SQLite Optimization**:
  - [ ] `[universal-game-launcher]` Benchmark SQLite queries on 10,000+ title libraries; implement covering indices on title, platform, and flags.
  - [ ] `[universal-game-launcher]` Virtualize library card grid to maintain smooth 60fps scrolling on low-power APUs.
- [ ] **[universal-game-launcher] 4. Quality Verification**:
  - [ ] `[universal-game-launcher]` Verify zero errors and zero warnings across `npm run check` and `RUSTFLAGS="-D warnings" cargo check`.

---

### [universal-game-launcher] Workstream W.56: Platform-Native Game Launching and Auto-Detected Launchers
- **Project Identifier:** [universal-game-launcher]
- **Repository Path:** projects/universal-game-launcher
- **Status:** In progress (user directive 2026-10-21).
- **Target Component:** Core Launcher (Rust) + Settings UI (Svelte)

**Locked user directives (normalized by domain):**
- **Platform-native launch**: Games on platforms that require their store/launcher (Steam, Epic, Battle.net, Ubisoft, EA, Rockstar, Amazon, Xbox, Jagex Launcher, XIV Launcher, FFXIV official launcher, Guild Wars 2 official launcher, etc.) MUST be launched through the official launcher executable/protocol so DRM, overlays, and account injection work. Direct binary execution is only a fallback for platforms that permit it (itch.io, GOG, Standalone, Other).
- **Steam account injection**: Steam games must invoke steam.exe directly with steam://run/{appid} (not cmd /c start) so the logged-in account is passed to the game.
- **Auto-detection**: Launchers/stores/platforms are auto-detected; they no longer need to be enabled/disabled/displayed as toggles in Settings.
- **Legendary optional toggle**: Legendary CLI support is an optional setting (legendary_enabled) that bypasses the Epic Games Launcher method.
- **API keys in Settings**: Settings must expose fields for Steam Web API key, Guild Wars 2 API key, IGDB client id/secret, and other game API keys required for full support.
- **Additional launchers**: Add support for Jagex Launcher, XIV Launcher, Square Enix FFXIV official launcher, Guild Wars 2 official launcher, etc.
- **Capabilities**: Tauri capabilities folder verified present and correct (default.json with opener plugin); only the opener plugin is used.

**Implementation checklist:**
- [x] [universal-game-launcher] Steam games launch via steam.exe steam://run/{appid} (account injection).
- [x] [universal-game-launcher] Reorder launch_game so store-required platforms skip direct-exe-first.
- [x] [universal-game-launcher] Add legendary_enabled, gw2_api_key, jagex_launcher_path, xiv_launcher_path to Settings.
- [ ] [universal-game-launcher] Implement direct-executable invocation for Epic, Battle.net, Ubisoft, EA, Rockstar, etc.
- [ ] [universal-game-launcher] Add Jagex Launcher, XIV Launcher, FFXIV official, GW2 official launcher support.
- [ ] [universal-game-launcher] Remove launcher enable/disable toggles from Settings UI; rely on auto-detection.
- [ ] [universal-game-launcher] Add API key input fields (Steam Web API, GW2, IGDB, etc.) to Settings UI.
- [ ] [universal-game-launcher] Research correct invocation for each launcher/store and document.
- [ ] [universal-game-launcher] Quality gate: cargo check + npm run check with 0 warnings.

---

## ✅ Completed Workstreams

### 🎯 [universal-game-launcher] Workstream W.53: Helix Gaming DB Three-Tier REST API Integration & Hand-Rolled Catalog Deprecation (Release v1.4.1)
- **Delivered (2026-10-16):**
  - [x] `[universal-game-launcher]` Upgraded `src/lib/plugins/metadata/helix-db.ts` to support `/api/manifest.json`, `/api/v2/` relational discovery, and `/api/v3/` dedicated adult tier.
  - [x] `[universal-game-launcher]` Implemented in-memory caching (`cachedV2Index`, `cachedV3Index`) and persistent KV fallback (`api.kvSetJson`, `api.kvGetJson`).
  - [x] `[universal-game-launcher]` Deleted all 28 redundant static developer files (`src/lib/plugins/metadata/developers/*.ts`, saving 3,000+ lines of static code).
  - [x] `[universal-game-launcher]` Streamlined `developers/index.ts` with lightweight studio profiles preserving matching algorithms.
  - [x] `[universal-game-launcher]` Quality verification: passed `svelte-check` (0 errors, 0 warnings), `cargo test` clean.

### 🎯 [universal-game-launcher] Workstream W.49: Universal Core Plugins, Authentic SVG Platform Icons & Per-Game Customization (Release v1.4.0)
- **Delivered (2026-10-14):**
  - [x] `[universal-game-launcher]` Consolidated Universal Core Plugins architecture under `src/lib/plugins/`.
  - [x] `[universal-game-launcher]` Authored authentic vector SVG platform icon variants (`<PlatformIcon />`).
  - [x] `[universal-game-launcher]` Multi-source automatic metadata cascade (Local Engine, Libretro Thumbnails, Steam Store, GOG, Wikipedia, PCGamingWiki, ProtonDB).
  - [x] `[universal-game-launcher]` Implemented `⚙ Manage` per-game customization modal with `userEnhanced: true` protection.

### 🎯 [universal-game-launcher] Workstream W.47: Project Script Standards & Manifest Schema 1.12.0 (Release v1.3.3)
- **Delivered (2026-10-10):**
  - [x] `[universal-game-launcher]` Canonical `main:sub:option` scripts, schema 1.12.0 validation, `.vscode` untracking, release CI.

### 🎯 [universal-game-launcher] Workstream W.46: Cross-Platform Compiler Warning Elimination (Release v1.3.2)
- **Delivered (2026-10-06):**
  - [x] `[universal-game-launcher]` Resolved compiler warnings across POSIX and Windows targets with zero warnings.

### 🎯 [universal-game-launcher] Workstream W.45: Multi-Platform Free Game Checker & Deals Engine (Release v1.3.1)
- **Delivered (2026-10-02):**
  - [x] `[universal-game-launcher]` Per-platform checkers, deals engine (`utility-gamerpower`), patch notes feed, sidebar cog.
