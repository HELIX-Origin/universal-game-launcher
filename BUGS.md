# Windows Game Explorer — 🐛 Bug Tracker

> 🐛 **Living Source of Truth**: Active tracking ledger for open bugs, quirks, external limitations, and resolution history for **Windows Game Explorer** (`projects/windows-game-explorer`).
> Pair with [`ROADMAP.md`](./ROADMAP.md) (direction), [`PLAN.md`](./PLAN.md) (sprints), and [`TODO.md`](./TODO.md) (tasks).

---

## 📜 Tracking Rules

- **100% Active Direction Accuracy**: Reflect the current, active state of Windows Game Explorer with zero contradictions.
- **Explicit Project Tagging**: Every entry is tagged with `[windows-game-explorer]`.
- **Zero-Warning Gate**: Compiler warnings are treated as errors and resolved before release.
- **Game Library Experience Invariant**: Must provide an authentic, rich game library (two-section categorization, vertical box art, metadata properties); generic shortcuts folder degradation is strictly rejected.

---

## 🚫 Known Quirks & External Limitations

- 🔒 **[windows-game-explorer] Zero External File Writes (Native Isolation Invariant)**: Windows Game Explorer never writes to, modifies, or backs up external launcher files (`shortcuts.vdf`, `cloud-storage-namespace-1.json`, or `appmanifest.acf`). Game records, metadata, and user overrides persist exclusively in `%USERPROFILE%\Game Explorer\`.
- 🔒 **[windows-game-explorer] Native Launcher Delegation**: Game launches always delegate strictly through official platform clients (`steam.exe -applaunch <appId>`, etc.) to ensure DRM validation, user profiles, and overlays remain intact.
- 🔒 **[windows-game-explorer] Storefront-First Discovery (No External Scrapers)**: Unlike Microsoft's legacy Game Explorer that required manual Game Definition Files (GDF XML) and defunct online servers, Windows Game Explorer automatically discovers installed titles and harvests metadata/artwork directly from installed storefronts on disk without third-party scrapers.

---

## ⚠️ Active & Open Bugs

*No open bugs.*

---

## ✅ Resolved Bugs & Historical Fixes

- ✅ **[windows-game-explorer] BUG-1 — File Explorer 0xc00000fd Stack Overflow Crash on Games Navigation & Generic Shortcuts Reversion**: Resolved in `v2.0.0` via in-process WinUI XAML Island (`DesktopWindowXamlSource`), two-section categorization (`▾ Installed Providers` and `▾ Games Library`), `IThumbnailProvider`, `IShellItemImageFactory`, and `IPropertyStore`.
- ✅ **[windows-game-explorer] BUG-2 — Duplicate Steam Entries, Missing Itch.io Cover Art, and Manager Metadata Override Persistence**: Resolved in `v2.0.0` by unescaping VDF double backslashes, normalizing drive root separators, preserving `.gif`/`.png`/`.jpg`/`.webp` artwork, calling `LoadPerGameMetadata` prior to saving in all scanners, and broadcasting `WM_APP + 42` refresh messages to open File Explorer views.
- ✅ **[windows-game-explorer] BUG-3 — Desktop Namespace Icon Opening Legacy Windows 10 Ribbon Explorer UI Without Tabs**: Resolved in `v2.0.0` by setting `HideDesktopIcons\NewStartPanel` and `HideDesktopIcons\ClassicStartMenu` to `1` in `Registration.cpp` (matching Windows 11's native `Home` and `Gallery` pinned navigation items) so the redundant Desktop icon is removed and File Explorer always uses Windows 11's native Tabbed UI.
- *Legacy Tauri v2 issues pruned following the v2.0.0 Native C++20 Windows Game Explorer re-architecture.*
