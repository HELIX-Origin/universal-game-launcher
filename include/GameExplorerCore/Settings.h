#pragma once

#include "Types.h"
#include <string>

namespace GameExplorer {

struct AppSettings {
    LayoutMode layoutMode{ LayoutMode::CoverArt };
    
    // Theme preference (0 = System, 1 = Light, 2 = Dark)
    uint32_t themeMode{ 0 };
    
    // Storefront scanner toggles
    bool enableSteamScanning{ true };
    bool enableEpicScanning{ true };
    bool enableGOGScanning{ true };
    bool enableBattleNetScanning{ true };
    bool enableEAAppScanning{ true };
    bool enableXboxScanning{ true };
    
    // API Keys
    std::wstring steamGridDbApiKey;
    std::wstring igdbClientId;
    std::wstring igdbClientSecret;
    
    // Cloud sync
    bool enableOneDriveBackup{ false };
};

class SettingsManager {
public:
    [[nodiscard]] static AppSettings Load();
    static bool Save(const AppSettings& settings);
    
    [[nodiscard]] static LayoutMode GetLayoutMode();
    static bool SetLayoutMode(LayoutMode mode);
};

} // namespace GameExplorer
