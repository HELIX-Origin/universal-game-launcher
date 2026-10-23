# Local Conventions — universal-game-launcher

## Layout

- **Rust backend:** `src-tauri/src/` — `lib.rs` (builder + command registration), `commands.rs`,
  `plugins/` (platform adapters, title normalizer), `metadata/` (metadata cascade engine),
  `db.rs` (SQLite pool, migrations), `image.rs` (image pipeline), `error.rs`.
- **Frontend:** `src/` SvelteKit SPA:
  - `src/lib/plugins/` — Universal Core Plugins (thin TS bindings over Rust logic)
    - `platforms/universal.ts` (`platform-universal`) — all 12 platform providers
    - `metadata/universal.ts` (`metadata-universal`) — metadata & artwork cascade
    - `utility/title-normalizer.ts` (`utility-title-normalizer`)
    - `media/universal.ts` (`media-universal`)
    - `utility/gamerpower.ts`, `utility/storage-analyzer.ts`
  - `src/lib/styles/` — `_tokens.scss` (design tokens), `app.scss`, component styles
  - `src/lib/components/` — Svelte components
  - `src/lib/stores/` — Svelte stores
  - `src/routes/` — SvelteKit routes (`/`, `/library`, `/store`, `/social`, `/storage`, `/settings`)
- All Tauri commands are registered in `src-tauri/src/lib.rs` and typed via `specta`/`tauri-specta`.

## Frontend

- **Svelte 5 runes** (`$state`, `$derived`, `$effect`, `$props`); strict TypeScript.
- **SCSS-first:** all styles in SCSS; design tokens defined in `src/lib/styles/_tokens.scss`.
  No hardcoded color values anywhere — always use a token variable (e.g. `var(--accent)`,
  `var(--panel)`). No Tailwind, no utility-class frameworks.
- `kebab-case` files/folders; `PascalCase` component names; prefer `$lib/` imports.

## Plugin architecture

- Each plugin lives in `src/lib/plugins/<domain>/<name>.ts` (e.g. `platforms/universal.ts`).
- Every plugin file must **export a typed interface** (e.g. `PlatformPlugin`, `MetadataProvider`).
- **Never add new business logic to the TypeScript layer.** Rust is the single source of truth.
  The TS plugins are thin bindings that invoke Rust commands and type the responses.
- **Platform detection** always goes through the `platform-universal` plugin
  (`src/lib/plugins/platforms/universal.ts`). UI components must never call Tauri APIs directly
  for platform detection or scanning.

## Metadata cascade

Metadata scrapers implement the `MetadataProvider` interface with a field-by-field cascade:

```
SteamGridDB (optional key) → IGDB (optional key) → OpenCritic → PCGamingWiki (keyless)
  → Steam Store CDN (keyless) → GOG Catalog (keyless) → offline fallback
```

- Providers are implemented in Rust (`src-tauri/src/metadata/`) and invoked via Tauri commands.
- **Never overwrite `userEnhanced: true` metadata** unless the user explicitly purges the cache.
- No manual "Refresh Metadata" buttons in the library topbar — cache management is in Settings only.

## SCSS tokens

All token variables are defined in `src/lib/styles/_tokens.scss`. Key tokens:

| Token | Role |
|---|---|
| `--bg-0` / `--bg-1` / `--bg-2` | Background depth layers |
| `--panel` / `--panel-strong` | Card and modal surfaces |
| `--accent` / `--accent-2` | Primary/secondary accent colors |
| `--text` / `--text-dim` / `--text-mute` | Text hierarchy |
| `--ok` / `--warn` / `--danger` | Status colors |
| `--platform-<id>-gradient/accent/glow` | Per-platform auto-generated tokens |

Never hardcode a hex or rgba value in a component. Always use a token.

## Backend

- **Rust `snake_case`**; small, focused modules.
- **Never panic across the Tauri command boundary** — always return `Result<T, AppError>`.
- **Zero client file tampering:** never write to storefront files (`shortcuts.vdf`, manifests, etc.).
  Never require a storefront client to be closed.
- **SQLite lean schema:** metadata index only; no binary blobs in the database. Store images on
  disk via the image pipeline and reference paths/URIs. Use WAL mode and FTS5 for search.
- **SQLite migrations** are forward-only — never modify existing migration files.
- **Image pipeline:** pre-sized thumbnails cached on disk, served via a custom URI protocol.
  Never use base64 or data-URL artwork.
- **Push, don't poll:** scans and metadata sync stream progress via Tauri channels/events.
  The UI never polls with chatty per-key KV IPC calls.
- **Windowed queries:** the UI calls `query_library(filter, sort, offset, limit)` and renders
  a virtualized grid. Never send the full `Game[]` array over IPC.

## Verification gate

Run before every merge (see `.agents/rules/verification.md` for full commands):

```
RUSTFLAGS="-D warnings" cargo check --manifest-path src-tauri/Cargo.toml
cargo test --manifest-path src-tauri/Cargo.toml
npm run check        # svelte-check — 0 errors, 0 warnings
npm run build        # production Vite build
node scripts/test.mjs check rust frontend build
```

## General

- **Never commit generated bundles** (`build/`, `.svelte-kit/`, `src-tauri/target/`); CI builds them.
- Zero warnings across Rust and TypeScript/Svelte — warnings are treated as errors.
- **No credential storage:** never handle, scrape, or store third-party user passwords or tokens.
