# HANDOFF.md — Universal Game Launcher Session Handoff

**Last updated:** 2026-10-21
**Current session status:** v1.4.1 stable · W.54 v2.0.0 File Explorer Shell Extension & Lifecycle Management + Rebrand to Windows Game Explorer (framework decision recorded) · implementation not yet started
**Project:** universal-game-launcher
**Root:** projects/universal-game-launcher

> Permanent, git-tracked session-state ledger for `universal-game-launcher`. Maintained **incrementally** — refresh at every
> workstream state change, fully rewrite at session end. New session: read this file right after `AGENTS.md`,
> migrate every item into `ROADMAP.md` / `PLAN.md` / `TODO.md` / `BUGS.md`, then **refresh this file**.
> **Never delete it.**

---

## 1) [universal-game-launcher] W.54: File Explorer Shell Extension & Lifecycle Management + Rebrand to Windows Game Explorer (Active, v2.0.0)

### Overview

**Core Architecture & OS Integration**

- **Complete Architecture Pivot**: Rewritten from scratch as a native C++ application utilizing Win32 APIs and Rust to achieve low-overhead system performance and deep OS integration.
- **Classic Shell Rebirth**: Recreates the core workflow of the legacy Windows Games Explorer directly inside the modern Windows 11 File Explorer environment.
- **Virtual Namespace Extension**: Integrates directly into the File Explorer UI layer via a native shell extension to present an automated, highly responsive interface.
- **User-Scoped Environments**: Isolates library databases, metadata stores, and configuration states on a per-user basis to fully support multi-account Windows PCs.

**Library Presentation & User Interface**

- **Visual Cover Grid**: Renders local game collections as a visually uniform, asset-rich art grid built seamlessly into the active File Explorer canvas.
- **Contextual Details Pane**: Displays an in-shell information drawer showing game metadata, media previews, origin indicators, and inline action triggers.
- **Deep Workspace Interactivity**: Enables game launches, file state inspections, and metadata manipulation directly from the active window without an external app.
- **State Dimming Engine**: Modulates tile styling to display uninstalled storefront items as visually distinct or dimmed for instant library scanning.
- **Fluent Sorting & Filtering**: Introduces quick-toggle view structures to slice the collection by platform, genre, install status, or manual tags.

**Platform Support & Custom Imports**

- **Omni-Store Integration**: Delivers robust indexing pipelines for mainstream digital clients alongside niche storefront networks.
- **Launcher Abstraction Layer**: Native compatibility handling for first-party launchers (e.g., Guild Wars 2) and community forks (e.g., XIV Launcher, Legendary).
- **Manual Game Overrides**: Allows manual executable, batch file, or custom path registration to track titles completely outside of digital ecosystems.

**Scrapers, Security, & Sync Data**

- **Universal Metadata Engine**: Built as a native metadata scraping engine to fetch and enforce consistent text descriptions and covers across all titles.
- **User-Owned API Keys**: Puts configuration power back in user hands by enabling personal Web API keys for remote storefronts and asset repositories.
- **Secure Vault Storage**: Implements protected system credential storage to safeguard API tokens and account hashes.
- **Profile Cloud Sync**: Provides decentralized database backups to secure user preferences and custom configurations across systems. The best method is to use the local OneDrive folder for seamless cloud integration and have a check to determine if OneDrive is available/running and properly synced before initiating the backup process.

### Development Phases

**Phase 1**: Shell Integration & Core Architecture (Pure Native Windows Execution)


- **Define Class ID (CLSID) & COM Registration**: Generate a unique GUID for the shell extension. Author the C++ registration logic (DllRegisterServer/DllUnregisterServer) to map the COM server beneath the Windows Registry framework required to inject a virtual folder node into the File Explorer sidebar (NameSpace and FolderDescriptions).
- **Implement IShellFolder and IPersistFolder COM Interfaces**: Write the foundational C++ COM objects required to establish a Virtual Shell Namespace Extension. This allows File Explorer to recognize your custom directory node as a valid system root path rather than a standard physical file path.
- **Implement IShellView and Subclass Content Containers**: Implement the core view window object. When a user navigates to your namespace node, use the Win32 CreateWindowEx API to intercept the creation of the native Explorer list container (SHELLDLL_DefView) and subclass it, allocating a native paint window bounded directly to the active Explorer frame handle (HWND).
- **Build the Native Rust-to-C++ Binding Layer**: Set up a static C-ABI link or low-overhead DLL bridge using Rust (windows-sys/winapi). This allows the C++ shell presentation layer to query your optimized Rust backend database directly for installed game titles, sorting schemas, and metadata structures over local memory rather than network sockets.


**Phase 2**: Multi-Store Installed Application Indexing Engine

- **Steam Integration**: Parse local installation paths via registry keys, read libraryfolders.vdf and individual appmanifest_*.acf files to instantly track local game folders, app IDs, and execution parameters.
- **Epic Games Integration**: Crawl the Epic manifest directory (C:\ProgramData\Epic\EpicGamesLauncher\Data\Manifests\) to parse .item configuration files for active local game paths and launch flags.
- **GOG Galaxy Integration**: Query the Windows Registry (HKLM\SOFTWARE\WOW6432Node\GOG.com\Games) or pull from GOG's local runtime SQLite database to identify games currently on disk.
- **Battle.net & EA App Integration**: Parse local platform cache directories, local configuration databases, and desktop shortcut targets to locate active installations.
- **Xbox App / Microsoft Store (UWP/MSIX)**: Use Windows API commands (Windows.Management.Deployment.PackageManager) to identify locally installed cryptographic app packages belonging to gaming manifests.

