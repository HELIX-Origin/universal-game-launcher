#include "GameExplorerCore/StorefrontScanner.h"
#include "GameExplorerCore/Database.h"
#include "GameExplorerCore/Storage.h"
#include "GameExplorerCore/Types.h"
#include "sqlite3.h"
#include <windows.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <set>

namespace fs = std::filesystem;

namespace GameExplorer {

namespace {

std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    const int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (sizeNeeded <= 0) return {};
    std::string utf8(sizeNeeded, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8.data(), sizeNeeded, nullptr, nullptr);
    return utf8;
}

std::wstring Utf8ToWide(const char* utf8) {
    if (utf8 == nullptr || utf8[0] == '\0') return {};
    const int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (sizeNeeded <= 1) return {};
    std::wstring wide(sizeNeeded - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide.data(), sizeNeeded);
    return wide;
}

bool ParseKeyValueLine(const std::wstring& line, std::wstring& outKey, std::wstring& outVal) {
    size_t firstQuote = line.find(L'"');
    if (firstQuote == std::wstring::npos) return false;
    size_t secondQuote = line.find(L'"', firstQuote + 1);
    if (secondQuote == std::wstring::npos) return false;

    outKey = line.substr(firstQuote + 1, secondQuote - firstQuote - 1);

    size_t thirdQuote = line.find(L'"', secondQuote + 1);
    if (thirdQuote == std::wstring::npos) return false;
    size_t fourthQuote = line.find(L'"', thirdQuote + 1);
    if (fourthQuote == std::wstring::npos) return false;

    outVal = line.substr(thirdQuote + 1, fourthQuote - thirdQuote - 1);
    return true;
}

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

// Recursively searches for the primary game executable (up to 3 levels deep)
fs::path FindPrimaryGameExecutable(const fs::path& rootDir) {
    if (!fs::exists(rootDir)) return {};

    fs::path preferredExe;
    uintmax_t maxExeSize = 0;

    std::error_code ec;
    auto iter = fs::recursive_directory_iterator(
        rootDir,
        fs::directory_options::skip_permission_denied,
        ec
    );

    for (const auto& entry : iter) {
        if (iter.depth() > 3) {
            iter.pop();
            continue;
        }

        if (!entry.is_regular_file()) continue;

        if (entry.path().extension() == L".exe") {
            const auto fn = entry.path().filename().wstring();
            if (StorefrontScanner::IsBlacklistedExecutable(fn)) continue;

            // Highly prioritized canonical game executable names
            if (_wcsicmp(fn.c_str(), L"Game.exe") == 0 ||
                _wcsicmp(fn.c_str(), L"ffxivboot64.exe") == 0 ||
                _wcsicmp(fn.c_str(), L"ffxivboot.exe") == 0) {
                return entry.path();
            }

            const uintmax_t sz = entry.file_size(ec);
            if (sz > maxExeSize) {
                maxExeSize = sz;
                preferredExe = entry.path();
            }
        }
    }

    return preferredExe;
}

std::wstring CleanTitleFromFolderName(const std::wstring& folderName) {
    std::wstring title = folderName;
    // Replace hyphens and underscores with spaces
    for (auto& ch : title) {
        if (ch == L'-' || ch == L'_') ch = L' ';
    }

    // Capitalize first letter of each word
    bool newWord = true;
    for (auto& ch : title) {
        if (ch == L' ') {
            newWord = true;
        } else if (newWord) {
            ch = static_cast<wchar_t>(::towupper(ch));
            newWord = false;
        }
    }

    return title;
}

} // namespace

bool StorefrontScanner::IsBlacklistedExecutable(const std::wstring& filename) {
    std::wstring lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);

    const wchar_t* blacklisted[] = {
        L"notification_helper.exe",
        L"nwjc.exe",
        L"unitycrashhandler64.exe",
        L"unitycrashhandler32.exe",
        L"crashreport.exe",
        L"crashpad_handler.exe",
        L"unins000.exe",
        L"uninstall.exe",
        L"dxsetup.exe",
        L"vcredist_x64.exe",
        L"vcredist_x86.exe",
        L"update.exe",
        L"setup.exe"
    };

    for (const auto* b : blacklisted) {
        if (lower == b) return true;
    }
    return false;
}

std::vector<std::wstring> StorefrontScanner::GetAllMountedDrives() {
    std::vector<std::wstring> drives;
    wchar_t driveStrings[512] = { 0 };
    const DWORD len = GetLogicalDriveStringsW(511, driveStrings);
    if (len > 0) {
        const wchar_t* p = driveStrings;
        while (*p) {
            const UINT type = GetDriveTypeW(p);
            if (type == DRIVE_FIXED || type == DRIVE_REMOVABLE) {
                std::wstring root = p;
                if (!root.empty() && root.back() == L'\\') {
                    root.pop_back(); // "C:" instead of "C:\"
                }
                drives.push_back(root);
            }
            p += wcslen(p) + 1;
        }
    }
    return drives;
}

size_t StorefrontScanner::ScanAll(Database& db) {
    size_t total = 0;
    const auto allGames = ScanAllInstalledGames();

    db.BeginTransaction();
    for (const auto& game : allGames) {
        if (db.UpsertGame(game)) {
            ++total;
        }
    }
    db.CommitTransaction();

    return total;
}

