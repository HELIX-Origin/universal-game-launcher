#include "GameExplorerCore/PlatformLauncher.h"
#include "GameExplorerCore/StorefrontScanner.h"
#include <filesystem>
#include <shellapi.h>

namespace fs = std::filesystem;

namespace GameExplorer {

namespace {

std::wstring ReadRegistryString(HKEY hRoot, const wchar_t* subKey, const wchar_t* valueName) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(hRoot, subKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return L"";
    }

    wchar_t buffer[MAX_PATH] = { 0 };
    DWORD bufSize = sizeof(buffer);
    DWORD type = 0;
    const LSTATUS status = RegQueryValueExW(hKey, valueName, nullptr, &type, reinterpret_cast<BYTE*>(buffer), &bufSize);
    RegCloseKey(hKey);

    if (status == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ)) {
        return std::wstring(buffer);
    }
    return L"";
}

std::wstring CleanAppId(const std::wstring& rawId, const std::wstring& prefix) {
    if (rawId.rfind(prefix, 0) == 0) {
        return rawId.substr(prefix.size());
    }
    return rawId;
}

} // namespace

std::wstring PlatformLauncher::GetSteamExecutablePath() {
    std::wstring path = ReadRegistryString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamExe");
    if (!path.empty() && fs::exists(path)) return path;

    path = ReadRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath");
    if (!path.empty()) {
        const fs::path exe = fs::path(path) / L"steam.exe";
        if (fs::exists(exe)) return exe.wstring();
    }

    const auto drives = StorefrontScanner::GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path cand = fs::path(drive) / L"Steam" / L"steam.exe";
        if (fs::exists(cand)) return cand.wstring();
        const fs::path cand2 = fs::path(drive) / L"Games" / L"Steam" / L"steam.exe";
        if (fs::exists(cand2)) return cand2.wstring();
    }

    return L"";
}

std::wstring PlatformLauncher::GetEpicExecutablePath() {
    const auto drives = StorefrontScanner::GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path candidates[] = {
            fs::path(drive) / L"Games" / L"Epic Games" / L"Launcher" / L"Portal" / L"Binaries" / L"Win64" / L"EpicGamesLauncher.exe",
            fs::path(drive) / L"Program Files (x86)" / L"Epic Games" / L"Launcher" / L"Portal" / L"Binaries" / L"Win64" / L"EpicGamesLauncher.exe",
            fs::path(drive) / L"Program Files" / L"Epic Games" / L"Launcher" / L"Portal" / L"Binaries" / L"Win64" / L"EpicGamesLauncher.exe",
        };
        for (const auto& cand : candidates) {
            if (fs::exists(cand)) return cand.wstring();
        }
    }
    return L"";
}

std::wstring PlatformLauncher::GetGOGGalaxyExecutablePath() {
    const std::wstring clientDir = ReadRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\GOG.com\\GalaxyClient\\paths", L"client");
    if (!clientDir.empty()) {
        const fs::path exe = fs::path(clientDir) / L"GalaxyClient.exe";
        if (fs::exists(exe)) return exe.wstring();
    }

    const auto drives = StorefrontScanner::GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path cand = fs::path(drive) / L"Games" / L"GOG Galaxy" / L"GalaxyClient.exe";
        if (fs::exists(cand)) return cand.wstring();
        const fs::path cand2 = fs::path(drive) / L"Program Files (x86)" / L"GOG Galaxy" / L"GalaxyClient.exe";
        if (fs::exists(cand2)) return cand2.wstring();
    }

    return L"";
}

std::wstring PlatformLauncher::GetBattleNetExecutablePath() {
    std::wstring iconVal = ReadRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Blizzard Entertainment\\Battle.net\\Capabilities", L"ApplicationIcon");
    if (!iconVal.empty()) {
        // Strip quotes and index: "\"E:\\Games\\Battle.net\\Battle.net.exe\",0" -> "E:\\Games\\Battle.net\\Battle.net.exe"
        if (iconVal.front() == L'"') {
            size_t nextQuote = iconVal.find(L'"', 1);
            if (nextQuote != std::wstring::npos) {
                iconVal = iconVal.substr(1, nextQuote - 1);
            }
        }
        if (fs::exists(iconVal)) return iconVal;
    }

    const auto drives = StorefrontScanner::GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path cand = fs::path(drive) / L"Games" / L"Battle.net" / L"Battle.net.exe";
        if (fs::exists(cand)) return cand.wstring();
    }

    return L"";
}

std::wstring PlatformLauncher::GetUbisoftExecutablePath() {
    const std::wstring installDir = ReadRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Ubisoft\\Launcher", L"InstallDir");
    if (!installDir.empty()) {
        const fs::path exe = fs::path(installDir) / L"upc.exe";
        if (fs::exists(exe)) return exe.wstring();
    }

    const auto drives = StorefrontScanner::GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path cand = fs::path(drive) / L"Games" / L"Ubisoft" / L"Ubisoft Game Launcher" / L"upc.exe";
        if (fs::exists(cand)) return cand.wstring();
    }

    return L"";
}

std::wstring PlatformLauncher::GetEAAppExecutablePath() {
    std::wstring path = ReadRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Electronic Arts\\EA Desktop", L"LauncherPath");
    if (!path.empty() && fs::exists(path)) return path;

    path = ReadRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Electronic Arts\\EA Desktop", L"LauncherPath");
    if (!path.empty() && fs::exists(path)) return path;

    const auto drives = StorefrontScanner::GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path cand = fs::path(drive) / L"Program Files" / L"Electronic Arts" / L"EA Desktop" / L"EA Desktop" / L"EADesktop.exe";
        if (fs::exists(cand)) return cand.wstring();
    }
    return L"";
}

