#include "ShellFolder.h"
#include "ShellView.h"
#include "GameItemUI.h"
#include "CategoryProvider.h"
#include "GameExplorerCore/Guid.h"
#include "GameExplorerCore/Database.h"
#include "GameExplorerCore/Storage.h"
#include "GameExplorerCore/Settings.h"
#include "GameExplorerCore/StorefrontScanner.h"
#include "GameExplorerCore/PlatformLauncher.h"
#include <shlwapi.h>
#include <shellapi.h>
#include <strsafe.h>
#include <propkey.h>
#include <filesystem>

namespace fs = std::filesystem;

extern std::atomic<ULONG> g_dllRefCount;
extern HINSTANCE g_hInstance;

namespace GameExplorer {

namespace {

std::wstring GetPlatformCompany(PlatformType platform) {
    switch (platform) {
    case PlatformType::Steam: return L"Valve Corporation";
    case PlatformType::Epic: return L"Epic Games, Inc.";
    case PlatformType::GOG: return L"CD PROJEKT / GOG";
    case PlatformType::BattleNet: return L"Blizzard Entertainment";
    case PlatformType::Ubisoft: return L"Ubisoft";
    case PlatformType::EAApp: return L"Electronic Arts";
    case PlatformType::Itch: return L"itch corp.";
    case PlatformType::Xbox: return L"Microsoft Corporation";
    default: return L"Game Provider";
    }
}

std::wstring GetPlatformExecutablePath(PlatformType platform) {
    switch (platform) {
    case PlatformType::Steam: return PlatformLauncher::GetSteamExecutablePath();
    case PlatformType::Epic: return PlatformLauncher::GetEpicExecutablePath();
    case PlatformType::GOG: return PlatformLauncher::GetGOGGalaxyExecutablePath();
    case PlatformType::BattleNet: return PlatformLauncher::GetBattleNetExecutablePath();
    case PlatformType::Ubisoft: return PlatformLauncher::GetUbisoftExecutablePath();
    case PlatformType::EAApp: return PlatformLauncher::GetEAAppExecutablePath();
    case PlatformType::Itch: return PlatformLauncher::GetItchExecutablePath();
    default: return L"";
    }
}

} // namespace

constexpr USHORT GAME_PIDL_MAGIC = 0x4745; // 'GE'

// EnumIDList implementation
class GameEnumIDList final : public IEnumIDList {
public:
    explicit GameEnumIDList(std::vector<PITEMID_CHILD> items)
        : m_items(std::move(items)) {
        ++g_dllRefCount;
    }

    ~GameEnumIDList() {
        for (auto* p : m_items) {
            CoTaskMemFree(p);
        }
        --g_dllRefCount;
    }

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (ppv == nullptr) return E_POINTER;
        *ppv = nullptr;