std::vector<std::wstring> StorefrontScanner::GetSteamLibraryFolders(const std::wstring& steamPath) {
    std::vector<std::wstring> folders;
    folders.push_back(steamPath);

    const fs::path vdfPath = fs::path(steamPath) / L"steamapps" / L"libraryfolders.vdf";
    if (fs::exists(vdfPath)) {
        std::wifstream stream(vdfPath);
        if (stream.is_open()) {
            std::wstring line;
            while (std::getline(stream, line)) {
                std::wstring key, val;
                if (ParseKeyValueLine(line, key, val)) {
                    if (key == L"path") {
                        std::replace(val.begin(), val.end(), L'/', L'\\');
                        if (fs::exists(val)) {
                            bool already = false;
                            for (const auto& existing : folders) {
                                if (_wcsicmp(existing.c_str(), val.c_str()) == 0) {
                                    already = true;
                                    break;
                                }
                            }
                            if (!already) folders.push_back(val);
                        }
                    }
                }
            }
        }
    }

    // Also scan all mounted drives for non-standard or external Steam libraries
    const auto drives = GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path candidates[] = {
            fs::path(drive) / L"SteamLibrary",
            fs::path(drive) / L"Games" / L"SteamLibrary",
            fs::path(drive) / L"Steam"
        };
        for (const auto& cand : candidates) {
            if (fs::exists(cand / L"steamapps")) {
                bool already = false;
                for (const auto& existing : folders) {
                    if (_wcsicmp(existing.c_str(), cand.c_str()) == 0) {
                        already = true;
                        break;
                    }
                }
                if (!already) folders.push_back(cand.wstring());
            }
        }
    }

    return folders;
}

void StorefrontScanner::DiscoverSteamGridArtwork(
    const std::wstring& steamPath,
    const std::wstring& appId,
    std::wstring& outCover,
    std::wstring& outHero,
    std::wstring& outIcon
) {
    // 1. Check modern Steam librarycache: steamPath/appcache/librarycache/<appId>/
    const fs::path libCacheDir = fs::path(steamPath) / L"appcache" / L"librarycache" / appId;
    if (fs::exists(libCacheDir)) {
        if (outCover.empty()) {
            const fs::path cover600 = libCacheDir / L"library_600x900.jpg";
            const fs::path coverPng = libCacheDir / L"library_600x900.png";
            if (fs::exists(cover600)) outCover = cover600.wstring();
            else if (fs::exists(coverPng)) outCover = coverPng.wstring();
        }
        if (outHero.empty()) {
            const fs::path heroJpg = libCacheDir / L"library_hero.jpg";
            const fs::path heroPng = libCacheDir / L"library_hero.png";
            if (fs::exists(heroJpg)) outHero = heroJpg.wstring();
            else if (fs::exists(heroPng)) outHero = heroPng.wstring();
        }
        if (outIcon.empty()) {
            const fs::path iconIco = libCacheDir / L"logo.png";
            if (fs::exists(iconIco)) outIcon = iconIco.wstring();
        }
    }

    // 2. Also check userdata/<userId>/config/grid/ (custom/overridden grid art)
    const fs::path userdataPath = fs::path(steamPath) / L"userdata";
    if (fs::exists(userdataPath)) {
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(userdataPath, ec)) {
            if (!entry.is_directory()) continue;

            const fs::path gridDir = entry.path() / L"config" / L"grid";
            if (!fs::exists(gridDir)) continue;

            if (outCover.empty()) {
                const fs::path coverPng = gridDir / (appId + L"p.png");
                const fs::path coverJpg = gridDir / (appId + L"p.jpg");
                if (fs::exists(coverPng)) outCover = coverPng.wstring();
                else if (fs::exists(coverJpg)) outCover = coverJpg.wstring();
            }

            if (outHero.empty()) {
                const fs::path heroPng = gridDir / (appId + L"_hero.png");
                const fs::path heroJpg = gridDir / (appId + L"_hero.jpg");
                if (fs::exists(heroPng)) outHero = heroPng.wstring();
                else if (fs::exists(heroJpg)) outHero = heroJpg.wstring();
            }

            if (outIcon.empty()) {
                const fs::path iconIco = gridDir / (appId + L"_icon.ico");
                const fs::path iconPng = gridDir / (appId + L"_icon.png");
                const fs::path logoPng = gridDir / (appId + L"_logo.png");
                if (fs::exists(iconIco)) outIcon = iconIco.wstring();
                else if (fs::exists(iconPng)) outIcon = iconPng.wstring();
                else if (fs::exists(logoPng)) outIcon = logoPng.wstring();
            }

            if (!outCover.empty() && !outHero.empty() && !outIcon.empty()) {
                break;
            }
        }
    }
}

