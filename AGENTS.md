# windows-game-explorer (Windows Game Explorer)

Completely native Windows game library and File Explorer integration — C++20 Win32 application
and Windows Shell Namespace Extension. Recreates the classic Windows 7/Vista Game Explorer (`shell:Games`, `FOLDERTYPEID_Games`)
directly inside Windows 11 File Explorer with two collapsible sections (`▾ Game Providers` and `▾ Games`),
high-resolution box art, rich properties, and automated multi-store discovery. MIT license.

## Architecture at a glance

- **Triple-component native Windows architecture:**
  - **`GameExplorerShell.dll` (In-shell primary extension):** File Explorer Virtual Shell Namespace Extension (`IShellFolder2`, `IPersistFolder2`, `ICategoryProvider`, `ICategorizer`, `IThumbnailProvider`, `IPropertyStore`, `IExtractIconW`, `IContextMenu`) organizing installed storefront providers (`▾ Game Providers`) and installed games (`▾ Games`) with vertical ~2:3 box art and rich metadata.
  - **`GameExplorerConfig.exe` (Integration configuration GUI):** Standalone lightweight Win32 configuration utility dedicated strictly to configuring the shell integration (enable/disable namespace registration, layout mode preference, storefront integration toggles, and OneDrive cloud sync).
  - **`GameExplorerManager.exe` (Game Explorer Manager GUI):** Separate Win32 GUI application alongside `GameExplorerConfig.exe`, reviving the classic Game Explorer Manager (GEM) utility for managing games in the library, adding local/custom games (`.exe`, `.lnk`, `.bat`, ROMs), picking box art/icons, and editing game metadata (`%USERPROFILE%\Game Explorer\metadata\<appId>.json`).
- **Automated storefront-first discovery (no external scrapers):** Automatically indexes games and harvests metadata/artwork directly from installed storefronts on disk (Steam `libraryfolders.vdf`/`*.acf`/`librarycache`, Epic `.item`, GOG Registry/SQLite, Battle.net, EA App, Ubisoft Connect, itch.io) without third-party scrapers (IGDB, SteamGridDB, VNDB).
- **Decentralized sync & user scoping:** User-scoped environment rooted in `%USERPROFILE%\Game Explorer\` with local OneDrive profile backup and sync readiness verification.

## Build & test

| Command / Tool | Purpose |
|---|---|
| `cmake -B build -S .` & `cmake --build build --config Release` | Native C++20 compilation with `/W4 /WX` (warnings as errors) |
| `ctest --test-dir build -C Release --output-on-failure` | Run native C++ unit and integration test suite (`GameExplorerTests.exe`) |
| `regsvr32 build\Release\GameExplorerShell.dll` | Register COM virtual shell namespace extension |
| `regsvr32 /u build\Release\GameExplorerShell.dll` | Unregister COM virtual shell namespace extension |

## Conventions (summary)

- **C++:** C++20 standard; MSVC warning level `/W4` with warnings treated as errors (`/WX`).
- **COM & Shell:** Clean separation between shell presentation interfaces and fast in-memory storefront discovery.
- **Discovery:** Fast, non-blocking disk parsing; never write to or modify proprietary storefront files.
- **Metadata:** Storefront-first metadata truth; per-game overrides managed via `GameExplorerManager.exe` in `%USERPROFILE%\Game Explorer\metadata\<appId>.json`.
- **Isolation:** Multi-user isolation under `%USERPROFILE%\Game Explorer\`.

See `.agents/rules/local-conventions.md` for full conventions and `.agents/rules/verification.md` for quality gates.

## Invariants

MIT license · SemVer releases · zero-warning builds (`/W4 /WX`) · community-health files · non-invasive core.
See `CONTRIBUTING.md` for full contributor guidelines.
