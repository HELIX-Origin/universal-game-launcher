#pragma once

#include <string>
#include <string_view>
#include <cstdint>

namespace GameExplorer {

enum class LayoutMode : uint32_t {
    CoverArt = 0,    // Tall box art style (~2:3 or ~3:4 aspect ratio)
    HeroBanner = 1,  // Wide banner style (~16:9 or ~21:9 aspect ratio)
    DetailsView = 2  // Structured list with app icons, title, rating, etc.
};

[[nodiscard]] constexpr std::wstring_view LayoutModeToString(LayoutMode mode) noexcept {
    switch (mode) {
    case LayoutMode::CoverArt:
        return L"CoverArt";
    case LayoutMode::HeroBanner:
        return L"HeroBanner";
    case LayoutMode::DetailsView:
        return L"DetailsView";
    default:
        return L"CoverArt";
    }
}

[[nodiscard]] inline LayoutMode StringToLayoutMode(std::wstring_view str) noexcept {
    if (str == L"HeroBanner") {
        return LayoutMode::HeroBanner;
    }
    if (str == L"DetailsView") {
        return LayoutMode::DetailsView;
    }
    return LayoutMode::CoverArt;
}

enum class PlatformType : uint32_t {
    Unknown = 0,
    Steam = 1,
    Epic = 2,
    GOG = 3,
    BattleNet = 4,
    EAApp = 5,
    Xbox = 6,
    RetroArch = 7,
    RPCS3 = 8,
    Dolphin = 9,
    CustomExecutable = 10,
    Itch = 11,
    SquareEnix = 12,
    Ubisoft = 13,
    Rockstar = 14,
    Amazon = 15
};

[[nodiscard]] constexpr std::wstring_view PlatformToString(PlatformType platform) noexcept {
    switch (platform) {
    case PlatformType::Steam:
        return L"Steam";
    case PlatformType::Epic:
        return L"Epic Games";
    case PlatformType::GOG:
        return L"GOG";
    case PlatformType::BattleNet:
        return L"Battle.net";
    case PlatformType::EAApp:
        return L"EA App";
    case PlatformType::Xbox:
        return L"Xbox";
    case PlatformType::RetroArch:
        return L"RetroArch";
    case PlatformType::RPCS3:
        return L"RPCS3";
    case PlatformType::Dolphin:
        return L"Dolphin";
    case PlatformType::Itch:
        return L"itch.io";
    case PlatformType::SquareEnix:
        return L"Square Enix";
    case PlatformType::Ubisoft:
        return L"Ubisoft";
    case PlatformType::Rockstar:
        return L"Rockstar Games";
    case PlatformType::Amazon:
        return L"Amazon Games";
    case PlatformType::CustomExecutable:
        return L"Standalone";
    default:
        return L"PC";
    }
}

[[nodiscard]] inline PlatformType StringToPlatform(std::wstring_view str) noexcept {
    if (str == L"Steam") return PlatformType::Steam;
    if (str == L"Epic Games" || str == L"Epic") return PlatformType::Epic;
    if (str == L"GOG" || str == L"GOG Galaxy") return PlatformType::GOG;
    if (str == L"Battle.net") return PlatformType::BattleNet;
    if (str == L"EA App" || str == L"EA" || str == L"Origin") return PlatformType::EAApp;
    if (str == L"Xbox" || str == L"Microsoft Store") return PlatformType::Xbox;
    if (str == L"itch.io" || str == L"itch") return PlatformType::Itch;
    if (str == L"Square Enix") return PlatformType::SquareEnix;
    if (str == L"Ubisoft" || str == L"Uplay") return PlatformType::Ubisoft;
    if (str == L"Rockstar Games" || str == L"Rockstar") return PlatformType::Rockstar;
    if (str == L"Amazon Games" || str == L"Amazon") return PlatformType::Amazon;
    if (str == L"RetroArch") return PlatformType::RetroArch;
    if (str == L"RPCS3") return PlatformType::RPCS3;
    if (str == L"Dolphin") return PlatformType::Dolphin;
    if (str == L"Standalone" || str == L"Custom") return PlatformType::CustomExecutable;
    return PlatformType::Unknown;
}

} // namespace GameExplorer