std::vector<GameItem> StorefrontScanner::ScanSteam() {
    std::vector<GameItem> games;

    std::wstring steamPath = ReadRegistryString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
    if (steamPath.empty()) {
        steamPath = ReadRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath");
    }
    if (steamPath.empty()) {
        steamPath = ReadRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath");
    }
    if (steamPath.empty() || !fs::exists(steamPath)) {
        return games;
    }

    const auto libraryFolders = GetSteamLibraryFolders(steamPath);

    for (const auto& lib : libraryFolders) {
        const fs::path steamappsDir = fs::path(lib) / L"steamapps";
        if (!fs::exists(steamappsDir)) continue;

        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(steamappsDir, ec)) {
            if (!entry.is_regular_file()) continue;

            const auto filename = entry.path().filename().wstring();
            if (filename.rfind(L"appmanifest_", 0) != 0 || entry.path().extension() != L".acf") {
                continue;
            }

            std::wifstream stream(entry.path());
            if (!stream.is_open()) continue;

            std::wstring rawAppId, gameName, installDir, sizeOnDiskStr, lastPlayedStr;
            std::wstring line;
            while (std::getline(stream, line)) {
                std::wstring key, val;
                if (ParseKeyValueLine(line, key, val)) {
                    if (key == L"appid") rawAppId = val;
                    else if (key == L"name") gameName = val;
                    else if (key == L"installdir") installDir = val;
                    else if (key == L"SizeOnDisk") sizeOnDiskStr = val;
                    else if (key == L"LastPlayed") lastPlayedStr = val;
                }
            }

            if (rawAppId.empty() || gameName.empty()) continue;

            // Filter out tools, runtimes, and redistributables
            if (rawAppId == L"228980" ||
                gameName.find(L"SteamVR") != std::wstring::npos ||
                gameName.find(L"Dedicated Server") != std::wstring::npos ||
                gameName.find(L"Steamworks") != std::wstring::npos) {
                continue;
            }

            GameItem item{};
            item.appId = L"steam_" + rawAppId;
            item.title = gameName;
            item.platform = PlatformType::Steam;
            item.launchUri = L"steam://rungameid/" + rawAppId;

            if (!sizeOnDiskStr.empty()) {
                item.diskSizeBytes = _wcstoui64(sizeOnDiskStr.c_str(), nullptr, 10);
            }

            if (!lastPlayedStr.empty()) {
                item.lastPlayedTimestamp = _wcstoui64(lastPlayedStr.c_str(), nullptr, 10);
            }

            const fs::path commonDir = steamappsDir / L"common" / installDir;
            if (fs::exists(commonDir)) {
                item.workingDirectory = commonDir.wstring();
            }

            DiscoverSteamGridArtwork(steamPath, rawAppId, item.coverArtPath, item.heroArtPath, item.iconPath);

            games.push_back(std::move(item));
        }
    }

    return games;
}

std::vector<GameItem> StorefrontScanner::ScanItch() {
    std::vector<GameItem> games;
    std::set<std::wstring> discoveredFolderNames;

    // 1. Check butler.db in %APPDATA%\itch\db\butler.db
    wchar_t appData[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH) > 0) {
        const fs::path dbPath = fs::path(appData) / L"itch" / L"db" / L"butler.db";
        if (fs::exists(dbPath)) {
            sqlite3* pItchDb = nullptr;
            const std::string utf8Path = WideToUtf8(dbPath.wstring());
            if (sqlite3_open_v2(utf8Path.c_str(), &pItchDb, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK) {
                const char* sql =
                    "SELECT c.id, g.id, g.title, g.cover_url, g.url, l.path, c.install_folder_name "
                    "FROM caves c "
                    "JOIN games g ON c.game_id = g.id "
                    "JOIN install_locations l ON c.install_location_id = l.id;";

                sqlite3_stmt* stmt = nullptr;
                if (sqlite3_prepare_v2(pItchDb, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                    while (sqlite3_step(stmt) == SQLITE_ROW) {
                        const int64_t gameId = sqlite3_column_int64(stmt, 1);
                        const auto* titleUtf8 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                        const auto* coverUrlUtf8 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
                        const auto* urlUtf8 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
                        const auto* locPathUtf8 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
                        const auto* folderUtf8 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));

                        const std::wstring title = Utf8ToWide(titleUtf8);
                        const std::wstring locPath = Utf8ToWide(locPathUtf8);
                        const std::wstring folderName = Utf8ToWide(folderUtf8);

                        if (!locPath.empty() && !folderName.empty()) {
                            const fs::path gameDir = fs::path(locPath) / folderName;
                            discoveredFolderNames.insert(folderName);

                            GameItem item{};
                            item.appId = L"itch_" + std::to_wstring(gameId);
                            item.title = title.empty() ? CleanTitleFromFolderName(folderName) : title;
                            item.platform = PlatformType::Itch;
                            item.storePageUrl = Utf8ToWide(urlUtf8);

                            // Find primary executable for direct execution without the itch app
                            const fs::path exePath = FindPrimaryGameExecutable(gameDir);
                            if (!exePath.empty()) {
                                item.executablePath = exePath.wstring();
                                item.workingDirectory = exePath.parent_path().wstring();
                            } else {
                                item.workingDirectory = gameDir.wstring();
                            }

                            // Do NOT set launchUri - direct executable execution is prioritized!
                            item.launchUri = L"";

                            if (coverUrlUtf8 != nullptr) {
                                item.coverArtPath = Utf8ToWide(coverUrlUtf8);
                            }

                            games.push_back(std::move(item));
                        }
                    }
                    sqlite3_finalize(stmt);
                }
                sqlite3_close(pItchDb);
            }
        }
    }

    // 2. Scan all mounted drives for itch game folders (e.g. E:\Games\itch\games)
    const auto drives = GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path itchDirs[] = {
            fs::path(drive) / L"Games" / L"itch" / L"games",
            fs::path(drive) / L"itch" / L"games",
            fs::path(drive) / L"Games" / L"itch",
        };

        for (const auto& itchDir : itchDirs) {
            if (!fs::exists(itchDir)) continue;

            std::error_code ec;
            for (const auto& entry : fs::directory_iterator(itchDir, ec)) {
                if (!entry.is_directory()) continue;

                const auto fn = entry.path().filename().wstring();
                if (fn == L"downloads" || fn == L".itch" || discoveredFolderNames.contains(fn)) {
                    continue;
                }

                const fs::path exePath = FindPrimaryGameExecutable(entry.path());
                if (!exePath.empty()) {
                    GameItem item{};
                    item.appId = L"itch_" + fn;
                    item.title = CleanTitleFromFolderName(fn);
                    item.platform = PlatformType::Itch;
                    item.executablePath = exePath.wstring();
                    item.workingDirectory = exePath.parent_path().wstring();
                    item.launchUri = L""; // Direct executable execution!
                    games.push_back(std::move(item));
                    discoveredFolderNames.insert(fn);
                }
            }
        }
    }

    return games;
}

