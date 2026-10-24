#include "Registration.h"
#include "GameExplorerCore/Guid.h"
#include "GameExplorerCore/Database.h"
#include "GameExplorerCore/Storage.h"
#include "GameExplorerCore/StorefrontScanner.h"
#include <string>
#include <shlobj.h>
#include <filesystem>

namespace fs = std::filesystem;

namespace GameExplorer {

namespace {

bool SetRegistryValue(HKEY hRoot, const std::wstring& subKey, const std::wstring& valueName, const std::wstring& data) {
    HKEY hKey = nullptr;
    const LSTATUS status = RegCreateKeyExW(
        hRoot, subKey.c_str(), 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr
    );
    if (status != ERROR_SUCCESS) return false;

    const LSTATUS setStatus = RegSetValueExW(
        hKey,
        valueName.empty() ? nullptr : valueName.c_str(),
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(data.c_str()),
        static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t))
    );

    RegCloseKey(hKey);
    return (setStatus == ERROR_SUCCESS);
}

bool SetRegistryDword(HKEY hRoot, const std::wstring& subKey, const std::wstring& valueName, DWORD data) {
    HKEY hKey = nullptr;
    const LSTATUS status = RegCreateKeyExW(
        hRoot, subKey.c_str(), 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr
    );
    if (status != ERROR_SUCCESS) return false;

    const LSTATUS setStatus = RegSetValueExW(
        hKey,
        valueName.empty() ? nullptr : valueName.c_str(),
        0,
        REG_DWORD,
        reinterpret_cast<const BYTE*>(&data),
        sizeof(DWORD)
    );

    RegCloseKey(hKey);
    return (setStatus == ERROR_SUCCESS);
}

void DeleteRegistryKeyRecursive(HKEY hRoot, const std::wstring& subKey) {
    RegDeleteTreeW(hRoot, subKey.c_str());
}

void DeleteRegistryValue(HKEY hRoot, const std::wstring& subKey, const std::wstring& valueName) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(hRoot, subKey.c_str(), 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        RegDeleteValueW(hKey, valueName.c_str());
        RegCloseKey(hKey);
    }
}

} // namespace

