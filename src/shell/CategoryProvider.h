#pragma once

#include <windows.h>
#include <shlobj.h>
#include <atomic>

namespace GameExplorer {

class GameCategorizer final : public ICategorizer {
public:
    GameCategorizer();
    virtual ~GameCategorizer();

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    // ICategorizer
    IFACEMETHODIMP GetDescription(PWSTR pszDesc, UINT cch) override;
    IFACEMETHODIMP GetCategory(UINT cidl, PCUITEMID_CHILD_ARRAY apidl, DWORD* rgCategoryIDs) override;
    IFACEMETHODIMP GetCategoryInfo(DWORD dwCategoryId, CATEGORY_INFO* pCategoryInfo) override;
    IFACEMETHODIMP CompareCategory(CATSORT_FLAGS tsfFlags, DWORD dwCategoryId1, DWORD dwCategoryId2) override;

private:
    std::atomic<ULONG> m_refCount{ 1 };
};

class GameCategoryProvider final : public ICategoryProvider {
public:
    explicit GameCategoryProvider(IShellFolder* pFolder);
    virtual ~GameCategoryProvider();

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    // ICategoryProvider
    IFACEMETHODIMP CanCategorizeOnSCID(const SHCOLUMNID* pscid) override;
    IFACEMETHODIMP GetDefaultCategory(GUID* pguid, SHCOLUMNID* pscid) override;
    IFACEMETHODIMP GetCategoryForSCID(const SHCOLUMNID* pscid, GUID* pguid) override;
    IFACEMETHODIMP EnumCategories(IEnumGUID** penum) override;
    IFACEMETHODIMP GetCategoryName(const GUID* pguid, LPWSTR pszName, UINT cch) override;
    IFACEMETHODIMP CreateCategory(const GUID* pguid, REFIID riid, void** ppv) override;

private:
    std::atomic<ULONG> m_refCount{ 1 };
    IShellFolder* m_pFolder{ nullptr };
};

} // namespace GameExplorer
