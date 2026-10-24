# Windows Game Explorer — Task Checklist & Workstream Tracking

> 📋 **Living Source of Truth**: Actionable task ledger for **Windows Game Explorer** (`projects/windows-game-explorer`).
> Statuses: ⬜ backlog · 🚧 in progress · ✅ done (tracked as `- [ ]` and `- [x]` checkboxes).
> High-level direction lives in [`ROADMAP.md`](./ROADMAP.md), active sprints in [`PLAN.md`](./PLAN.md), issues in [`BUGS.md`](./BUGS.md).

---

## 📜 Tracking Rules

- **100% Active Direction Accuracy**: Task items and checklists must reflect the current state of Windows Game Explorer with zero contradictions.
- **Normalized Directives**: Rewrite user directives into clean, self-contained engineering requirements.
- **Done Moves Down**: Completed items are marked with `- [x]` and collapsed to minimal one-line entries under completed workstreams.
- **Zero-Warning Gate**: All builds and tests must pass with 0 errors and 0 warnings (`MSVC /W4 /WX` warnings treated as errors).
- **Game Library Experience Invariant**: Must provide an authentic, rich game library (two-section categorization, vertical box art, metadata properties); generic shortcuts folder degradation is strictly rejected.

---

## 🔥 Active Workstreams

### 🎯 [windows-game-explorer] Workstream W.54: Windows Game Explorer Shell Extension & Lifecycle Management (v2.0.0)
- **Project Identifier:** `[windows-game-explorer]`
- **Repository Path:** `projects/windows-game-explorer`
- **Status:** 🚧 In progress (v2.0.0 native C++20 architecture).
- **Target Component:** Native C++20 Win32 Shell Extension & Companion Apps (`src/shell/`, `src/config/`, `src/manager/`, `src/core/`).

**Locked user directives (normalized by domain):**
- **Project Rebrand to Windows Game Explorer**: Formally name and brand the project **Windows Game Explorer** (`projects/windows-game-explorer`) across all rules, skills, ledgers, CMake targets, and repository headers, removing all legacy Tauri v2 references.
- **Authentic Windows Game Explorer Revival (No Generic Shortcuts Folders)**: Recreate and modernize the authentic Windows 7/Vista Game Explorer (`shell:Games`, `FOLDERTYPEID_Games`) directly inside Windows 11 File Explorer.
  - **Two-Section Categorization**: Separate items into two collapsible groups via `ICategoryProvider` and `ICategorizer` (`PKEY_ItemTypeText`): `▾ Game Providers (N)` for installed storefront clients and `▾ Games (N)` for installed game titles.
  - **Main View**: Rich game representations—tall ~2:3 box art tiles or grouped details list with developer/publisher. Falling back to a generic shortcuts folder (`CDefView` file rows without categorization and box art) is strictly prohibited.
  - **Bottom / Side Property Details**: Game metadata (Publisher, Developer, Release date, Rating, Size) populated via `IPropertyStore`.
- **Storefront-First Metadata Model (No External Scrapers)**: External third-party metadata scrapers (IGDB, SteamGridDB, VNDB) are removed. Installed storefront providers (Steam, Epic Games, GOG Galaxy, Battle.net, itch.io) are used directly as the sole source of truth for their games' metadata and box art.
- **Core Architecture & Triple-Component OS Integration**:
  - **1. In-Shell Primary App (`GameExplorerShell.dll`)**: Virtual Shell Namespace Extension (`IShellFolder2`, `IPersistFolder2`, `IShellView` hosting an in-process C++/WinRT WinUI XAML Island via `DesktopWindowXamlSource`, `IThumbnailProvider`, `IShellItemImageFactory`, `IPropertyStore`, `IExtractIconW`, `IContextMenu`) pinned in the File Explorer left navigation pane alongside Gallery and Home.
  - **2. Integration Configuration GUI (`GameExplorerConfig.exe`)**: Standalone lightweight Win32 configuration utility dedicated strictly to configuring the shell integration (enable/disable namespace registration, default WinUI XAML layout preference, and OneDrive cloud sync preferences, with a button to launch `GameExplorerManager.exe`). Storefront discovery is 100% automatic with no manual storefront toggles.
  - **3. Game Explorer Manager GUI (`GameExplorerManager.exe`)**: Separate, dedicated Win32 GUI application alongside `GameExplorerConfig.exe` reviving the classic Game Explorer Manager (GEM) utility for managing the games in the library, adding local/custom games (`.exe`, `.lnk`, `.bat`, ROMs), picking box art/icons, and editing game metadata (`%USERPROFILE%\Game Explorer\metadata\<appId>.json`).