std::vector<GameItem> StorefrontScanner::ScanGOG() {
    std::vector<GameItem> games;

    const wchar_t* gogRegKeys[] = {
        L"SOFTWARE\\WOW6432Node\\GOG.com\\Games",
        L"SOFTWARE\\GOG.com\\Games"
    };

    for (const auto* rootSubKey : gogRegKeys) {
        HKEY hParent = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, rootSubKey, 0, KEY_READ, &hParent) != ERROR_SUCCESS) {
            continue;
        }

        wchar_t subKeyName[256];
        DWORD subKeyLen = 256;
        DWORD index = 0;

        while (RegEnumKeyExW(hParent, index++, subKeyName, &subKeyLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
            subKeyLen = 256;
            const std::wstring fullSubKey = std::wstring(rootSubKey) + L"\\" + subKeyName;

            const std::wstring gameId = ReadRegistryString(HKEY_LOCAL_MACHINE, fullSubKey.c_str(), L"gameID");
            const std::wstring gameName = ReadRegistryString(HKEY_LOCAL_MACHINE, fullSubKey.c_str(), L"gameName");
            const std::wstring exePath = ReadRegistryString(HKEY_LOCAL_MACHINE, fullSubKey.c_str(), L"exe");
            const std::wstring installPath = ReadRegistryString(HKEY_LOCAL_MACHINE, fullSubKey.c_str(), L"path");

            if (!gameName.empty()) {
                GameItem item{};
                item.appId = L"gog_" + (gameId.empty() ? std::wstring(subKeyName) : gameId);
                item.title = gameName;
                item.platform = PlatformType::GOG;
                item.workingDirectory = installPath;
                item.executablePath = exePath.empty() ? (installPath + L"\\" + subKeyName + L".exe") : exePath;
                games.push_back(std::move(item));
            }
        }

        RegCloseKey(hParent);
    }

    return games;
}

std::vector<GameItem> StorefrontScanner::ScanEpic() {
    std::vector<GameItem> games;

    const fs::path manifestsDir = L"C:\\ProgramData\\Epic\\EpicGamesLauncher\\Data\\Manifests";
    if (!fs::exists(manifestsDir)) return games;

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(manifestsDir, ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != L".item") {
            continue;
        }

        std::wifstream stream(entry.path());
        if (!stream.is_open()) continue;

        std::wstring line;
        std::wstring displayName, appName, installLocation, launchExe;

        while (std::getline(stream, line)) {
            std::wstring key, val;
            if (ParseKeyValueLine(line, key, val)) {
                if (key == L"DisplayName") displayName = val;
                else if (key == L"AppName") appName = val;
                else if (key == L"InstallLocation") installLocation = val;
                else if (key == L"LaunchExecutable") launchExe = val;
            }
        }

        if (!displayName.empty() && !appName.empty()) {
            GameItem item{};
            item.appId = L"epic_" + appName;
            item.title = displayName;
            item.platform = PlatformType::Epic;
            item.workingDirectory = installLocation;
            if (!launchExe.empty()) {
                item.executablePath = (fs::path(installLocation) / launchExe).wstring();
            }
            item.launchUri = L"com.epicgames.launcher://apps/" + appName + L"?action=launch&silent=true";
            games.push_back(std::move(item));
        }
    }

    return games;
}

std::vector<GameItem> StorefrontScanner::ScanStandaloneAndDrives() {
    std::vector<GameItem> games;
    const auto drives = GetAllMountedDrives();

    for (const auto& drive : drives) {
        // 1. Scan Square Enix (FFXIV)
        const fs::path ffxivDir = fs::path(drive) / L"Games" / L"Square Enix" / L"FINAL FANTASY XIV - A Realm Reborn";
        if (fs::exists(ffxivDir)) {
            const fs::path boot64 = ffxivDir / L"boot" / L"ffxivboot64.exe";
            const fs::path boot = ffxivDir / L"boot" / L"ffxivboot.exe";

            GameItem item{};
            item.appId = L"sqex_ffxiv";
            item.title = L"FINAL FANTASY XIV Online";
            item.platform = PlatformType::SquareEnix;
            item.workingDirectory = (fs::exists(boot64) || fs::exists(boot)) ? (ffxivDir / L"boot").wstring() : ffxivDir.wstring();
            item.executablePath = fs::exists(boot64) ? boot64.wstring() : (fs::exists(boot) ? boot.wstring() : L"");
            games.push_back(std::move(item));
        }

        // 2. Scan Battle.net games
        const fs::path bnetDir = fs::path(drive) / L"Games" / L"Battle.net";
        if (fs::exists(bnetDir)) {
            std::error_code ec;
            for (const auto& entry : fs::directory_iterator(bnetDir, ec)) {
                if (!entry.is_directory()) continue;
                const auto exe = FindPrimaryGameExecutable(entry.path());
                if (!exe.empty()) {
                    GameItem item{};
                    item.appId = L"bnet_" + entry.path().filename().wstring();
                    item.title = entry.path().filename().wstring();
                    item.platform = PlatformType::BattleNet;
                    item.executablePath = exe.wstring();
                    item.workingDirectory = exe.parent_path().wstring();
                    games.push_back(std::move(item));
                }
            }
        }
    }

    return games;
}

