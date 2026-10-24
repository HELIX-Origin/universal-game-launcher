#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <filesystem>
#include <algorithm>
#include <vector>
#include <string>
#include "GameExplorerCore/DarkMode.h"
#include "GameExplorerCore/Settings.h"
#include "GameExplorerCore/Storage.h"
#include "GameExplorerCore/Guid.h"
#include "GameExplorerCore/StorefrontScanner.h"
#include "GameExplorerCore/PlatformLauncher.h"
#include "GameExplorerCore/Localization.h"

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "gdiplus.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_COLOR_NONE
#define DWMWA_COLOR_NONE 0xFFFFFFFE
#endif

namespace fs = std::filesystem;
using namespace GameExplorer;

constexpr int TITLE_BAR_HEIGHT = 38;

enum class TitleButton { None, Min, Max, Close };
TitleButton g_hoverBtn = TitleButton::None;
TitleButton g_pressedBtn = TitleButton::None;
bool g_trackingMouse = false;
HICON g_hAppIcon = nullptr;
ULONG_PTR g_gdiplusToken = 0;

RECT GetPillRect(int w) {
    return RECT{ w - 96, 6, w - 8, 32 };
}

RECT GetMinButtonRect(int w) {
    RECT pill = GetPillRect(w);
    return RECT{ pill.left + 2, pill.top + 2, pill.left + 28, pill.bottom - 2 };
}

RECT GetMaxButtonRect(int w) {
    RECT pill = GetPillRect(w);
    return RECT{ pill.left + 30, pill.top + 2, pill.left + 56, pill.bottom - 2 };
}

RECT GetCloseButtonRect(int w) {
    RECT pill = GetPillRect(w);
    return RECT{ pill.left + 58, pill.top + 2, pill.left + 84, pill.bottom - 2 };
}

TitleButton HitTestTitleButtons(int x, int y, int w) {
    POINT pt{ x, y };
    RECT rClose = GetCloseButtonRect(w);
    if (PtInRect(&rClose, pt)) return TitleButton::Close;
    RECT rMax = GetMaxButtonRect(w);
    if (PtInRect(&rMax, pt)) return TitleButton::Max;
    RECT rMin = GetMinButtonRect(w);
    if (PtInRect(&rMin, pt)) return TitleButton::Min;
    return TitleButton::None;
}

// Control IDs
constexpr int IDC_LIST_GAMES          = 3001;
constexpr int IDC_BTN_ADD_GAME        = 3002;
constexpr int IDC_BTN_REMOVE_GAME     = 3003;
constexpr int IDC_BTN_REFRESH_LIST    = 3004;

constexpr int IDC_EDIT_TITLE          = 3010;
constexpr int IDC_EDIT_DEVELOPER      = 3011;
constexpr int IDC_EDIT_PUBLISHER      = 3012;
constexpr int IDC_EDIT_RELEASE_DATE   = 3013;
constexpr int IDC_EDIT_GENRES         = 3014;
constexpr int IDC_EDIT_RATING         = 3015;
constexpr int IDC_EDIT_EXE_PATH       = 3016;
constexpr int IDC_BTN_BROWSE_EXE      = 3017;
constexpr int IDC_EDIT_LAUNCH_ARGS    = 3018;
constexpr int IDC_EDIT_WORK_DIR       = 3019;
constexpr int IDC_EDIT_COVER_PATH     = 3020;
constexpr int IDC_BTN_BROWSE_COVER    = 3021;
constexpr int IDC_EDIT_ICON_PATH      = 3022;
constexpr int IDC_BTN_BROWSE_ICON     = 3023;
constexpr int IDC_EDIT_SYNOPSIS       = 3024;

constexpr int IDC_BTN_SAVE_METADATA   = 3030;
constexpr int IDC_BTN_LAUNCH_SELECTED = 3031;
constexpr int IDC_BTN_OPEN_SHELL      = 3032;

// Globals
HWND g_hwndListGames      = nullptr;
HWND g_hwndEditTitle      = nullptr;
HWND g_hwndEditDeveloper  = nullptr;
HWND g_hwndEditPublisher  = nullptr;
HWND g_hwndEditRelease    = nullptr;
HWND g_hwndEditGenres     = nullptr;
HWND g_hwndEditRating     = nullptr;
HWND g_hwndEditExePath    = nullptr;
HWND g_hwndEditLaunchArgs = nullptr;
HWND g_hwndEditWorkDir    = nullptr;
HWND g_hwndEditCoverPath  = nullptr;
HWND g_hwndEditIconPath   = nullptr;
HWND g_hwndEditSynopsis   = nullptr;

std::vector<GameItem> g_games;
int g_selectedIndex = -1;
std::wstring g_initialSelectAppId;

HFONT g_hFont = nullptr;
HFONT g_hFontBold = nullptr;
bool g_isDarkMode = false;
ThemePalette g_palette{};
HBRUSH g_hBgBrush = nullptr;
HBRUSH g_hCardBrush = nullptr;
HBRUSH g_hEditBrush = nullptr;

constexpr int PREVIEW_BOX_X = 368;
constexpr int PREVIEW_BOX_Y = TITLE_BAR_HEIGHT + 44;
constexpr int PREVIEW_BOX_W = 148;
constexpr int PREVIEW_BOX_H = 222;

