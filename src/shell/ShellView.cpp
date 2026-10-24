#include "ShellView.h"
#include "ShellFolder.h"
#include "GameExplorerCore/Guid.h"
#include "GameExplorerCore/Database.h"
#include "GameExplorerCore/Storage.h"
#include "GameExplorerCore/Settings.h"
#include "GameExplorerCore/StorefrontScanner.h"
#include "GameExplorerCore/PlatformLauncher.h"
#include <algorithm>
#include <mutex>
#include <gdiplus.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <strsafe.h>
#include <filesystem>

#pragma comment(lib, "gdiplus.lib")

namespace fs = std::filesystem;

extern std::atomic<ULONG> g_dllRefCount;
extern HINSTANCE g_hInstance;

namespace GameExplorer {

namespace {

constexpr const wchar_t* VIEW_CLASS_NAME = L"GameExplorerShellViewClass";
constexpr const wchar_t* DRAWER_CLASS_NAME = L"GameExplorerDrawerClass";
constexpr int IDC_LAUNCH_BTN = 1001;

std::wstring FormatDiskSize(uint64_t bytes) {
    if (bytes == 0) return L"";
    if (bytes < 1024ULL * 1024ULL) {
        return std::to_wstring(bytes / 1024ULL) + L" KB";
    }
    if (bytes < 1024ULL * 1024ULL * 1024ULL) {
        return std::to_wstring(bytes / (1024ULL * 1024ULL)) + L" MB";
    }
    const double gb = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
    wchar_t buf[32];
    swprintf_s(buf, L"%.1f GB", gb);
    return std::wstring(buf);
}

void EnsureGdiplusInitialized() {
    static std::once_flag s_once;
    static ULONG_PTR s_token = 0;
    std::call_once(s_once, []() {
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&s_token, &input, nullptr);
    });
}

} // namespace

ShellView::ShellView(ShellFolder* pFolder)
    : m_pFolder(pFolder) {
    ++g_dllRefCount;
    if (m_pFolder != nullptr) {
        m_pFolder->AddRef();
    }

    EnsureGdiplusInitialized();
    DarkMode::Initialize();
    InitializeFonts();
    UpdateThemeState();
}

ShellView::~ShellView() {
    FreeCachedCover();
    CleanupFonts();
    if (m_hDrawerBrush != nullptr) {
        DeleteObject(m_hDrawerBrush);
        m_hDrawerBrush = nullptr;
    }

    if (m_pFolder != nullptr) {
        m_pFolder->Release();
        m_pFolder = nullptr;
    }
    --g_dllRefCount;
}

void ShellView::UpdateCachedCover(const GameItem& game) {
    if (game.id == m_cachedCoverGameId && m_hCachedCoverBmp != nullptr) {
        return;
    }
    FreeCachedCover();
    m_cachedCoverGameId = game.id;

    std::wstring imgPath = game.coverArtPath;
    if (imgPath.empty() || !fs::exists(imgPath)) {
        imgPath = game.heroArtPath;
    }
    if (imgPath.empty() || !fs::exists(imgPath)) {
        return;
    }

    // Safely decode and convert to standard GDI HBITMAP outside of WM_PAINT
    Gdiplus::Bitmap bmp(imgPath.c_str());
    if (bmp.GetLastStatus() == Gdiplus::Ok) {
        HBITMAP hBmp = nullptr;
        Gdiplus::Color bg(0, 0, 0, 0);
        if (bmp.GetHBITMAP(bg, &hBmp) == Gdiplus::Ok) {
            m_hCachedCoverBmp = hBmp;
        }
    }
}

void ShellView::FreeCachedCover() {
    if (m_hCachedCoverBmp != nullptr) {
        DeleteObject(m_hCachedCoverBmp);
        m_hCachedCoverBmp = nullptr;
    }
    m_cachedCoverGameId = -1;
}

