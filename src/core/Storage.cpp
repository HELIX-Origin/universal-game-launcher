#include "GameExplorerCore/Storage.h"
#include <windows.h>
#include <shlobj.h>
#include <cstdlib>

namespace GameExplorer {

std::filesystem::path Storage::GetUserRootPath() {
    PWSTR pPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &pPath))) {
        std::filesystem::path root(pPath);
        CoTaskMemFree(pPath);
        return root / L"Game Explorer";
    }

    // Fallback to APPDATA or USERPROFILE environment variable
    const wchar_t* userProfile = _wgetenv(L"USERPROFILE");
    if (userProfile != nullptr) {
        return std::filesystem::path(userProfile) / L"Game Explorer";
    }

    return std::filesystem::current_path() / L"Game Explorer";
}

std::filesystem::path Storage::GetDatabasePath() {
    return GetUserRootPath() / L"library.db";
}

std::filesystem::path Storage::GetCachePath() {
    return GetUserRootPath() / L"cache";
}

std::filesystem::path Storage::GetArtPath() {
    return GetCachePath() / L"art";
}

std::filesystem::path Storage::GetMetadataPath() {
    return GetUserRootPath() / L"metadata";
}

std::filesystem::path Storage::GetBackupPath() {
    return GetUserRootPath() / L"backups";
}

std::filesystem::path Storage::GetOneDrivePath() {
    // Check specific OneDrive environment variables
    const wchar_t* oneDrive = _wgetenv(L"OneDriveConsumer");
    if (oneDrive == nullptr) {
        oneDrive = _wgetenv(L"OneDriveCommercial");
    }
    if (oneDrive == nullptr) {
        oneDrive = _wgetenv(L"OneDrive");
    }

    if (oneDrive != nullptr) {
        std::filesystem::path p(oneDrive);
        if (std::filesystem::exists(p)) {
            return p / L"Game Explorer";
        }
    }

    // Fallback check: %USERPROFILE%\OneDrive
    PWSTR pPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &pPath))) {
        std::filesystem::path profile(pPath);
        CoTaskMemFree(pPath);
        std::filesystem::path candidate = profile / L"OneDrive";
        if (std::filesystem::exists(candidate)) {
            return candidate / L"Game Explorer";
        }
    }

    return {};
}

bool Storage::EnsureDirectoriesCreated() {
    std::error_code ec;
    const auto root = GetUserRootPath();
    std::filesystem::create_directories(root, ec);
    if (ec) return false;

    std::filesystem::create_directories(GetCachePath(), ec);
    if (ec) return false;

    std::filesystem::create_directories(GetArtPath(), ec);
    if (ec) return false;

    std::filesystem::create_directories(GetMetadataPath(), ec);
    if (ec) return false;

    std::filesystem::create_directories(GetBackupPath(), ec);
    if (ec) return false;

    return true;
}

} // namespace GameExplorer
