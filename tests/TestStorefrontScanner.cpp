#include "TestHarness.h"
#include "GameExplorerCore/StorefrontScanner.h"
#include "GameExplorerCore/Storage.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST_CASE(ScannerMountedDrivesDiscovery) {
    const auto drives = GameExplorer::StorefrontScanner::GetAllMountedDrives();
    ASSERT_FALSE(drives.empty());

    // C: should always be present on standard Windows systems
    bool hasDriveC = false;
    for (const auto& d : drives) {
        if (_wcsicmp(d.c_str(), L"C:") == 0 || _wcsicmp(d.c_str(), L"C:\\") == 0) {
            hasDriveC = true;
            break;
        }
    }
    ASSERT_TRUE(hasDriveC);
}

TEST_CASE(ScannerBlacklistExecutableFilter) {
    // Tests that non-game helpers and uninstallers are properly excluded
    ASSERT_TRUE(GameExplorer::StorefrontScanner::IsBlacklistedExecutable(L"unins000.exe"));
    ASSERT_TRUE(GameExplorer::StorefrontScanner::IsBlacklistedExecutable(L"UnityCrashHandler64.exe"));
    ASSERT_TRUE(GameExplorer::StorefrontScanner::IsBlacklistedExecutable(L"dxsetup.exe"));
    ASSERT_FALSE(GameExplorer::StorefrontScanner::IsBlacklistedExecutable(L"MyGame.exe"));
    ASSERT_FALSE(GameExplorer::StorefrontScanner::IsBlacklistedExecutable(L"EldenRing.exe"));
}

TEST_CASE(PerGameMetadataSerializationAndPriority) {
    const fs::path metaDir = GameExplorer::Storage::GetMetadataPath();
    fs::create_directories(metaDir);

    const std::wstring testAppId = L"test_game_priority_9999";
    const fs::path jsonPath = metaDir / (testAppId + L".json");

    // 1. Create a simulated metadata.json (written by GameExplorerManager.exe)
    GameExplorer::GameItem original;
    original.appId = testAppId;
    original.title = L"Custom User Title Override";
    original.developer = L"Original Studio";
    original.rating = L"90";

    ASSERT_TRUE(GameExplorer::StorefrontScanner::SavePerGameMetadata(original));
    ASSERT_TRUE(fs::exists(jsonPath));

    // 2. Load it back into an item and verify user overrides and missing fields are applied
    GameExplorer::GameItem incoming;
    incoming.appId = testAppId;
    incoming.title = L"Default Scanner Title";

    ASSERT_TRUE(GameExplorer::StorefrontScanner::LoadPerGameMetadata(incoming));

    ASSERT_EQ(incoming.title, L"Custom User Title Override");
    ASSERT_EQ(incoming.developer, L"Original Studio");
    ASSERT_EQ(incoming.rating, L"90");

    // Cleanup
    if (fs::exists(jsonPath)) {
        fs::remove(jsonPath);
    }
}