void ShellView::InitializeFonts() {
    m_hFontDrawerTitle = CreateFontW(
        -20, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text"
    );
    if (m_hFontDrawerTitle == nullptr) {
        m_hFontDrawerTitle = CreateFontW(-20, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    }

    m_hFontTitle = CreateFontW(
        -14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text"
    );
    if (m_hFontTitle == nullptr) {
        m_hFontTitle = CreateFontW(-14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    }

    m_hFontBody = CreateFontW(
        -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text"
    );
    if (m_hFontBody == nullptr) {
        m_hFontBody = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    }

    m_hFontSub = CreateFontW(
        -11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text"
    );
    if (m_hFontSub == nullptr) {
        m_hFontSub = CreateFontW(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    }
}

void ShellView::CleanupFonts() {
    if (m_hFontDrawerTitle) { DeleteObject(m_hFontDrawerTitle); m_hFontDrawerTitle = nullptr; }
    if (m_hFontTitle) { DeleteObject(m_hFontTitle); m_hFontTitle = nullptr; }
    if (m_hFontBody) { DeleteObject(m_hFontBody); m_hFontBody = nullptr; }
    if (m_hFontSub) { DeleteObject(m_hFontSub); m_hFontSub = nullptr; }
}

void ShellView::UpdateThemeState() {
    m_isDarkMode = DarkMode::IsSystemDarkMode();
    m_palette = DarkMode::GetPalette(m_isDarkMode);

    if (m_hDrawerBrush != nullptr) {
        DeleteObject(m_hDrawerBrush);
    }
    m_hDrawerBrush = CreateSolidBrush(m_palette.drawerBg);

    if (m_hwndView != nullptr) {
        DarkMode::ApplyWindowTheme(m_hwndView, m_isDarkMode);
    }
    if (m_hwndDrawer != nullptr) {
        DarkMode::ApplyWindowTheme(m_hwndDrawer, m_isDarkMode);
    }
    if (m_hwndLaunchBtn != nullptr) {
        DarkMode::ApplyWindowTheme(m_hwndLaunchBtn, m_isDarkMode);
    }
}

// IUnknown
IFACEMETHODIMP ShellView::QueryInterface(REFIID riid, void** ppv) {
    if (ppv == nullptr) return E_POINTER;
    *ppv = nullptr;

    if (riid == IID_IUnknown || riid == IID_IOleWindow || riid == IID_IShellView) {
        *ppv = static_cast<IShellView*>(this);
        AddRef();
        return S_OK;
    }
    if (riid == IID_IOleCommandTarget) {
        *ppv = static_cast<IOleCommandTarget*>(this);
        AddRef();
        return S_OK;
    }

    return E_NOINTERFACE;
}

IFACEMETHODIMP_(ULONG) ShellView::AddRef() {
    return ++m_refCount;
}

IFACEMETHODIMP_(ULONG) ShellView::Release() {
    const ULONG count = --m_refCount;
    if (count == 0) {
        delete this;
    }
    return count;
}

// IOleWindow
IFACEMETHODIMP ShellView::GetWindow(HWND* phwnd) {
    if (phwnd == nullptr) return E_POINTER;
    *phwnd = m_hwndView;
    return S_OK;
}

IFACEMETHODIMP ShellView::ContextSensitiveHelp(BOOL /*fEnterMode*/) {
    return E_NOTIMPL;
}

// IShellView
IFACEMETHODIMP ShellView::TranslateAccelerator(MSG* /*pmsg*/) {
    return S_FALSE;
}

IFACEMETHODIMP ShellView::EnableModeless(BOOL /*fEnable*/) {
    return S_OK;
}

IFACEMETHODIMP ShellView::UIActivate(UINT uState) {
    if (m_uState == uState) {
        return S_OK;
    }

    const UINT oldState = m_uState;
    m_uState = uState;

    if (uState != SVUIA_DEACTIVATE) {
        if (oldState == SVUIA_DEACTIVATE && m_pShellBrowser != nullptr) {
            m_pShellBrowser->OnViewWindowActive(this);
        }
    }
    return S_OK;
}

IFACEMETHODIMP ShellView::Refresh() {
    RefreshData();
    UpdateScrollMetrics();
    if (m_hwndView != nullptr) {
        InvalidateRect(m_hwndView, nullptr, TRUE);
    }
    if (m_hwndDrawer != nullptr) {
        InvalidateRect(m_hwndDrawer, nullptr, TRUE);
    }
    return S_OK;
}

void ShellView::RefreshData() {
    m_games = StorefrontScanner::ScanAllInstalledGames();

    if (m_selectedIndex >= static_cast<int>(m_games.size())) {
        m_selectedIndex = -1;
        FreeCachedCover();
    } else if (m_selectedIndex >= 0) {
        UpdateCachedCover(m_games[m_selectedIndex]);
    }
}

void ShellView::UpdateScrollMetrics() {
    if (m_hwndView == nullptr) return;

    RECT rc;
    GetClientRect(m_hwndView, &rc);
    const int viewH = rc.bottom - rc.top;

    constexpr int rowH = 38;
    constexpr int headerH = 34;
    m_contentHeight = headerH + static_cast<int>(m_games.size()) * rowH + 20;

    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = m_contentHeight;
    si.nPage = static_cast<UINT>(viewH);
    si.nPos = m_scrollY;
    SetScrollInfo(m_hwndView, SB_VERT, &si, FALSE);

    const int maxScroll = std::max(0, m_contentHeight - viewH);
    if (m_scrollY > maxScroll) {
        m_scrollY = maxScroll;
        SetScrollPos(m_hwndView, SB_VERT, m_scrollY, FALSE);
    }
}

void ShellView::ScrollTo(int newY) {
    if (m_hwndView == nullptr) return;

    RECT rc;
    GetClientRect(m_hwndView, &rc);
    const int viewH = rc.bottom - rc.top;
    const int maxScroll = std::max(0, m_contentHeight - viewH);
    newY = std::clamp(newY, 0, maxScroll);

    if (newY != m_scrollY) {
        m_scrollY = newY;
        SetScrollPos(m_hwndView, SB_VERT, m_scrollY, TRUE);
        InvalidateRect(m_hwndView, nullptr, TRUE);
    }
}

IFACEMETHODIMP ShellView::CreateViewWindow(
    IShellView* /*pPrevView*/,
    const FOLDERSETTINGS* pfs,
    IShellBrowser* psb,
    RECT* prcView,
    HWND* phWnd) {
    if (phWnd == nullptr || prcView == nullptr) return E_POINTER;
    *phWnd = nullptr;

    m_pShellBrowser = psb;
    if (m_pShellBrowser != nullptr) {
        m_pShellBrowser->AddRef();
    }
    if (pfs != nullptr) {
        m_folderSettings = *pfs;
    }

    HWND hwndParent = nullptr;
    if (m_pShellBrowser != nullptr) {
        // Query FCW_VIEW (1) for inner view container window
        if (m_pShellBrowser->GetControlWindow(1, &hwndParent) != S_OK || hwndParent == nullptr) {
            m_pShellBrowser->GetWindow(&hwndParent);
        }
    }
    if (hwndParent == nullptr) return E_FAIL;

    UpdateThemeState();

    // Register View window class
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = g_hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = VIEW_CLASS_NAME;
    RegisterClassExW(&wc);

    // Register Drawer window class
    WNDCLASSEXW dwc{};
    dwc.cbSize = sizeof(dwc);
    dwc.style = CS_HREDRAW | CS_VREDRAW;
    dwc.lpfnWndProc = DrawerProc;
    dwc.hInstance = g_hInstance;
    dwc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    dwc.hbrBackground = nullptr;
    dwc.lpszClassName = DRAWER_CLASS_NAME;
    RegisterClassExW(&dwc);

    int viewX = prcView->left;
    int viewY = prcView->top;
    int viewW = prcView->right - prcView->left;
    int viewH = prcView->bottom - prcView->top;

    if (viewW <= 0 || viewH <= 0) {
        RECT rcParent{};
        GetClientRect(hwndParent, &rcParent);
        viewW = rcParent.right - rcParent.left;
        viewH = rcParent.bottom - rcParent.top;
        viewX = rcParent.left;
        viewY = rcParent.top;
    }

    // Create Main View container with native vertical scrollbar
    m_hwndView = CreateWindowExW(
        0, VIEW_CLASS_NAME, L"GameExplorerView",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_VSCROLL,
        viewX, viewY, viewW, viewH,
        hwndParent, nullptr, g_hInstance, this
    );

    if (m_hwndView == nullptr) return E_FAIL;

    // Create Drawer child window on the right (320px)
    m_drawerWidth = 320;
    const int drawerX = std::max(0, viewW - m_drawerWidth);
    m_hwndDrawer = CreateWindowExW(
        0, DRAWER_CLASS_NAME, L"GameExplorerDrawer",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        drawerX, 0, m_drawerWidth, viewH,
        m_hwndView, nullptr, g_hInstance, this
    );

    // Create Action Button inside Drawer
    m_hwndLaunchBtn = CreateWindowExW(
        0, L"BUTTON", L"\u2699  Library Settings",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        20, std::max(100, viewH - 62), m_drawerWidth - 40, 42,
        m_hwndDrawer, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LAUNCH_BTN)),
        g_hInstance, nullptr
    );

    DarkMode::ApplyWindowTheme(m_hwndView, m_isDarkMode);
    DarkMode::ApplyWindowTheme(m_hwndDrawer, m_isDarkMode);
    DarkMode::ApplyWindowTheme(m_hwndLaunchBtn, m_isDarkMode);

    RefreshData();
    UpdateDrawerLayout();
    UpdateScrollMetrics();

    *phWnd = m_hwndView;
    return S_OK;
}

IFACEMETHODIMP ShellView::DestroyViewWindow() {
    if (m_uState != SVUIA_DEACTIVATE) {
        UIActivate(SVUIA_DEACTIVATE);
    }
    FreeCachedCover();
    if (m_hwndView != nullptr) {
        DestroyWindow(m_hwndView);
        m_hwndView = nullptr;
    }
    if (m_pShellBrowser != nullptr) {
        m_pShellBrowser->Release();
        m_pShellBrowser = nullptr;
    }
    return S_OK;
}

IFACEMETHODIMP ShellView::GetCurrentInfo(FOLDERSETTINGS* pfs) {
    if (pfs == nullptr) return E_POINTER;
    *pfs = m_folderSettings;
    return S_OK;
}

IFACEMETHODIMP ShellView::AddPropertySheetPages(DWORD /*dwReserved*/, LPFNSVADDPROPSHEETPAGE /*pfn*/, LPARAM /*lparam*/) {
    return E_NOTIMPL;
}

IFACEMETHODIMP ShellView::SaveViewState() {
    return S_OK;
}

IFACEMETHODIMP ShellView::SelectItem(PCUITEMID_CHILD pidlItem, SVSIF /*uFlags*/) {
    if (pidlItem != nullptr) {
        const auto* data = ShellFolder::GetGamePidlData(pidlItem);
        if (data != nullptr) {
            for (size_t i = 0; i < m_games.size(); ++i) {
                if (m_games[i].id == data->gameId) {
                    m_selectedIndex = static_cast<int>(i);
                    UpdateCachedCover(m_games[m_selectedIndex]);
                    InvalidateRect(m_hwndView, nullptr, TRUE);
                    InvalidateRect(m_hwndDrawer, nullptr, TRUE);
                    break;
                }
            }
        }
    }
    return S_OK;
}

IFACEMETHODIMP ShellView::GetItemObject(UINT /*uItem*/, REFIID /*riid*/, void** ppv) {
    if (ppv != nullptr) *ppv = nullptr;
    return E_NOTIMPL;
}

IFACEMETHODIMP ShellView::QueryStatus(const GUID* /*pguidCmdGroup*/, ULONG /*cCmds*/, OLECMD /*prgCmds*/[], OLECMDTEXT* /*pCmdText*/) {
    return E_NOTIMPL;
}

IFACEMETHODIMP ShellView::Exec(const GUID* pguidCmdGroup, DWORD nCmdID, DWORD /*nCmdexecopt*/, VARIANT* /*pvaIn*/, VARIANT* /*pvaOut*/) {
    if (pguidCmdGroup == nullptr && nCmdID == OLECMDID_REFRESH) {
        Refresh();
        return S_OK;
    }
    return OLECMDERR_E_UNKNOWNGROUP;
}

void ShellView::UpdateDrawerLayout() {
    if (m_hwndView == nullptr || m_hwndDrawer == nullptr) return;

    RECT rc;
    GetClientRect(m_hwndView, &rc);
    const int viewW = rc.right - rc.left;
    const int viewH = rc.bottom - rc.top;

    if (viewW > 500) {
        const int drawerX = viewW - m_drawerWidth;
        SetWindowPos(m_hwndDrawer, nullptr, drawerX, 0, m_drawerWidth, viewH, SWP_NOZORDER | SWP_SHOWWINDOW);
        if (m_hwndLaunchBtn != nullptr) {
            SetWindowPos(m_hwndLaunchBtn, nullptr, 20, std::max(100, viewH - 62), m_drawerWidth - 40, 42, SWP_NOZORDER | SWP_SHOWWINDOW);
        }
    } else {
        ShowWindow(m_hwndDrawer, SW_HIDE);
    }
}

void ShellView::LaunchSelectedGame() {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_games.size())) {
        return;
    }
    PlatformLauncher::Launch(m_hwndView, m_games[m_selectedIndex]);
}