        if (riid == IID_IUnknown || riid == IID_IEnumIDList) {
            *ppv = static_cast<IEnumIDList*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    IFACEMETHODIMP_(ULONG) AddRef() override {
        return ++m_refCount;
    }

    IFACEMETHODIMP_(ULONG) Release() override {
        const ULONG count = --m_refCount;
        if (count == 0) {
            delete this;
        }
        return count;
    }

    IFACEMETHODIMP Next(ULONG celt, PITEMID_CHILD* rgelt, ULONG* pceltFetched) override {
        if (rgelt == nullptr) return E_POINTER;
        if (celt > 1 && pceltFetched == nullptr) return E_INVALIDARG;

        ULONG fetched = 0;
        while (fetched < celt && m_currentIndex < m_items.size()) {
            // Clone item PIDL
            const auto* src = m_items[m_currentIndex];
            const auto cb = src->mkid.cb + sizeof(USHORT); // + null terminator
            auto* clone = static_cast<PITEMID_CHILD>(CoTaskMemAlloc(cb));
            if (clone == nullptr) break;

            memcpy(clone, src, cb);
            rgelt[fetched] = clone;
            ++fetched;
            ++m_currentIndex;
        }

        if (pceltFetched != nullptr) {
            *pceltFetched = fetched;
        }

        return (fetched == celt) ? S_OK : S_FALSE;
    }

    IFACEMETHODIMP Skip(ULONG celt) override {
        m_currentIndex += celt;
        if (m_currentIndex > m_items.size()) {
            m_currentIndex = m_items.size();
            return S_FALSE;
        }
        return S_OK;
    }

    IFACEMETHODIMP Reset() override {
        m_currentIndex = 0;
        return S_OK;
    }

    IFACEMETHODIMP Clone(IEnumIDList** ppenum) override {
        if (ppenum == nullptr) return E_POINTER;
        *ppenum = nullptr;
        return E_NOTIMPL;
    }

private:
    std::atomic<ULONG> m_refCount{ 1 };
    std::vector<PITEMID_CHILD> m_items;
    size_t m_currentIndex{ 0 };
};

// ShellFolder implementation
ShellFolder::ShellFolder() {
    ++g_dllRefCount;
}

ShellFolder::~ShellFolder() {
    if (m_pidlRoot != nullptr) {
        CoTaskMemFree(m_pidlRoot);
        m_pidlRoot = nullptr;
    }
    --g_dllRefCount;
}

IFACEMETHODIMP ShellFolder::QueryInterface(REFIID riid, void** ppv) {
    if (ppv == nullptr) return E_POINTER;
    *ppv = nullptr;

    if (riid == IID_IUnknown || riid == IID_IShellFolder || riid == IID_IShellFolder2) {
        *ppv = static_cast<IShellFolder2*>(this);
    } else if (riid == IID_IPersist || riid == IID_IPersistFolder || riid == IID_IPersistFolder2) {
        *ppv = static_cast<IPersistFolder2*>(this);
    } else if (riid == IID_IExtractIconW) {
        *ppv = static_cast<IExtractIconW*>(this);
    }

    if (*ppv != nullptr) {
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

IFACEMETHODIMP_(ULONG) ShellFolder::AddRef() {
    return ++m_refCount;
}

IFACEMETHODIMP_(ULONG) ShellFolder::Release() {
    const ULONG count = --m_refCount;
    if (count == 0) {
        delete this;
    }
    return count;
}

IFACEMETHODIMP ShellFolder::GetClassID(CLSID* pClassID) {
    if (pClassID == nullptr) return E_POINTER;
    *pClassID = CLSID_GameExplorer;
    return S_OK;
}

IFACEMETHODIMP ShellFolder::Initialize(PCIDLIST_ABSOLUTE pidl) {
    if (m_pidlRoot != nullptr) {
        CoTaskMemFree(m_pidlRoot);
        m_pidlRoot = nullptr;
    }
    if (pidl != nullptr) {
        m_pidlRoot = ILCloneFull(pidl);
    }
    return S_OK;
}

IFACEMETHODIMP ShellFolder::GetCurFolder(PIDLIST_ABSOLUTE* ppidl) {
    if (ppidl == nullptr) return E_POINTER;
    *ppidl = nullptr;
    if (m_pidlRoot == nullptr) {
        return S_FALSE; // Not initialized yet — do NOT call SHParseDisplayName!
    }
    *ppidl = ILCloneFull(m_pidlRoot);
    return (*ppidl != nullptr) ? S_OK : E_OUTOFMEMORY;
}

PITEMID_CHILD ShellFolder::CreateGamePidl(const GameItem& item) {
    const USHORT dataSize = sizeof(GamePidlData);
    const USHORT totalSize = dataSize + sizeof(USHORT); // includes null terminator SHITEMID

    auto* pidl = static_cast<PITEMID_CHILD>(CoTaskMemAlloc(totalSize));
    if (pidl == nullptr) return nullptr;

    memset(pidl, 0, totalSize);
    auto* data = reinterpret_cast<GamePidlData*>(pidl);
    data->cb = dataSize;
    data->magic = GAME_PIDL_MAGIC;
    data->itemKind = 1; // Game
    data->platform = static_cast<uint16_t>(item.platform);
    data->gameId = item.id;
    StringCchCopyW(data->title, ARRAYSIZE(data->title), item.title.c_str());

    return pidl;
}

PITEMID_CHILD ShellFolder::CreateProviderPidl(PlatformType platform, const std::wstring& title) {
    const USHORT dataSize = sizeof(GamePidlData);
    const USHORT totalSize = dataSize + sizeof(USHORT); // includes null terminator SHITEMID

    auto* pidl = static_cast<PITEMID_CHILD>(CoTaskMemAlloc(totalSize));
    if (pidl == nullptr) return nullptr;

    memset(pidl, 0, totalSize);
    auto* data = reinterpret_cast<GamePidlData*>(pidl);
    data->cb = dataSize;
    data->magic = GAME_PIDL_MAGIC;
    data->itemKind = 0; // Provider
    data->platform = static_cast<uint16_t>(platform);
    data->gameId = 0;
    StringCchCopyW(data->title, ARRAYSIZE(data->title), title.c_str());

    return pidl;
}

bool ShellFolder::IsGamePidl(PCUIDLIST_RELATIVE pidl) noexcept {
    if (pidl == nullptr || pidl->mkid.cb < sizeof(GamePidlData)) {
        return false;
    }
    const auto* data = reinterpret_cast<const GamePidlData*>(pidl);
    return (data->magic == GAME_PIDL_MAGIC);
}

const GamePidlData* ShellFolder::GetGamePidlData(PCUIDLIST_RELATIVE pidl) noexcept {
    if (!IsGamePidl(pidl)) return nullptr;
    return reinterpret_cast<const GamePidlData*>(pidl);
}

IFACEMETHODIMP ShellFolder::ParseDisplayName(
    HWND /*hwnd*/, IBindCtx* /*pbc*/, PWSTR pszDisplayName,
    ULONG* pchEaten, PIDLIST_RELATIVE* ppidl, ULONG* pdwAttributes) {
    if (ppidl != nullptr) *ppidl = nullptr;
    if (pszDisplayName == nullptr) return E_INVALIDARG;

    if (pszDisplayName[0] == L'\0' || _wcsicmp(pszDisplayName, (std::wstring(L"::") + SZ_CLSID_GAME_EXPLORER).c_str()) == 0) {
        auto* pidl = static_cast<PITEMID_CHILD>(CoTaskMemAlloc(sizeof(USHORT)));
        if (pidl == nullptr) return E_OUTOFMEMORY;
        pidl->mkid.cb = 0;
        if (pchEaten != nullptr) *pchEaten = static_cast<ULONG>(wcslen(pszDisplayName));
        if (pdwAttributes != nullptr) *pdwAttributes = (SFGAO_FOLDER | SFGAO_BROWSABLE | SFGAO_CANLINK);
        *ppidl = pidl;
        return S_OK;
    }

    return E_NOTIMPL;
}

IFACEMETHODIMP ShellFolder::EnumObjects(HWND /*hwnd*/, SHCONTF grfFlags, IEnumIDList** ppenumIDList) {
    if (ppenumIDList == nullptr) return E_POINTER;
    *ppenumIDList = nullptr;

    if (!(grfFlags & SHCONTF_NONFOLDERS)) {
        // We only host items (providers & games), no child folders
        return S_FALSE;
    }

    std::vector<PITEMID_CHILD> items;

    // 1. Providers section
    struct ProviderEntry {
        PlatformType platform;
        std::wstring name;
    };

    std::vector<ProviderEntry> detectedProviders;

    const std::wstring steamExe = PlatformLauncher::GetSteamExecutablePath();
    if (!steamExe.empty() && fs::exists(steamExe)) {
        detectedProviders.push_back({ PlatformType::Steam, L"Steam" });
    }

    const std::wstring epicExe = PlatformLauncher::GetEpicExecutablePath();
    if (!epicExe.empty() && fs::exists(epicExe)) {
        detectedProviders.push_back({ PlatformType::Epic, L"Epic Games" });
    }

    const std::wstring gogExe = PlatformLauncher::GetGOGGalaxyExecutablePath();
    if (!gogExe.empty() && fs::exists(gogExe)) {
        detectedProviders.push_back({ PlatformType::GOG, L"GOG GALAXY" });
    }

    const std::wstring bnetExe = PlatformLauncher::GetBattleNetExecutablePath();
    if (!bnetExe.empty() && fs::exists(bnetExe)) {
        detectedProviders.push_back({ PlatformType::BattleNet, L"Battle.net" });
    }

    const std::wstring ubiExe = PlatformLauncher::GetUbisoftExecutablePath();
    if (!ubiExe.empty() && fs::exists(ubiExe)) {
        detectedProviders.push_back({ PlatformType::Ubisoft, L"Ubisoft Connect" });
    }

    const std::wstring eaExe = PlatformLauncher::GetEAAppExecutablePath();
    if (!eaExe.empty() && fs::exists(eaExe)) {
        detectedProviders.push_back({ PlatformType::EAApp, L"EA App" });
    }

    const std::wstring itchExe = PlatformLauncher::GetItchExecutablePath();
    if (!itchExe.empty() && fs::exists(itchExe)) {
        detectedProviders.push_back({ PlatformType::Itch, L"itch.io" });
    }

    for (const auto& prov : detectedProviders) {
        if (auto* pidl = CreateProviderPidl(prov.platform, prov.name)) {
            items.push_back(pidl);
        }
    }

    // 2. Games section
    auto games = StorefrontScanner::ScanAllInstalledGames();
    for (const auto& g : games) {
        if (auto* pidl = CreateGamePidl(g)) {
            items.push_back(pidl);
        }
    }

    *ppenumIDList = new (std::nothrow) GameEnumIDList(std::move(items));
    return (*ppenumIDList != nullptr) ? S_OK : E_OUTOFMEMORY;
}

IFACEMETHODIMP ShellFolder::BindToObject(PCUIDLIST_RELATIVE /*pidl*/, IBindCtx* /*pbc*/, REFIID /*riid*/, void** ppv) {
    if (ppv != nullptr) *ppv = nullptr;
    return E_NOTIMPL;
}

IFACEMETHODIMP ShellFolder::BindToStorage(PCUIDLIST_RELATIVE /*pidl*/, IBindCtx* /*pbc*/, REFIID /*riid*/, void** ppv) {
    if (ppv != nullptr) *ppv = nullptr;
    return E_NOTIMPL;
}

IFACEMETHODIMP ShellFolder::CompareIDs(LPARAM /*lParam*/, PCUIDLIST_RELATIVE pidl1, PCUIDLIST_RELATIVE pidl2) {
    if (pidl1 == pidl2) {
        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
    }
    const bool empty1 = (pidl1 == nullptr || pidl1->mkid.cb == 0);
    const bool empty2 = (pidl2 == nullptr || pidl2->mkid.cb == 0);
    if (empty1 && empty2) {
        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
    }
    if (empty1) {
        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, static_cast<unsigned short>(-1));
    }
    if (empty2) {
        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 1);
    }

    const auto* d1 = GetGamePidlData(pidl1);
    const auto* d2 = GetGamePidlData(pidl2);
    if (d1 == nullptr && d2 != nullptr) {
        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, static_cast<unsigned short>(-1));
    }
    if (d1 != nullptr && d2 == nullptr) {
        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 1);
    }
    if (d1 == nullptr && d2 == nullptr) {
        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
    }

