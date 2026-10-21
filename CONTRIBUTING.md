# Contributing to Universal Game Launcher

Thanks for your interest in improving **Universal Game Launcher**! This guide covers development setup, conventions, the quality gate, and how to submit changes.

> By participating you agree to our [Code of Conduct](./CODE_OF_CONDUCT.md). For security issues,
> follow [SECURITY.md](./SECURITY.md) — **do not** open a public issue.

Universal Game Launcher is a high-performance cross-platform desktop application built with [Tauri v2](https://tauri.app) (Rust backend + Svelte 5 frontend with modular SCSS). It aggregates games from across PC storefronts and emulators into a unified, privacy-focused library with automatic metadata and artwork enrichment.

---

## Table of Contents

1. [Ways to Contribute](#ways-to-contribute)
2. [Development Setup](#development-setup)
3. [Architecture Notes](#architecture-notes)
4. [Quality Gate](#quality-gate)
5. [Conventions](#conventions)
6. [Commit Style](#commit-style)
7. [Pull Request Process](#pull-request-process)
8. [Getting Help](#getting-help)

---

## Ways to Contribute

- 🐛 **Report bugs** via [GitHub Issues](https://github.com/HELIX-Origin/universal-game-launcher/issues) (bug form).
- ✨ **Propose features** via [GitHub Issues](https://github.com/HELIX-Origin/universal-game-launcher/issues) (feature form).
- 📝 **Improve documentation** in the centralized documentation and technical wiki hubs.
- 🔧 **Submit code** via pull request.
- 👀 **Review pull requests** from other contributors.

Open an issue or comment on an existing one before starting significant work, so the approach can be discussed first.

---

## Development Setup

Prerequisites:
- [Node.js](https://nodejs.org/) (v20+)
- [Rust](https://rustup.rs/) (stable, 1.80+)
- [Tauri prerequisites](https://tauri.app/start/prerequisites/) for your operating system (Windows, macOS, or Linux)

```bash
npm install
npm run tauri dev
```

The Rust build embeds the compiled frontend, so `npm run build` must run before a fresh `cargo` command if `frontendDist` (`build/`) does not yet exist.

---

## Architecture Notes

- **Native App Isolation**: Universal Game Launcher never writes to, modifies, or backs up external launcher files (`shortcuts.vdf`, `cloud-storage-namespace-1.json`, or `appmanifest.acf`). Library records and user custom metadata persist exclusively in our local SQLite database (`database.sqlite`) and in-memory Redis KV tier (`mem_kv`).
- **Universal Core Plugins**: Core architecture is organized into modular plugins (`platform-universal`, `metadata-universal`, `utility-title-normalizer`, `media-universal`, `utility-gamerpower`, `utility-storage`) under `src/lib/plugins/` and `src-tauri/src/`.
- **Zero In-App Transactions**: Game installations, updates, uninstallation, launches, and storefront checkouts delegate strictly to official platform clients or verified external browser tabs.

---

## Quality Gate

Everything below must pass with **zero errors and zero warnings** before a pull request can be merged:

```bash
npm run check             # svelte-check reporting 0 errors and 0 warnings
npm run build             # vite production build
cargo test --manifest-path src-tauri/Cargo.toml
RUSTFLAGS="-D warnings" cargo check --manifest-path src-tauri/Cargo.toml
```

---

## Conventions

- **Rust**: Zero warnings (`RUSTFLAGS="-D warnings"`), `cargo fmt` formatting.
- **Frontend**: Svelte 5 runes (`$state`, `$derived`, `$props`), SCSS design tokens, zero Svelte diagnostics warnings.
- **Licensing**: Universal Game Launcher is licensed under the **MIT License**.
- **Changelog**: Update `CHANGELOG.md` when behavior or features change.

---

## Commit Style

We use [Conventional Commits](https://www.conventionalcommits.org/) with concise, imperative subjects:

```
<type>(<scope>): <short summary in imperative mood>
```

Common types: `feat`, `fix`, `docs`, `test`, `refactor`, `perf`, `chore`, `security`, `build`.

---

## Pull Request Process

1. Fork the repository and create a descriptive branch name from `main`.
2. Ensure the quality gate passes cleanly with 0 errors and 0 warnings.
3. Submit a pull request referencing any relevant issue numbers.

---

## Getting Help

- 📖 **Documentation**: [docs.helix-origin.club/universal-game-launcher](https://docs.helix-origin.club/universal-game-launcher/)
- 🧠 **Technical Wiki**: [wiki.helix-origin.club/universal-game-launcher](https://wiki.helix-origin.club/universal-game-launcher/)
- 💬 **Discussions**: [GitHub Discussions](https://github.com/HELIX-Origin/universal-game-launcher/discussions)
