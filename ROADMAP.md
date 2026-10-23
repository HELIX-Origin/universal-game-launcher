# Universal Game Launcher — Roadmap & Milestones

> 🗺️ **Living Source of Truth**: Product direction, positioning principles, and milestone delivery history for **Universal Game Launcher**.
> Pair with [`PLAN.md`](./PLAN.md) (sprint execution), [`TODO.md`](./TODO.md) (task ledger), and [`BUGS.md`](./BUGS.md) (issues and quirks).

---

## 📜 Tracking Rules

- **100% Active Direction Accuracy**: `ROADMAP.md`, `PLAN.md`, `TODO.md`, and `BUGS.md` reflect the current, active state of Universal Game Launcher with zero contradictions.
- **Self-Contained Repository Memory**: Universal Game Launcher tracks its own product roadmap and development sprints independently.
- **Mandatory Quality Gate**: Zero compiler warnings (`RUSTFLAGS="-D warnings"`), zero Svelte diagnostics warnings.
- **License**: Strictly licensed under **MIT**.

---

## 🔭 Vision & Positioning Principles

1. **Core Value Proposition**: A unified, high-performance cross-platform desktop gaming hub built on Tauri v2 (Rust + Svelte 5 + modular SCSS). Aggregates titles from across all major PC storefronts and emulation cores with automatic metadata cascade, high-resolution artwork caching, and privacy-first local persistence.
2. **Design Principles**:
   - **Native App Isolation**: The application never writes to, modifies, or backs up external launcher proprietary files (`shortcuts.vdf`, `cloud-storage-namespace-1.json`, etc.). All data persists in our local SQLite database (`database.sqlite`) and in-memory serverless Redis-compatible KV tier (`mem_kv`).
   - **Universal Core Plugins (Rust-owned from v2.0.0)**: Core capabilities are structured as modular plugins (`platform-universal`, `metadata-universal`, `utility-title-normalizer`, `media-universal`, `utility-gamerpower`, `utility-storage`); their logic is consolidating into the Rust core behind a `StoreAdapter` trait, and the TS layer becomes render-only.
   - **Zero In-App Transactions**: Installations, updates, launches, and purchases delegate strictly to official storefront clients or system browsers.
   - **Obsidian Dark SCSS**: Aesthetic coherence with `#07111f` / `#0d1b2d` background, cyan (`#48d8f2`) and purple (`#b06cff`) accents, and authentic vector SVG platform icons (`<PlatformIcon />`).

---

## 🗺️ Milestone Overview

| # | Milestone | Scope | Status | Version / Target | Focus |
|---|---|---|---|---|---|
| **M1–M9** | Foundation through v1.0.0 Packaging | Desktop core | ✅ Shipped | `v1.0.0` | Tauri v2 scaffold, multi-platform discovery, SQLite cache, obsidian UI |
| **M10** | Store Deals & Free Games Engine | Deals engine | ✅ Shipped | `v1.3.1` | Multi-platform free game checker, store deals engine, inline settings |
| **M11** | Zero-Warning Compiler Hardening | Quality gate | ✅ Shipped | `v1.3.2` | Warning elimination across Windows and POSIX targets |
| **M12** | Manifest Schema & Script Standards | Tooling | ✅ Shipped | `v1.3.3` | Canonical `main:sub:option` script taxonomy, manifest schema 1.12.0 |
| **M13** | Universal Plugins & Platform SVG Icons | Plugin system | ✅ Shipped | `v1.4.0` | Universal Core Plugins, Authentic Vector SVG icons, `⚙ Manage` modal |
| **M14** | HELIX Gaming DB 3-Tier API & KV Cache | Metadata API | ✅ Shipped | `v1.4.1` | Three-tier REST client, serverless KV offline cache, hand-rolled purge |
| **M15** | Rust-Owned Core Re-Architecture | Core & performance | 🚀 Active | `v2.0.0` | Rust single source of truth, storefront-neutral adapters, query-driven virtualized library, image pipeline, perf budgets, benchmark-gated shell decision |
| **M16** | Cloud Gaming Hooks & Handheld Overlay | Handheld/Cloud | ⏳ Planned | `v2.1.0` | Cloud streaming hooks (GeForce NOW, Xbox), Gamepad Big Picture mode |
| **M17** | Emulator Auto-Configuration Presets | Emulation | ⏳ Planned | `v2.2.0` | Standalone emulator presets (RetroArch, Dolphin, PCSX2, RPCS3) |