**Phase 3**: Manual Addition & Custom Path Injector

- **Manual Game Registration API**: Create a native Rust command module exposing a clean C-ABI that accepts custom filesystem targets (.exe, .lnk, or .bat), registers them under a unique app-generated hash, and generates an internal launch configuration.
- **Custom Working Directory Controls**: Provide structural overrides for manual files that require specific execution flags, startup arguments, or custom environmental variables.
- **Portable / Retro Emulator Mapping**: Allow folder paths to be mapped directly to specific execution frontends (e.g., RetroArch, RPCS3) while appending the game path dynamically as a trailing command-line argument.


**Phase 4**: Universal Metadata & Scraping Infrastructure

- **Build the Universal Scraper Client**: Design a unified Rust client that accepts a title string and normalizes metadata schemas across disparate remote backends.
- **SteamGridDB API Integration**: Connect to SteamGridDB to fetch high-resolution vertical cover grids, horizontal posters, panoramic hero assets, transparent logos, and icon formats.
- **IGDB / Twitch API Integration**: Implement OAuth flows for the Internet Game Database (IGDB) to aggregate structured text data including developers, publishers, explicit release dates, synopsis, genres, and parent game groupings.
- **Local Asset Pipeline**: Build an artwork sanitization routine that converts diverse remote image formats into optimized local formats (like compressed WebP), saving them to a structured local directory (%APPDATA%/your-app/cache/).
- **Disambiguation / Match Resolution UI**: Create a manual override panel in the config app that displays search results with thumbnails when an automated title match is ambiguous, letting users manually pick the correct game profile.

**Phase 5**: Front-End Custom Gallery & Details Side Pane (Native Windows UI)

- **Develop the Native Fluent Grid Canvas**: Build a responsive, visually dense cover art grid using native Windows UI layouts (such as WinUI 3, Direct2D, or standard Win32 Custom Controls) matching Windows 11 Fluent Design principles, scaled for typical local installation limits.
- **Implement Keyboard Navigation & Explorer Shortcuts**: Wire up standard keyboard messaging hooks (WM_KEYDOWN, WM_CHAR) to handle standard native Explorer behavior, including arrow keys for grid item navigation, Enter to execute launch routines, F5 to invalidate the scanner cache, and alphanumeric keys to instantly jump to games alphabetically.
- **Build the Contextual Details Side Panel**: Create a collapsible native container docking to the right-hand margin of the custom view canvas that populates instantly upon grid item selection, pulling cached text and visual assets to display store origin, media links, and a unified play button.
- **Incorporate a Quick Search & Filter Header**: Implement a high-performance filtering bar at the top of the canvas to cull the active memory collection instantly by platform, genre, or custom user tags.

**Phase 6**: Core Configuration & Background Management Utility (Native Win32)

- **Design the Native Configuration Dashboard**: Build a standalone, lightweight Win32 configuration utility window (leveraging native dialog templates, Windows Property Sheets, or WinUI 3) dedicated entirely to global library parameters, background synchronization intervals, and scanner toggles.
- **Store Account/Path Settings Controls**: Create native input forms, directory pickers (IFileDialog), and toggle controls to manage explicit storefront installation directories, enable/disable individual crawler modules, and pass API authentication strings safely.
- **Universal Metadata Search & Manual Overrides**: Implement a search utility within the configuration layout allowing users to manually force-re-scrape any game entry, paste specific artwork resource paths, or bind custom local JPEG/PNG image assets directly to a game profile.
- **Database Integrity Utilities**: Implement cache-clearing routines, local file database backup engines, and an asynchronous validation loop to instantly purge orphaned database records when games are uninstalled externally via third-party storefront clients.

**Phase 7**: Advanced Execution & OS Integration Features

- **Add "Run as Administrator" & Advanced Launch Context Menus**: Integrate a custom right-click context menu layer allowing users to launch games with specific parameter arguments, force compatibility modes, or open the local file directory.
- **Implement a Headless Fallback Execution Layer**: Ensure that clicking the launch button executes background platform APIs silently (like Legendary CLI for Epic or URI shortcuts for Steam) so the game fires up immediately without forcing full storefront windows to steal focus.
- **Develop Global Playtime & Achievement Syncing**: Build background routines that hook into local save paths or web APIs to keep tracking metrics up to date right within the File Explorer detail window.
- **Add Dynamic Cloud-Save Status Badging**: Place subtle overlay icons on the corner of the gallery tiles (like OneDrive sync badges) indicating if local game saves are currently synced, out of date, or backing up.


---

## Tooling & Quality Gate
- **C++ Engine**: MSVC toolchain compiling with C++20 compliance; standard warning level /W4 enforced.
- **CMake**: Ensure proper configuration of build targets, include directories, and linking for both the main executable and companion DLLs.
- **Static Analysis & Linting**: Integrate tools like clang-tidy and include-what-you-use to enforce code quality and catch potential issues early in the development cycle.
- **Testing & Validation**: Establish unit tests, integration tests, and end-to-end validation routines to ensure the stability and correctness of both the core engine and the shell extension components.
- **Packaging**: Combined native MSI/NSIS installer managing registry hooks, the native companion .dll, and the background configuration utility asset binaries.

---

## Quick Resume Checklist (New Session)
1. Read `AGENTS.md`
2. Read this file immediately, then `ROADMAP.md` → `PLAN.md` → `TODO.md` → `BUGS.md`
3. Migrate this file's open items into the active project ledgers, then refresh it.
4. Resume at W.54 Phase 1 (Setting up the C++ MSVC/CMake baseline project and gathering the targeted CLSID GUIDs) in `projects/iniversal-game-launcher`.
