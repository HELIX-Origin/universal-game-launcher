# Windows Game Explorer — Active Sprint Planning

> 🎯 **Living Source of Truth**: Active sprint roadmap, execution architecture, and completed sprint history for **Windows Game Explorer** (`projects/windows-game-explorer`).
> Pair with [`ROADMAP.md`](./ROADMAP.md) (direction), [`TODO.md`](./TODO.md) (task ledger), and [`BUGS.md`](./BUGS.md) (known issues).

---

## 📜 Tracking Rules

- **100% Active Direction Accuracy**: Reflect the current, active direction of Windows Game Explorer with zero contradictions.
- **Active Sprints Only in Top Section**: `## 🎯 Active Sprints` contains only the currently active sprint. Completed sprints move immediately to `## ✅ Completed Sprints` (newest first).
- **Mandatory Quality Gate**: Compilers and linters must pass with zero errors and zero warnings (`MSVC /W4 /WX`, zero warnings treated as errors).
- **Game Library Experience Invariant**: Must provide an authentic, rich game library (two-section categorization, vertical box art, metadata properties); generic shortcuts folder degradation is strictly rejected.

---

## 🎯 Active Sprints

*No active sprint — `v2.0.0` released. Next up: Sprint 33 (`v2.1.0`).*

---

## ⏳ Queued Sprints

### 🚀 Sprint 33: [windows-game-explorer] — Cloud Gaming Streaming Hooks & Gamepad Handheld Mode (deferred to v2.1.0)
- **Project Identifier:** `[windows-game-explorer]`
- **Repository Path:** `projects/windows-game-explorer`
- **Target Objective:** Expand platform launcher hooks to cloud gaming providers (GeForce NOW, Xbox Cloud Gaming) and deliver a controller-friendly Big Picture overlay mode for handheld PCs.

---

## ✅ Completed Sprints

- ✅ **Sprint 32: [windows-game-explorer] — Windows Game Explorer Shell Extension & Lifecycle Management (`v2.0.0`, W.54)**: Delivered `GameExplorerShell.dll` (WinUI XAML Island `IShellView` with two-section `▾ Installed Providers` and `▾ Games Library` categorization, animated `.gif`/`.png`/`.jpg`/`.webp` artwork, and persistent right-hand Inspector & Library Overview sidepane), 17-platform non-hardcoded storefront discovery (including Xbox `AppModel\Repository\Packages` & `.GamingRoot`), live install/uninstall detection, system locale support, authentic Windows `imageres.dll` #14 Games icon, `GameExplorerConfig.exe`, and `GameExplorerManager.exe` (0 warnings `/W4 /WX`, 10/10 unit tests passing).
- *Completed sprints prior to the v2.0.0 Native C++ Windows Game Explorer re-architecture are archived in [`CHANGELOG.md`](./CHANGELOG.md).*