---

## 🚀 Active Milestones

### [universal-game-launcher] — Milestone M15: Rust-Owned Core Re-Architecture (v2.0.0)

> **Decision record (2026-10-21):** Framework shift evaluated. The clunkiness is architectural: business logic duplicated between Rust and TS, the full library shipped as one IPC payload, chatty KV IPC, a Steam-centric model, and monolith files. It is not caused by Tauri itself; the one framework-level weakness is WebKitGTK on Linux. Direction: stay on **Tauri v2 + Svelte 5** and rebuild the core first. The UI shell is decided by benchmark; a Chromium shell (Electron + Rust N-API core, Svelte UI kept) is the approved fallback if any OS misses the budgets.

```mermaid
flowchart TD
    M14_Done["M14 3-Tier API Delivered (v1.4.1)"] --> M15_Base["Perf baseline & budgets"]
    M15_Base --> M15_Core["Rust single source of truth + StoreAdapter model"]
    M15_Core --> M15_UI["Query-driven virtualized library + image pipeline"]
    M15_UI --> Gate{"Budgets met on all 3 OSes?"}
    Gate -- Yes --> Rel["Zero-Warning Gate & Release v2.0.0"]
    Gate -- No --> Shell["Chromium-shell fallback spike"]
```

#### Detailed Deliverables

- 🎯 **[universal-game-launcher] Dynamic Multi-Source Metadata Scraper & Dropping Hand-Rolled Catalogs**: Drop static/manual metadata lists and developer tables (`HAND_ROLLED_GAMES`, static `KNOWN_DEVELOPERS` catalog). Replace with a robust, automated multi-source scraper engine in Rust covering indie & non-Steam titles (itch.io, IndieDB, VNDB, PCGamingWiki Cargo API, Wikipedia/Wikidata, Steam Store & GOG Catalog) with automated field cascade and artwork retrieval.
- 🎯 **[universal-game-launcher] Rust Single Source of Truth**: All scanning, metadata, normalization, filtering, and launch resolution live in Rust; TS becomes render-only with generated bindings.
- 🎯 **[universal-game-launcher] Storefront-Neutral Model**: `Game` keyed by `(store, store_id)`; `StoreAdapter` trait for every storefront, launcher, and emulator; no Steam-account coupling.
- 🎯 **[universal-game-launcher] Steam-Smooth UI**: SQLite FTS5 windowed queries, virtualized grid/list, Rust thumbnail cache over a custom protocol, streamed progress events.
- 🎯 **[universal-game-launcher] Performance Budgets (CI-gated)**: cold start < 1.5 s, 60 fps at 10k games, search < 50 ms, idle-RAM target.

---

## 📌 Historical Milestones & Shipped Releases

### [universal-game-launcher] — Milestone M14: 3-Tier REST Client & Serverless KV (v1.4.1)
- Integrated HELIX Gaming DB three-tier REST client with dynamic shard discovery across volume shards (`00` through `03`).
- Built persistent serverless KV offline fallback (`api.kvSetJson`, `api.kvGetJson`).
- Deleted 28 redundant static developer files in favor of dynamic API resolution.

### [universal-game-launcher] — Milestone M13: Universal Core Plugins & Authentic SVG Icons (v1.4.0)
- Consolidated Universal Core Plugins architecture under `src/lib/plugins/`.
- Authored custom authentic vector SVG platform icon variants (`<PlatformIcon />`).
- Built `⚙ Manage` per-game customization modal with `userEnhanced: true` protection.
- Deployed multi-source automatic metadata cascade across 7+ providers.