    // Provider items (itemKind == 0) precede Game items (itemKind == 1)
    if (d1->itemKind < d2->itemKind) {
        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, static_cast<unsigned short>(-1));
    }
    if (d1->itemKind > d2->itemKind) {
        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 1);
    }

    int cmp = _wcsicmp(d1->title, d2->title);
    if (cmp == 0) {
        cmp = (d1->gameId < d2->gameId) ? -1 : ((d1->gameId > d2->gameId) ? 1 : 0);
    }

    const short code = (cmp < 0) ? -1 : ((cmp > 0) ? 1 : 0);
    return MAKE_HRESULT(SEVERITY_SUCCESS, 0, static_cast<unsigned short>(code));
}

class GameFolderViewCB final : public IShellFolderViewCB {
public:
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (ppv == nullptr) return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_IShellFolderViewCB) {
            *ppv = static_cast<IShellFolderViewCB*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    IFACEMETHODIMP_(ULONG) AddRef() override { return ++m_refCount; }
    IFACEMETHODIMP_(ULONG) Release() override {
        const ULONG c = --m_refCount;
        if (c == 0) delete this;
        return c;
    }

    IFACEMETHODIMP MessageSFVCB(UINT uMsg, WPARAM /*wParam*/, LPARAM lParam) override {
        if (uMsg == SFVM_DEFVIEWMODE && lParam != 0) {
            const auto layout = SettingsManager::GetLayoutMode();
            FOLDERVIEWMODE mode = FVM_TILE;
            if (layout == LayoutMode::DetailsView) {
                mode = FVM_DETAILS;
            } else if (layout == LayoutMode::CoverArt) {
                mode = FVM_THUMBNAIL;
            }
            *reinterpret_cast<FOLDERVIEWMODE*>(lParam) = mode;
            return S_OK;
        }
        if (uMsg == SFVM_GETSORTDEFAULTS && lParam != 0) {
            auto* pSort = reinterpret_cast<int*>(lParam);
            pSort[0] = 0; // ColTitle
            pSort[1] = 1; // Ascending
            return S_OK;
        }
        if (uMsg == SFVM_SETISFV && lParam != 0) {
            auto* pView = reinterpret_cast<IUnknown*>(lParam);
            IFolderView2* pFv2 = nullptr;
            if (SUCCEEDED(pView->QueryInterface(IID_IFolderView2, reinterpret_cast<void**>(&pFv2)))) {
                pFv2->SetGroupBy(PKEY_ItemTypeText, TRUE);
                pFv2->Release();
            }
            return S_OK;
        }
        return E_NOTIMPL;
    }

private:
    std::atomic<ULONG> m_refCount{ 1 };
};

