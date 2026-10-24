#pragma once

#include "Types.h"
#include <string>
#include <cstdint>

namespace GameExplorer {

struct GameItem {
    int64_t id{ 0 };
    std::wstring appId;             // e.g., "steam_730", "epic_Fortnite"
    std::wstring title;
    std::wstring sortTitle;
    PlatformType platform{ PlatformType::Unknown };
    
    // Execution paths & commands
    std::wstring executablePath;
    std::wstring launchArguments;
    std::wstring workingDirectory;
    std::wstring installPath;
    std::wstring launchUri;         // e.g. "steam://rungameid/730"
    std::wstring launchCommand;     // e.g. custom command line or executable with arguments
    
    // Artwork & Assets (paths in %USERPROFILE%\Game Explorer\cache\art\)
    std::wstring coverArtPath;      // Tall box art (~2:3 or ~3:4)
    std::wstring heroArtPath;       // Wide steam hero banner (~16:9 or ~21:9)
    std::wstring iconPath;          // Square icon for list details view
    std::wstring coverArtUrl;       // Remote CDN / storefront image link
    std::wstring heroArtUrl;        // Remote CDN / storefront image link
    std::wstring iconUrl;           // Remote CDN / storefront image link
    
    // Metadata
    std::wstring developer;
    std::wstring publisher;
    std::wstring releaseDate;
    std::wstring rating;            // ESRB/PEGI or review rating
    std::wstring synopsis;
    std::wstring genres;
    std::wstring officialWebsite;
    std::wstring storePageUrl;
    
    // Status & Statistics
    bool isInstalled{ true };
    bool isFavorite{ false };
    uint64_t diskSizeBytes{ 0 };
    uint32_t playtimeMinutes{ 0 };
    int64_t lastPlayedTimestamp{ 0 };
};

} // namespace GameExplorer
