# Architecture — universal-game-launcher

## Directory map

```
universal-game-launcher/
├── src-tauri/
│   ├── Cargo.toml
│   ├── tauri.conf.json               # shared Tauri config
│   ├── tauri.windows.conf.json       # Windows NSIS packaging
│   ├── tauri.macos.conf.json         # macOS DMG packaging
│   ├── tauri.linux.conf.json         # Linux deb/AppImage packaging
│   ├── capabilities/default.json     # Tauri v2 ACL (least-privilege)
│   ├── migrations/                   # SQLite forward-only migration files
│   └── src/
│       ├── lib.rs                    # Tauri builder, plugin setup, command registration
│       ├── commands.rs               # All #[tauri::command] handlers (thin layer)
│       ├── db.rs                     # SQLite pool (WAL + FTS5), migration runner, query layer
│       ├── image.rs                  # Image pipeline: fetch → resize → cache → serve via URI protocol
│       ├── error.rs                  # AppError enum; all command errors derive From here
│       ├── plugins/
│       │   ├── mod.rs                # Plugin registry and trait definitions
│       │   ├── title_normalizer.rs   # Article inversion, No-Intro tag stripping, slug splitting
│       │   └── platforms/
│       │       ├── mod.rs            # PlatformAdapter trait + scan orchestrator
│       │       ├── steam.rs          # Steam adapter (detect, scan, launch_spec, URIs)
│       │       ├── gog.rs            # GOG Galaxy adapter
│       │       ├── epic.rs           # Epic Games adapter
│       │       ├── itch.rs           # itch.io adapter (butler CLI)
│       │       ├── xbox.rs           # Xbox / MS Store adapter
│       │       ├── ea.rs             # EA App adapter
│       │       ├── ubisoft.rs        # Ubisoft Connect adapter
│       │       ├── battlenet.rs      # Battle.net adapter
│       │       ├── amazon.rs         # Amazon Games adapter
│       │       ├── rockstar.rs       # Rockstar Games adapter
│       │       ├── emulation.rs      # EmulationStation / ES-DE ROM adapter
│       │       └── standalone.rs     # Manual / standalone EXE adapter
│       └── metadata/
│           ├── mod.rs                # MetadataProvider trait + cascade orchestrator
│           ├── universal.rs          # Universal metadata engine (field-by-field cascade)
│           ├── steam.rs              # Steam Store CDN + Web API provider
│           ├── steamgriddb.rs        # SteamGridDB provider (optional key)
│           ├── igdb.rs               # IGDB provider (optional key)
│           ├── opencritic.rs         # OpenCritic provider (keyless)
│           ├── pcgamingwiki.rs       # PCGamingWiki Cargo provider (keyless)
│           └── gog.rs                # GOG Catalog provider (keyless)
├── src/
│   ├── lib/
│   │   ├── plugins/
│   │   │   ├── platforms/universal.ts    # platform-universal — PLATFORM_SPECS registry, helpers
│   │   │   ├── metadata/universal.ts     # metadata-universal — TS bindings for Rust engine
│   │   │   ├── utility/
│   │   │   │   ├── title-normalizer.ts   # utility-title-normalizer
│   │   │   │   ├── gamerpower.ts         # utility-gamerpower (free games/deals feeds)
│   │   │   │   └── storage-analyzer.ts   # utility-storage (multi-drive footprint)
│   │   │   └── media/universal.ts        # media-universal (trailer + soundtrack player)
│   │   ├── styles/
│   │   │   ├── _tokens.scss              # All design tokens (colors, spacing, typography)
│   │   │   └── app.scss                  # Global styles, reset, layout
│   │   ├── components/                   # Svelte components (PascalCase.svelte)
│   │   │   └── PlatformIcon.svelte       # Authentic SVG platform icons
│   │   └── stores/                       # Svelte stores (games, settings, customNames)
│   └── routes/
│       ├── +layout.svelte                # App shell: frameless titlebar, sidebar, nav
│       ├── +page.svelte                  # Dashboard (/) — Jump Back In, stats, notifications
│       ├── library/+page.svelte          # Library (/library) — game showcase + sidebar
│       ├── store/+page.svelte            # Store (/store) — free games, deals, discovery
│       ├── social/+page.svelte           # Social (/social) — cross-platform friends
│       ├── storage/+page.svelte          # Storage (/storage) — multi-drive explorer
│       └── settings/+page.svelte         # Settings (/settings) — 5-tab config workspace
├── scripts/
│   ├── lib/runner.mjs                    # Shared process spawner, ANSI output, timing
│   ├── dev.mjs                           # Dev orchestrator
│   ├── build.mjs                         # Production build (platform config injection)
│   └── test.mjs                          # Full gate runner
└── package.json                          # npm scripts (main:sub naming convention)
```

