#include "GameExplorerCore/Settings.h"
#include "GameExplorerCore/Database.h"
#include "GameExplorerCore/Storage.h"

namespace GameExplorer {

AppSettings SettingsManager::Load() {
    AppSettings s;
    auto& db = Database::Instance();
    if (!db.IsOpen()) {
        db.Open(Storage::GetDatabasePath());
    }

    if (auto val = db.GetSetting(L"layout_mode")) {
        s.layoutMode = StringToLayoutMode(*val);
    }
    if (auto val = db.GetSetting(L"theme_mode")) {
        try {
            s.themeMode = static_cast<uint32_t>(std::stoul(*val));
        } catch (...) {}
    }
    if (auto val = db.GetSetting(L"enable_steam")) {
        s.enableSteamScanning = (*val == L"1" || *val == L"true");
    }
    if (auto val = db.GetSetting(L"enable_epic")) {
        s.enableEpicScanning = (*val == L"1" || *val == L"true");
    }
    if (auto val = db.GetSetting(L"enable_gog")) {
        s.enableGOGScanning = (*val == L"1" || *val == L"true");
    }
    if (auto val = db.GetSetting(L"enable_battlenet")) {
        s.enableBattleNetScanning = (*val == L"1" || *val == L"true");
    }
    if (auto val = db.GetSetting(L"enable_ea")) {
        s.enableEAAppScanning = (*val == L"1" || *val == L"true");
    }
    if (auto val = db.GetSetting(L"enable_xbox")) {
        s.enableXboxScanning = (*val == L"1" || *val == L"true");
    }
    if (auto val = db.GetSetting(L"steamgriddb_api_key")) {
        s.steamGridDbApiKey = *val;
    }
    if (auto val = db.GetSetting(L"igdb_client_id")) {
        s.igdbClientId = *val;
    }
    if (auto val = db.GetSetting(L"igdb_client_secret")) {
        s.igdbClientSecret = *val;
    }
    if (auto val = db.GetSetting(L"enable_onedrive_backup")) {
        s.enableOneDriveBackup = (*val == L"1" || *val == L"true");
    }

    return s;
}

bool SettingsManager::Save(const AppSettings& settings) {
    auto& db = Database::Instance();
    if (!db.IsOpen()) {
        if (!db.Open(Storage::GetDatabasePath())) return false;
    }

    db.SetSetting(L"layout_mode", std::wstring(LayoutModeToString(settings.layoutMode)));
    db.SetSetting(L"theme_mode", std::to_wstring(settings.themeMode));
    db.SetSetting(L"enable_steam", settings.enableSteamScanning ? L"1" : L"0");
    db.SetSetting(L"enable_epic", settings.enableEpicScanning ? L"1" : L"0");
    db.SetSetting(L"enable_gog", settings.enableGOGScanning ? L"1" : L"0");
    db.SetSetting(L"enable_battlenet", settings.enableBattleNetScanning ? L"1" : L"0");
    db.SetSetting(L"enable_ea", settings.enableEAAppScanning ? L"1" : L"0");
    db.SetSetting(L"enable_xbox", settings.enableXboxScanning ? L"1" : L"0");
    db.SetSetting(L"steamgriddb_api_key", settings.steamGridDbApiKey);
    db.SetSetting(L"igdb_client_id", settings.igdbClientId);
    db.SetSetting(L"igdb_client_secret", settings.igdbClientSecret);
    db.SetSetting(L"enable_onedrive_backup", settings.enableOneDriveBackup ? L"1" : L"0");

    return true;
}

LayoutMode SettingsManager::GetLayoutMode() {
    auto& db = Database::Instance();
    if (!db.IsOpen()) {
        db.Open(Storage::GetDatabasePath());
    }

    if (auto val = db.GetSetting(L"layout_mode")) {
        return StringToLayoutMode(*val);
    }
    return LayoutMode::CoverArt;
}

bool SettingsManager::SetLayoutMode(LayoutMode mode) {
    auto& db = Database::Instance();
    if (!db.IsOpen()) {
        if (!db.Open(Storage::GetDatabasePath())) return false;
    }

    return db.SetSetting(L"layout_mode", std::wstring(LayoutModeToString(mode)));
}

} // namespace GameExplorer