std::wstring PlatformLauncher::GetItchExecutablePath() {
    wchar_t localAppData[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH) > 0) {
        const fs::path cand1 = fs::path(localAppData) / L"Programs" / L"itch" / L"itch.exe";
        if (fs::exists(cand1)) return cand1.wstring();

        const fs::path cand2 = fs::path(localAppData) / L"itch" / L"itch.exe";
        if (fs::exists(cand2)) return cand2.wstring();
    }
    return L"";
}

bool PlatformLauncher::Launch(HWND hwndOwner, const GameItem& game) {
    // 1. STEAM: DRM validation and Steam profile overlay strictly require passing steam.exe in launch path
    if (game.platform == PlatformType::Steam) {
        const std::wstring steamExe = GetSteamExecutablePath();
        const std::wstring cleanId = CleanAppId(game.appId, L"steam_");

        if (!steamExe.empty()) {
            std::wstring args = L"-applaunch " + cleanId;
            if (!game.launchArguments.empty()) {
                args += L" " + game.launchArguments;
            }
            const auto res = reinterpret_cast<INT_PTR>(
                ShellExecuteW(hwndOwner, L"open", steamExe.c_str(), args.c_str(), nullptr, SW_SHOWNORMAL)
            );
            return (res > 32);
        }

        // Protocol fallback if executable not found in registry
        const std::wstring uri = L"steam://rungameid/" + cleanId;
        const auto res = reinterpret_cast<INT_PTR>(
            ShellExecuteW(hwndOwner, L"open", uri.c_str(), nullptr, nullptr, SW_SHOWNORMAL)
        );
        return (res > 32);
    }

    // 2. EPIC GAMES: Pass EpicGamesLauncher.exe in launch path to ensure authentication tokens and DRM validation
    if (game.platform == PlatformType::Epic) {
        const std::wstring epicExe = GetEpicExecutablePath();
        const std::wstring cleanName = CleanAppId(game.appId, L"epic_");
        const std::wstring launchUri = game.launchUri.empty()
            ? (L"com.epicgames.launcher://apps/" + cleanName + L"?action=launch&silent=true")
            : game.launchUri;

        if (!epicExe.empty()) {
            std::wstring args = L"\"" + launchUri + L"\"";
            const auto res = reinterpret_cast<INT_PTR>(
                ShellExecuteW(hwndOwner, L"open", epicExe.c_str(), args.c_str(), nullptr, SW_SHOWNORMAL)
            );
            return (res > 32);
        }

        const auto res = reinterpret_cast<INT_PTR>(
            ShellExecuteW(hwndOwner, L"open", launchUri.c_str(), nullptr, nullptr, SW_SHOWNORMAL)
        );
        return (res > 32);
    }

    // 3. GOG GALAXY: If GalaxyClient is installed, launch through client for achievements/cloud saves
    if (game.platform == PlatformType::GOG) {
        const std::wstring gogExe = GetGOGGalaxyExecutablePath();
        const std::wstring cleanId = CleanAppId(game.appId, L"gog_");

        if (!gogExe.empty()) {
            std::wstring args = L"/command=runGame /gameId=" + cleanId;
            if (!game.workingDirectory.empty()) {
                args += L" /path=\"" + game.workingDirectory + L"\"";
            }
            const auto res = reinterpret_cast<INT_PTR>(
                ShellExecuteW(hwndOwner, L"open", gogExe.c_str(), args.c_str(), nullptr, SW_SHOWNORMAL)
            );
            return (res > 32);
        }

        // GOG games are DRM-free: if GalaxyClient is not present, launch direct executable
        if (!game.executablePath.empty()) {
            const wchar_t* pArgs = game.launchArguments.empty() ? nullptr : game.launchArguments.c_str();
            const wchar_t* pDir = game.workingDirectory.empty() ? nullptr : game.workingDirectory.c_str();
            const auto res = reinterpret_cast<INT_PTR>(
                ShellExecuteW(hwndOwner, L"open", game.executablePath.c_str(), pArgs, pDir, SW_SHOWNORMAL)
            );
            return (res > 32);
        }
    }

    // 4. BATTLE.NET: Pass Battle.net.exe with launch command
    if (game.platform == PlatformType::BattleNet) {
        const std::wstring bnetExe = GetBattleNetExecutablePath();
        if (!bnetExe.empty()) {
            const std::wstring cleanCode = CleanAppId(game.appId, L"bnet_");
            std::wstring args = L"--exec=\"launch " + cleanCode + L"\"";
            const auto res = reinterpret_cast<INT_PTR>(
                ShellExecuteW(hwndOwner, L"open", bnetExe.c_str(), args.c_str(), nullptr, SW_SHOWNORMAL)
            );
            return (res > 32);
        }
    }

    // 5. ITCH.IO & STANDALONE & SQUARE ENIX (FFXIV):
    // DRM-free stores: Direct executable invocation is the most reliable, optimal method (zero client required)
    if (!game.executablePath.empty()) {
        const wchar_t* pArgs = game.launchArguments.empty() ? nullptr : game.launchArguments.c_str();
        const wchar_t* pDir = game.workingDirectory.empty() ? nullptr : game.workingDirectory.c_str();
        const auto res = reinterpret_cast<INT_PTR>(
            ShellExecuteW(hwndOwner, L"open", game.executablePath.c_str(), pArgs, pDir, SW_SHOWNORMAL)
        );
        return (res > 32);
    }

    // 6. Generic URI fallback
    if (!game.launchUri.empty()) {
        const auto res = reinterpret_cast<INT_PTR>(
            ShellExecuteW(hwndOwner, L"open", game.launchUri.c_str(), nullptr, nullptr, SW_SHOWNORMAL)
        );
        return (res > 32);
    }

    return false;
}

} // namespace GameExplorer