std::vector<GameItem> StorefrontScanner::ScanUbisoft() {
    std::vector<GameItem> games;
    std::set<std::wstring> seenDirs;

    auto checkInstall = [&](HKEY root, const wchar_t* subKey) {
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(root, subKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            wchar_t name[256];
            DWORD idx = 0;
            DWORD nameLen = ARRAYSIZE(name);
            while (RegEnumKeyExW(hKey, idx, name, &nameLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
                const std::wstring itemSubKey = std::wstring(subKey) + L"\\" + name;
                std::wstring installDir = ReadRegistryString(root, itemSubKey.c_str(), L"InstallDir");
                if (!installDir.empty() && fs::exists(installDir)) {
                    std::wstring lower = installDir;
                    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
                    if (seenDirs.insert(lower).second) {
                        const auto exe = FindPrimaryGameExecutable(installDir);
                        GameItem item{};
                        item.appId = L"uplay_" + std::wstring(name);
                        item.title = fs::path(installDir).filename().wstring();
                        item.platform = PlatformType::Ubisoft;
                        item.workingDirectory = installDir;
                        if (!exe.empty()) {
                            item.executablePath = exe.wstring();
                        }
                        item.launchUri = L"uplay://launch/" + std::wstring(name) + L"/0";
                        games.push_back(std::move(item));
                    }
                }
                ++idx;
                nameLen = ARRAYSIZE(name);
            }
            RegCloseKey(hKey);
        }
    };

    checkInstall(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Ubisoft\\Launcher\\Installs");
    checkInstall(HKEY_CURRENT_USER, L"Software\\Ubisoft\\Launcher\\Installs");

    // Scan all mounted drives for Ubisoft library folders
    const auto drives = GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path ubiDirs[] = {
            fs::path(drive) / L"Games" / L"Ubisoft",
            fs::path(drive) / L"Ubisoft Games",
            fs::path(drive) / L"Ubisoft" / L"games"
        };
        for (const auto& dir : ubiDirs) {
            if (!fs::exists(dir)) continue;
            std::error_code ec;
            for (const auto& entry : fs::directory_iterator(dir, ec)) {
                if (!entry.is_directory()) continue;
                std::wstring lower = entry.path().wstring();
                std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
                if (seenDirs.insert(lower).second) {
                    const auto exe = FindPrimaryGameExecutable(entry.path());
                    if (!exe.empty()) {
                        GameItem item{};
                        item.appId = L"uplay_" + entry.path().filename().wstring();
                        item.title = entry.path().filename().wstring();
                        item.platform = PlatformType::Ubisoft;
                        item.executablePath = exe.wstring();
                        item.workingDirectory = entry.path().wstring();
                        games.push_back(std::move(item));
                    }
                }
            }
        }
    }

    return games;
}

std::vector<GameItem> StorefrontScanner::ScanRockstar() {
    std::vector<GameItem> games;
    std::set<std::wstring> seenDirs;

    auto checkRockstar = [&](HKEY root, const wchar_t* subKey) {
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(root, subKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            wchar_t name[256];
            DWORD idx = 0;
            DWORD nameLen = ARRAYSIZE(name);
            while (RegEnumKeyExW(hKey, idx, name, &nameLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
                if (_wcsicmp(name, L"Launcher") != 0) {
                    const std::wstring itemSubKey = std::wstring(subKey) + L"\\" + name;
                    std::wstring installFolder = ReadRegistryString(root, itemSubKey.c_str(), L"InstallFolder");
                    if (installFolder.empty()) {
                        installFolder = ReadRegistryString(root, itemSubKey.c_str(), L"InstallDir");
                    }
                    if (!installFolder.empty() && fs::exists(installFolder)) {
                        std::wstring lower = installFolder;
                        std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
                        if (seenDirs.insert(lower).second) {
                            const auto exe = FindPrimaryGameExecutable(installFolder);
                            GameItem item{};
                            item.appId = L"rockstar_" + std::wstring(name);
                            item.title = name;
                            item.platform = PlatformType::Rockstar;
                            item.workingDirectory = installFolder;
                            if (!exe.empty()) {
                                item.executablePath = exe.wstring();
                            }
                            games.push_back(std::move(item));
                        }
                    }
                }
                ++idx;
                nameLen = ARRAYSIZE(name);
            }
            RegCloseKey(hKey);
        }
    };

    checkRockstar(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Rockstar Games");
    checkRockstar(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Rockstar Games");

    // Scan all mounted drives for Rockstar games
    const auto drives = GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path rDirs[] = {
            fs::path(drive) / L"Games" / L"Rockstar Games",
            fs::path(drive) / L"Rockstar Games",
            fs::path(drive) / L"Program Files" / L"Rockstar Games"
        };
        for (const auto& dir : rDirs) {
            if (!fs::exists(dir)) continue;
            std::error_code ec;
            for (const auto& entry : fs::directory_iterator(dir, ec)) {
                if (!entry.is_directory()) continue;
                if (_wcsicmp(entry.path().filename().c_str(), L"Launcher") == 0) continue;
                std::wstring lower = entry.path().wstring();
                std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
                if (seenDirs.insert(lower).second) {
                    const auto exe = FindPrimaryGameExecutable(entry.path());
                    if (!exe.empty()) {
                        GameItem item{};
                        item.appId = L"rockstar_" + entry.path().filename().wstring();
                        item.title = entry.path().filename().wstring();
                        item.platform = PlatformType::Rockstar;
                        item.executablePath = exe.wstring();
                        item.workingDirectory = entry.path().wstring();
                        games.push_back(std::move(item));
                    }
                }
            }
        }
    }

    return games;
}

std::vector<GameItem> StorefrontScanner::ScanEA() {
    std::vector<GameItem> games;
    std::set<std::wstring> seenDirs;

    // Check registry for EA Games
    auto checkEA = [&](HKEY root, const wchar_t* subKey) {
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(root, subKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            wchar_t name[256];
            DWORD idx = 0;
            DWORD nameLen = ARRAYSIZE(name);
            while (RegEnumKeyExW(hKey, idx, name, &nameLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
                const std::wstring itemSubKey = std::wstring(subKey) + L"\\" + name;
                std::wstring installDir = ReadRegistryString(root, itemSubKey.c_str(), L"Install Dir");
                if (installDir.empty()) {
                    installDir = ReadRegistryString(root, itemSubKey.c_str(), L"InstallDir");
                }
                if (!installDir.empty() && fs::exists(installDir)) {
                    std::wstring lower = installDir;
                    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
                    if (seenDirs.insert(lower).second) {
                        const auto exe = FindPrimaryGameExecutable(installDir);
                        GameItem item{};
                        item.appId = L"ea_" + std::wstring(name);
                        item.title = name;
                        item.platform = PlatformType::EAApp;
                        item.workingDirectory = installDir;
                        if (!exe.empty()) {
                            item.executablePath = exe.wstring();
                        }
                        games.push_back(std::move(item));
                    }
                }
                ++idx;
                nameLen = ARRAYSIZE(name);
            }
            RegCloseKey(hKey);
        }
    };

    checkEA(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\EA Games");
    checkEA(HKEY_LOCAL_MACHINE, L"SOFTWARE\\EA Games");
    checkEA(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Electronic Arts\\EA Games");

    // Scan all mounted drives for EA Games library folders
    const auto drives = GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path eaDirs[] = {
            fs::path(drive) / L"Games" / L"EA Games",
            fs::path(drive) / L"EA Games",
            fs::path(drive) / L"Program Files" / L"EA Games"
        };
        for (const auto& dir : eaDirs) {
            if (!fs::exists(dir)) continue;
            std::error_code ec;
            for (const auto& entry : fs::directory_iterator(dir, ec)) {
                if (!entry.is_directory()) continue;
                std::wstring lower = entry.path().wstring();
                std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
                if (seenDirs.insert(lower).second) {
                    const auto exe = FindPrimaryGameExecutable(entry.path());
                    if (!exe.empty()) {
                        GameItem item{};
                        item.appId = L"ea_" + entry.path().filename().wstring();
                        item.title = entry.path().filename().wstring();
                        item.platform = PlatformType::EAApp;
                        item.executablePath = exe.wstring();
                        item.workingDirectory = entry.path().wstring();
                        games.push_back(std::move(item));
                    }
                }
            }
        }
    }

    return games;
}

std::vector<GameItem> StorefrontScanner::ScanAmazon() {
    std::vector<GameItem> games;
    std::set<std::wstring> seenDirs;

    // Amazon Games SQLite database
    wchar_t localAppData[MAX_PATH];
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH) > 0) {
        const fs::path dbPath = fs::path(localAppData) / L"Amazon Games" / L"Data" / L"Games" / L"Sql" / L"GameInstallInfo.sqlite";
        if (fs::exists(dbPath)) {
            sqlite3* db = nullptr;
            const std::string utf8Path = WideToUtf8(dbPath.wstring());
            if (sqlite3_open_v2(utf8Path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK) {
                sqlite3_stmt* stmt = nullptr;
                const char* sql = "SELECT Id, ProductTitle, InstallDirectory FROM GameInstallInfo;";
                if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                    while (sqlite3_step(stmt) == SQLITE_ROW) {
                        const char* rawId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                        const char* rawTitle = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                        const char* rawDir = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));

                        std::wstring id = Utf8ToWide(rawId);
                        std::wstring title = Utf8ToWide(rawTitle);
                        std::wstring dir = Utf8ToWide(rawDir);

                        if (!dir.empty() && fs::exists(dir)) {
                            std::wstring lower = dir;
                            std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
                            if (seenDirs.insert(lower).second) {
                                const auto exe = FindPrimaryGameExecutable(dir);
                                GameItem item{};
                                item.appId = L"amzn_" + id;
                                item.title = title.empty() ? fs::path(dir).filename().wstring() : title;
                                item.platform = PlatformType::Amazon;
                                item.workingDirectory = dir;
                                if (!exe.empty()) {
                                    item.executablePath = exe.wstring();
                                }
                                item.launchUri = L"amazon-games://play/" + id;
                                games.push_back(std::move(item));
                            }
                        }
                    }
                    sqlite3_finalize(stmt);
                }
                sqlite3_close(db);
            }
        }
    }

    // Also scan all mounted drives for Amazon Games folders
    const auto drives = GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path amznDirs[] = {
            fs::path(drive) / L"Games" / L"Amazon Games",
            fs::path(drive) / L"Amazon Games"
        };
        for (const auto& dir : amznDirs) {
            if (!fs::exists(dir)) continue;
            std::error_code ec;
            for (const auto& entry : fs::directory_iterator(dir, ec)) {
                if (!entry.is_directory()) continue;
                std::wstring lower = entry.path().wstring();
                std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
                if (seenDirs.insert(lower).second) {
                    const auto exe = FindPrimaryGameExecutable(entry.path());
                    if (!exe.empty()) {
                        GameItem item{};
                        item.appId = L"amzn_" + entry.path().filename().wstring();
                        item.title = entry.path().filename().wstring();
                        item.platform = PlatformType::Amazon;
                        item.executablePath = exe.wstring();
                        item.workingDirectory = entry.path().wstring();
                        games.push_back(std::move(item));
                    }
                }
            }
        }
    }

    return games;
}

