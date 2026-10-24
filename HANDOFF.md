# HANDOFF.md — Windows Game Explorer Session Handoff

**Last updated:** 2026-10-23
**Current session status:** W.54 active · Windows Game Explorer Rebrand & Two-Section Shell Categorization Completed · Syncing Progress to Remote Branch
**Project:** windows-game-explorer (Windows Game Explorer)
**Root:** projects/windows-game-explorer

> Permanent, git-tracked session-state ledger for `windows-game-explorer`. Maintained **incrementally** — refresh at every
> workstream state change, fully rewrite at session end. New session: read this file right after `AGENTS.md`,
> migrate every item into `ROADMAP.md` / `PLAN.md` / `TODO.md` / `BUGS.md`, then **refresh this file**.
> **Never delete it.**

---

## 1) [windows-game-explorer] W.54: Windows Game Explorer Shell Extension & Lifecycle Management (Active, v2.0.0)

### Authentic Windows Game Explorer Architecture (Windows 7 Revival)
The project's definitive vision is recreating the classic **Windows 7/Vista Game Explorer** (`shell:Games`, `FOLDERTYPEID_Games`) inside Windows 11 through a triple-component native C++20 architecture:
- **`GameExplorerShell.dll` (In-Shell Primary Extension)**:
  - **Two-Section Categorization**: Uses `ICategoryProvider` and `ICategorizer` (`PKEY_ItemTypeText`) to display two collapsible groups inside File Explorer:
    1. `▾ Game Providers (N)`: Installed storefront clients (Steam, Epic Games, GOG GALAXY, Battle.net, Ubisoft Connect, EA App, itch.io).
    2. `▾ Games (N)`: Installed game titles with vertical ~2:3 box art (`IThumbnailProvider` / `IShellItemImageFactory`) and rich properties (`IPropertyStore`).
  - **Storefront-First Discovery (No External Scrapers)**: Automatically discovers installed titles and harvests metadata/box art directly from installed storefronts on disk without third-party scrapers (IGDB, SteamGridDB, VNDB).
- **`GameExplorerConfig.exe` (Integration Configuration Utility)**:
  - Dedicated strictly to configuring the shell integration (enable/disable namespace registration, layout mode preference, storefront integration toggles, and OneDrive cloud sync).
- **`GameExplorerManager.exe` (Game Explorer Manager GUI)**:
  - Separate Win32 GUI application alongside `GameExplorerConfig.exe`, reviving the classic Game Explorer Manager (GEM) utility for managing the games in the library, adding local/custom games (`.exe`, `.lnk`, `.bat`, ROMs), picking box art/icons, and editing game metadata (`%USERPROFILE%\Game Explorer\metadata\<appId>.json`).

### Delivered in This Session

1. **Complete Project Rebranding (`windows-game-explorer` / `Windows Game Explorer`)**:
   - Renamed local project directory from `projects/universal-game-launcher` to `projects/windows-game-explorer`.
   - Renamed global skill `.agents/skills/universal-game-launcher.md` to `.agents/skills/windows-game-explorer.md`.
   - Updated `CMakeLists.txt` (`project(WindowsGameExplorer VERSION 2.0.0 ...)`), community-health files, tracking ledgers, and agent playbooks to eliminate legacy `universal-game-launcher` and Tauri v2 references.
   - Deleted obsolete legacy Tauri v2 `.mjs` scripts, assets, and log files.

2. **Two-Section Native Shell Categorization (`ICategoryProvider` & `ICategorizer`)**:
   - Implemented `GameCategoryProvider` and `GameCategorizer` (`src/shell/CategoryProvider.cpp`) grouping items by `PKEY_ItemTypeText` into `▾ Game Providers` (`Category 0`) and `▾ Games` (`Category 1`).
   - Updated `ShellFolder::EnumObjects` (`src/shell/ShellFolder.cpp`) to discover installed storefront providers (`itemKind = 0`) followed by installed games (`itemKind = 1`).
   - Wired `GameFolderViewCB` (`SFVM_SETISFV`) to invoke `IFolderView2::SetGroupBy(PKEY_ItemTypeText, TRUE)` and configured `FOLDERTYPEID_Games` in `Registration.cpp`.
   - Added `ProviderContextMenu` and updated `GameItemUI` (`IThumbnailProvider`, `IShellItemImageFactory`, `IPropertyStore`, `IExtractIconW`) to support both storefront providers and games.
   - Updated `StorefrontScanner::LoadPerGameMetadata` to apply user overrides saved in `%USERPROFILE%\Game Explorer\metadata\<appId>.json`.

---

## 2) Active Build & Test Verification

- **Compiler**: MSVC 19.51 / CMake 4.3.1 (Visual Studio 2026 BuildTools)
- **Compilation Gate**: `/W4 /WX /permissive- /utf-8` enforced across all targets (`sqlite3`, `GameExplorerCore`, `GameExplorerConfig`, `GameExplorerShell`, `GameExplorerTests`).
- **Result**: 0 compiler warnings, 0 linker warnings, 0 errors.
- **Unit Tests**: `GameExplorerTests.exe` -> 9/9 passed.

---

## 3) Next Immediate Priorities

1. **Sync Current Progress to Upstream & Rename Repository**:
   - Commit and push current state to upstream via a new branch (`v2.0-windows-game-explorer`) without creating a release yet.
   - Rename upstream GitHub repository to `windows-game-explorer` and update local git remote URL.
2. **Finish `GameExplorerConfig.exe` Cleanup & Implement `GameExplorerManager.exe`**:
   - Strip legacy external scraper API key inputs from `GameExplorerConfig.exe` (`src/config/ConfigMain.cpp`).
   - Implement `GameExplorerManager.exe` (`src/manager/ManagerMain.cpp`) as a separate Win32 GUI application for managing library games and adding local games.