IFACEMETHODIMP ShellFolder::CreateViewObject(HWND /*hwndOwner*/, REFIID riid, void** ppv) {
    if (ppv == nullptr) return E_POINTER;
    *ppv = nullptr;

    if (riid == IID_ICategoryProvider) {
        auto* pCatProv = new (std::nothrow) GameCategoryProvider(this);
        if (pCatProv == nullptr) return E_OUTOFMEMORY;
        *ppv = static_cast<ICategoryProvider*>(pCatProv);
        return S_OK;
    }

    if (riid == IID_IShellView) {
        SFV_CREATE sfv{};
        sfv.cbSize = sizeof(sfv);
        sfv.pshf = this;
        auto* pCb = new (std::nothrow) GameFolderViewCB();
        sfv.psfvcb = pCb;
        const HRESULT hr = SHCreateShellFolderView(&sfv, reinterpret_cast<IShellView**>(ppv));
        if (pCb != nullptr) {
            pCb->Release();
        }
        return hr;
    }

    return E_NOINTERFACE;
}

class ProviderContextMenu final : public IContextMenu {
public:
    ProviderContextMenu(PlatformType platform, std::wstring title, std::wstring exePath)
        : m_platform(platform), m_title(std::move(title)), m_exePath(std::move(exePath)) {
        ++g_dllRefCount;
    }
    ~ProviderContextMenu() {
        --g_dllRefCount;
    }

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (ppv == nullptr) return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_IContextMenu) {
            *ppv = static_cast<IContextMenu*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    IFACEMETHODIMP_(ULONG) AddRef() override { return ++m_refCount; }
    IFACEMETHODIMP_(ULONG) Release() override {
        const ULONG c = --m_refCount;
        if (c == 0) delete this;
        return c;
    }

    IFACEMETHODIMP QueryContextMenu(HMENU hmenu, UINT indexMenu, UINT idCmdFirst, UINT /*idCmdLast*/, UINT uFlags) override {
        if (uFlags & CMF_DEFAULTONLY) return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);

        std::wstring openLabel = L"\u25B6 Open " + m_title;
        InsertMenuW(hmenu, indexMenu, MF_BYPOSITION | MF_STRING, idCmdFirst + 0, openLabel.c_str());
        SetMenuDefaultItem(hmenu, idCmdFirst + 0, FALSE);

        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 1);
    }

    IFACEMETHODIMP InvokeCommand(CMINVOKECOMMANDINFO* pici) override {
        if (pici == nullptr) return E_POINTER;
        UINT cmd = 0;
        if (HIWORD(pici->lpVerb) != 0) {
            cmd = 0;
        } else {
            cmd = LOWORD(pici->lpVerb);
        }

        if (cmd == 0) {
            if (!m_exePath.empty() && fs::exists(m_exePath)) {
                ShellExecuteW(pici->hwnd, L"open", m_exePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                return S_OK;
            }
            if (m_platform == PlatformType::Steam) {
                ShellExecuteW(pici->hwnd, L"open", L"steam://open/main", nullptr, nullptr, SW_SHOWNORMAL);
                return S_OK;
            }
        }
        return S_OK;
    }

    IFACEMETHODIMP GetCommandString(UINT_PTR idCmd, UINT uType, UINT* /*pReserved*/, PSTR pszName, UINT cchMax) override {
        if (idCmd == 0 && (uType == GCS_VERBA || uType == GCS_VERBW)) {
            if (uType == GCS_VERBA) {
                StringCchCopyA(pszName, cchMax, "open");
            } else {
                StringCchCopyW(reinterpret_cast<PWSTR>(pszName), cchMax, L"open");
            }
            return S_OK;
        }
        return E_NOTIMPL;
    }

private:
    std::atomic<ULONG> m_refCount{ 1 };
    PlatformType m_platform{ PlatformType::Unknown };
    std::wstring m_title;
    std::wstring m_exePath;
};

