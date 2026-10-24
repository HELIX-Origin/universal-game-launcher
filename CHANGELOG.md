# Changelog

All notable changes to **Windows Game Explorer** are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [2.0.0] - 2026-10-24

### Added
- **Native C++20 Windows Game Explorer Architecture (`v2.0.0`)**:
  - `GameExplorerShell.dll`: In-process Windows 11 File Explorer Shell Namespace Extension (`IShellFolder2`, `IPersistFolder2`, `IShellView` hosting a native WinUI XAML Island via `DesktopWindowXamlSource`, `ICategoryProvider`, `ICategorizer`, `IThumbnailProvider`, `IShellItemImageFactory`, `IPropertyStore`, `IExtractIconW`, `IContextMenu`) reviving the classic Windows 7/Vista Game Explorer (`shell:Games`, `FOLDERTYPEID_Games`).
  - **Two-Section Native Categorization**: Collapsible `▾ Installed Providers (N)` and `▾ Games Library (N)` sections inside the File Explorer canvas alongside a persistent right-hand Game Inspector & Library Overview sidepane.
  - **17-Platform Non-Hardcoded Storefront & Launcher Discovery (`StorefrontScanner` & `PlatformLauncher`)**:
    - **Steam** (`libraryfolders.vdf`, `appmanifest_*.acf`, local `appcache/librarycache` 600×900 cover/hero art, and keyless Steam Store API `appdetails` metadata enrichment).
    - **Xbox / Microsoft Store** (`HKCU\...\AppModel\Repository\Packages`, `<Drive>:\.GamingRoot` UTF-16LE header parsing, `AppxManifest.xml` metadata, and full-bleed `Assets\` tile extraction).
    - **Epic Games** (`%PROGRAMDATA%\Epic\EpicGamesLauncher\Data\Manifests\*.item`).
    - **GOG GALAXY** (`HKLM\SOFTWARE\WOW6432Node\GOG.com\Games`).
    - **EA App** (Registry & multi-drive library discovery).
    - **Ubisoft Connect** (`Launcher\Installs` Registry & multi-drive discovery).
    - **Battle.net** (`Uninstall` Registry & multi-drive discovery).
    - **itch.io** (`%APPDATA%\itch\db\butler.db` SQLite `caves` + `games` + `users` join with developer, synopsis, playtime, disk size, and cover caching).
    - **Amazon Games** (`GameInstallInfo.sqlite`).
    - **Legacy Games** (`%APPDATA%\legacy-games-launcher\app-state.json`).
    - **Jagex Launcher / RuneLite** (`%LOCALAPPDATA%\RuneLite` & `%LOCALAPPDATA%\Jagex`).
    - **XIVLauncher** (`%APPDATA%\XIVLauncher\launcherConfigV3.json`) & **Square Enix (FFXIV)**.
    - **Guild Wars 2** (`%APPDATA%\Guild Wars 2\GFXSettings.Gw2-64.exe.xml` & drive discovery).
    - **Rockstar Games Launcher**, **Riot Client** (`%PROGRAMDATA%\Riot Games\Metadata`), **HoYoPlay**, **Paradox Launcher**, and **ES-DE / RetroArch**.
  - **Animated `.gif` & Multi-Format Artwork Pipeline**: Preserves and renders animated `.gif` (`BitmapImage::AutoPlay(true)`), `.png`, `.jpg`/`.jpeg`, `.webp`, `.bmp`, `.ico`, and `.tiff` cover and hero artwork.
  - **Executable Version Resource Enrichment**: Extracts `CompanyName` and `FileDescription` from `.exe` `VS_VERSION_INFO` blocks matched to the user's UI language.
  - **Live Game Install/Uninstall Detection**: Automatically detects newly installed or uninstalled games and metadata edits in real time via `SHChangeNotifyRegister` and a lightweight 5-second manifest state fingerprint check (`ComputeEnvironmentStateHash`).
  - **System Locale Support (`Localization`)**: Automatically adapts UI strings and Steam Store API queries to the active Windows system locale (`GetUserDefaultUILanguage` / `GetUserDefaultLocaleName`).
  - **Authentic Windows Games Icon**: Embedded 16-layer multi-resolution Windows `imageres.dll` #14 Games icon (`16x16` to `256x256` with full alpha transparency) across all three binaries.
  - **`GameExplorerConfig.exe`**: Standalone Win32 shell integration & layout configuration utility with dark/light mode styling and live Explorer notification.
  - **`GameExplorerManager.exe`**: Dedicated Win32 companion reviving the classic Game Explorer Manager (GEM) for adding custom local games, selecting cover art (including animated `.gif`s), and editing per-game metadata (`%USERPROFILE%\Game Explorer\metadata\<appId>.json`).
  - **Native PowerShell Build, Test & Signing Suite**: Added `scripts/build.ps1`, `scripts/test.ps1`, and `scripts/sign.ps1` enforcing `/W4 /WX` zero-warning compilation and automated C++ unit testing (`GameExplorerTests.exe`).

### Changed
- Re-architected and rebranded the repository to **Windows Game Explorer** (`v2.0.0`), replacing the legacy Tauri v2 / Rust / SvelteKit desktop application with a pure native C++20 Win32 Shell Namespace Extension and companion suite.

### Fixed
- Eliminated duplicate Steam entries caused by VDF escaped double backslashes (`\\`) and drive-root path concatenation.
- Fixed `GameExplorerManager.exe` user metadata overrides being overwritten during storefront rescans and wired instant `WM_APP + 42` refresh broadcasts to open File Explorer views.
- Excluded transparent `SplashScreen` overlays when selecting Xbox/AppX cover tiles so full-bleed `LargeTile` and `WideTile` assets are chosen.
- Hid the redundant Desktop namespace icon (`HideDesktopIcons\NewStartPanel = 1` and `HideDesktopIcons\ClassicStartMenu = 1`, matching Windows 11's native `Home` and `Gallery` pinned navigation items) so opening Games never spawns a legacy Windows 10 non-tabbed Explorer frame.

### Removed
- Removed all legacy Tauri v2, Rust, Node.js, and external scraper (IGDB, SteamGridDB, VNDB) dependencies and configuration inputs.

[Unreleased]: https://github.com/HELIX-Origin/windows-game-explorer/compare/v2.0.0...HEAD
[2.0.0]: https://github.com/HELIX-Origin/windows-game-explorer/compare/v1.4.1...v2.0.0
