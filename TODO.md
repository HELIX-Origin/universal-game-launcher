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

*No active workstreams — `v2.0.0` complete. Next workstream will open with `v2.1.0`.*

---

## ✅ Completed Workstreams

- ✅ **[windows-game-explorer] Workstream W.54: Windows Game Explorer Shell Extension & Lifecycle Management (`v2.0.0`)** — Delivered `GameExplorerShell.dll` (in-process WinUI XAML Island `IShellView` with two-section `▾ Installed Providers` and `▾ Games Library` categorization, animated `.gif`/`.png`/`.jpg`/`.webp` cover art, and persistent right-hand Inspector & Library Overview sidepane), 17-platform non-hardcoded storefront discovery (including Xbox `AppModel\Repository\Packages` & `.GamingRoot`), live install/uninstall detection, system locale support, authentic Windows `imageres.dll` #14 Games icon, `GameExplorerConfig.exe`, and `GameExplorerManager.exe` (full details in [`CHANGELOG.md`](./CHANGELOG.md)).
- *Historical pre-v2.0.0 workstreams are recorded in [`CHANGELOG.md`](./CHANGELOG.md).*
