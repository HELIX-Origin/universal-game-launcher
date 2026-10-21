# Security Policy

We take the security of **Universal Game Launcher** seriously — for the application itself and for the privacy of the data you keep on your own machine.

## 🛡️ Supported Versions

| Version | Supported |
| --- | --- |
| 1.4.x | ✅ Active |
| < 1.4 | ❌ No longer supported |

Only the latest release receives security fixes. See the [Releases page](https://github.com/HELIX-Origin/universal-game-launcher/releases) for the current version.

## 🚨 Reporting a Vulnerability

**Do not open a public issue** for a security vulnerability.

Report privately using GitHub's **private vulnerability reporting**:

- Repository → **Security** → **Report a vulnerability**, or
- Directly: `https://github.com/HELIX-Origin/universal-game-launcher/security`

Please include:

1. The affected version(s) and operating system platform.
2. A clear description of the vulnerability.
3. Minimal steps to reproduce.
4. Impact assessment and — if you have one — a suggested fix.

We aim to acknowledge reports within **5 business days** and to ship a fix for confirmed issues in a reasonable window depending on severity. Security fixes are noted in [CHANGELOG.md](./CHANGELOG.md).

## 🎯 Scope

In scope:
- Code execution, path traversal, or arbitrary file writes via the desktop client, installers, or plugins.
- Exposure, logging, or insecure storage of user-provided API credentials (Steam Web API, SteamGridDB, Twitch/IGDB).
- Privilege escalation or IPC capabilities escape beyond the Tauri main window.

Out of scope:
- Issues requiring physical machine access or pre-existing administrator root compromise.
- Denial of service via local process killing.
- Upstream vulnerabilities in official storefront APIs or third-party web endpoints.

## 🔒 Credential & Data Hygiene

Universal Game Launcher operates on a **local-only, privacy-first model**:
- Credentials and API keys you provide are stored exclusively on your local machine in SQLite (`database.sqlite`) or configuration files and transmitted strictly to the respective official APIs.
- No telemetry, analytics, or tracking data is sent to HELIX Origin servers.
- The app never reads, modifies, or backs up external launcher proprietary configuration files.
