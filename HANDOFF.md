# HANDOFF.md — Windows Game Explorer Session Handoff

**Last updated:** 2026-10-24
**Current session status:** W.54 completed · `v2.0.1` Official Release Published & README Preview Added
**Project:** windows-game-explorer (Windows Game Explorer)
**Root:** projects/windows-game-explorer

> Permanent, git-tracked session-state ledger for `windows-game-explorer`. Maintained **incrementally** — refresh at every
> workstream state change, fully rewrite at session end. New session: read this file right after `AGENTS.md`,
> migrate every item into `ROADMAP.md` / `PLAN.md` / `TODO.md` / `BUGS.md`, then **refresh this file**.
> **Never delete it.**

---

## 1) [windows-game-explorer] W.54: Windows Game Explorer v2.0.0 Architecture & Release (Completed, v2.0.0)

### Authentic Windows Game Explorer Architecture (Windows 7 Revival)
The project's definitive vision is recreating the classic **Windows 7/Vista Game Explorer** (`shell:Games`, `FOLDERTYPEID_Games`) inside Windows 11 through a triple-component native C++20 architecture:
- **`GameExplorerShell.dll` (In-Shell Primary Extension)**:
  - **Two-Section Categorization**: Uses `ICategoryProvider` and `ICategorizer` (`PKEY_ItemTypeText`) to display two collapsible groups inside File Explorer:
    1. `▾ Game Providers (N)`: Installed storefront clients across 17 supported platforms.
    2. `▾ Games (N)`: Installed game titles with vertical ~2:3 box art (`IThumbnailProvider` / `IShellItemImageFactory`) and rich properties (`IPropertyStore`: Title, Platform, Developer, Publisher, Release Year, Genre, Description, Install Path).
  - **Real-Time Installation & Metadata Refresh**: Automatically detects newly installed or uninstalled games across storefronts via `ComputeEnvironmentStateHash` (`WM_TIMER`), `SHChangeNotifyRegister` (`WM_APP + 43`), and inter-process `WM_APP + 42` broadcasts from `GameExplorerManager.exe` and `GameExplorerConfig.exe`.
- **`GameExplorerConfig.exe` (Integration Configuration Utility)**:
  - Native Windows 11 Settings-inspired Win32 GUI with Sidebar Navigation (`Shell Integration`, `Game Providers`, `Cloud Sync & Backup`), custom pill toggle switches, and 17 storefront integration toggles.
- **`GameExplorerManager.exe` (Game Explorer Manager GUI)**:
  - Split-view Win32 GUI utility (`CreateFontIndirectW` DPI-scaled `Segoe UI Variable`, owner-drawn 2:3 cover art list view, right-side inspector with `Gdiplus::Image` preview and multi-frame animated GIF timer playback, persistent user metadata overrides in `%USERPROFILE%\Game Explorer\metadata\<appId>.json`, and custom game `.exe`/`.lnk`/`.bat` registration).

---

## 2) Active Build & Test Verification

- **Compiler**: MSVC 19.51 / CMake 4.3.1 (Visual Studio 2026 BuildTools)
- **Compilation Gate**: `/W4 /WX /permissive- /utf-8` enforced across all targets (`sqlite3`, `GameExplorerCore`, `GameExplorerConfig`, `GameExplorerManager`, `GameExplorerShell`, `GameExplorerTests`).
- **Result**: 0 compiler warnings, 0 linker warnings, 0 errors.
- **Unit Tests**: `GameExplorerTests.exe` -> 11/11 passed (`pwsh -NoProfile -File scripts/test.ps1 -Configuration Release`).

---

## 3) Next Immediate Priorities

1. **Post-v2.0.0 Maintenance**:
   - Tab switching stability and `DesktopWindowXamlSource` teardown verified.
   - Catalog normalization, deduplication, and database synchronization verified with 11 unit tests passing.
