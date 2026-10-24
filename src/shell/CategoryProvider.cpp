#include "CategoryProvider.h"
#include "ShellFolder.h"
#include "GameExplorerCore/Guid.h"
#include <strsafe.h>
#include <propkey.h>

extern std::atomic<ULONG> g_dllRefCount;

namespace GameExplorer {

// =============================================================================
// GameCategorizer Implementation
// =============================================================================

GameCategorizer::GameCategorizer() {
    ++g_dllRefCount;
}

GameCategorizer::~GameCategorizer() {
    --g_dllRefCount;
}

IFACEMETHODIMP GameCategorizer::QueryInterface(REFIID riid, void** ppv) {
    if (ppv == nullptr) return E_POINTER;
    *ppv = nullptr;

    if (riid == IID_IUnknown || riid == IID_ICategorizer) {
        *ppv = static_cast<ICategorizer*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

IFACEMETHODIMP_(ULONG) GameCategorizer::AddRef() {
    return ++m_refCount;
}

IFACEMETHODIMP_(ULONG) GameCategorizer::Release() {
    const ULONG c = --m_refCount;
    if (c == 0) delete this;
    return c;
}

IFACEMETHODIMP GameCategorizer::GetDescription(PWSTR pszDesc, UINT cch) {
    if (pszDesc == nullptr) return E_POINTER;
    return StringCchCopyW(pszDesc, cch, L"Game Explorer Categorizer");
}

IFACEMETHODIMP GameCategorizer::GetCategory(UINT cidl, PCUITEMID_CHILD_ARRAY apidl, DWORD* rgCategoryIDs) {
    if (apidl == nullptr || rgCategoryIDs == nullptr) return E_POINTER;

    for (UINT i = 0; i < cidl; ++i) {
        const auto* data = ShellFolder::GetGamePidlData(apidl[i]);
        if (data != nullptr && data->itemKind == 0) {
            rgCategoryIDs[i] = 0; // Game Providers
        } else {
            rgCategoryIDs[i] = 1; // Games
        }
    }
    return S_OK;
}

IFACEMETHODIMP GameCategorizer::GetCategoryInfo(DWORD dwCategoryId, CATEGORY_INFO* pCategoryInfo) {
    if (pCategoryInfo == nullptr) return E_POINTER;
    pCategoryInfo->cif = CATINFO_NORMAL;

    if (dwCategoryId == 0) {
        StringCchCopyW(pCategoryInfo->wszName, ARRAYSIZE(pCategoryInfo->wszName), L"Game Providers");
        return S_OK;
    } else if (dwCategoryId == 1) {
        StringCchCopyW(pCategoryInfo->wszName, ARRAYSIZE(pCategoryInfo->wszName), L"Games");
        return S_OK;
    }
    return E_INVALIDARG;
}

IFACEMETHODIMP GameCategorizer::CompareCategory(
    CATSORT_FLAGS /*tsfFlags*/, DWORD dwCategoryId1, DWORD dwCategoryId2) {
    // Game Providers (0) precede Games (1)
    if (dwCategoryId1 < dwCategoryId2) {
        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, static_cast<unsigned short>(-1));
    }
    if (dwCategoryId1 > dwCategoryId2) {
        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 1);
    }
    return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
}

// =============================================================================
// GameCategoryProvider Implementation
// =============================================================================

GameCategoryProvider::GameCategoryProvider(IShellFolder* pFolder) : m_pFolder(pFolder) {
    ++g_dllRefCount;
    if (m_pFolder != nullptr) {
        m_pFolder->AddRef();
    }
}

GameCategoryProvider::~GameCategoryProvider() {
    if (m_pFolder != nullptr) {
        m_pFolder->Release();
        m_pFolder = nullptr;
    }
    --g_dllRefCount;
}

IFACEMETHODIMP GameCategoryProvider::QueryInterface(REFIID riid, void** ppv) {
    if (ppv == nullptr) return E_POINTER;
    *ppv = nullptr;

    if (riid == IID_IUnknown || riid == IID_ICategoryProvider) {
        *ppv = static_cast<ICategoryProvider*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

IFACEMETHODIMP_(ULONG) GameCategoryProvider::AddRef() {
    return ++m_refCount;
}

IFACEMETHODIMP_(ULONG) GameCategoryProvider::Release() {
    const ULONG c = --m_refCount;
    if (c == 0) delete this;
    return c;
}

IFACEMETHODIMP GameCategoryProvider::CanCategorizeOnSCID(const SHCOLUMNID* pscid) {
    if (pscid == nullptr) return E_POINTER;
    if (IsEqualPropertyKey(*pscid, PKEY_ItemTypeText) || pscid->pid == 0) {
        return S_OK;
    }
    return S_FALSE;
}

IFACEMETHODIMP GameCategoryProvider::GetDefaultCategory(GUID* pguid, SHCOLUMNID* pscid) {
    if (pguid != nullptr) *pguid = CLSID_GameCategorizer;
    if (pscid != nullptr) *pscid = PKEY_ItemTypeText;
    return S_OK;
}

IFACEMETHODIMP GameCategoryProvider::GetCategoryForSCID(const SHCOLUMNID* /*pscid*/, GUID* pguid) {
    if (pguid == nullptr) return E_POINTER;
    *pguid = CLSID_GameCategorizer;
    return S_OK;
}

IFACEMETHODIMP GameCategoryProvider::EnumCategories(IEnumGUID** penum) {
    if (penum != nullptr) *penum = nullptr;
    return E_NOTIMPL;
}

IFACEMETHODIMP GameCategoryProvider::GetCategoryName(const GUID* /*pguid*/, LPWSTR pszName, UINT cch) {
    if (pszName == nullptr) return E_POINTER;
    return StringCchCopyW(pszName, cch, L"Type");
}

IFACEMETHODIMP GameCategoryProvider::CreateCategory(const GUID* /*pguid*/, REFIID riid, void** ppv) {
    if (ppv == nullptr) return E_POINTER;
    *ppv = nullptr;

    auto* pCat = new (std::nothrow) GameCategorizer();
    if (pCat == nullptr) return E_OUTOFMEMORY;
    const HRESULT hr = pCat->QueryInterface(riid, ppv);
    pCat->Release();
    return hr;
}

} // namespace GameExplorer
