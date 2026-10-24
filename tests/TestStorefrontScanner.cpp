#include "TestHarness.h"
#include "GameExplorerCore/StorefrontScanner.h"
#include "GameExplorerCore/Storage.h"
#include "GameExplorerCore/Database.h"
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

TEST_CASE(CustomLocalGameLifecycle) {
    const fs::path tempExe = fs::temp_directory_path() / L"GameExplorer_CustomTestGame.exe";
    {
        std::ofstream stub(tempExe);
        stub << "MZ";
    }

    ASSERT_TRUE(GameExplorer::StorefrontScanner::AddCustomLocalGame(tempExe.wstring(), L"Custom Local Test Game"));

    const auto customGames = GameExplorer::StorefrontScanner::ScanCustomGames();
    std::wstring createdAppId;
    for (const auto& g : customGames) {
        if (g.title == L"Custom Local Test Game") {
            createdAppId = g.appId;
            break;
        }
    }

    ASSERT_FALSE(createdAppId.empty());
    ASSERT_TRUE(GameExplorer::StorefrontScanner::RemoveCustomLocalGame(createdAppId));

    if (fs::exists(tempExe)) {
        fs::remove(tempExe);
    }
}

TEST_CASE(ScannerScanAllPrunesObsoleteGames) {
    const fs::path tempDbPath = fs::temp_directory_path() / L"GameExplorer_TestPrune.db";
    if (fs::exists(tempDbPath)) fs::remove(tempDbPath);

    auto& db = GameExplorer::Database::Instance();
    // Use direct sqlite or test db
    ASSERT_TRUE(db.Open(tempDbPath));
    ASSERT_TRUE(db.InitializeSchema());

    GameExplorer::GameItem obsolete;
    obsolete.appId = L"itch_bogus_ghost_999";
    obsolete.title = L"Bogus Ghost Game";
    obsolete.platform = GameExplorer::PlatformType::Itch;
    ASSERT_TRUE(db.UpsertGame(obsolete));
    ASSERT_TRUE(db.GetGameByAppId(L"itch_bogus_ghost_999").has_value());

    // Run ScanAll - should prune obsolete ghost game not found in actual scans
    GameExplorer::StorefrontScanner::ScanAll(db);
    ASSERT_FALSE(db.GetGameByAppId(L"itch_bogus_ghost_999").has_value());

    db.Close();
    if (fs::exists(tempDbPath)) fs::remove(tempDbPath);
}
