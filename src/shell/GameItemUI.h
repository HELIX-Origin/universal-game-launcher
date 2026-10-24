#pragma once

#include <windows.h>
#include <shlobj.h>
#include <thumbcache.h>
#include <propsys.h>
#include <propkey.h>
#include <atomic>
#include "GameExplorerCore/Models.h"

namespace GameExplorer {

class GameItemUI final :
    public IThumbnailProvider,
    public IShellItemImageFactory,
    public IPropertyStore,
    public IExtractIconW,
    public IQueryInfo {
public:
    explicit GameItemUI(GameItem item, uint16_t itemKind = 1);
    ~GameItemUI();

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    // IThumbnailProvider
    IFACEMETHODIMP GetThumbnail(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha) override;

    // IShellItemImageFactory
    IFACEMETHODIMP GetImage(SIZE size, SIIGBF flags, HBITMAP* phbm) override;

    // IPropertyStore
    IFACEMETHODIMP GetCount(DWORD* cProps) override;
    IFACEMETHODIMP GetAt(DWORD iProp, PROPERTYKEY* pkey) override;
    IFACEMETHODIMP GetValue(REFPROPERTYKEY key, PROPVARIANT* pv) override;
    IFACEMETHODIMP SetValue(REFPROPERTYKEY key, REFPROPVARIANT propvar) override;
    IFACEMETHODIMP Commit() override;

    // IExtractIconW
    IFACEMETHODIMP GetIconLocation(UINT uFlags, PWSTR pszIconFile, UINT cchMax, int* piIndex, UINT* pwFlags) override;
    IFACEMETHODIMP Extract(PCWSTR pszFile, UINT nIconIndex, HICON* phiconLarge, HICON* phiconSmall, UINT nIconSize) override;

    // IQueryInfo
    IFACEMETHODIMP GetInfoTip(DWORD dwFlags, PWSTR* ppwszTip) override;
    IFACEMETHODIMP GetInfoFlags(DWORD* pdwFlags) override;

private:
    HBITMAP CreateThumbnailBitmap(UINT targetW, UINT targetH);

    std::atomic<ULONG> m_refCount{ 1 };
    uint16_t m_itemKind{ 1 };
    GameItem m_item;
};

} // namespace GameExplorer