std::vector<GameItem> StorefrontScanner::ScanXbox() {
    std::vector<GameItem> games;
    std::set<std::wstring> seenDirs;

    // Scan all mounted drives for modern XboxGames folders (<Drive>\XboxGames)
    const auto drives = GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path xboxDir = fs::path(drive) / L"XboxGames";
        if (!fs::exists(xboxDir)) continue;

        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(xboxDir, ec)) {
            if (!entry.is_directory()) continue;
            const auto gameDir = entry.path();
            std::wstring lower = gameDir.wstring();
            std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
            if (!seenDirs.insert(lower).second) continue;

            const fs::path contentDir = gameDir / L"Content";
            const fs::path searchDir = fs::exists(contentDir) ? contentDir : gameDir;

            const fs::path launcherHelper = searchDir / L"gamelaunchhelper.exe";
            fs::path targetExe;
            if (fs::exists(launcherHelper)) {
                targetExe = launcherHelper;
            } else {
                targetExe = FindPrimaryGameExecutable(searchDir);
            }

            if (!targetExe.empty()) {
                GameItem item{};
                item.appId = L"xbox_" + gameDir.filename().wstring();
                item.title = gameDir.filename().wstring();
                item.platform = PlatformType::Xbox;
                item.executablePath = targetExe.wstring();
                item.workingDirectory = targetExe.parent_path().wstring();
                games.push_back(std::move(item));
            }
        }
    }

    return games;
}

