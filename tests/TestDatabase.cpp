#include "TestHarness.h"
#include "GameExplorerCore/Database.h"
#include <filesystem>

namespace fs = std::filesystem;

TEST_CASE(DatabaseInitializationAndSchema) {
    auto& db = GameExplorer::Database::Instance();
    const fs::path testDbPath = fs::temp_directory_path() / L"GameExplorer_Test_Init.db";
    if (fs::exists(testDbPath)) {
        fs::remove(testDbPath);
    }

    ASSERT_TRUE(db.Open(testDbPath));
    ASSERT_TRUE(db.IsOpen());

    db.Close();
    ASSERT_FALSE(db.IsOpen());

    if (fs::exists(testDbPath)) {
        fs::remove(testDbPath);
    }
}

TEST_CASE(DatabaseUpsertAndQueryGame) {
    auto& db = GameExplorer::Database::Instance();
    const fs::path testDbPath = fs::temp_directory_path() / L"GameExplorer_Test_Games.db";
    if (fs::exists(testDbPath)) {
        fs::remove(testDbPath);
    }

    ASSERT_TRUE(db.Open(testDbPath));

    GameExplorer::GameItem item;
    item.appId = L"steam_123456";
    item.title = L"Test Adventure RPG";
    item.platform = GameExplorer::PlatformType::Steam;
    item.executablePath = L"C:\\Games\\TestRPG\\game.exe";
    item.workingDirectory = L"C:\\Games\\TestRPG";
    item.diskSizeBytes = 1024ULL * 1024ULL * 1024ULL * 25ULL; // 25 GB
    item.developer = L"HELIX Studios";
    item.publisher = L"HELIX Publishing";
    item.releaseDate = L"2026-10-23";
    item.rating = L"95";

    ASSERT_TRUE(db.UpsertGame(item));

    const auto fetched = db.GetGameByAppId(L"steam_123456");
    ASSERT_TRUE(fetched.has_value());
    ASSERT_EQ(fetched->title, L"Test Adventure RPG");
    ASSERT_EQ(fetched->developer, L"HELIX Studios");
    ASSERT_EQ(fetched->rating, L"95");
    ASSERT_EQ(fetched->platform, GameExplorer::PlatformType::Steam);

    const auto all = db.GetAllGames();
    ASSERT_EQ(all.size(), static_cast<size_t>(1));

    db.Close();
    if (fs::exists(testDbPath)) {
        fs::remove(testDbPath);
    }
}

TEST_CASE(DatabaseKeyValueSettingsPersistence) {
    auto& db = GameExplorer::Database::Instance();
    const fs::path testDbPath = fs::temp_directory_path() / L"GameExplorer_Test_Meta.db";
    if (fs::exists(testDbPath)) {
        fs::remove(testDbPath);
    }

    ASSERT_TRUE(db.Open(testDbPath));

    const std::wstring testVal = L"{\"developer\":\"TestDev\",\"score\":\"88\"}";
    ASSERT_TRUE(db.SetSetting(L"steam_999", testVal));

    const auto retrievedVal = db.GetSetting(L"steam_999");
    ASSERT_TRUE(retrievedVal.has_value());
    ASSERT_EQ(*retrievedVal, testVal);

    db.Close();
    if (fs::exists(testDbPath)) {
        fs::remove(testDbPath);
    }
}