class GameContextMenu final : public IContextMenu {
public:
    explicit GameContextMenu(GameItem item) : m_item(std::move(item)) {
        ++g_dllRefCount;
    }
    ~GameContextMenu() {
        --g_dllRefCount;
    }

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (ppv == nullptr) return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_IContextMenu) {
            *ppv = static_cast<IContextMenu*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    IFACEMETHODIMP_(ULONG) AddRef() override { return ++m_refCount; }
    IFACEMETHODIMP_(ULONG) Release() override {
        const ULONG c = --m_refCount;
        if (c == 0) delete this;
        return c;
    }

    IFACEMETHODIMP QueryContextMenu(HMENU hmenu, UINT indexMenu, UINT idCmdFirst, UINT /*idCmdLast*/, UINT uFlags) override {
        if (uFlags & CMF_DEFAULTONLY) return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);

        InsertMenuW(hmenu, indexMenu, MF_BYPOSITION | MF_STRING, idCmdFirst + 0, L"\u25B6 Play Game");
        SetMenuDefaultItem(hmenu, idCmdFirst + 0, FALSE);

        if (!m_item.workingDirectory.empty() && fs::exists(m_item.workingDirectory)) {
            InsertMenuW(hmenu, indexMenu + 1, MF_BYPOSITION | MF_STRING, idCmdFirst + 1, L"Open Game Directory");
        }

        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 2);
    }

    IFACEMETHODIMP InvokeCommand(CMINVOKECOMMANDINFO* pici) override {
        if (pici == nullptr) return E_POINTER;
        UINT cmd = 0;
        if (HIWORD(pici->lpVerb) != 0) {
            if (_stricmp(pici->lpVerb, "open") == 0 || _stricmp(pici->lpVerb, "play") == 0) {
                cmd = 0;
            } else {
                cmd = 1;
            }
        } else {
            cmd = LOWORD(pici->lpVerb);
        }

        if (cmd == 0) {
            if (PlatformLauncher::Launch(pici->hwnd, m_item)) {
                return S_OK;
            }
            return E_FAIL;
        } else if (cmd == 1) {
            if (!m_item.workingDirectory.empty() && fs::exists(m_item.workingDirectory)) {
                ShellExecuteW(nullptr, L"open", m_item.workingDirectory.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                return S_OK;
            }
        }
        return S_OK;
    }

    IFACEMETHODIMP GetCommandString(UINT_PTR idCmd, UINT uType, UINT* /*pReserved*/, PSTR pszName, UINT cchMax) override {
        if (idCmd == 0 && (uType == GCS_VERBA || uType == GCS_VERBW)) {
            if (uType == GCS_VERBA) {
                StringCchCopyA(pszName, cchMax, "play");
            } else {
                StringCchCopyW(reinterpret_cast<PWSTR>(pszName), cchMax, L"play");
            }
            return S_OK;
        }
        return E_NOTIMPL;
    }

private:
    std::atomic<ULONG> m_refCount{ 1 };
    GameItem m_item;
};