void PopulateEditorFromSelected(HWND hwnd) {
    if (g_selectedIndex < 0 || static_cast<size_t>(g_selectedIndex) >= g_games.size()) {
        SetWindowTextW(g_hwndEditTitle, L"");
        SetWindowTextW(g_hwndEditDeveloper, L"");
        SetWindowTextW(g_hwndEditPublisher, L"");
        SetWindowTextW(g_hwndEditRelease, L"");
        SetWindowTextW(g_hwndEditGenres, L"");
        SetWindowTextW(g_hwndEditRating, L"");
        SetWindowTextW(g_hwndEditExePath, L"");
        SetWindowTextW(g_hwndEditLaunchArgs, L"");
        SetWindowTextW(g_hwndEditWorkDir, L"");
        SetWindowTextW(g_hwndEditCoverPath, L"");
        SetWindowTextW(g_hwndEditIconPath, L"");
        SetWindowTextW(g_hwndEditSynopsis, L"");
    } else {
        const auto& g = g_games[static_cast<size_t>(g_selectedIndex)];
        SetWindowTextW(g_hwndEditTitle, g.title.c_str());
        SetWindowTextW(g_hwndEditDeveloper, g.developer.c_str());
        SetWindowTextW(g_hwndEditPublisher, g.publisher.c_str());
        SetWindowTextW(g_hwndEditRelease, g.releaseDate.c_str());
        SetWindowTextW(g_hwndEditGenres, g.genres.c_str());
        SetWindowTextW(g_hwndEditRating, g.rating.c_str());
        SetWindowTextW(g_hwndEditExePath, g.executablePath.c_str());
        SetWindowTextW(g_hwndEditLaunchArgs, g.launchArguments.c_str());
        SetWindowTextW(g_hwndEditWorkDir, g.workingDirectory.c_str());
        SetWindowTextW(g_hwndEditCoverPath, g.coverArtPath.c_str());
        SetWindowTextW(g_hwndEditIconPath, g.iconPath.c_str());
        SetWindowTextW(g_hwndEditSynopsis, g.synopsis.c_str());
    }

    RECT rcPreview = { PREVIEW_BOX_X - 4, PREVIEW_BOX_Y - 4, PREVIEW_BOX_X + PREVIEW_BOX_W + 4, PREVIEW_BOX_Y + PREVIEW_BOX_H + 30 };
    InvalidateRect(hwnd, &rcPreview, FALSE);
}

