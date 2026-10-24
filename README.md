<div align="center">

# Windows Game Explorer

<p align="center">
  <a href="https://github.com/HELIX-Origin/windows-game-explorer/releases"><img src="https://img.shields.io/github/v/release/HELIX-Origin/windows-game-explorer?include_prereleases&label=Release&color=blue" alt="Release" height="20" /></a>
  <a href="https://opensource.org/licenses/MIT"><img src="https://img.shields.io/badge/License-MIT-blue.svg" alt="License: MIT" height="20" /></a>
  <img src="https://img.shields.io/badge/Platform-Windows%2011-0078D4?logo=windows" alt="Platform: Windows 11" height="20" />
  <img src="https://img.shields.io/badge/C%2B%2B-20-blue.svg" alt="C++20" height="20" />
</p>

**A native C++ Windows Game Explorer reviving the classic Windows 7/Vista Game Explorer directly inside Windows 11 File Explorer.**

Organize, browse, and launch your entire multi-platform game library with high-resolution box art, rich preview pane, and automated storefront detection without scraping or credentials.

<br />

<img src="./assets/preview.png" alt="Windows Game Explorer Preview" width="100%" />

</div>

---

## ⚡ Quick Start

### Prerequisites
- Windows 11 (Build 22000+)
- Visual Studio 2022 / Build Tools (C++20 MSVC v143+)
- CMake 3.25+

### Building Locally
```powershell
cmake -B build -S .
cmake --build build --config Release
```

### Running Tests
```powershell
.\build\tests\Release\GameExplorerTests.exe
```

---

## 🤝 Community & Governance

- [Contributing](./CONTRIBUTING.md) · [Code of Conduct](./CODE_OF_CONDUCT.md) · [Support](./SUPPORT.md)
- [Security Policy](./SECURITY.md) · [Privacy Policy](./PRIVACY.md) · [Terms of Service](./TOS.md)
- [Changelog](./CHANGELOG.md) · [Governance](./GOVERNANCE.md) · [Citation](./CITATION.cff) · [License](./LICENSE)

---

## ⚖️ License

[MIT](LICENSE) — Copyright (c) 2026, HELIX Origin.
