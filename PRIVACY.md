# Privacy Policy

**Universal Game Launcher** is committed to protecting your privacy. This policy explains what data the application touches, where it goes, and how it is stored.

## 🔒 Summary: Local-Only, Zero Telemetry

- **No telemetry**: Universal Game Launcher contains zero user analytics, diagnostic trackers, or remote telemetry.
- **Local persistence**: Your library records, custom display names, tags, artwork overrides, and playtime logs are stored exclusively on your device in a local SQLite database (`database.sqlite`) and in-memory cache.
- **No HELIX servers**: We run no central servers that log your gaming habits, installed games, or IP address.

## 🌐 External Services & APIs

Universal Game Launcher connects to external services **only** to fulfill features you explicitly enable:

1. **Official Store APIs (Steam, GOG, Epic, etc.)**: Connects to retrieve owned game lists, descriptions, and official store artwork.
2. **Metadata APIs (HELIX Gaming DB, Libretro, Wikipedia, PCGamingWiki)**: Connects to public, open metadata endpoints to fetch game synopses, developer info, and public box art.
3. **Optional Third-Party Keys (SteamGridDB, IGDB/Twitch)**: If you provide an API key for custom artwork or metadata, that key is stored locally in your configuration file and sent directly to the corresponding provider's official REST endpoint over HTTPS.

## 🛑 Third-Party Store Isolation

Universal Game Launcher operates as an isolated frontend aggregator:
- It never inspects, captures, or transmits passwords, payment information, or authentication cookies from storefront accounts.
- Game installations, launches, and purchases delegate entirely to the respective official platform client or browser.

## 🗑️ Data Retention & Deletion

Because all data lives on your device:
- You can clear your metadata cache at any time via `/settings` (Storage & Cache tab).
- Uninstalling the application or deleting the application data directory completely removes all stored data.