void RefreshGameList(HWND hwnd, const std::wstring& selectAppId = L"") {
    g_games = StorefrontScanner::ScanAllInstalledGames();
    SendMessageW(g_hwndListGames, LB_RESETCONTENT, 0, 0);

    int targetIdx = 0;
    for (size_t i = 0; i < g_games.size(); ++i) {
        const auto& g = g_games[i];
        std::wstring label = L"[" + std::wstring(PlatformToString(g.platform)) + L"] " + g.title;
        SendMessageW(g_hwndListGames, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        if (!selectAppId.empty() && g.appId == selectAppId) {
            targetIdx = static_cast<int>(i);
        }
    }

    if (!g_games.empty()) {
        g_selectedIndex = targetIdx;
        SendMessageW(g_hwndListGames, LB_SETCURSEL, static_cast<WPARAM>(g_selectedIndex), 0);
    } else {
        g_selectedIndex = -1;
    }

    PopulateEditorFromSelected(hwnd);
}

std::wstring ReadEditString(HWND hEdit) {
    const int len = GetWindowTextLengthW(hEdit);
    if (len <= 0) return L"";
    std::wstring str(static_cast<size_t>(len), L'\0');
    GetWindowTextW(hEdit, str.data(), len + 1);
    return str;
}

void SaveCurrentGameEdits(HWND hwnd) {
    if (g_selectedIndex < 0 || static_cast<size_t>(g_selectedIndex) >= g_games.size()) {
        MessageBoxW(hwnd, L"Please select a game from the library list or add a local game first.", L"Game Explorer Manager", MB_OK | MB_ICONINFORMATION);
        return;
    }

    auto& g = g_games[static_cast<size_t>(g_selectedIndex)];
    g.title            = ReadEditString(g_hwndEditTitle);
    g.developer        = ReadEditString(g_hwndEditDeveloper);
    g.publisher        = ReadEditString(g_hwndEditPublisher);
    g.releaseDate      = ReadEditString(g_hwndEditRelease);
    g.genres           = ReadEditString(g_hwndEditGenres);
    g.rating           = ReadEditString(g_hwndEditRating);
    g.executablePath   = ReadEditString(g_hwndEditExePath);
    g.launchArguments  = ReadEditString(g_hwndEditLaunchArgs);
    g.workingDirectory = ReadEditString(g_hwndEditWorkDir);
    g.coverArtPath     = ReadEditString(g_hwndEditCoverPath);
    g.iconPath         = ReadEditString(g_hwndEditIconPath);
    g.synopsis         = ReadEditString(g_hwndEditSynopsis);

    if (StorefrontScanner::SavePerGameMetadata(g)) {
        SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_PATHW, Storage::GetUserRootPath().c_str(), nullptr);
        const std::wstring currentAppId = g.appId;
        RefreshGameList(hwnd, currentAppId);
        MessageBoxW(hwnd, L"Game metadata and box art saved to your Game Explorer library.", L"Game Explorer Manager", MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxW(hwnd, L"Failed to write per-game metadata JSON.", L"Error", MB_OK | MB_ICONERROR);
    }
}

bool BrowseForFile(HWND hwnd, const wchar_t* title, const wchar_t* filter, std::wstring& outPath) {
    wchar_t szFile[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = ARRAYSIZE(szFile);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = title;

    if (GetOpenFileNameW(&ofn)) {
        outPath = szFile;
        return true;
    }
    return false;
}

LRESULT CALLBACK ManagerWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        NONCLIENTMETRICSW ncm{};
        ncm.cbSize = sizeof(ncm);
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        g_hFont = CreateFontIndirectW(&ncm.lfMessageFont);

        ncm.lfMessageFont.lfWeight = FW_BOLD;
        g_hFontBold = CreateFontIndirectW(&ncm.lfMessageFont);

        g_hAppIcon = static_cast<HICON>(LoadImageW(
            GetModuleHandleW(nullptr), MAKEINTRESOURCEW(100), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR
        ));

        // Left Pane: Games List
        constexpr int leftX = 32;
        constexpr int topY  = TITLE_BAR_HEIGHT + 44;

        g_hwndListGames = CreateWindowExW(
            0, L"LISTBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
            leftX, topY, 296, 480,
            hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LIST_GAMES)), nullptr, nullptr
        );
        SendMessageW(g_hwndListGames, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
        DarkMode::ApplyWindowTheme(g_hwndListGames, g_isDarkMode);

        HWND hBtnAdd = CreateWindowExW(0, L"BUTTON", Localization::GetStrings().managerAddCustomBtn,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            leftX, topY + 490, 152, 32, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_ADD_GAME)), nullptr, nullptr);
        SendMessageW(hBtnAdd, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBold), TRUE);

        HWND hBtnRemove = CreateWindowExW(0, L"BUTTON", Localization::GetStrings().managerRemoveBtn,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            leftX + 160, topY + 490, 74, 32, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_REMOVE_GAME)), nullptr, nullptr);
        SendMessageW(hBtnRemove, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        HWND hBtnRefresh = CreateWindowExW(0, L"BUTTON", L"\u21BB",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            leftX + 242, topY + 490, 54, 32, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_REFRESH_LIST)), nullptr, nullptr);
        SendMessageW(hBtnRefresh, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBold), TRUE);

        // Right Pane: Editor controls next to the 2:3 Box Art Preview
        constexpr int formLabelX = 532;
        constexpr int formEditX  = 648;
        constexpr int formEditW  = 300;

        auto createField = [&](const wchar_t* label, HWND& outEdit, int id, int xLbl, int xEd, int wEd, int y) {
            HWND hLbl = CreateWindowExW(0, L"STATIC", label, WS_CHILD | WS_VISIBLE,
                xLbl, y + 3, xEd - xLbl - 6, 20, hwnd, nullptr, nullptr, nullptr);
            SendMessageW(hLbl, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

            outEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                xEd, y + 2, wEd, 20, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
            SendMessageW(outEdit, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
            DarkMode::ApplyWindowTheme(outEdit, g_isDarkMode);
        };

        createField(L"Game Title:",    g_hwndEditTitle,     IDC_EDIT_TITLE,        formLabelX, formEditX, formEditW, topY + 4);
        createField(L"Developer:",     g_hwndEditDeveloper, IDC_EDIT_DEVELOPER,    formLabelX, formEditX, formEditW, topY + 40);
        createField(L"Publisher:",     g_hwndEditPublisher, IDC_EDIT_PUBLISHER,    formLabelX, formEditX, formEditW, topY + 76);
        createField(L"Release Date:",  g_hwndEditRelease,   IDC_EDIT_RELEASE_DATE, formLabelX, formEditX, formEditW, topY + 112);
        createField(L"Genres:",        g_hwndEditGenres,    IDC_EDIT_GENRES,       formLabelX, formEditX, formEditW, topY + 148);
        createField(L"Box Rating:",    g_hwndEditRating,    IDC_EDIT_RATING,       formLabelX, formEditX, formEditW, topY + 184);

        // Full-width fields below the 2:3 Box Art Preview
        constexpr int fullLabelX = 368;
        constexpr int fullEditX  = 496;
        constexpr int fullEditW  = 364;

        createField(L"Executable:",    g_hwndEditExePath,    IDC_EDIT_EXE_PATH,    fullLabelX, fullEditX, fullEditW, topY + 244);
        HWND hBtnBrowseExe = CreateWindowExW(0, L"BUTTON", L"Browse...",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            fullEditX + fullEditW + 8, topY + 240, 80, 26, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_BROWSE_EXE)), nullptr, nullptr);
        SendMessageW(hBtnBrowseExe, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        createField(L"Arguments:",     g_hwndEditLaunchArgs, IDC_EDIT_LAUNCH_ARGS, fullLabelX, fullEditX, 452, topY + 280);
        createField(L"Working Dir:",   g_hwndEditWorkDir,    IDC_EDIT_WORK_DIR,    fullLabelX, fullEditX, 452, topY + 316);

        createField(L"Box Art (~2:3):", g_hwndEditCoverPath, IDC_EDIT_COVER_PATH,  fullLabelX, fullEditX, fullEditW, topY + 352);
        HWND hBtnBrowseCover = CreateWindowExW(0, L"BUTTON", L"Browse...",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            fullEditX + fullEditW + 8, topY + 348, 80, 26, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_BROWSE_COVER)), nullptr, nullptr);
        SendMessageW(hBtnBrowseCover, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        createField(L"Icon Path:",     g_hwndEditIconPath,   IDC_EDIT_ICON_PATH,   fullLabelX, fullEditX, fullEditW, topY + 388);
        HWND hBtnBrowseIcon = CreateWindowExW(0, L"BUTTON", L"Browse...",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            fullEditX + fullEditW + 8, topY + 384, 80, 26, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_BROWSE_ICON)), nullptr, nullptr);
        SendMessageW(hBtnBrowseIcon, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        createField(L"Synopsis:",      g_hwndEditSynopsis,   IDC_EDIT_SYNOPSIS,    fullLabelX, fullEditX, 452, topY + 424);

        // Bottom Action Buttons in Right Pane
        HWND hBtnSave = CreateWindowExW(0, L"BUTTON", L"Save Game Metadata",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            fullLabelX, topY + 486, 190, 36, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_SAVE_METADATA)), nullptr, nullptr);
        SendMessageW(hBtnSave, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBold), TRUE);

        HWND hBtnLaunch = CreateWindowExW(0, L"BUTTON", L"\u25B6 Play Selected Game",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            fullLabelX + 202, topY + 486, 180, 36, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_LAUNCH_SELECTED)), nullptr, nullptr);
        SendMessageW(hBtnLaunch, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBold), TRUE);

        HWND hBtnOpenShell = CreateWindowExW(0, L"BUTTON", L"Open Games Folder",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            fullLabelX + 394, topY + 486, 186, 36, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_OPEN_SHELL)), nullptr, nullptr);
        SendMessageW(hBtnOpenShell, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        RefreshGameList(hwnd, g_initialSelectAppId);
        return 0;
    }

    case WM_NCACTIVATE:
        return DefWindowProcW(hwnd, WM_NCACTIVATE, wParam, -1);

    case WM_NCPAINT:
        return 0;

    case WM_NCCALCSIZE:
        if (wParam == TRUE) return 0;
        break;

    case WM_NCHITTEST: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hwnd, &pt);

        RECT rc;
        GetClientRect(hwnd, &rc);
        const int w = rc.right - rc.left;
        const int h = rc.bottom - rc.top;

        if (pt.y >= 0 && pt.y < TITLE_BAR_HEIGHT) {
            RECT rcPill = GetPillRect(w);
            if (PtInRect(&rcPill, pt)) return HTCLIENT;
            return HTCAPTION;
        }

        constexpr int border = 6;
        if (pt.y >= h - border) {
            if (pt.x <= border) return HTBOTTOMLEFT;
            if (pt.x >= w - border) return HTBOTTOMRIGHT;
            return HTBOTTOM;
        }
        if (pt.x <= border) return HTLEFT;
        if (pt.x >= w - border) return HTRIGHT;

        return HTCLIENT;
    }

    case WM_MOUSEMOVE: {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);

        RECT rc;
        GetClientRect(hwnd, &rc);
        const int w = rc.right - rc.left;

        const TitleButton newHover = HitTestTitleButtons(x, y, w);
        if (newHover != g_hoverBtn) {
            g_hoverBtn = newHover;
            RECT rcPill = GetPillRect(w);
            InvalidateRect(hwnd, &rcPill, FALSE);
        }

        if (!g_trackingMouse) {
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            g_trackingMouse = true;
        }
        return 0;
    }

    case WM_MOUSELEAVE: {
        g_trackingMouse = false;
        if (g_hoverBtn != TitleButton::None) {
            g_hoverBtn = TitleButton::None;
            RECT rc;
            GetClientRect(hwnd, &rc);
            RECT rcPill = GetPillRect(rc.right - rc.left);
            InvalidateRect(hwnd, &rcPill, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);

        RECT rc;
        GetClientRect(hwnd, &rc);
        const int w = rc.right - rc.left;

        const TitleButton btn = HitTestTitleButtons(x, y, w);
        if (btn != TitleButton::None) {
            g_pressedBtn = btn;
            SetCapture(hwnd);
            RECT rcPill = GetPillRect(w);
            InvalidateRect(hwnd, &rcPill, FALSE);
            return 0;
        }
        break;
    }

    case WM_LBUTTONUP: {
        if (g_pressedBtn != TitleButton::None) {
            ReleaseCapture();
            const TitleButton clicked = g_pressedBtn;
            g_pressedBtn = TitleButton::None;

            RECT rc;
            GetClientRect(hwnd, &rc);
            const int w = rc.right - rc.left;
            RECT rcPill = GetPillRect(w);
            InvalidateRect(hwnd, &rcPill, FALSE);

            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);

            if (HitTestTitleButtons(x, y, w) == clicked) {
                if (clicked == TitleButton::Close) {
                    PostMessageW(hwnd, WM_CLOSE, 0, 0);
                    return 0;
                }
                if (clicked == TitleButton::Max) {
                    ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
                    return 0;
                }
                if (clicked == TitleButton::Min) {
                    ShowWindow(hwnd, SW_MINIMIZE);
                    return 0;
                }
            }
            return 0;
        }
        break;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rcClient;
        GetClientRect(hwnd, &rcClient);
        const int w = rcClient.right - rcClient.left;

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, rcClient.bottom);
        HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

        FillRect(memDC, &rcClient, g_hBgBrush);

        // Title Bar
        RECT rcTitleBar = { 0, 0, w, TITLE_BAR_HEIGHT };
        HBRUSH titleBrush = CreateSolidBrush(g_palette.drawerBg);
        FillRect(memDC, &rcTitleBar, titleBrush);
        DeleteObject(titleBrush);

        HPEN borderPen = CreatePen(PS_SOLID, 1, g_palette.divider);
        HGDIOBJ oldPen = SelectObject(memDC, borderPen);
        MoveToEx(memDC, 0, TITLE_BAR_HEIGHT - 1, nullptr);
        LineTo(memDC, w, TITLE_BAR_HEIGHT - 1);
        SelectObject(memDC, oldPen);
        DeleteObject(borderPen);

        if (g_hAppIcon != nullptr) {
            DrawIconEx(memDC, 12, 11, g_hAppIcon, 16, 16, 0, nullptr, DI_NORMAL);
        }

        SetBkMode(memDC, TRANSPARENT);
        SetTextColor(memDC, g_palette.textPrimary);
        SelectObject(memDC, g_hFontBold);
        RECT rcText = { 36, 10, w - 110, TITLE_BAR_HEIGHT };
        DrawTextW(memDC, L"Windows Game Explorer \u2014 Game Explorer Manager", -1, &rcText, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

        // Window Controls Pill Pod
        RECT rcPill = GetPillRect(w);
        HBRUSH pillBg = CreateSolidBrush(g_isDarkMode ? RGB(32, 32, 32) : RGB(236, 236, 236));
        HPEN pillPen = CreatePen(PS_SOLID, 1, g_palette.divider);
        HGDIOBJ prevBr = SelectObject(memDC, pillBg);
        HGDIOBJ prevPen = SelectObject(memDC, pillPen);
        RoundRect(memDC, rcPill.left, rcPill.top, rcPill.right, rcPill.bottom, 6, 6);
        SelectObject(memDC, prevPen);
        SelectObject(memDC, prevBr);
        DeleteObject(pillPen);
        DeleteObject(pillBg);

        const RECT rMin = GetMinButtonRect(w);
        const RECT rMax = GetMaxButtonRect(w);
        const RECT rClose = GetCloseButtonRect(w);

        if (g_hoverBtn == TitleButton::Min || g_pressedBtn == TitleButton::Min) {
            HBRUSH hBr = CreateSolidBrush(g_isDarkMode ? RGB(52, 52, 52) : RGB(220, 220, 220));
            HPEN hP = CreatePen(PS_NULL, 0, 0);
            HGDIOBJ oB = SelectObject(memDC, hBr);
            HGDIOBJ oP = SelectObject(memDC, hP);
            RoundRect(memDC, rMin.left, rMin.top, rMin.right, rMin.bottom, 4, 4);
            SelectObject(memDC, oP);
            SelectObject(memDC, oB);
            DeleteObject(hP);
            DeleteObject(hBr);
        }
        {
            const int cx = (rMin.left + rMin.right) / 2;
            const int cy = (rMin.top + rMin.bottom) / 2;
            HPEN glyphPen = CreatePen(PS_SOLID, 1, g_palette.textPrimary);
            HGDIOBJ oP = SelectObject(memDC, glyphPen);
            MoveToEx(memDC, cx - 4, cy, nullptr);
            LineTo(memDC, cx + 5, cy);
            SelectObject(memDC, oP);
            DeleteObject(glyphPen);
        }

        if (g_hoverBtn == TitleButton::Max || g_pressedBtn == TitleButton::Max) {
            HBRUSH hBr = CreateSolidBrush(g_isDarkMode ? RGB(52, 52, 52) : RGB(220, 220, 220));
            HPEN hP = CreatePen(PS_NULL, 0, 0);
            HGDIOBJ oB = SelectObject(memDC, hBr);
            HGDIOBJ oP = SelectObject(memDC, hP);
            RoundRect(memDC, rMax.left, rMax.top, rMax.right, rMax.bottom, 4, 4);
            SelectObject(memDC, oP);
            SelectObject(memDC, oB);
            DeleteObject(hP);
            DeleteObject(hBr);
        }
        {
            const int cx = (rMax.left + rMax.right) / 2;
            const int cy = (rMax.top + rMax.bottom) / 2;
            HPEN glyphPen = CreatePen(PS_SOLID, 1, g_palette.textPrimary);
            HGDIOBJ oP = SelectObject(memDC, glyphPen);
            HGDIOBJ oB = SelectObject(memDC, GetStockObject(NULL_BRUSH));
            Rectangle(memDC, cx - 4, cy - 4, cx + 5, cy + 5);
            SelectObject(memDC, oB);
            SelectObject(memDC, oP);
            DeleteObject(glyphPen);
        }

        const bool closeHover = (g_hoverBtn == TitleButton::Close);
        const bool closePressed = (g_pressedBtn == TitleButton::Close);
        if (closeHover || closePressed) {
            HBRUSH hBr = CreateSolidBrush(closePressed ? RGB(158, 33, 21) : RGB(196, 43, 28));
            HPEN hP = CreatePen(PS_NULL, 0, 0);
            HGDIOBJ oB = SelectObject(memDC, hBr);
            HGDIOBJ oP = SelectObject(memDC, hP);
            RoundRect(memDC, rClose.left, rClose.top, rClose.right, rClose.bottom, 4, 4);
            SelectObject(memDC, oP);
            SelectObject(memDC, oB);
            DeleteObject(hP);
            DeleteObject(hBr);
        }
        {
            const int cx = (rClose.left + rClose.right) / 2;
            const int cy = (rClose.top + rClose.bottom) / 2;
            COLORREF cColor = (closeHover || closePressed) ? RGB(255, 255, 255) : g_palette.textPrimary;
            HPEN glyphPen = CreatePen(PS_SOLID, 1, cColor);
            HGDIOBJ oP = SelectObject(memDC, glyphPen);
            MoveToEx(memDC, cx - 4, cy - 4, nullptr);
            LineTo(memDC, cx + 5, cy + 5);
            MoveToEx(memDC, cx + 4, cy - 4, nullptr);
            LineTo(memDC, cx - 5, cy + 5);
            SelectObject(memDC, oP);
            DeleteObject(glyphPen);
        }

        // Left Card (Installed & Local Games List) and Right Card (Game Metadata & Box Art Editor)
        constexpr int cardTop = TITLE_BAR_HEIGHT + 12;
        constexpr int cardBottom = cardTop + 572;

        RECT rcLeftCard  = { 18, cardTop, 342, cardBottom };
        RECT rcRightCard = { 354, cardTop, w - 18, cardBottom };

        auto drawCard = [&](const RECT& rc, const wchar_t* heading) {
            HBRUSH cardBrush = CreateSolidBrush(g_palette.cardBg);
            HPEN cardPen = CreatePen(PS_SOLID, 1, g_palette.cardBorder);
            HGDIOBJ oB = SelectObject(memDC, cardBrush);
            HGDIOBJ oP = SelectObject(memDC, cardPen);
            RoundRect(memDC, rc.left, rc.top, rc.right, rc.bottom, 8, 8);
            SelectObject(memDC, oP);
            SelectObject(memDC, oB);
            DeleteObject(cardPen);
            DeleteObject(cardBrush);

            SetBkMode(memDC, TRANSPARENT);
            SetTextColor(memDC, g_palette.textPrimary);
            SelectObject(memDC, g_hFontBold);
            RECT rcHdr = { rc.left + 14, rc.top + 10, rc.right - 14, rc.top + 28 };
            DrawTextW(memDC, heading, -1, &rcHdr, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
        };

        drawCard(rcLeftCard, L"Installed & Custom Games");
        drawCard(rcRightCard, L"Game Metadata, Launch Options & Box Art Editor");

        // Render ~2:3 Tall Box Art Preview Frame
        RECT rcBoxFrame = { PREVIEW_BOX_X, PREVIEW_BOX_Y, PREVIEW_BOX_X + PREVIEW_BOX_W, PREVIEW_BOX_Y + PREVIEW_BOX_H };
        HBRUSH boxBgBrush = CreateSolidBrush(g_isDarkMode ? RGB(24, 24, 24) : RGB(242, 242, 242));
        HPEN boxPen = CreatePen(PS_SOLID, 1, g_palette.cardBorder);
        HGDIOBJ oBoxB = SelectObject(memDC, boxBgBrush);
        HGDIOBJ oBoxP = SelectObject(memDC, boxPen);
        RoundRect(memDC, rcBoxFrame.left, rcBoxFrame.top, rcBoxFrame.right, rcBoxFrame.bottom, 6, 6);
        SelectObject(memDC, oBoxP);
        SelectObject(memDC, oBoxB);
        DeleteObject(boxPen);
        DeleteObject(boxBgBrush);

        bool drewCover = false;
        const std::wstring coverPath = ReadEditString(g_hwndEditCoverPath);
        if (!coverPath.empty() && fs::exists(coverPath)) {
            Gdiplus::Graphics graphics(memDC);
            graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            Gdiplus::Image img(coverPath.c_str());
            if (img.GetLastStatus() == Gdiplus::Ok && img.GetWidth() > 0 && img.GetHeight() > 0) {
                graphics.DrawImage(&img, PREVIEW_BOX_X + 2, PREVIEW_BOX_Y + 2, PREVIEW_BOX_W - 4, PREVIEW_BOX_H - 4);
                drewCover = true;
            }
        }

        if (!drewCover) {
            SetTextColor(memDC, g_palette.textSecondary);
            SelectObject(memDC, g_hFont);
            RECT rcNoArt = rcBoxFrame;
            DrawTextW(memDC, L"~2:3 Vertical\nBox Art Preview", -1, &rcNoArt, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
        }

        // Sleek borders around edit boxes
        constexpr int topY = TITLE_BAR_HEIGHT + 44;
        const RECT editRects[] = {
            { 646, topY,       950, topY + 24 },
            { 646, topY + 36,  950, topY + 60 },
            { 646, topY + 72,  950, topY + 96 },
            { 646, topY + 108, 950, topY + 132 },
            { 646, topY + 144, 950, topY + 168 },
            { 646, topY + 180, 950, topY + 204 },
            { 494, topY + 240, 862, topY + 264 },
            { 494, topY + 276, 950, topY + 300 },
            { 494, topY + 312, 950, topY + 336 },
            { 494, topY + 348, 862, topY + 372 },
            { 494, topY + 384, 862, topY + 408 },
            { 494, topY + 420, 950, topY + 444 }
        };

        for (const auto& er : editRects) {
            HPEN ePen = CreatePen(PS_SOLID, 1, g_palette.cardBorder);
            HBRUSH eBr = CreateSolidBrush(g_isDarkMode ? RGB(32, 32, 32) : RGB(255, 255, 255));
            HGDIOBJ oB = SelectObject(memDC, eBr);
            HGDIOBJ oP = SelectObject(memDC, ePen);
            RoundRect(memDC, er.left, er.top, er.right, er.bottom, 4, 4);
            SelectObject(memDC, oP);
            SelectObject(memDC, oB);
            DeleteObject(ePen);
            DeleteObject(eBr);
        }

        BitBlt(hdc, 0, 0, w, rcClient.bottom, memDC, 0, 0, SRCCOPY);

        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DRAWITEM: {
        auto* pDis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (pDis == nullptr) break;

        HDC hdcBtn = pDis->hDC;
        RECT rcBtn = pDis->rcItem;
        const bool isPressed = (pDis->itemState & ODS_SELECTED) != 0;

        auto drawBtn = [&](const wchar_t* text, bool accent, bool bold) {
            COLORREF btnBg = accent
                ? (isPressed ? g_palette.accentPressed : g_palette.accent)
                : (isPressed ? (g_isDarkMode ? RGB(65, 65, 65) : RGB(210, 210, 210))
                             : (g_isDarkMode ? RGB(52, 52, 52) : RGB(230, 230, 230)));
            COLORREF txtColor = accent ? g_palette.accentText : g_palette.textPrimary;
            COLORREF borderCol = accent ? g_palette.accentHover : g_palette.cardBorder;

            HBRUSH hBr = CreateSolidBrush(btnBg);
            HPEN hPen = CreatePen(PS_SOLID, 1, borderCol);
            HGDIOBJ oldBr = SelectObject(hdcBtn, hBr);
            HGDIOBJ oldPen = SelectObject(hdcBtn, hPen);
            RoundRect(hdcBtn, rcBtn.left, rcBtn.top, rcBtn.right, rcBtn.bottom, 6, 6);
            SelectObject(hdcBtn, oldBr);
            SelectObject(hdcBtn, oldPen);
            DeleteObject(hBr);
            DeleteObject(hPen);

            SetBkMode(hdcBtn, TRANSPARENT);
            SetTextColor(hdcBtn, txtColor);
            SelectObject(hdcBtn, bold ? g_hFontBold : g_hFont);
            DrawTextW(hdcBtn, text, -1, &rcBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        };

        switch (pDis->CtlID) {
        case IDC_BTN_ADD_GAME:        drawBtn(L"\u2795 Add Local Game...", true, true); return TRUE;
        case IDC_BTN_REMOVE_GAME:     drawBtn(L"Remove", false, false); return TRUE;
        case IDC_BTN_REFRESH_LIST:    drawBtn(L"\u21BB", false, true); return TRUE;
        case IDC_BTN_BROWSE_EXE:      drawBtn(L"Browse...", false, false); return TRUE;
        case IDC_BTN_BROWSE_COVER:    drawBtn(L"Browse...", false, false); return TRUE;
        case IDC_BTN_BROWSE_ICON:     drawBtn(L"Browse...", false, false); return TRUE;
        case IDC_BTN_SAVE_METADATA:   drawBtn(L"Save Game Metadata", true, true); return TRUE;
        case IDC_BTN_LAUNCH_SELECTED: drawBtn(L"\u25B6 Play Selected Game", false, true); return TRUE;
        case IDC_BTN_OPEN_SHELL:      drawBtn(L"Open Games Folder", false, false); return TRUE;
        }
        break;
    }

    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        const int code = HIWORD(wParam);

        if (id == IDC_LIST_GAMES && code == LBN_SELCHANGE) {
            g_selectedIndex = static_cast<int>(SendMessageW(g_hwndListGames, LB_GETCURSEL, 0, 0));
            PopulateEditorFromSelected(hwnd);
            return 0;
        }

        if (id == IDC_BTN_ADD_GAME) {
            std::wstring chosenExe;
            if (BrowseForFile(
                hwnd,
                L"Select Local Game Executable, Shortcut, or ROM",
                L"Game Executables & Shortcuts (*.exe;*.lnk;*.bat;*.cmd)\0*.exe;*.lnk;*.bat;*.cmd\0All Files (*.*)\0*.*\0",
                chosenExe
            )) {
                if (StorefrontScanner::AddCustomLocalGame(chosenExe)) {
                    RefreshGameList(hwnd);
                    if (!g_games.empty()) {
                        g_selectedIndex = static_cast<int>(g_games.size() - 1);
                        SendMessageW(g_hwndListGames, LB_SETCURSEL, static_cast<WPARAM>(g_selectedIndex), 0);
                        PopulateEditorFromSelected(hwnd);
                    }
                }
            }
            return 0;
        }

        if (id == IDC_BTN_REMOVE_GAME) {
            if (g_selectedIndex >= 0 && static_cast<size_t>(g_selectedIndex) < g_games.size()) {
                const auto& g = g_games[static_cast<size_t>(g_selectedIndex)];
                if (StorefrontScanner::RemoveCustomLocalGame(g.appId)) {
                    RefreshGameList(hwnd);
                } else {
                    MessageBoxW(hwnd, L"Storefront games are managed by their provider client. Only custom local games or saved metadata overrides can be removed.", L"Game Explorer Manager", MB_OK | MB_ICONINFORMATION);
                }
            }
            return 0;
        }

        if (id == IDC_BTN_REFRESH_LIST) {
            RefreshGameList(hwnd);
            return 0;
        }

        if (id == IDC_BTN_BROWSE_EXE) {
            std::wstring path;
            if (BrowseForFile(hwnd, L"Select Game Executable", L"Executables (*.exe;*.bat;*.cmd;*.lnk)\0*.exe;*.bat;*.cmd;*.lnk\0All Files (*.*)\0*.*\0", path)) {
                SetWindowTextW(g_hwndEditExePath, path.c_str());
                if (ReadEditString(g_hwndEditWorkDir).empty()) {
                    SetWindowTextW(g_hwndEditWorkDir, fs::path(path).parent_path().wstring().c_str());
                }
            }
            return 0;
        }

        if (id == IDC_BTN_BROWSE_COVER) {
            std::wstring path;
            if (BrowseForFile(hwnd, L"Select Vertical Box Art (~2:3 Cover Image or Animated GIF)", L"Image Files (*.png;*.jpg;*.jpeg;*.gif;*.webp;*.bmp;*.ico;*.tiff)\0*.png;*.jpg;*.jpeg;*.gif;*.webp;*.bmp;*.ico;*.tiff\0All Files (*.*)\0*.*\0", path)) {
                SetWindowTextW(g_hwndEditCoverPath, path.c_str());
                RECT rcPreview = { PREVIEW_BOX_X - 4, PREVIEW_BOX_Y - 4, PREVIEW_BOX_X + PREVIEW_BOX_W + 4, PREVIEW_BOX_Y + PREVIEW_BOX_H + 4 };
                InvalidateRect(hwnd, &rcPreview, FALSE);
            }
            return 0;
        }

        if (id == IDC_BTN_BROWSE_ICON) {
            std::wstring path;
            if (BrowseForFile(hwnd, L"Select Game Icon or Executable", L"Icons & Images (*.ico;*.png;*.gif;*.jpg;*.jpeg;*.webp;*.bmp;*.exe)\0*.ico;*.png;*.gif;*.jpg;*.jpeg;*.webp;*.bmp;*.exe\0All Files (*.*)\0*.*\0", path)) {
                SetWindowTextW(g_hwndEditIconPath, path.c_str());
            }
            return 0;
        }

        if (id == IDC_BTN_SAVE_METADATA) {
            SaveCurrentGameEdits(hwnd);
            return 0;
        }

        if (id == IDC_BTN_LAUNCH_SELECTED) {
            if (g_selectedIndex >= 0 && static_cast<size_t>(g_selectedIndex) < g_games.size()) {
                PlatformLauncher::Launch(hwnd, g_games[static_cast<size_t>(g_selectedIndex)]);
            }
            return 0;
        }

        if (id == IDC_BTN_OPEN_SHELL) {
            const std::wstring cmd = std::wstring(L"::") + SZ_CLSID_GAME_EXPLORER;
            ShellExecuteW(hwnd, L"open", L"explorer.exe", cmd.c_str(), nullptr, SW_SHOWNORMAL);
            return 0;
        }
        break;
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        auto hdc = reinterpret_cast<HDC>(wParam);
        SetBkColor(hdc, g_palette.cardBg);
        SetTextColor(hdc, g_palette.textPrimary);
        return reinterpret_cast<LRESULT>(g_hCardBrush);
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        auto hdc = reinterpret_cast<HDC>(wParam);
        const COLORREF editBg = g_isDarkMode ? RGB(32, 32, 32) : RGB(255, 255, 255);
        SetBkColor(hdc, editBg);
        SetTextColor(hdc, g_palette.textPrimary);
        return reinterpret_cast<LRESULT>(g_hEditBrush);
    }

    case WM_DESTROY:
        if (g_hFont != nullptr) DeleteObject(g_hFont);
        if (g_hFontBold != nullptr) DeleteObject(g_hFontBold);
        if (g_hBgBrush != nullptr) DeleteObject(g_hBgBrush);
        if (g_hCardBrush != nullptr) DeleteObject(g_hCardBrush);
        if (g_hEditBrush != nullptr) DeleteObject(g_hEditBrush);
        if (g_hAppIcon != nullptr) DestroyIcon(g_hAppIcon);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, PWSTR lpCmdLine, int nCmdShow) {
    if (lpCmdLine != nullptr && lpCmdLine[0] != L'\0') {
        g_initialSelectAppId = lpCmdLine;
    }
    INITCOMMONCONTROLSEX icex{};
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icex);

    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, nullptr);

    DarkMode::Initialize();
    g_isDarkMode = DarkMode::IsSystemDarkMode();
    g_palette = DarkMode::GetPalette(g_isDarkMode);
    g_hBgBrush = CreateSolidBrush(g_palette.canvasBg);
    g_hCardBrush = CreateSolidBrush(g_palette.cardBg);
    g_hEditBrush = CreateSolidBrush(g_isDarkMode ? RGB(32, 32, 32) : RGB(255, 255, 255));

    const wchar_t* CLASS_NAME = L"GameExplorerManagerWnd";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = ManagerWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = g_hBgBrush;
    wc.lpszClassName = CLASS_NAME;
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        0, CLASS_NAME, Localization::GetStrings().managerWindowTitle,
        WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        CW_USEDEFAULT, CW_USEDEFAULT, 984, 646,
        nullptr, nullptr, hInstance, nullptr
    );

    if (hwnd == nullptr) return 1;

    const int roundPreference = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &roundPreference, sizeof(roundPreference));

    BOOL isDark = g_isDarkMode ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &isDark, sizeof(isDark));

    const COLORREF noneBorder = DWMWA_COLOR_NONE;
    DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &noneBorder, sizeof(noneBorder));

    MARGINS margins = { 0, 0, 1, 0 };
    DwmExtendFrameIntoClientArea(hwnd, &margins);

    DarkMode::ApplyWindowTheme(hwnd, g_isDarkMode);
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_gdiplusToken != 0) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
    }

    return static_cast<int>(msg.wParam);
}
