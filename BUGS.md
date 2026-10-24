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

### [windows-game-explorer] BUG-1 — File Explorer 0xc00000fd Stack Overflow Crash on Games Navigation & Generic Shortcuts Reversion
- **Project Identifier:** `[windows-game-explorer]`
- **Repository Path:** `projects/windows-game-explorer`
- **Severity**: 🔴 Critical (explorer.exe stack overflow crash & view degradation)
- **Status**: 🚧 fix applied / verifying (two-section `ICategoryProvider`/`ICategorizer`, `IThumbnailProvider`, `IPropertyStore`, and `FOLDERTYPEID_Games` wired)
- **Scope:** `src/shell/ShellFolder.cpp`, `src/shell/CategoryProvider.cpp`, `src/shell/GameItemUI.cpp`, `src/shell/Registration.cpp`
- **Symptom:** Clicking the "Games" item in the Windows 11 File Explorer navigation pane previously triggered an `0xc00000fd` (`STATUS_STACK_OVERFLOW`) in `ntdll.dll`, and vanilla `SHCreateShellFolderView` (`CDefView`) without categorization degraded the view into a generic shortcuts folder.
- **Root Cause Analysis**:
  1. On modern Windows 11 (Build 22000–26100+), File Explorer uses WinUI 3 XAML Islands (`DesktopWindowXamlSource`) with tabbed navigation frames.
  2. Legacy `IShellView` implementations triggered recursive activation ping-pong loops (`UIActivate` <-> `OnViewWindowActive` <-> `SetFocus`) and synchronous WIC decoding on the STA thread.
  3. Default `CDefView` without `ICategoryProvider`, `IThumbnailProvider`, and `IPropertyStore` did not provide the authentic two-section Game Explorer layout (`▾ Game Providers` and `▾ Games`) or vertical box art.
- **Applied Fix**:
  1. Registered `FOLDERTYPEID_Games` (`{b689b0d0-76d3-4cbb-87f7-585d0e0ce070}`) with `GroupBy = System.ItemTypeText` and `LogicalViewMode = 3`.
  2. Implemented `GameCategoryProvider` (`ICategoryProvider`) and `GameCategorizer` (`ICategorizer`) grouping items into `▾ Game Providers` (`itemKind = 0`) and `▾ Games` (`itemKind = 1`).
  3. Implemented `IThumbnailProvider`, `IShellItemImageFactory`, and `IPropertyStore` on `GameItemUI` to render vertical ~2:3 box art for games and storefront client icons for providers.

---

## ✅ Resolved Bugs & Historical Fixes

- *Legacy Tauri v2 issues pruned following the v2.0.0 Native C++20 Windows Game Explorer re-architecture.*
