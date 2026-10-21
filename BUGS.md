# Universal Game Launcher — 🐛 Bug Tracker

> 🐛 **Living Source of Truth**: Active tracking ledger for open bugs, quirks, external limitations, and resolution history for **Universal Game Launcher**.
> Pair with [`ROADMAP.md`](./ROADMAP.md) (direction), [`PLAN.md`](./PLAN.md) (sprints), and [`TODO.md`](./TODO.md) (tasks).

---

## 📜 Tracking Rules

- **100% Active Direction Accuracy**: Reflect the current, active state of Universal Game Launcher with zero contradictions.
- **Explicit Project Tagging**: Every entry is tagged with `[universal-game-launcher]`.
- **Zero-Warning Gate**: Compiler warnings are treated as errors and resolved before release.

---

## 🚫 Known Quirks & External Limitations

- 🔒 **[universal-game-launcher] Zero External File Writes (Native App Isolation Invariant)**: Universal Game Launcher never writes to, modifies, or backs up external launcher files (`shortcuts.vdf`, `cloud-storage-namespace-1.json`, or `appmanifest.acf`). Game records, collections, categories, custom display names, custom metadata/artwork overrides, and user preferences persist exclusively in our own SQLite database (`database.sqlite`) and in-memory Redis KV tier (`mem_kv`).
- 🔒 **[universal-game-launcher] Steam Web API Profile Visibility**: `IPlayerService/GetOwnedGames` returns data only when profile game details are public; otherwise Steam returns HTTP `403`. Local library data is always discovered offline.
- 🔒 **[universal-game-launcher] Native Launcher Delegation & Zero In-App Transactions**: Game installation, updates, uninstallation, launches, and store checkouts delegate strictly to official platform clients or verified external browser tabs. No user passwords or financial transactions are ever processed inside the app.
- 🔒 **[universal-game-launcher] Authentic Vector SVG Platform & Console Icons (`PlatformIcon.svelte`)**: All platform headers, pills, and fallback capsules across the UI render custom vector SVG icon variants (`<PlatformIcon />`) representing each actual provider and emulation console logo rather than generic Unicode shapes.

---

## 💡 Explicitly Not Bugs

- 💡 **[universal-game-launcher] Single-Game Showcase View & Conditional Left Sidebar**: The custom frameless title bar contains no right quick-filter bar, and the collapsible platform/console accordion sidebar is rendered exclusively on `/library` to drive the single-game showcase view by design.
- 💡 **[universal-game-launcher] Out-of-the-Box System Locale**: UI language defaults directly to the host system locale (`navigator.language`).

---

## ⚠️ Active & Open Bugs

### [universal-game-launcher] BUG-1 — Process Launch Failure & Immediate Crash on Specific Titles (RuneScape)
- **Project Identifier:** `[universal-game-launcher]`
- **Repository Path:** `projects/universal-game-launcher`
- **Severity**: 🟠 High (functional failure — game cannot be launched from launcher)
- **Status**: ⚠️ open
- **Scope:** `src-tauri/src/` (launch command execution, environment variables, working directory) & `src/lib/plugins/platform-universal/`
- **Symptom:** Attempting to launch certain games fails; the target executable or launcher process initiates but immediately crashes or terminates unexpectedly. Observed specifically with RuneScape and potentially impacting other titles with specialized startup parameters, multi-stage bootstrappers, or custom working directory dependencies.
- **Notes**: Deferred for dedicated future debugging sprint per maintainer directive; logged for tracking.

---

## ✅ Resolved Bugs & Historical Fixes

### 2026-10-10 — [universal-game-launcher]: Cross-Platform POSIX / Windows Compiler Warnings
- **Severity**: 🟡 Medium
- **Status**: ✅ resolved
- **Scope**: `src-tauri/src/`
- **Root Cause**: Conditional compilation flags on non-Windows targets produced dead-code and unused-import warnings under strict `RUSTFLAGS="-D warnings"`.
- **Fix**: Refactored platform conditionals (`#[cfg(target_os = ...)]`) and verified clean build across all runners.
