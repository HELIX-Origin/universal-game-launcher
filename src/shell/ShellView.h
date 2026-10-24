#pragma once

#include <windows.h>
#include <shlobj.h>
#include <atomic>
#include <vector>
#include <optional>
#include "GameExplorerCore/Models.h"
#include "GameExplorerCore/Types.h"
#include "GameExplorerCore/DarkMode.h"

namespace GameExplorer {

class ShellFolder;

class ShellView final : public IShellView, public IOleCommandTarget {
public:
    explicit ShellView(ShellFolder* pFolder);
    ~ShellView();

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    // IOleWindow
    IFACEMETHODIMP GetWindow(HWND* phwnd) override;
    IFACEMETHODIMP ContextSensitiveHelp(BOOL fEnterMode) override;

    // IShellView
    IFACEMETHODIMP TranslateAccelerator(MSG* pmsg) override;
    IFACEMETHODIMP EnableModeless(BOOL fEnable) override;
    IFACEMETHODIMP UIActivate(UINT uState) override;
    IFACEMETHODIMP Refresh() override;
    IFACEMETHODIMP CreateViewWindow(
        IShellView* pPrevView,
        const FOLDERSETTINGS* pfs,
        IShellBrowser* psb,
        RECT* prcView,
        HWND* phWnd) override;
    IFACEMETHODIMP DestroyViewWindow() override;
    IFACEMETHODIMP GetCurrentInfo(FOLDERSETTINGS* pfs) override;
    IFACEMETHODIMP AddPropertySheetPages(DWORD dwReserved, LPFNSVADDPROPSHEETPAGE pfn, LPARAM lparam) override;
    IFACEMETHODIMP SaveViewState() override;
    IFACEMETHODIMP SelectItem(PCUITEMID_CHILD pidlItem, SVSIF uFlags) override;
    IFACEMETHODIMP GetItemObject(UINT uItem, REFIID riid, void** ppv) override;

    // IOleCommandTarget
    IFACEMETHODIMP QueryStatus(const GUID* pguidCmdGroup, ULONG cCmds, OLECMD prgCmds[], OLECMDTEXT* pCmdText) override;
    IFACEMETHODIMP Exec(const GUID* pguidCmdGroup, DWORD nCmdID, DWORD nCmdexecopt, VARIANT* pvaIn, VARIANT* pvaOut) override;

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK DrawerProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    void OnPaint(HDC hdc, const RECT& rc);
    void OnDrawerPaint(HDC hdc, const RECT& rc);
    void OnSize(int cx, int cy);
    void OnLButtonDown(int x, int y);
    void OnKeyDown(WPARAM key);
    void LaunchSelectedGame();
    void RefreshData();
    void UpdateDrawerLayout();
    void UpdateThemeState();
    void InitializeFonts();
    void CleanupFonts();
    void UpdateScrollMetrics();
    void ScrollTo(int newY);
    void UpdateCachedCover(const GameItem& game);
    void FreeCachedCover();

    std::atomic<ULONG> m_refCount{ 1 };
    ShellFolder* m_pFolder{ nullptr };
    IShellBrowser* m_pShellBrowser{ nullptr };
    
    HWND m_hwndView{ nullptr };
    HWND m_hwndCanvas{ nullptr };
    HWND m_hwndDrawer{ nullptr };
    HWND m_hwndLaunchBtn{ nullptr };
    HWND m_hwndListView{ nullptr };

    FOLDERSETTINGS m_folderSettings{};
    LayoutMode m_layoutMode{ LayoutMode::DetailsView };
    std::vector<GameItem> m_games;
    int m_selectedIndex{ -1 };

    // Cached sidepane cover bitmap
    HBITMAP m_hCachedCoverBmp{ nullptr };
    int64_t m_cachedCoverGameId{ -1 };

    // Layout metrics
    int m_drawerWidth{ 320 };
    bool m_isDrawerVisible{ true };
    bool m_isDarkMode{ false };
    UINT m_uState{ SVUIA_DEACTIVATE };
    int m_scrollY{ 0 };
    int m_contentHeight{ 0 };
    ThemePalette m_palette{};

    // Typography
    HFONT m_hFontDrawerTitle{ nullptr };
    HFONT m_hFontTitle{ nullptr };
    HFONT m_hFontBody{ nullptr };
    HFONT m_hFontSub{ nullptr };
    HBRUSH m_hDrawerBrush{ nullptr };
};

} // namespace GameExplorer
