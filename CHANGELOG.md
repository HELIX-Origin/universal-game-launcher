# Changelog

All notable changes to **Windows Game Explorer** are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- **Native C++20 Windows Game Explorer Architecture (`v2.0.0` Rewrite in Progress)**:
  - `GameExplorerShell.dll`: In-process Windows 11 File Explorer Shell Namespace Extension (`IShellFolder2`, `IPersistFolder2`, `ICategoryProvider`, `ICategorizer`, `IThumbnailProvider`, `IShellItemImageFactory`, `IPropertyStore`, `IExtractIconW`, `IContextMenu`) recreating the classic Windows 7/Vista Game Explorer (`shell:Games`, `FOLDERTYPEID_Games`).
  - Two-section native shell categorization (`▾ Game Providers` and `▾ Games`) grouped by `PKEY_ItemTypeText`.
  - Storefront-first discovery across mounted drives (`StorefrontScanner`) for Steam, Epic Games, GOG Galaxy, Battle.net, Ubisoft Connect, EA App, and itch.io without external third-party scrapers.
  - `GameExplorerConfig.exe`: Standalone Win32 integration configuration utility.
  - `GameExplorerManager.exe`: Companion GUI utility reviving the classic Game Explorer Manager (GEM) for managing library games, adding local games, and editing per-game metadata (`%USERPROFILE%\Game Explorer\metadata\<appId>.json`).

[Unreleased]: https://github.com/HELIX-Origin/windows-game-explorer/commits/main