std::vector<GameItem> StorefrontScanner::ScanCustomGames() {
    std::vector<GameItem> games;
    const fs::path metaDir = Storage::GetMetadataPath();
    if (!fs::exists(metaDir)) return games;

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(metaDir, ec)) {
        if (!entry.is_regular_file()) continue;
        const auto filename = entry.path().filename().wstring();
        if (filename.rfind(L"custom_", 0) == 0 && entry.path().extension() == L".json") {
            GameItem item{};
            item.appId = entry.path().stem().wstring();
            item.platform = PlatformType::CustomExecutable;
            if (LoadPerGameMetadata(item) && !item.executablePath.empty() && fs::exists(item.executablePath)) {
                if (item.title.empty()) {
                    item.title = fs::path(item.executablePath).stem().wstring();
                }
                games.push_back(std::move(item));
            }
        }
    }

    return games;
}

bool StorefrontScanner::AddCustomLocalGame(const std::wstring& executablePath, const std::wstring& customTitle) {
    if (executablePath.empty() || !fs::exists(executablePath)) return false;

    Storage::EnsureDirectoriesCreated();
    const auto now = GetTickCount64();
    GameItem item{};
    item.appId = L"custom_" + std::to_wstring(now);
    item.title = customTitle.empty() ? fs::path(executablePath).stem().wstring() : customTitle;
    item.platform = PlatformType::CustomExecutable;
    item.executablePath = executablePath;
    item.workingDirectory = fs::path(executablePath).parent_path().wstring();

    SavePerGameMetadata(item);
    SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_PATHW, Storage::GetUserRootPath().c_str(), nullptr);
    return true;
}