## Plugin map

| Plugin ID | TS file | Rust backing | Purpose |
|---|---|---|---|
| `platform-universal` | `plugins/platforms/universal.ts` | `plugins/platforms/*.rs` | All 12 platform adapters |
| `metadata-universal` | `plugins/metadata/universal.ts` | `metadata/universal.rs` | Metadata & artwork cascade |
| `utility-title-normalizer` | `plugins/utility/title-normalizer.ts` | `plugins/title_normalizer.rs` | Title normalization |
| `media-universal` | `plugins/media/universal.ts` | _(frontend-only)_ | Trailer + soundtrack player |
| `utility-gamerpower` | `plugins/utility/gamerpower.ts` | _(frontend-only)_ | Free games / deals feeds |
| `utility-storage` | `plugins/utility/storage-analyzer.ts` | _(frontend-only)_ | Storage footprint analyzer |

## Tauri command flow

```
Frontend (Svelte)
  └─ invoke("command_name", { arg })          // src/lib/plugins/<domain>/<name>.ts
       │
       ▼
Tauri IPC bridge (capabilities/default.json — least-privilege ACL)
       │
       ▼
commands.rs  #[tauri::command]                // thin handler; delegates immediately
  └─ db.rs / plugins/ / metadata/            // business logic (Rust owns it)
       │
       ▼  Ok(T) | Err(AppError)
Frontend receives typed Result (via specta/tauri-specta bindings)
```

- All types generated by `specta` — Rust struct changes auto-propagate to TypeScript.
- Commands use windowed queries: `query_library(filter, sort, offset, limit)` not `get_all_games()`.
- Progress events (scans, metadata sync) stream via Tauri channels — no polling from the frontend.

## Metadata cascade flow

```
query_metadata(game_id, game_title, platform)
  │
  ▼
metadata/universal.rs (cascade orchestrator)
  ├─ 1. Local receipt inspector (Steam appmanifest, GOG manifest, etc.)
  ├─ 2. Steam Store CDN + Web API (keyless)
  ├─ 3. GOG Catalog v1 + Products API (keyless)
  ├─ 4. PCGamingWiki Cargo (keyless)
  ├─ 5. OpenCritic (keyless)
  ├─ 6. SteamGridDB (optional API key)
  ├─ 7. IGDB (optional API key)
  └─ 8. Offline fallback (title-normalized local data)
       │
       ▼
Field-by-field merge: highest-priority non-null value wins.
userEnhanced: true fields are never overwritten by providers.
```

## SQLite cache schema principles

- **Lean schema:** store identifiers, normalized titles, metadata fields, and file paths.
  No binary blobs in the database — images live on disk.
- **WAL mode + FTS5:** full-text search over game titles and descriptions.
- **Covering indices** on `(store, store_id)` (primary key), `(platform, installed)`, `(last_played)`.
- **Forward-only migrations:** numbered SQL files in `src-tauri/migrations/`; never edit existing files.
- Key tables: `games` (library), `game_meta` (enriched metadata), `custom_names`, `playtime`, `settings`.

## Frontend route structure

| Route | Purpose |
|---|---|
| `/` | Dashboard — Jump Back In, stats, notifications, active downloads |
| `/library` | Primary game showcase with left sidebar (platform accordions + search) |
| `/store` | Multi-platform store discovery, free games, deals |
| `/social` | Cross-platform friends roster + presence status |
| `/storage` | Multi-drive capacity bars, installed games disk explorer |
| `/settings` | 5-tab config: General, Providers, API & Credentials, Media & Audio, Storage & Cache |

## Tauri command registration pattern

1. Define handler in `commands.rs` (thin; no business logic).
2. Add business logic in `db.rs`, `plugins/`, or `metadata/`.
3. Register in `lib.rs` → `.invoke_handler(tauri::generate_handler![…])`.
4. Run `cargo build` — `specta` regenerates TypeScript bindings automatically.
5. Import and use the typed binding in the appropriate TS plugin file.
