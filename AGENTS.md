# universal-game-launcher

Tauri v2 multi-platform game library and launcher — Rust core (`src-tauri/`) + SvelteKit/Svelte 5
frontend with SCSS (`src/`). Unifies Steam, GOG, Epic, itch, Xbox, EA, Ubisoft, Battle.net, Amazon,
Rockstar, EmulationStation, and standalone games in one fast, privacy-focused library. MIT license.

## Architecture at a glance

- **Rust is the single source of truth** for all business logic: platform scanning, metadata
  cascade, title normalization, filtering/sorting, launch resolution, image pipeline.
- **Plugin architecture:** core capabilities are consolidated into Universal Core Plugins under
  `src/lib/plugins/` (TS thin bindings) and `src-tauri/src/plugins/` + `src-tauri/src/metadata/` (Rust logic).
- **SQLite (WAL + FTS5)** for the game library; UI requests windowed queries — never the full list.
- **Generated IPC bindings** via `specta` / `tauri-specta` — Rust↔TypeScript types cannot drift.

## Build & test

| Command | Purpose |
|---|---|
| `npm run tauri:dev` | Dev server + Tauri window |
| `npm run tauri:build` | Production Tauri bundle |
| `npm run check` | svelte-check (0 errors, 0 warnings required) |
| `npm run build` | Vite production frontend build |
| `node scripts/test.mjs check rust frontend build` | Full gate |
| `cargo check` / `cargo test` | Run from `src-tauri/` |

## Conventions (summary)

- **Rust:** `RUSTFLAGS="-D warnings"` — zero warnings; no panics across command boundary.
- **Frontend:** Svelte 5 runes, strict TypeScript, SCSS token-driven (no hardcoded colors).
- **Plugins:** each plugin in `src/lib/plugins/<domain>/<name>.ts`; exports a typed interface.
- **Platform detection:** always via `platform-universal` plugin; never call Tauri APIs directly from UI components.
- **Metadata:** scrapers implement `MetadataProvider`; cascade is SteamGridDB → IGDB → OpenCritic → offline fallback.
- **SQLite:** lean schema (metadata index only, no binary blobs); WAL mode; forward-only migrations.
- **No client file tampering:** never write to or modify storefront files (Steam shortcuts.vdf, etc.).

See `.agents/rules/local-conventions.md` for the full conventions reference.

## Invariants

MIT license · SemVer releases · zero-warning builds · community-health files · non-invasive core.
See `CONTRIBUTING.md` for full contributor guidelines.