IFACEMETHODIMP ShellFolder::GetIconLocation(
    UINT /*uFlags*/, PWSTR pszIconFile, UINT cchMax, int* piIndex, UINT* pwFlags) {
    if (pszIconFile == nullptr || piIndex == nullptr || pwFlags == nullptr) {
        return E_POINTER;
    }
    wchar_t modulePath[MAX_PATH];
    if (GetModuleFileNameW(g_hInstance, modulePath, MAX_PATH) == 0) {
        return E_FAIL;
    }
    StringCchCopyW(pszIconFile, cchMax, modulePath);
    *piIndex = -100; // IDI_GAME_EXPLORER in Resource.rc
    *pwFlags = GIL_NOTFILENAME | GIL_PERINSTANCE;
    return S_OK;
}

IFACEMETHODIMP ShellFolder::Extract(
    PCWSTR /*pszFile*/, UINT /*nIconIndex*/, HICON* phiconLarge, HICON* phiconSmall, UINT nIconSize) {
    const int largeSize = LOWORD(nIconSize);
    const int smallSize = HIWORD(nIconSize);
    bool extracted = false;

    if (phiconLarge != nullptr && largeSize > 0) {
        *phiconLarge = static_cast<HICON>(LoadImageW(
            g_hInstance, MAKEINTRESOURCEW(100), IMAGE_ICON, largeSize, largeSize, LR_DEFAULTCOLOR
        ));
        if (*phiconLarge != nullptr) extracted = true;
    }
    if (phiconSmall != nullptr && smallSize > 0) {
        *phiconSmall = static_cast<HICON>(LoadImageW(
            g_hInstance, MAKEINTRESOURCEW(100), IMAGE_ICON, smallSize, smallSize, LR_DEFAULTCOLOR
        ));
        if (*phiconSmall != nullptr) extracted = true;
    }

    return extracted ? S_OK : S_FALSE;
}

IFACEMETHODIMP ShellFolder::GetAttributesOf(UINT cidl, PCUITEMID_CHILD_ARRAY apidl, SFGAOF* rgfInOut) {
    if (rgfInOut == nullptr) return E_POINTER;
    if (cidl == 0 || (cidl == 1 && (apidl == nullptr || apidl[0] == nullptr || apidl[0]->mkid.cb == 0))) {
        // Attributes for this folder root: strictly single entry point without drop down chevron, NO SFGAO_STREAM
        *rgfInOut &= (SFGAO_FOLDER | SFGAO_BROWSABLE | SFGAO_CANLINK);
        return S_OK;
    }

    // Attributes for game items
    SFGAOF flags = SFGAO_CANLINK | SFGAO_HASPROPSHEET;
    *rgfInOut &= flags;
    return S_OK;
}

