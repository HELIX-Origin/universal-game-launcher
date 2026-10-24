#include "TestHarness.h"
#include "GameExplorerCore/Settings.h"
#include "GameExplorerCore/Types.h"

TEST_CASE(SettingsDefaultsAndLayoutModes) {
    GameExplorer::AppSettings s;
    ASSERT_EQ(s.layoutMode, GameExplorer::LayoutMode::CoverArt);
    ASSERT_FALSE(s.enableOneDriveBackup);
    ASSERT_TRUE(s.steamGridDbApiKey.empty());
    ASSERT_TRUE(s.igdbClientId.empty());

    // Layout mode enum values
    ASSERT_EQ(static_cast<int>(GameExplorer::LayoutMode::CoverArt), 0);
    ASSERT_EQ(static_cast<int>(GameExplorer::LayoutMode::HeroBanner), 1);
    ASSERT_EQ(static_cast<int>(GameExplorer::LayoutMode::DetailsView), 2);
}