bool StorefrontScanner::LoadPerGameMetadata(GameItem& item) {
    const fs::path metaDir = Storage::GetMetadataPath();
    const fs::path jsonPath = metaDir / (item.appId + L".json");
    if (!fs::exists(jsonPath)) return false;

    std::wifstream stream(jsonPath);
    if (!stream.is_open()) return false;

    std::wstring line;
    while (std::getline(stream, line)) {
        std::wstring key, val;
        if (ParseKeyValueLine(line, key, val)) {
            if (key == L"title" && !val.empty()) item.title = val;
            else if (key == L"developer" && !val.empty()) item.developer = val;
            else if (key == L"publisher" && !val.empty()) item.publisher = val;
            else if (key == L"releaseDate" && !val.empty()) item.releaseDate = val;
            else if ((key == L"rating" || key == L"score") && !val.empty()) item.rating = val;
            else if ((key == L"synopsis" || key == L"description") && !val.empty()) item.synopsis = val;
            else if (key == L"genres" && !val.empty()) item.genres = val;
            else if ((key == L"coverArtPath" || key == L"cover") && !val.empty() && fs::exists(val)) item.coverArtPath = val;
            else if ((key == L"heroArtPath" || key == L"hero") && !val.empty() && fs::exists(val)) item.heroArtPath = val;
            else if ((key == L"iconPath" || key == L"icon") && !val.empty() && fs::exists(val)) item.iconPath = val;
            else if ((key == L"launchString" || key == L"launchCommand") && !val.empty()) item.launchCommand = val;
            else if (key == L"launchArguments" && !val.empty()) item.launchArguments = val;
            else if (key == L"executablePath" && !val.empty() && fs::exists(val)) item.executablePath = val;
            else if (key == L"workingDirectory" && !val.empty() && fs::exists(val)) item.workingDirectory = val;
        }
    }
    return true;
}

bool StorefrontScanner::SavePerGameMetadata(const GameItem& item) {
    Storage::EnsureDirectoriesCreated();
    const fs::path metaDir = Storage::GetMetadataPath();
    const fs::path jsonPath = metaDir / (item.appId + L".json");

    std::wofstream out(jsonPath);
    if (!out.is_open()) return false;

    const auto platStr = PlatformToString(item.platform);

    out << L"{\n";
    out << L"  \"appId\": \"" << item.appId << L"\",\n";
    out << L"  \"title\": \"" << item.title << L"\",\n";
    out << L"  \"platform\": \"" << std::wstring(platStr) << L"\",\n";
    out << L"  \"description\": \"" << item.synopsis << L"\",\n";
    out << L"  \"score\": \"" << item.rating << L"\",\n";
    out << L"  \"developer\": \"" << item.developer << L"\",\n";
    out << L"  \"publisher\": \"" << item.publisher << L"\",\n";
    out << L"  \"releaseDate\": \"" << item.releaseDate << L"\",\n";
    out << L"  \"genres\": \"" << item.genres << L"\",\n";
    out << L"  \"launchString\": \"" << (item.launchCommand.empty() ? item.launchUri : item.launchCommand) << L"\",\n";
    out << L"  \"launchArguments\": \"" << item.launchArguments << L"\",\n";
    out << L"  \"executablePath\": \"" << item.executablePath << L"\",\n";
    out << L"  \"workingDirectory\": \"" << item.workingDirectory << L"\",\n";
    out << L"  \"iconPath\": \"" << item.iconPath << L"\",\n";
    out << L"  \"coverArtPath\": \"" << item.coverArtPath << L"\",\n";
    out << L"  \"heroArtPath\": \"" << item.heroArtPath << L"\"\n";
    out << L"}\n";
    return true;
}

std::vector<GameItem> StorefrontScanner::ScanAllInstalledGames() {
    Storage::EnsureDirectoriesCreated();
    std::vector<GameItem> allGames;

    auto steam = ScanSteam();
    allGames.insert(allGames.end(), std::make_move_iterator(steam.begin()), std::make_move_iterator(steam.end()));

    auto epic = ScanEpic();
    allGames.insert(allGames.end(), std::make_move_iterator(epic.begin()), std::make_move_iterator(epic.end()));

    auto gog = ScanGOG();
    allGames.insert(allGames.end(), std::make_move_iterator(gog.begin()), std::make_move_iterator(gog.end()));

    auto itch = ScanItch();
    allGames.insert(allGames.end(), std::make_move_iterator(itch.begin()), std::make_move_iterator(itch.end()));

    auto ubi = ScanUbisoft();
    allGames.insert(allGames.end(), std::make_move_iterator(ubi.begin()), std::make_move_iterator(ubi.end()));

    auto rstar = ScanRockstar();
    allGames.insert(allGames.end(), std::make_move_iterator(rstar.begin()), std::make_move_iterator(rstar.end()));

    auto ea = ScanEA();
    allGames.insert(allGames.end(), std::make_move_iterator(ea.begin()), std::make_move_iterator(ea.end()));

    auto amzn = ScanAmazon();
    allGames.insert(allGames.end(), std::make_move_iterator(amzn.begin()), std::make_move_iterator(amzn.end()));

    auto xbox = ScanXbox();
    allGames.insert(allGames.end(), std::make_move_iterator(xbox.begin()), std::make_move_iterator(xbox.end()));

    auto custom = ScanCustomGames();
    allGames.insert(allGames.end(), std::make_move_iterator(custom.begin()), std::make_move_iterator(custom.end()));

    auto standalone = ScanStandaloneAndDrives();
    allGames.insert(allGames.end(), std::make_move_iterator(standalone.begin()), std::make_move_iterator(standalone.end()));

    std::vector<GameItem> deduped;
    std::set<std::wstring> seenKeys;
    for (auto& game : allGames) {
        std::wstring key = game.executablePath;
        std::transform(key.begin(), key.end(), key.begin(), ::towlower);
        if (key.empty()) {
            key = game.appId;
        }
        if (!key.empty() && seenKeys.count(key) > 0) {
            continue;
        }
        if (!key.empty()) seenKeys.insert(key);

        game.id = static_cast<int64_t>(deduped.size() + 1);
        LoadPerGameMetadata(game);
        deduped.push_back(std::move(game));
    }

    return deduped;
}

} // namespace GameExplorer
