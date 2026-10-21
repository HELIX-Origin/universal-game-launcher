# universal-game-launcher

Tauri v2 desktop application (Rust core + Svelte/SCSS frontend).

## Project-local agents

This repository ships its own self-contained agents ecosystem (`AGENTS.md`, `.agents/`) and
tracking files (`ROADMAP.md`, `PLAN.md`, `TODO.md`, `BUGS.md`). Keep them accurate as work progresses.

## Build & test

- `cargo check` / `cargo test` (Rust core)
- `npm run build` / `npm run check` (frontend)
- Tauri: `cargo tauri dev`, `cargo tauri build`

## Conventions

- Rust: zero warnings (`RUSTFLAGS="-D warnings"`).
- Frontend: Svelte 5 runes, SCSS-first, zero svelte-check warnings.
- See `.agents/rules/local-conventions.md`.

## Invariants

MIT license, SemVer releases, zero-warning builds, community-health files.
See `CONTRIBUTING.md` for the full contributor guidelines.