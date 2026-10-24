#pragma once

#include "Models.h"
#include <windows.h>
#include <string>

namespace GameExplorer {

class PlatformLauncher {
public:
    // Launches the given game enforcing platform-specific DRM client executable rules
    static bool Launch(HWND hwndOwner, const GameItem& game);

    // Platform launcher executable discovery
    static std::wstring GetSteamExecutablePath();
    static std::wstring GetEpicExecutablePath();
    static std::wstring GetGOGGalaxyExecutablePath();
    static std::wstring GetBattleNetExecutablePath();
    static std::wstring GetUbisoftExecutablePath();
    static std::wstring GetEAAppExecutablePath();
    static std::wstring GetItchExecutablePath();
};

} // namespace GameExplorer