IFACEMETHODIMP ShellFolder::GetUIObjectOf(
    HWND /*hwndOwner*/, UINT cidl, PCUITEMID_CHILD_ARRAY apidl,
    REFIID riid, UINT* /*rgfReserved*/, void** ppv) {
    if (ppv == nullptr) return E_POINTER;
    *ppv = nullptr;

    // If querying for the folder itself (cidl == 0 or empty PIDL)
    if (cidl == 0 || apidl == nullptr || apidl[0] == nullptr || apidl[0]->mkid.cb == 0) {
        if (riid == IID_IExtractIconW) {
            *ppv = static_cast<IExtractIconW*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    // Querying for a child item (Provider or Game)
    if (cidl == 1 && apidl[0] != nullptr) {
        const auto* data = GetGamePidlData(apidl[0]);
        if (data != nullptr) {
            if (data->itemKind == 0) {
                // Provider Item
                const auto platform = static_cast<PlatformType>(data->platform);
                const std::wstring exePath = GetPlatformExecutablePath(platform);
                const std::wstring company = GetPlatformCompany(platform);

                if (riid == IID_IContextMenu) {
                    auto* pMenu = new (std::nothrow) ProviderContextMenu(platform, data->title, exePath);
                    if (pMenu == nullptr) return E_OUTOFMEMORY;
                    const HRESULT hr = pMenu->QueryInterface(riid, ppv);
                    pMenu->Release();
                    return hr;
                }

                if (riid == IID_IThumbnailProvider ||
                    riid == IID_IShellItemImageFactory ||
                    riid == IID_IPropertyStore ||
                    riid == IID_IExtractIconW ||
                    riid == IID_IQueryInfo) {
                    GameItem provItem{};
                    provItem.title = data->title;
                    provItem.platform = platform;
                    provItem.executablePath = exePath;
                    provItem.publisher = company;
                    provItem.developer = L"Game Provider";
                    provItem.rating = L"Storefront";

                    auto* pUI = new (std::nothrow) GameItemUI(std::move(provItem), 0);
                    if (pUI == nullptr) return E_OUTOFMEMORY;
                    const HRESULT hr = pUI->QueryInterface(riid, ppv);
                    pUI->Release();
                    return hr;
                }
            } else {
                // Game Item
                auto games = StorefrontScanner::ScanAllInstalledGames();
                const GameItem* pGame = nullptr;
                for (const auto& g : games) {
                    if (g.id == data->gameId || _wcsicmp(g.title.c_str(), data->title) == 0) {
                        pGame = &g;
                        break;
                    }
                }

                if (pGame != nullptr) {
                    if (riid == IID_IContextMenu) {
                        auto* pMenu = new (std::nothrow) GameContextMenu(*pGame);
                        if (pMenu == nullptr) return E_OUTOFMEMORY;
                        const HRESULT hr = pMenu->QueryInterface(riid, ppv);
                        pMenu->Release();
                        return hr;
                    }

                    if (riid == IID_IThumbnailProvider ||
                        riid == IID_IShellItemImageFactory ||
                        riid == IID_IPropertyStore ||
                        riid == IID_IExtractIconW ||
                        riid == IID_IQueryInfo) {
                        auto* pUI = new (std::nothrow) GameItemUI(*pGame, 1);
                        if (pUI == nullptr) return E_OUTOFMEMORY;
                        const HRESULT hr = pUI->QueryInterface(riid, ppv);
                        pUI->Release();
                        return hr;
                    }
                }
            }
        }
    }

    return E_NOINTERFACE;
}

IFACEMETHODIMP ShellFolder::GetDisplayNameOf(PCUITEMID_CHILD pidl, SHGDNF uFlags, STRRET* pName) {
    if (pName == nullptr) return E_POINTER;

    if (pidl == nullptr || pidl->mkid.cb == 0) {
        pName->uType = STRRET_WSTR;
        static const std::wstring s_parsingName = std::wstring(L"::") + SZ_CLSID_GAME_EXPLORER;
        PCWSTR name = (uFlags & SHGDN_FORPARSING) ? s_parsingName.c_str() : SZ_GAME_EXPLORER_TITLE;
        const size_t len = wcslen(name) + 1;
        pName->pOleStr = static_cast<PWSTR>(CoTaskMemAlloc(len * sizeof(wchar_t)));
        if (pName->pOleStr == nullptr) return E_OUTOFMEMORY;
        StringCchCopyW(pName->pOleStr, len, name);
        return S_OK;
    }

    const auto* data = GetGamePidlData(pidl);
    if (data == nullptr) return E_INVALIDARG;

    pName->uType = STRRET_WSTR;
    const size_t len = wcslen(data->title) + 1;
    pName->pOleStr = static_cast<PWSTR>(CoTaskMemAlloc(len * sizeof(wchar_t)));
    if (pName->pOleStr == nullptr) return E_OUTOFMEMORY;

    StringCchCopyW(pName->pOleStr, len, data->title);
    return S_OK;
}

IFACEMETHODIMP ShellFolder::SetNameOf(HWND /*hwnd*/, PCUITEMID_CHILD /*pidl*/, PCWSTR /*pszName*/, SHGDNF /*uFlags*/, PITEMID_CHILD* ppidlOut) {
    if (ppidlOut != nullptr) *ppidlOut = nullptr;
    return E_NOTIMPL;
}

// IShellFolder2 Column implementations
IFACEMETHODIMP ShellFolder::GetDefaultSearchGUID(GUID* pguid) {
    if (pguid != nullptr) *pguid = GUID_NULL;
    return E_NOTIMPL;
}

IFACEMETHODIMP ShellFolder::EnumSearches(IEnumExtraSearch** ppenum) {
    if (ppenum != nullptr) *ppenum = nullptr;
    return E_NOTIMPL;
}

IFACEMETHODIMP ShellFolder::GetDefaultColumn(DWORD /*dwRes*/, ULONG* pSort, ULONG* pDisplay) {
    if (pSort != nullptr) *pSort = 0;     // Sort by Title
    if (pDisplay != nullptr) *pDisplay = 0; // Display Title
    return S_OK;
}

IFACEMETHODIMP ShellFolder::GetDefaultColumnState(UINT iColumn, SHCOLSTATEF* pcsFlags) {
    if (pcsFlags == nullptr) return E_POINTER;
    *pcsFlags = SHCOLSTATE_TYPE_STR | SHCOLSTATE_ONBYDEFAULT;
    if (iColumn == 2) {
        *pcsFlags = SHCOLSTATE_TYPE_INT | SHCOLSTATE_ONBYDEFAULT; // Rating
    }
    return S_OK;
}

IFACEMETHODIMP ShellFolder::GetDetailsEx(PCUITEMID_CHILD pidl, const SHCOLUMNID* pscid, VARIANT* pv) {
    if (pv == nullptr || pscid == nullptr) return E_POINTER;
    VariantInit(pv);

    const auto* data = GetGamePidlData(pidl);
    if (data == nullptr) return E_INVALIDARG;

    if (IsEqualPropertyKey(*pscid, PKEY_ItemTypeText)) {
        pv->vt = VT_BSTR;
        pv->bstrVal = SysAllocString((data->itemKind == 0) ? L"Game Providers" : L"Games");
        return (pv->bstrVal != nullptr) ? S_OK : E_OUTOFMEMORY;
    }
    if (IsEqualPropertyKey(*pscid, PKEY_ItemNameDisplay)) {
        pv->vt = VT_BSTR;
        pv->bstrVal = SysAllocString(data->title);
        return (pv->bstrVal != nullptr) ? S_OK : E_OUTOFMEMORY;
    }

    return E_NOTIMPL;
}

enum ColumnIndex : UINT {
    ColTitle = 0,
    ColPlatform = 1,
    ColRating = 2,
    ColDeveloper = 3,
    ColReleaseDate = 4,
    ColPlaytime = 5,
    ColDiskSize = 6
};

IFACEMETHODIMP ShellFolder::GetDetailsOf(PCUITEMID_CHILD pidl, UINT iColumn, SHELLDETAILS* psd) {
    if (psd == nullptr) return E_POINTER;

    if (pidl == nullptr) {
        // Return column headers
        psd->fmt = LVCFMT_LEFT;
        psd->cxChar = 20;
        psd->str.uType = STRRET_CSTR;

        static const wchar_t* colHeaders[] = {
            L"Game Title",
            L"Platform",
            L"Rating",
            L"Developer",
            L"Release Date",
            L"Playtime",
            L"Size on Disk"
        };

        if (iColumn < ARRAYSIZE(colHeaders)) {
            psd->str.uType = STRRET_WSTR;
            const size_t len = wcslen(colHeaders[iColumn]) + 1;
            psd->str.pOleStr = static_cast<PWSTR>(CoTaskMemAlloc(len * sizeof(wchar_t)));
            if (psd->str.pOleStr != nullptr) {
                StringCchCopyW(psd->str.pOleStr, len, colHeaders[iColumn]);
                return S_OK;
            }
            return E_OUTOFMEMORY;
        }
        return E_FAIL;
    }

    // Item details
    const auto* data = GetGamePidlData(pidl);
    if (data == nullptr) return E_INVALIDARG;

    if (data->itemKind == 0) {
        std::wstring text;
        switch (iColumn) {
        case ColTitle:
            text = data->title;
            break;
        case ColPlatform:
            text = PlatformToString(static_cast<PlatformType>(data->platform));
            break;
        case ColRating:
            text = L"Storefront";
            break;
        case ColDeveloper:
            text = L"Game Provider";
            break;
        case ColReleaseDate:
        case ColPlaytime:
        case ColDiskSize:
            text = L"";
            break;
        default:
            return E_FAIL;
        }

        psd->str.uType = STRRET_WSTR;
        const size_t len = text.size() + 1;
        psd->str.pOleStr = static_cast<PWSTR>(CoTaskMemAlloc(len * sizeof(wchar_t)));
        if (psd->str.pOleStr == nullptr) return E_OUTOFMEMORY;
        StringCchCopyW(psd->str.pOleStr, len, text.c_str());
        return S_OK;
    }

    // Resolve game from live scanned games with enriched per-game metadata.json
    auto games = StorefrontScanner::ScanAllInstalledGames();
    const GameItem* pGame = nullptr;
    for (const auto& g : games) {
        if (g.id == data->gameId || _wcsicmp(g.title.c_str(), data->title) == 0) {
            pGame = &g;
            break;
        }
    }
    if (pGame == nullptr) return E_FAIL;
    const auto& game = *pGame;

    std::wstring text;
    switch (iColumn) {
    case ColTitle:
        text = game.title;
        break;
    case ColPlatform:
        text = PlatformToString(game.platform);
        break;
    case ColRating:
        text = game.rating.empty() ? L"Unrated" : game.rating;
        break;
    case ColDeveloper:
        text = game.developer;
        break;
    case ColReleaseDate:
        text = game.releaseDate;
        break;
    case ColPlaytime: {
        const uint32_t hours = game.playtimeMinutes / 60;
        const uint32_t mins = game.playtimeMinutes % 60;
        text = std::to_wstring(hours) + L"h " + std::to_wstring(mins) + L"m";
        break;
    }
    case ColDiskSize: {
        const double mb = static_cast<double>(game.diskSizeBytes) / (1024.0 * 1024.0);
        if (mb >= 1024.0) {
            wchar_t buf[32];
            swprintf_s(buf, L"%.2f GB", mb / 1024.0);
            text = buf;
        } else {
            wchar_t buf[32];
            swprintf_s(buf, L"%.0f MB", mb);
            text = buf;
        }
        break;
    }
    default:
        return E_FAIL;
    }

    psd->str.uType = STRRET_WSTR;
    const size_t len = text.size() + 1;
    psd->str.pOleStr = static_cast<PWSTR>(CoTaskMemAlloc(len * sizeof(wchar_t)));
    if (psd->str.pOleStr == nullptr) return E_OUTOFMEMORY;
    StringCchCopyW(psd->str.pOleStr, len, text.c_str());
    return S_OK;
}

IFACEMETHODIMP ShellFolder::MapColumnToSCID(UINT iColumn, SHCOLUMNID* pscid) {
    if (pscid == nullptr) return E_POINTER;
    switch (iColumn) {
    case ColTitle:
        *pscid = PKEY_ItemNameDisplay;
        return S_OK;
    case ColRating:
        *pscid = PKEY_Rating;
        return S_OK;
    default:
        return E_NOTIMPL;
    }
}

} // namespace GameExplorer
