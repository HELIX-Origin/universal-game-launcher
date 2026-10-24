# Windows Game Explorer — Startup Background Service Architecture Spike

> Research and architecture spike for Phase 1 of Windows Game Explorer (W.54 / v2.0.0).
> Evaluates startup lifecycle models, directory monitoring, and session scoping.

---

## 1. Requirements & Problem Statement

To deliver a truly native, instant-response Windows experience, the application requires:
1. **Silent Startup Execution:** Starts automatically when Windows boots or user logs in, without popping up a console window or stealing focus.
2. **Automated Directory Monitoring:** Monitors storefront manifest folders (Steam `libraryfolders.vdf`, Epic manifests, GOG SQLite) in real time using `ReadDirectoryChangesW`, keeping the local database current.
3. **OneDrive Cloud Sync Synchronization:** Runs automated backup routines into the user's local OneDrive folder with sync readiness validation.
4. **Instant In-Shell Responsiveness:** When a user clicks "Game Explorer" in the File Explorer sidebar, `IShellView` must read from an already-current in-memory / local SQLite cache without waiting for disk crawlers.
5. **Multi-User Isolation:** Each Windows user account must have its own isolated library rooted under `%USERPROFILE%\Game Explorer\`.

---

## 2. Evaluation of Architectural Options

### Option A: Windows System Service via Service Control Manager (SCM)
- **Mechanism:** Standard Windows Service installed into SCM (`services.msc`).
- **Challenges:**
  - Since Windows Vista, all Windows Services run in **Session 0** under `LocalSystem`, `LocalService`, or `NetworkService`.
  - Session 0 isolation prevents direct access to the logged-in user's `HKCU` registry, `%USERPROFILE%`, `%OneDrive%`, and mapped drives without complex token impersonation via `WTSQueryUserToken` and `DuplicateTokenEx`.
  - Multi-user scenarios become overly complex because a system service must detect user logons/logoffs and spawn impersonated threads for each user.
- **Verdict:** **NOT RECOMMENDED** due to Session 0 isolation and credential/path mismatch with user-scoped storage.

---

### Option B: User-Session Background Agent (`HKCU\...\Run` or Task Scheduler)
- **Mechanism:** Lightweight native Win32 executable (`GameExplorerAgent.exe`) launched at user logon via `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` or a Windows Task Scheduler logon trigger.
- **Characteristics:**
  - Runs with the active user's standard security token.
  - Native access to `%USERPROFILE%`, `%OneDrive%`, and `HKCU` with zero permission hurdles.
  - Spawns as a headless message-only window (`HWND_MESSAGE`) consuming < 5MB of RAM.
  - Multi-user isolation is automatic: each user runs their own instance when logged in.
- **Verdict:** **HIGHLY SUITABLE** for user-scoped background monitoring.

---

### Option C: Pure In-Process Shell Extension (Lazy Scanning Only)
- **Mechanism:** Shell extension DLL (`GameExplorerShell.dll`) loaded into `explorer.exe` via `InProcServer32`. Crawls directories on demand when the user opens the virtual folder.
- **Challenges:**
  - Cold navigation to Game Explorer can stutter if scanning thousands of titles across multiple drives.
  - No background directory watching or scheduled OneDrive cloud backups when File Explorer is closed.
- **Verdict:** **INSUFFICIENT ON ITS OWN** for automated continuous indexing.

---

## 3. Recommended Architecture: Hybrid In-Process Shell + User-Session Agent

The optimal, most native Windows design is a **hybrid dual-component architecture**:

```mermaid
flowchart TD
    subgraph UserSession["Active User Session (Logon)"]
        Agent["GameExplorerAgent.exe<br/>(Lightweight Win32 Background Agent)"]
        Watcher["ReadDirectoryChangesW<br/>(Steam, Epic, GOG manifests)"]
        OneDrive["OneDrive Sync Routine<br/>(%USERPROFILE%/Game Explorer/)"]
        DB[("Local SQLite Database<br/>%USERPROFILE%/Game Explorer/library.db")]
    end

    subgraph Explorer["Windows File Explorer Process (explorer.exe)"]
        ShellDLL["GameExplorerShell.dll<br/>(InProcServer32 COM Shell Extension)"]
        Folder["IShellFolder / IPersistFolder"]
        View["IShellView & Fluent Canvas"]
    end

    Agent --> Watcher
    Watcher --> DB
    Agent --> OneDrive
    Folder --> DB
    View --> DB
    ShellDLL --> Folder
    ShellDLL --> View
```

1. **`GameExplorerAgent.exe` (User-Session Background Service):**
   - Headless Win32 application registered in `HKCU\...\Run`.
   - Uses `ReadDirectoryChangesW` on store folders to catch game installs/uninstalls immediately.
   - Executes periodic OneDrive backup checks.
   - Writes directly to `%USERPROFILE%\Game Explorer\library.db`.
2. **`GameExplorerShell.dll` (InProcServer32 COM Extension):**
   - Registered under CLSID as a Virtual Shell Namespace Extension.
   - When opened in File Explorer, directly reads the pre-indexed SQLite cache.
   - Zero UI blocking, instant display of thousands of games.
