#pragma once

#include "Models.h"
#include <vector>
#include <string>

namespace GameExplorer {

class Database;

class StorefrontScanner {
public:
    // Performs direct in-memory scan of all installed storefronts without database intermediate bottleneck
    static std::vector<GameItem> ScanAllInstalledGames();

    // Per-game metadata.json file persistence in %USERPROFILE%\Game Explorer\metadata\<appId>.json
    static bool LoadPerGameMetadata(GameItem& item);
    static bool SavePerGameMetadata(const GameItem& item);

    // Performs full scan of all installed storefronts and drives (internal and external) and persists to database
    static size_t ScanAll(Database& db);

    // Storefront scanners with multi-drive detection
    static std::vector<GameItem> ScanSteam();
    static std::vector<GameItem> ScanGOG();
    static std::vector<GameItem> ScanEpic();
    static std::vector<GameItem> ScanItch();
    static std::vector<GameItem> ScanUbisoft();
    static std::vector<GameItem> ScanRockstar();
    static std::vector<GameItem> ScanEA();
    static std::vector<GameItem> ScanAmazon();
    static std::vector<GameItem> ScanXbox();
    static std::vector<GameItem> ScanStandaloneAndDrives();
    static std::vector<GameItem> ScanCustomGames();

    // Manual addition of custom local game executables
    static bool AddCustomLocalGame(const std::wstring& executablePath, const std::wstring& customTitle = L"");

    // Utility & filtering helpers
    static bool IsBlacklistedExecutable(const std::wstring& filename);

    // Drive utilities
    static std::vector<std::wstring> GetAllMountedDrives();

private:
    static std::vector<std::wstring> GetSteamLibraryFolders(const std::wstring& steamPath);
    static void DiscoverSteamGridArtwork(
        const std::wstring& steamPath,
        const std::wstring& appId,
        std::wstring& outCover,
        std::wstring& outHero,
        std::wstring& outIcon
    );
};

} // namespace GameExplorer