HRESULT RegisterServer(HMODULE hModule) {
    wchar_t modulePath[MAX_PATH];
    if (GetModuleFileNameW(hModule, modulePath, MAX_PATH) == 0) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    const std::wstring clsidKey = std::wstring(L"Software\\Classes\\CLSID\\") + SZ_CLSID_GAME_EXPLORER;
    // Canonical Windows 11 icon: Embedded resource ID 100 inside GameExplorerShell.dll
    const std::wstring iconPath = std::wstring(modulePath) + L",-100";

    // 1. Register CLSID in HKCU (per-user registration requires no elevation)
    SetRegistryValue(HKEY_CURRENT_USER, clsidKey, L"", SZ_GAME_EXPLORER_TITLE);
    SetRegistryValue(HKEY_CURRENT_USER, clsidKey, L"Icon", iconPath);
    SetRegistryValue(HKEY_CURRENT_USER, clsidKey, L"System.IsPinnedToNameSpaceTree", L"1");
    SetRegistryDword(HKEY_CURRENT_USER, clsidKey, L"System.IsPinnedToNameSpaceTree", 1);
    SetRegistryDword(HKEY_CURRENT_USER, clsidKey, L"SortOrderIndex", 0x40); // Pinned alongside Gallery & Home

    // InProcServer32
    const std::wstring inprocKey = clsidKey + L"\\InProcServer32";
    SetRegistryValue(HKEY_CURRENT_USER, inprocKey, L"", modulePath);
    SetRegistryValue(HKEY_CURRENT_USER, inprocKey, L"ThreadingModel", L"Apartment");

    // DefaultIcon (Embedded Fluent Controller Icon)
    const std::wstring iconKey = clsidKey + L"\\DefaultIcon";
    SetRegistryValue(HKEY_CURRENT_USER, iconKey, L"", iconPath);

    // Link Canonical FOLDERTYPEID_Games {b689b0d0-76d3-4cbb-87f7-585d0e0ce070}
    SetRegistryValue(HKEY_CURRENT_USER, clsidKey, L"FolderTypeID", SZ_FOLDERTYPEID_GAMES);

    // ShellFolder attributes: SFGAO_FOLDER | SFGAO_BROWSABLE | SFGAO_CANLINK (NO SFGAO_HASSUBFOLDER 0x80000000 and NO SFGAO_STREAM 0x40000000)
    const std::wstring shellFolderKey = clsidKey + L"\\ShellFolder";
    constexpr DWORD sfgaoAttributes = 0x20000000 | 0x08000000 | 0x00000004;
    SetRegistryDword(HKEY_CURRENT_USER, shellFolderKey, L"Attributes", sfgaoAttributes);
    SetRegistryDword(HKEY_CURRENT_USER, shellFolderKey, L"FolderValueFlags", 0x28);

    // Register CLSID_GameCategorizer in HKCU
    const std::wstring catClsidKey = std::wstring(L"Software\\Classes\\CLSID\\") + SZ_CLSID_GAME_CATEGORIZER;
    SetRegistryValue(HKEY_CURRENT_USER, catClsidKey, L"", L"Game Explorer Categorizer");
    SetRegistryValue(HKEY_CURRENT_USER, catClsidKey + L"\\InProcServer32", L"", modulePath);
    SetRegistryValue(HKEY_CURRENT_USER, catClsidKey + L"\\InProcServer32", L"ThreadingModel", L"Apartment");

    // Register FolderType Games with default GroupBy = System.ItemTypeText and LogicalViewMode = 3 (Tiles / Large Icons)
    const std::wstring ftKey = std::wstring(L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FolderTypes\\") + SZ_FOLDERTYPEID_GAMES;
    SetRegistryValue(HKEY_CURRENT_USER, ftKey, L"CanonicalName", L"Games");
    SetRegistryDword(HKEY_CURRENT_USER, ftKey, L"LayoutType", 1);
    const std::wstring topViewsKey = ftKey + L"\\TopViews\\{00000000-0000-0000-0000-000000000000}";
    SetRegistryValue(HKEY_CURRENT_USER, topViewsKey, L"ColumnList", L"prop:0System.ItemNameDisplay;0System.Company;0System.ItemTypeText;0System.Rating;0System.Size;0System.Media.Year");
    SetRegistryValue(HKEY_CURRENT_USER, topViewsKey, L"GroupBy", L"System.ItemTypeText");
    SetRegistryDword(HKEY_CURRENT_USER, topViewsKey, L"LogicalViewMode", 3);
    SetRegistryDword(HKEY_CURRENT_USER, topViewsKey, L"IconSize", 96);
    SetRegistryDword(HKEY_CURRENT_USER, topViewsKey, L"Order", 0);
    SetRegistryValue(HKEY_CURRENT_USER, topViewsKey, L"Name", L"NoName");

    // 2. Register into File Explorer Desktop NameSpace (Navigation Pane Games tab)
    const std::wstring nsKey = std::wstring(L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Desktop\\NameSpace\\") + SZ_CLSID_GAME_EXPLORER;
    SetRegistryValue(HKEY_CURRENT_USER, nsKey, L"", SZ_GAME_EXPLORER_TITLE);

    // 3. Always hide from the Desktop surface (1 = hidden on Desktop, visible only in File Explorer Navigation Pane like Home & Gallery)
    SetRegistryDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons\\NewStartPanel", SZ_CLSID_GAME_EXPLORER, 1);
    SetRegistryDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons\\ClassicStartMenu", SZ_CLSID_GAME_EXPLORER, 1);

    // 4. Try HKLM registration if elevated
    SetRegistryValue(HKEY_LOCAL_MACHINE, clsidKey, L"", SZ_GAME_EXPLORER_TITLE);
    SetRegistryValue(HKEY_LOCAL_MACHINE, clsidKey, L"Icon", iconPath);
    SetRegistryDword(HKEY_LOCAL_MACHINE, clsidKey, L"System.IsPinnedToNameSpaceTree", 1);
    SetRegistryDword(HKEY_LOCAL_MACHINE, clsidKey, L"SortOrderIndex", 0x40);
    SetRegistryValue(HKEY_LOCAL_MACHINE, inprocKey, L"", modulePath);
    SetRegistryValue(HKEY_LOCAL_MACHINE, inprocKey, L"ThreadingModel", L"Apartment");
    SetRegistryValue(HKEY_LOCAL_MACHINE, iconKey, L"", iconPath);
    SetRegistryDword(HKEY_LOCAL_MACHINE, shellFolderKey, L"Attributes", sfgaoAttributes);
    SetRegistryDword(HKEY_LOCAL_MACHINE, shellFolderKey, L"FolderValueFlags", 0x28);
    SetRegistryValue(HKEY_LOCAL_MACHINE, nsKey, L"", SZ_GAME_EXPLORER_TITLE);
    SetRegistryDword(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons\\NewStartPanel", SZ_CLSID_GAME_EXPLORER, 1);
    SetRegistryDword(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons\\ClassicStartMenu", SZ_CLSID_GAME_EXPLORER, 1);

    // Register CLSID_GameCategorizer in HKLM
    SetRegistryValue(HKEY_LOCAL_MACHINE, catClsidKey, L"", L"Game Explorer Categorizer");
    SetRegistryValue(HKEY_LOCAL_MACHINE, catClsidKey + L"\\InProcServer32", L"", modulePath);
    SetRegistryValue(HKEY_LOCAL_MACHINE, catClsidKey + L"\\InProcServer32", L"ThreadingModel", L"Apartment");

    // Register FolderType Games in HKLM
    SetRegistryValue(HKEY_LOCAL_MACHINE, ftKey, L"CanonicalName", L"Games");
    SetRegistryDword(HKEY_LOCAL_MACHINE, ftKey, L"LayoutType", 1);
    SetRegistryValue(HKEY_LOCAL_MACHINE, topViewsKey, L"ColumnList", L"prop:0System.ItemNameDisplay;0System.Company;0System.ItemTypeText;0System.Rating;0System.Size;0System.Media.Year");
    SetRegistryValue(HKEY_LOCAL_MACHINE, topViewsKey, L"GroupBy", L"System.ItemTypeText");
    SetRegistryDword(HKEY_LOCAL_MACHINE, topViewsKey, L"LogicalViewMode", 3);
    SetRegistryDword(HKEY_LOCAL_MACHINE, topViewsKey, L"IconSize", 96);
    SetRegistryDword(HKEY_LOCAL_MACHINE, topViewsKey, L"Order", 0);
    SetRegistryValue(HKEY_LOCAL_MACHINE, topViewsKey, L"Name", L"NoName");

    // Known Folder description (HKCU and HKLM)
    const std::wstring folderDescKey = std::wstring(L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FolderDescriptions\\") + SZ_FOLDERID_GAME_EXPLORER;
    SetRegistryValue(HKEY_CURRENT_USER, folderDescKey, L"Name", L"Games");
    SetRegistryValue(HKEY_CURRENT_USER, folderDescKey, L"Icon", iconPath);
    SetRegistryDword(HKEY_CURRENT_USER, folderDescKey, L"Category", 1); // KF_CATEGORY_VIRTUAL
    SetRegistryValue(HKEY_CURRENT_USER, folderDescKey, L"ParsingName", std::wstring(L"::") + SZ_CLSID_GAME_EXPLORER);
    SetRegistryValue(HKEY_CURRENT_USER, folderDescKey, L"FolderTypeID", SZ_FOLDERTYPEID_GAMES);

    SetRegistryValue(HKEY_LOCAL_MACHINE, folderDescKey, L"Name", L"Games");
    SetRegistryValue(HKEY_LOCAL_MACHINE, folderDescKey, L"Icon", iconPath);
    SetRegistryDword(HKEY_LOCAL_MACHINE, folderDescKey, L"Category", 1); // KF_CATEGORY_VIRTUAL
    SetRegistryValue(HKEY_LOCAL_MACHINE, folderDescKey, L"ParsingName", std::wstring(L"::") + SZ_CLSID_GAME_EXPLORER);
    SetRegistryValue(HKEY_LOCAL_MACHINE, folderDescKey, L"FolderTypeID", SZ_FOLDERTYPEID_GAMES);

    // Copy on-disk game-explorer-icon.ico to library folder for external shell queries
    const fs::path libDir = Storage::GetUserRootPath();
    std::error_code ec;
    fs::create_directories(libDir, ec);
    const fs::path moduleDir = fs::path(modulePath).parent_path();
    const fs::path localIco = libDir / L"game-explorer-icon.ico";
    const fs::path builtIco = moduleDir / L"Resource.ico";
    if (fs::exists(builtIco)) {
        fs::copy_file(builtIco, localIco, fs::copy_options::overwrite_existing, ec);
    }

    // Clean up any legacy GameExplorerAgent Run entry if previously set
    DeleteRegistryValue(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"GameExplorerAgent");

    // Automatically populate the database with installed games immediately upon registration
    auto& db = Database::Instance();
    if (!db.IsOpen()) {
        db.Open(Storage::GetDatabasePath());
    }
    StorefrontScanner::ScanAll(db);

    // Notify shell that associations and namespaces have updated
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

    return S_OK;
}

HRESULT UnregisterServer() {
    const std::wstring clsidKey = std::wstring(L"Software\\Classes\\CLSID\\") + SZ_CLSID_GAME_EXPLORER;
    const std::wstring catClsidKey = std::wstring(L"Software\\Classes\\CLSID\\") + SZ_CLSID_GAME_CATEGORIZER;
    const std::wstring ftKey = std::wstring(L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FolderTypes\\") + SZ_FOLDERTYPEID_GAMES;
    const std::wstring nsKey = std::wstring(L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Desktop\\NameSpace\\") + SZ_CLSID_GAME_EXPLORER;
    const std::wstring folderDescKey = std::wstring(L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FolderDescriptions\\") + SZ_FOLDERID_GAME_EXPLORER;

    // Signal running background agent to cleanly terminate
    HWND hwndAgent = FindWindowW(L"WindowsGameExplorerAgentClass", nullptr);
    if (hwndAgent != nullptr) {
        PostMessageW(hwndAgent, WM_CLOSE, 0, 0);
    }

    // Delete HKCU registrations
    DeleteRegistryKeyRecursive(HKEY_CURRENT_USER, clsidKey);
    DeleteRegistryKeyRecursive(HKEY_CURRENT_USER, catClsidKey);
    DeleteRegistryKeyRecursive(HKEY_CURRENT_USER, ftKey);
    DeleteRegistryKeyRecursive(HKEY_CURRENT_USER, nsKey);
    DeleteRegistryKeyRecursive(HKEY_CURRENT_USER, folderDescKey);
    DeleteRegistryValue(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons\\NewStartPanel", SZ_CLSID_GAME_EXPLORER);
    DeleteRegistryValue(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons\\ClassicStartMenu", SZ_CLSID_GAME_EXPLORER);
    DeleteRegistryValue(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"GameExplorerAgent");

    // Delete HKLM registrations
    DeleteRegistryKeyRecursive(HKEY_LOCAL_MACHINE, clsidKey);
    DeleteRegistryKeyRecursive(HKEY_LOCAL_MACHINE, catClsidKey);
    DeleteRegistryKeyRecursive(HKEY_LOCAL_MACHINE, ftKey);
    DeleteRegistryKeyRecursive(HKEY_LOCAL_MACHINE, nsKey);
    DeleteRegistryKeyRecursive(HKEY_LOCAL_MACHINE, folderDescKey);
    DeleteRegistryValue(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons\\NewStartPanel", SZ_CLSID_GAME_EXPLORER);
    DeleteRegistryValue(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons\\ClassicStartMenu", SZ_CLSID_GAME_EXPLORER);

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}

} // namespace GameExplorer