- **Zero Configuration Dependency Invariant**: The File Explorer shell extension (`GameExplorerShell.dll`) is 100% independent of `GameExplorerConfig.exe` and `GameExplorerManager.exe` running. Direct in-memory scanning operates entirely in-process.
- **Native Windows 11 Light & Dark UI Styles**: Dynamically detects `AppsUseLightTheme`, rendering authentic dark gray (`RGB(32, 32, 32)` dark / `RGB(255, 255, 255)` light), system accent highlights, and themed Win32 controls.
- **Execution & Cloud Sync**: Platform-native game launching invoking client executables directly in the launch path (`steam.exe -applaunch <appId>`, etc.) with direct execution for DRM-free titles. Decentralized metadata backup leveraging the local OneDrive folder.

**Implementation checklist:**
- [x] **[windows-game-explorer] 1. Shell Integration & Core Architecture (Pure Native Windows Execution)**:
  - [x] `[windows-game-explorer]` Generate unique GUID/CLSID (`CLSID_GameExplorer`, `CLSID_GameCategorizer`); implement COM registration (`DllRegisterServer`/`DllUnregisterServer`) mapping shell namespace node beneath Windows Registry (`NameSpace`, `FolderTypes`, and navigation tree pinning as a "Games" tab).
  - [x] `[windows-game-explorer]` Implement `IShellFolder2` and `IPersistFolder2` COM interfaces for virtual shell root recognized by File Explorer.
  - [x] `[windows-game-explorer]` Authentic Windows Games icon integration: embed the classic Windows `imageres.dll` #14 16-layer multi-res `.ico` (16x16 to 256x256 with full alpha transparency) across `GameExplorerShell.dll`, `GameExplorerConfig.exe`, `GameExplorerManager.exe`, and the `Library Overview` sidepane.
  - [x] `[windows-game-explorer]` Windows 11 Light/Dark theme rendering with double-buffering, authentic palettes, Segoe UI Variable typography, and system locale (`GetUserDefaultUILanguage` / `GetUserDefaultLocaleName`) localization across all three surfaces (`GameExplorerShell.dll`, `GameExplorerConfig.exe`, and `GameExplorerManager.exe`).
- [x] **[windows-game-explorer] 2. Multi-Store & Multi-Drive Storefront Indexing Engine**:
  - [x] `[windows-game-explorer]` Enumerate all mounted drives (fixed and removable) via `GetLogicalDriveStringsW` with trailing root separator preserved (`"C:\\"`, `"E:\\"`).
  - [x] `[windows-game-explorer]` Steam parser across all library folders (`libraryfolders.vdf`, `appmanifest_*.acf`) with VDF backslash unescaping, local grid art discovery (`appcache/librarycache/<appId>/library_600x900.jpg`), and public keyless Steam Store API (`appdetails`) metadata enrichment (developer, publisher, ESRB/age rating, release date, genres, synopsis).
  - [x] `[windows-game-explorer]` Itch.io reader via `%APPDATA%\itch\db\butler.db` (`caves` + `games` + `users` join for developer, synopsis, release date, playtime, disk size, and `.gif`/`.png`/`.jpg` cover caching) and multi-drive scanning.
  - [x] `[windows-game-explorer]` Xbox / Microsoft Store discovery via `HKCU\...\AppModel\Repository\Packages`, `.GamingRoot` drive headers, and `GamingServices`, plus Epic Games, GOG GALAXY, Battle.net, Ubisoft Connect, EA App, Amazon Games, Legacy Games, Jagex Launcher, XIVLauncher, Square Enix (FFXIV), Guild Wars 2, Rockstar Games Launcher, Riot Client, HoYoPlay, Paradox Launcher, and ES-DE / RetroArch.
  - [x] `[windows-game-explorer]` DRM-aware client launching: `steam.exe -applaunch` for Steam DRM, `shell:AppsFolder` activation for Xbox/MSIX, client launch for Epic/GOG/Battle.net/Jagex/XIVLauncher, and direct execution for DRM-free itch.io/standalone games.
