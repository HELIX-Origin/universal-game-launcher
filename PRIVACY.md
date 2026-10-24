# Privacy Policy

**Windows Game Explorer** is committed to protecting your privacy. This policy explains what data the application touches, where it goes, and how it is stored.

## 🔒 Summary: 100% Local-Only, Zero Telemetry, Zero External Scrapers

- **No telemetry**: Windows Game Explorer contains zero user analytics, diagnostic trackers, or remote telemetry.
- **No external metadata scrapers**: Windows Game Explorer does not transmit your game library to third-party scraping services. It reads metadata and box art directly from the storefront manifests already installed on your local drives.
- **Local persistence**: Your library settings and per-game metadata overrides (`%USERPROFILE%\Game Explorer\metadata\<appId>.json`) are stored exclusively on your device.
- **No HELIX servers**: We run no central servers that log your gaming habits, installed games, or IP address.

## 🛑 Third-Party Store Isolation

Windows Game Explorer operates as a non-invasive local shell integration:
- It never inspects, captures, or transmits passwords, payment information, or authentication cookies from storefront accounts.
- It never modifies proprietary storefront files (`shortcuts.vdf`, `appmanifest_*.acf`, etc.).
- Game launches delegate directly to the respective official platform client or local executable.

## 🗑️ Data Retention & Deletion

Because all data lives on your device:
- You can unregister the shell integration at any time via `GameExplorerConfig.exe` or `regsvr32 /u GameExplorerShell.dll`.
- Uninstalling the application or deleting `%USERPROFILE%\Game Explorer` completely removes all stored user overrides and settings.