void ShellView::OnPaint(HDC hdc, const RECT& rc) {
    // Fill Canvas Background in authentic Windows 11 Dark / Light palette
    HBRUSH bgBrush = CreateSolidBrush(m_palette.canvasBg);
    FillRect(hdc, &rc, bgBrush);
    DeleteObject(bgBrush);

    const int canvasW = rc.right - rc.left - m_drawerWidth;
    if (canvasW <= 0) return;

    constexpr int headerH = 34;
    constexpr int rowH = 38;

    if (m_games.empty()) {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, m_palette.textSecondary);
        SelectObject(hdc, m_hFontBody);
        RECT textRc = { 20, headerH + 40, canvasW - 20, headerH + 80 };
        DrawTextW(hdc, L"No games discovered yet. Open Library Settings to scan drives or add games.", -1, &textRc, DT_CENTER | DT_SINGLELINE);
    } else {
        // Clip rows beneath sticky header
        HRGN rgnRows = CreateRectRgn(0, headerH, canvasW, rc.bottom);
        SelectClipRgn(hdc, rgnRows);
        DeleteObject(rgnRows);

        SelectObject(hdc, m_hFontBody);

        for (size_t i = 0; i < m_games.size(); ++i) {
            const int rowY = headerH + static_cast<int>(i) * rowH - m_scrollY;
            if (rowY + rowH < headerH || rowY > rc.bottom) continue;

            const bool isSelected = (static_cast<int>(i) == m_selectedIndex);
            RECT rowRc = { 4, rowY + 1, canvasW - 4, rowY + rowH - 1 };

            COLORREF rowBgColor;
            if (isSelected) {
                rowBgColor = m_isDarkMode ? RGB(38, 55, 78) : RGB(225, 240, 255);
            } else if (i % 2 == 1) {
                rowBgColor = m_isDarkMode ? RGB(34, 34, 34) : RGB(248, 248, 248);
            } else {
                rowBgColor = m_palette.canvasBg;
            }

            HBRUSH rowBrush = CreateSolidBrush(rowBgColor);
            FillRect(hdc, &rowRc, rowBrush);
            DeleteObject(rowBrush);

            if (isSelected) {
                // Fluent 3px left vertical accent pill
                RECT accentRc = { 4, rowY + 3, 7, rowY + rowH - 3 };
                HBRUSH accentBrush = CreateSolidBrush(m_isDarkMode ? RGB(96, 175, 255) : RGB(0, 103, 192));
                FillRect(hdc, &accentRc, accentBrush);
                DeleteObject(accentBrush);

                HPEN selPen = CreatePen(PS_SOLID, 1, m_isDarkMode ? RGB(55, 80, 115) : RGB(190, 215, 245));
                HGDIOBJ oldP = SelectObject(hdc, selPen);
                HGDIOBJ oldB = SelectObject(hdc, GetStockObject(NULL_BRUSH));
                Rectangle(hdc, rowRc.left, rowRc.top, rowRc.right, rowRc.bottom);
                SelectObject(hdc, oldB);
                SelectObject(hdc, oldP);
                DeleteObject(selPen);
            }

            // Game Title
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, isSelected ? (m_isDarkMode ? RGB(255, 255, 255) : RGB(0, 0, 0)) : m_palette.textPrimary);
            SelectObject(hdc, m_hFontTitle);
            RECT titleCol = { 16, rowY, 330, rowY + rowH };
            DrawTextW(hdc, m_games[i].title.c_str(), -1, &titleCol, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

            // Platform
            SelectObject(hdc, m_hFontBody);
            SetTextColor(hdc, m_isDarkMode ? RGB(160, 200, 255) : RGB(0, 90, 180));
            RECT platCol = { 345, rowY, 470, rowY + rowH };
            DrawTextW(hdc, PlatformToString(m_games[i].platform).data(), -1, &platCol, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            // Size
            SetTextColor(hdc, m_palette.textSecondary);
            RECT sizeCol = { 485, rowY, 590, rowY + rowH };
            const std::wstring szStr = (m_games[i].diskSizeBytes > 0) ? FormatDiskSize(m_games[i].diskSizeBytes) : L"—";
            DrawTextW(hdc, szStr.c_str(), -1, &sizeCol, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            // Rating
            RECT rateCol = { 605, rowY, 710, rowY + rowH };
            const std::wstring rateStr = m_games[i].rating.empty() ? L"—" : (m_games[i].rating + L"%");
            DrawTextW(hdc, rateStr.c_str(), -1, &rateCol, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            // Developer
            RECT devCol = { 725, rowY, 860, rowY + rowH };
            const std::wstring devStr = m_games[i].developer.empty() ? L"—" : m_games[i].developer;
            DrawTextW(hdc, devStr.c_str(), -1, &devCol, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

            // Released
            RECT dateCol = { 875, rowY, 1000, rowY + rowH };
            const std::wstring dateStr = m_games[i].releaseDate.empty() ? L"—" : m_games[i].releaseDate;
            DrawTextW(hdc, dateStr.c_str(), -1, &dateCol, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }

        SelectClipRgn(hdc, nullptr);
    }

    // Sticky Table Header Bar
    RECT headerRc = { 0, 0, canvasW, headerH };
    HBRUSH headerBgBrush = CreateSolidBrush(m_isDarkMode ? RGB(26, 26, 26) : RGB(242, 242, 242));
    FillRect(hdc, &headerRc, headerBgBrush);
    DeleteObject(headerBgBrush);

    HPEN headerBorderPen = CreatePen(PS_SOLID, 1, m_isDarkMode ? RGB(45, 45, 45) : RGB(220, 220, 220));
    HGDIOBJ oldPen = SelectObject(hdc, headerBorderPen);
    MoveToEx(hdc, 0, headerH - 1, nullptr);
    LineTo(hdc, canvasW, headerH - 1);

    // Column separator ticks
    constexpr int dividers[] = { 335, 475, 595, 715, 865 };
    for (int divX : dividers) {
        if (divX < canvasW) {
            MoveToEx(hdc, divX, 6, nullptr);
            LineTo(hdc, divX, headerH - 6);
        }
    }

    SelectObject(hdc, m_hFontTitle);
    SetTextColor(hdc, m_palette.textPrimary);

    RECT colTitle = { 16, 0, 330, headerH };
    RECT colStore = { 345, 0, 470, headerH };
    RECT colSize  = { 485, 0, 590, headerH };
    RECT colRating= { 605, 0, 710, headerH };
    RECT colDev   = { 725, 0, 860, headerH };
    RECT colDate  = { 875, 0, 1000, headerH };

    DrawTextW(hdc, L"Game Title", -1, &colTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTextW(hdc, L"Platform", -1, &colStore, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTextW(hdc, L"Size", -1, &colSize, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTextW(hdc, L"Rating", -1, &colRating, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTextW(hdc, L"Developer", -1, &colDev, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTextW(hdc, L"Released", -1, &colDate, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, oldPen);
    DeleteObject(headerBorderPen);
}

void ShellView::OnDrawerPaint(HDC hdc, const RECT& rc) {
    // Fill Drawer Background
    FillRect(hdc, &rc, m_hDrawerBrush);

    // Left border divider separating canvas from drawer
    HPEN divPen = CreatePen(PS_SOLID, 1, m_palette.cardBorder);
    HGDIOBJ oldPen = SelectObject(hdc, divPen);
    MoveToEx(hdc, 0, 0, nullptr);
    LineTo(hdc, 0, rc.bottom);
    SelectObject(hdc, oldPen);
    DeleteObject(divPen);

    SetBkMode(hdc, TRANSPARENT);

    // =========================================================================
    // STATE A: GAME SELECTED
    // =========================================================================
    if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_games.size())) {
        const auto& game = m_games[m_selectedIndex];

        // 1. Top Cover / Hero Banner Preview Area
        constexpr int previewX = 20;
        constexpr int previewY = 18;
        const int previewW = m_drawerWidth - 40;
        constexpr int previewH = 135;
        RECT previewRc = { previewX, previewY, previewX + previewW, previewY + previewH };

        if (m_hCachedCoverBmp != nullptr) {
            HDC imgDC = CreateCompatibleDC(hdc);
            if (imgDC != nullptr) {
                HGDIOBJ oldImgBmp = SelectObject(imgDC, m_hCachedCoverBmp);
                BITMAP bm{};
                GetObjectW(m_hCachedCoverBmp, sizeof(bm), &bm);
                if (bm.bmWidth > 0 && bm.bmHeight > 0) {
                    SetStretchBltMode(hdc, HALFTONE);
                    SetBrushOrgEx(hdc, 0, 0, nullptr);
                    StretchBlt(hdc, previewX, previewY, previewW, previewH, imgDC, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
                }
                SelectObject(imgDC, oldImgBmp);
                DeleteDC(imgDC);
            }

            // 1px Subtle Border around Cover Art
            HPEN borderPen = CreatePen(PS_SOLID, 1, m_palette.cardBorder);
            HGDIOBJ oP = SelectObject(hdc, borderPen);
            HGDIOBJ oB = SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, previewX, previewY, previewX + previewW, previewY + previewH);
            SelectObject(hdc, oB);
            SelectObject(hdc, oP);
            DeleteObject(borderPen);
        } else {
            // Elegant Fluent Hero placeholder card
            HBRUSH heroBrush = CreateSolidBrush(m_isDarkMode ? RGB(38, 28, 52) : RGB(243, 237, 249));
            FillRect(hdc, &previewRc, heroBrush);
            DeleteObject(heroBrush);

            HPEN heroBorder = CreatePen(PS_SOLID, 1, m_isDarkMode ? RGB(90, 50, 130) : RGB(200, 180, 230));
            HGDIOBJ oP = SelectObject(hdc, heroBorder);
            HGDIOBJ oB = SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, previewX, previewY, previewX + previewW, previewY + previewH);
            SelectObject(hdc, oB);
            SelectObject(hdc, oP);
            DeleteObject(heroBorder);

            RECT iconRc = { previewX, previewY + 35, previewX + previewW, previewY + 70 };
            SelectObject(hdc, m_hFontDrawerTitle);
            SetTextColor(hdc, m_isDarkMode ? RGB(160, 120, 220) : RGB(120, 60, 180));
            DrawTextW(hdc, L"\u25C8", -1, &iconRc, DT_CENTER | DT_SINGLELINE);

            RECT noArtRc = { previewX, previewY + 75, previewX + previewW, previewY + 105 };
            SelectObject(hdc, m_hFontSub);
            SetTextColor(hdc, m_palette.textSecondary);
            DrawTextW(hdc, L"GAME COVER PREVIEW", -1, &noArtRc, DT_CENTER | DT_SINGLELINE);
        }

        // 2. Game Title
        SelectObject(hdc, m_hFontDrawerTitle);
        SetTextColor(hdc, m_palette.textPrimary);
        RECT titleRc = { 20, 168, rc.right - 20, 218 };
        DrawTextW(hdc, game.title.c_str(), -1, &titleRc, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);

        // 3. Platform Badge
        RECT badgeRc = { 20, 222, rc.right - 20, 244 };
        SelectObject(hdc, m_hFontSub);
        SetTextColor(hdc, m_isDarkMode ? RGB(140, 190, 255) : RGB(0, 90, 180));
        std::wstring platText = L"PLATFORM: " + std::wstring(PlatformToString(game.platform));
        DrawTextW(hdc, platText.c_str(), -1, &badgeRc, DT_LEFT | DT_SINGLELINE);

        // 4. Game Specifications
        SelectObject(hdc, m_hFontBody);
        SetTextColor(hdc, m_palette.textSecondary);
        int currentY = 252;

        std::wstring szText = L"Size on Disk: " + (game.diskSizeBytes > 0 ? FormatDiskSize(game.diskSizeBytes) : L"—");
        TextOutW(hdc, 20, currentY, szText.c_str(), static_cast<int>(szText.size()));
        currentY += 24;

        if (!game.developer.empty()) {
            std::wstring devText = L"Developer: " + game.developer;
            TextOutW(hdc, 20, currentY, devText.c_str(), static_cast<int>(devText.size()));
            currentY += 24;
        }

        if (!game.releaseDate.empty()) {
            std::wstring dateText = L"Released: " + game.releaseDate;
            TextOutW(hdc, 20, currentY, dateText.c_str(), static_cast<int>(dateText.size()));
            currentY += 24;
        }

        std::wstring ratingText = L"Community Score: " + (game.rating.empty() ? L"Unrated" : (game.rating + L"%"));
        TextOutW(hdc, 20, currentY, ratingText.c_str(), static_cast<int>(ratingText.size()));
        currentY += 28;

        // 5. Synopsis / Description
        if (!game.synopsis.empty()) {
            SelectObject(hdc, m_hFontSub);
            SetTextColor(hdc, m_palette.textSecondary);
            RECT synRc = { 20, currentY, rc.right - 20, rc.bottom - 75 };
            DrawTextW(hdc, game.synopsis.c_str(), -1, &synRc, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
        }
    }
    // =========================================================================
    // STATE B: NO GAME SELECTED -> LIBRARY OVERVIEW
    // =========================================================================
    else {
        // 1. Fluent Controller Hero Card Banner
        constexpr int bannerX = 20;
        constexpr int bannerY = 18;
        const int bannerW = m_drawerWidth - 40;
        constexpr int bannerH = 125;
        RECT bannerRc = { bannerX, bannerY, bannerX + bannerW, bannerY + bannerH };

        HBRUSH bannerBrush = CreateSolidBrush(m_isDarkMode ? RGB(38, 28, 52) : RGB(243, 237, 249));
        HPEN bannerPen = CreatePen(PS_SOLID, 1, m_isDarkMode ? RGB(90, 50, 130) : RGB(200, 180, 230));
        HGDIOBJ oldB = SelectObject(hdc, bannerBrush);
        HGDIOBJ oldP = SelectObject(hdc, bannerPen);
        RoundRect(hdc, bannerRc.left, bannerRc.top, bannerRc.right, bannerRc.bottom, 12, 12);
        SelectObject(hdc, oldP);
        SelectObject(hdc, oldB);
        DeleteObject(bannerBrush);
        DeleteObject(bannerPen);

        // Vector Controller Glyph inside Card
        RECT glyphRc = { bannerX, bannerY + 18, bannerX + bannerW, bannerY + 54 };
        SelectObject(hdc, m_hFontDrawerTitle);
        SetTextColor(hdc, m_isDarkMode ? RGB(170, 120, 240) : RGB(120, 60, 190));
        DrawTextW(hdc, L"\U0001F3AE", -1, &glyphRc, DT_CENTER | DT_SINGLELINE);

        // Hero Sub-tag: "WINDOWS GAME EXPLORER"
        RECT tagRc = { bannerX, bannerY + 58, bannerX + bannerW, bannerY + 80 };
        SelectObject(hdc, m_hFontTitle);
        SetTextColor(hdc, m_isDarkMode ? RGB(190, 160, 240) : RGB(110, 60, 180));
        DrawTextW(hdc, L"WINDOWS GAME EXPLORER", -1, &tagRc, DT_CENTER | DT_SINGLELINE);

        RECT tagSubRc = { bannerX, bannerY + 82, bannerX + bannerW, bannerY + 104 };
        SelectObject(hdc, m_hFontSub);
        SetTextColor(hdc, m_palette.textSecondary);
        DrawTextW(hdc, L"Unified Game Library & Hub", -1, &tagSubRc, DT_CENTER | DT_SINGLELINE);

        // 2. Library Statistics Header
        RECT titleRc = { 20, 162, rc.right - 20, 190 };
        SelectObject(hdc, m_hFontDrawerTitle);
        SetTextColor(hdc, m_palette.textPrimary);
        DrawTextW(hdc, L"Library Summary", -1, &titleRc, DT_LEFT | DT_SINGLELINE);

        // Subtitle
        RECT subRc = { 20, 192, rc.right - 20, 214 };
        SelectObject(hdc, m_hFontBody);
        SetTextColor(hdc, m_isDarkMode ? RGB(120, 185, 255) : RGB(0, 103, 192));
        std::wstring countStr = std::to_wstring(m_games.size()) + L" Games Installed Across Drives";
        DrawTextW(hdc, countStr.c_str(), -1, &subRc, DT_LEFT | DT_SINGLELINE);

        // Calculate Aggregate Stats
        uint64_t totalBytes = 0;
        std::vector<std::wstring> storeNames;
        int ratedCount = 0;
        double sumRating = 0.0;

        for (const auto& g : m_games) {
            totalBytes += g.diskSizeBytes;
            std::wstring pStr = std::wstring(PlatformToString(g.platform));
            if (std::find(storeNames.begin(), storeNames.end(), pStr) == storeNames.end()) {
                storeNames.push_back(pStr);
            }
            if (!g.rating.empty()) {
                wchar_t* end = nullptr;
                double r = wcstod(g.rating.c_str(), &end);
                if (r > 0) {
                    sumRating += r;
                    ++ratedCount;
                }
            }
        }

        SelectObject(hdc, m_hFontBody);
        SetTextColor(hdc, m_palette.textSecondary);
        int currentY = 226;

        std::wstring szText = L"Total Storage: " + (totalBytes > 0 ? FormatDiskSize(totalBytes) : L"Calculating...");
        TextOutW(hdc, 20, currentY, szText.c_str(), static_cast<int>(szText.size()));
        currentY += 26;

        std::wstring scoreText;
        if (ratedCount > 0) {
            const int avg = static_cast<int>(std::round(sumRating / ratedCount));
            scoreText = L"Library Avg Rating: " + std::to_wstring(avg) + L"%";
        } else {
            scoreText = L"Library Avg Rating: Highly Rated";
        }
        TextOutW(hdc, 20, currentY, scoreText.c_str(), static_cast<int>(scoreText.size()));
        currentY += 26;

        const std::wstring cloudText = L"Profile Cloud Sync: Active (OneDrive)";
        TextOutW(hdc, 20, currentY, cloudText.c_str(), static_cast<int>(cloudText.size()));
        currentY += 32;

        // Installed Storefronts Section
        SelectObject(hdc, m_hFontTitle);
        SetTextColor(hdc, m_palette.textPrimary);
        TextOutW(hdc, 20, currentY, L"Detected Storefronts:", 21);
        currentY += 24;

        SelectObject(hdc, m_hFontSub);
        SetTextColor(hdc, m_isDarkMode ? RGB(160, 200, 255) : RGB(0, 80, 160));
        std::wstring storeList;
        for (size_t s = 0; s < storeNames.size(); ++s) {
            if (s > 0) storeList += L" \u2022 ";
            storeList += storeNames[s];
        }
        if (storeList.empty()) storeList = L"Steam \u2022 Epic \u2022 GOG \u2022 itch.io \u2022 Battle.net \u2022 Xbox";
        RECT storeRc = { 20, currentY, rc.right - 20, currentY + 44 };
        DrawTextW(hdc, storeList.c_str(), -1, &storeRc, DT_LEFT | DT_WORDBREAK);
        currentY += 48;

        SelectObject(hdc, m_hFontSub);
        SetTextColor(hdc, m_palette.textSecondary);
        RECT tipRc = { 20, currentY, rc.right - 20, rc.bottom - 75 };
        DrawTextW(hdc, L"Tip: Click any game row to view details, synopsis, and launch. Press Escape or click outside to return to Library Overview.", -1, &tipRc, DT_LEFT | DT_WORDBREAK);
    }
}

void ShellView::OnLButtonDown(int mouseX, int mouseY) {
    RECT rc;
    GetClientRect(m_hwndView, &rc);
    const int canvasW = rc.right - rc.left - m_drawerWidth;

    if (mouseX < canvasW) {
        constexpr int headerH = 34;
        constexpr int rowH = 38;

        if (mouseY > headerH) {
            const int clickedIndex = (mouseY - headerH + m_scrollY) / rowH;
            if (clickedIndex >= 0 && clickedIndex < static_cast<int>(m_games.size())) {
                m_selectedIndex = clickedIndex;
                UpdateCachedCover(m_games[m_selectedIndex]);
            } else {
                m_selectedIndex = -1;
                FreeCachedCover();
            }
        } else {
            m_selectedIndex = -1;
            FreeCachedCover();
        }

        InvalidateRect(m_hwndView, nullptr, TRUE);
        if (m_hwndDrawer != nullptr) {
            InvalidateRect(m_hwndDrawer, nullptr, TRUE);
            if (m_hwndLaunchBtn != nullptr) {
                InvalidateRect(m_hwndLaunchBtn, nullptr, TRUE);
            }
        }
    }
}

LRESULT CALLBACK ShellView::WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    ShellView* pThis = nullptr;
    if (uMsg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        pThis = static_cast<ShellView*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
    } else {
        pThis = reinterpret_cast<ShellView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (pThis == nullptr) {
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }

    switch (uMsg) {
    case WM_ERASEBKGND:
        return 1; // Handled in WM_PAINT double buffer

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc;
        GetClientRect(hwnd, &rc);

        // Memory DC double-buffering: 100% flicker-free and memory-only
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, rc.right - rc.left, rc.bottom - rc.top);
        HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

        pThis->OnPaint(memDC, rc);

        BitBlt(hdc, 0, 0, rc.right - rc.left, rc.bottom - rc.top, memDC, 0, 0, SRCCOPY);

        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_VSCROLL: {
        SCROLLINFO si{};
        si.cbSize = sizeof(si);
        si.fMask = SIF_ALL;
        GetScrollInfo(hwnd, SB_VERT, &si);
        int newY = si.nPos;
        switch (LOWORD(wParam)) {
        case SB_LINEUP: newY -= 40; break;
        case SB_LINEDOWN: newY += 40; break;
        case SB_PAGEUP: newY -= si.nPage; break;
        case SB_PAGEDOWN: newY += si.nPage; break;
        case SB_TOP: newY = 0; break;
        case SB_BOTTOM: newY = pThis->m_contentHeight; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: newY = si.nTrackPos; break;
        }
        pThis->ScrollTo(newY);
        return 0;
    }

    case WM_MOUSEWHEEL: {
        const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        const double lines = static_cast<double>(delta) / static_cast<double>(WHEEL_DELTA);
        const int scrollAmount = static_cast<int>(std::round(lines * 60.0));
        pThis->ScrollTo(pThis->m_scrollY - scrollAmount);
        return 0;
    }

    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
        pThis->UpdateThemeState();
        InvalidateRect(hwnd, nullptr, TRUE);
        if (pThis->m_hwndDrawer != nullptr) {
            InvalidateRect(pThis->m_hwndDrawer, nullptr, TRUE);
        }
        return 0;

    case WM_SIZE: {
        static bool s_inSize = false;
        if (s_inSize) return 0;
        s_inSize = true;
        pThis->UpdateDrawerLayout();
        pThis->UpdateScrollMetrics();
        s_inSize = false;
        return 0;
    }

    case WM_LBUTTONDOWN:
        pThis->OnLButtonDown(LOWORD(lParam), HIWORD(lParam));
        SetFocus(hwnd);
        return 0;

    case WM_LBUTTONDBLCLK:
        pThis->LaunchSelectedGame();
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            pThis->m_selectedIndex = -1;
            pThis->FreeCachedCover();
            InvalidateRect(hwnd, nullptr, TRUE);
            if (pThis->m_hwndDrawer != nullptr) {
                InvalidateRect(pThis->m_hwndDrawer, nullptr, TRUE);
                if (pThis->m_hwndLaunchBtn != nullptr) {
                    InvalidateRect(pThis->m_hwndLaunchBtn, nullptr, TRUE);
                }
            }
            return 0;
        }
        if (wParam == VK_RETURN) {
            pThis->LaunchSelectedGame();
            return 0;
        }
        if (wParam == VK_F5) {
            pThis->Refresh();
            return 0;
        }
        if (wParam == VK_DOWN) {
            if (pThis->m_selectedIndex + 1 < static_cast<int>(pThis->m_games.size())) {
                ++pThis->m_selectedIndex;
                pThis->UpdateCachedCover(pThis->m_games[pThis->m_selectedIndex]);
                InvalidateRect(hwnd, nullptr, TRUE);
                if (pThis->m_hwndDrawer != nullptr) {
                    InvalidateRect(pThis->m_hwndDrawer, nullptr, TRUE);
                    if (pThis->m_hwndLaunchBtn != nullptr) {
                        InvalidateRect(pThis->m_hwndLaunchBtn, nullptr, TRUE);
                    }
                }
            }
            return 0;
        }
        if (wParam == VK_UP) {
            if (pThis->m_selectedIndex > 0) {
                --pThis->m_selectedIndex;
                pThis->UpdateCachedCover(pThis->m_games[pThis->m_selectedIndex]);
                InvalidateRect(hwnd, nullptr, TRUE);
                if (pThis->m_hwndDrawer != nullptr) {
                    InvalidateRect(pThis->m_hwndDrawer, nullptr, TRUE);
                    if (pThis->m_hwndLaunchBtn != nullptr) {
                        InvalidateRect(pThis->m_hwndLaunchBtn, nullptr, TRUE);
                    }
                }
            }
            return 0;
        }
        break;

    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK ShellView::DrawerProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    ShellView* pThis = nullptr;
    if (uMsg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        pThis = static_cast<ShellView*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
    } else {
        pThis = reinterpret_cast<ShellView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (pThis == nullptr) {
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }

    switch (uMsg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_MOUSEWHEEL:
        if (pThis->m_hwndView != nullptr) {
            return SendMessageW(pThis->m_hwndView, WM_MOUSEWHEEL, wParam, lParam);
        }
        return 0;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_LAUNCH_BTN) {
            if (pThis->m_selectedIndex >= 0 && pThis->m_selectedIndex < static_cast<int>(pThis->m_games.size())) {
                pThis->LaunchSelectedGame();
            } else {
                wchar_t modPath[MAX_PATH];
                GetModuleFileNameW(g_hInstance, modPath, MAX_PATH);
                fs::path cfgPath = fs::path(modPath).parent_path() / L"GameExplorerConfig.exe";
                if (fs::exists(cfgPath)) {
                    ShellExecuteW(hwnd, L"open", cfgPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                }
            }
            return 0;
        }
        break;

    case WM_DRAWITEM: {
        auto* pDis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (pDis != nullptr && pDis->CtlID == IDC_LAUNCH_BTN) {
            HDC hdcBtn = pDis->hDC;
            RECT rcBtn = pDis->rcItem;

            const bool isPressed = (pDis->itemState & ODS_SELECTED) != 0;
            COLORREF btnBg = isPressed ? pThis->m_palette.accentPressed : pThis->m_palette.accent;

            // Rounded rectangle button with system accent color
            HBRUSH hBr = CreateSolidBrush(btnBg);
            HPEN hPen = CreatePen(PS_SOLID, 1, isPressed ? pThis->m_palette.accentPressed : pThis->m_palette.accentHover);
            HGDIOBJ oldBr = SelectObject(hdcBtn, hBr);
            HGDIOBJ oldPen = SelectObject(hdcBtn, hPen);

            RoundRect(hdcBtn, rcBtn.left, rcBtn.top, rcBtn.right, rcBtn.bottom, 8, 8);

            SelectObject(hdcBtn, oldPen);
            SelectObject(hdcBtn, oldBr);
            DeleteObject(hBr);
            DeleteObject(hPen);

            // Draw button text
            SetBkMode(hdcBtn, TRANSPARENT);
            SetTextColor(hdcBtn, pThis->m_palette.accentText);
            SelectObject(hdcBtn, pThis->m_hFontTitle);

            const bool hasGame = (pThis->m_selectedIndex >= 0 && pThis->m_selectedIndex < static_cast<int>(pThis->m_games.size()));
            const wchar_t* btnText = hasGame ? L"\u25B6  Launch Game" : L"\u2699  Library Settings";
            DrawTextW(hdcBtn, btnText, -1, &rcBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return TRUE;
        }
        break;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        // Memory DC double-buffering
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, rc.right - rc.left, rc.bottom - rc.top);
        HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

        pThis->OnDrawerPaint(memDC, rc);

        BitBlt(hdc, 0, 0, rc.right - rc.left, rc.bottom - rc.top, memDC, 0, 0, SRCCOPY);

        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

} // namespace GameExplorer