- [x] **[windows-game-explorer] 3. Storefront-First Metadata & Native Shell Categorization (No External Scrapers)**:
  - [x] `[windows-game-explorer]` Direct Storefront Data Invariant: Eliminate external scrapers (IGDB, VNDB, SteamGridDB); use installed storefront providers directly as the sole source of truth for their metadata and box art.
  - [x] `[windows-game-explorer]` Harvest local Steam 600×900 vertical box art and hero banners directly from disk with ~5ms scan time.
  - [x] `[windows-game-explorer]` Implement `ICategoryProvider` and `ICategorizer` dividing File Explorer into two authentic collapsible sections:
    - [x] `▾ Game Providers (N)`: Installed storefront clients (Steam, Epic Games, GOG GALAXY, Battle.net, Ubisoft Connect, EA App, itch.io).
    - [x] `▾ Games (N)`: Installed game titles with rich vertical box art and properties.
  - [x] `[windows-game-explorer]` Implement `IThumbnailProvider` and `IShellItemImageFactory` delivering ~2:3 vertical cover art for games and authentic provider logos for storefronts.
  - [x] `[windows-game-explorer]` Implement `IPropertyStore` mapping `PKEY_ItemTypeText` ("Game Providers" vs "Games"), `PKEY_ItemNameDisplay`, `PKEY_Company`, `PKEY_Author`, `PKEY_Rating`, and `PKEY_Size`.
- [x] **[windows-game-explorer] 4. Game Explorer Manager GUI Application (`GameExplorerManager.exe`)**:
  - [x] `[windows-game-explorer]` Revival of classic Game Explorer Manager (GEM) utility as a dedicated Win32 management companion (`src/manager/ManagerMain.cpp`).
  - [x] `[windows-game-explorer]` Native custom game registration: add local executables (`.exe`, `.bat`, `.cmd`, `.lnk`, ROMs) via standard Win32 file picker.
  - [x] `[windows-game-explorer]` Rich box art selector: choose tall box art (~2:3 case style), wide hero banners, and app icons with preview canvas.
  - [x] `[windows-game-explorer]` Metadata editor: edit title, developer, publisher, release date, genre, box rating (ESRB/PEGI/USK), synopsis, command-line arguments, and working directory.
  - [x] `[windows-game-explorer]` Per-game persistence: save clean, portable definitions to `%USERPROFILE%\Game Explorer\metadata\<appId>.json`.
  - [x] `[windows-game-explorer]` Library management: customize or remove local game entries and trigger immediate File Explorer namespace refresh.
- [x] **[windows-game-explorer] 5. Integration Configuration Utility Updates (`GameExplorerConfig.exe`)**:
  - [x] `[windows-game-explorer]` Remove legacy external scraper API key fields (IGDB, SteamGridDB) and unneeded storefront provider toggles from `ConfigMain.cpp` (storefront discovery is 100% automatic).
  - [x] `[windows-game-explorer]` Focus `GameExplorerConfig.exe` strictly on shell integration enable/disable, default WinUI XAML layout mode preference, and OneDrive cloud sync, with a button to launch `GameExplorerManager.exe`.
- [x] **[windows-game-explorer] 6. Quality Verification, Native Build Tooling & Upstream Sync**:
  - [x] `[windows-game-explorer]` Complete project rebranding from `universal-game-launcher` to `windows-game-explorer` across directory name, CMake, docs, and agent files.
  - [x] `[windows-game-explorer]` Design and implement a native CMake/PowerShell/C++ build, test, and signing runner (`scripts/`) replacing the retired legacy `.mjs` Tauri v2 scripts.
  - [x] `[windows-game-explorer]` Enforce zero compiler warnings (`MSVC /W4 /WX` warnings as errors) and 100% unit test pass rate (`GameExplorerTests.exe`).
  - [x] `[windows-game-explorer]` Commit and push current progress to upstream via a new branch and rename upstream repository to `windows-game-explorer`.

---

## ✅ Completed Workstreams

- *Historical pre-v2.0.0 workstreams are recorded in [`CHANGELOG.md`](./CHANGELOG.md).*
