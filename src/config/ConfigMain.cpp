#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <filesystem>
#include <algorithm>
#include "GameExplorerCore/DarkMode.h"
#include "GameExplorerCore/Settings.h"
#include "GameExplorerCore/Database.h"
#include "GameExplorerCore/Storage.h"
#include "GameExplorerCore/Guid.h"
#include "GameExplorerCore/StorefrontScanner.h"

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comdlg32.lib")

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

constexpr int IDC_BTN_TOGGLE_INTEGRATION = 2000;
constexpr int IDC_LBL_INTEGRATION_STATUS = 2001;

constexpr int IDC_BTN_ADD_LOCAL_GAME = 2011;
constexpr int IDC_BTN_POPULATE_LIBRARY = 2012;
constexpr int IDC_BTN_OPEN_METADATA_DIR = 2013;
constexpr int IDC_CHK_ONEDRIVE = 2017;

constexpr int IDC_EDIT_SGDB_KEY = 2021;
constexpr int IDC_EDIT_IGDB_ID = 2022;
constexpr int IDC_EDIT_IGDB_SECRET = 2023;

constexpr int IDC_BTN_SAVE = 2031;
constexpr int IDC_BTN_OPEN_EXPLORER = 2032;

HWND g_hwndBtnToggleIntegration = nullptr;
HWND g_hwndLblIntegrationStatus = nullptr;

HWND g_hwndBtnAddLocalGame = nullptr;
HWND g_hwndBtnPopulateLibrary = nullptr;
HWND g_hwndBtnOpenMetadataDir = nullptr;
HWND g_hwndChkOneDrive = nullptr;

HWND g_hwndEditSgdb = nullptr;
HWND g_hwndEditIgdb = nullptr;
HWND g_hwndEditIgdbSecret = nullptr;

HFONT g_hFont = nullptr;
HFONT g_hFontBold = nullptr;
bool g_isDarkMode = false;
ThemePalette g_palette{};
HBRUSH g_hBgBrush = nullptr;
HBRUSH g_hCardBrush = nullptr;
HBRUSH g_hEditBrush = nullptr;

bool IsIntegrationEnabled() {
    HKEY hKey = nullptr;
    const std::wstring clsidKey = std::wstring(L"Software\\Classes\\CLSID\\") + SZ_CLSID_GAME_EXPLORER;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, clsidKey.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return true;
    }
    return false;
}

void UpdateIntegrationStatusUI() {
    const bool enabled = IsIntegrationEnabled();
    if (g_hwndLblIntegrationStatus != nullptr) {
        SetWindowTextW(
            g_hwndLblIntegrationStatus,
            enabled ? L"Integration Status: \u25CF Enabled (Pinned in File Explorer && Service Active)"
                    : L"Integration Status: \u25CB Disabled (Not Registered in File Explorer)"
        );
        InvalidateRect(g_hwndLblIntegrationStatus, nullptr, TRUE);
    }
    if (g_hwndBtnToggleIntegration != nullptr) {
        InvalidateRect(g_hwndBtnToggleIntegration, nullptr, TRUE);
    }
}

void ToggleIntegration(HWND hwndParent) {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    const fs::path dir = fs::path(exePath).parent_path();
    const fs::path dllPath = dir / L"GameExplorerShell.dll";

    if (!fs::exists(dllPath)) {
        MessageBoxW(hwndParent, L"GameExplorerShell.dll not found in the application directory.", L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    const bool currentlyEnabled = IsIntegrationEnabled();

    std::wstring cmd = L"regsvr32.exe /s " + (currentlyEnabled ? (L"/u \"" + dllPath.wstring() + L"\"") : (L"\"" + dllPath.wstring() + L"\""));
    
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    if (CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 3000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    UpdateIntegrationStatusUI();
    const bool nowEnabled = IsIntegrationEnabled();

    MessageBoxW(
        hwndParent,
        nowEnabled
            ? L"Windows Game Explorer integration and background service have been enabled.\n\nThe Games tab is now pinned alongside Home and Gallery in File Explorer."
            : L"Windows Game Explorer integration and background service have been disabled.\n\nThe Games tab has been removed from File Explorer.",
        L"Windows Game Explorer",
        MB_OK | MB_ICONINFORMATION
    );
}

void LoadConfigValues() {
    const auto s = SettingsManager::Load();

    SendMessageW(g_hwndChkOneDrive, BM_SETCHECK, s.enableOneDriveBackup ? BST_CHECKED : BST_UNCHECKED, 0);

    SetWindowTextW(g_hwndEditSgdb, s.steamGridDbApiKey.c_str());
    SetWindowTextW(g_hwndEditIgdb, s.igdbClientId.c_str());
    SetWindowTextW(g_hwndEditIgdbSecret, s.igdbClientSecret.c_str());

    UpdateIntegrationStatusUI();
}

void SaveConfigValues(HWND hwndParent) {
    AppSettings s;
    s.layoutMode = LayoutMode::DetailsView;

    s.enableOneDriveBackup = (SendMessageW(g_hwndChkOneDrive, BM_GETCHECK, 0, 0) == BST_CHECKED);

    wchar_t buf[256];
    GetWindowTextW(g_hwndEditSgdb, buf, ARRAYSIZE(buf));
    s.steamGridDbApiKey = buf;

    GetWindowTextW(g_hwndEditIgdb, buf, ARRAYSIZE(buf));
    s.igdbClientId = buf;

    GetWindowTextW(g_hwndEditIgdbSecret, buf, ARRAYSIZE(buf));
    s.igdbClientSecret = buf;

    if (SettingsManager::Save(s)) {
        MessageBoxW(hwndParent, L"Settings successfully saved to Game Explorer library.", L"Windows Game Explorer", MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxW(hwndParent, L"Failed to save settings. Please ensure write access.", L"Error", MB_OK | MB_ICONERROR);
    }
}

LRESULT CALLBACK ConfigWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
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

        // Card 0: File Explorer Shell Integration
        constexpr int card0Y = TITLE_BAR_HEIGHT + 12;
        g_hwndLblIntegrationStatus = CreateWindowExW(0, L"STATIC", L"Integration Status: Checking...",
            WS_CHILD | WS_VISIBLE,
            36, card0Y + 34, 510, 20, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LBL_INTEGRATION_STATUS)), nullptr, nullptr);
        SendMessageW(g_hwndLblIntegrationStatus, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        g_hwndBtnToggleIntegration = CreateWindowExW(0, L"BUTTON", L"Enable Integration",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            36, card0Y + 58, 170, 32, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_TOGGLE_INTEGRATION)), nullptr, nullptr);
        SendMessageW(g_hwndBtnToggleIntegration, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBold), TRUE);

        HWND hBtnOpen = CreateWindowExW(0, L"BUTTON", L"Open Games in File Explorer",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            220, card0Y + 58, 220, 32, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_OPEN_EXPLORER)), nullptr, nullptr);
        SendMessageW(hBtnOpen, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        // Card 1: Library Presentation Model
        constexpr int card1Y = card0Y + 105 + 12;
        HWND hLblLayoutPrimary = CreateWindowExW(0, L"STATIC",
            L"Details / List View with Integrated Details Pane",
            WS_CHILD | WS_VISIBLE,
            36, card1Y + 32, 560, 20, hwnd, nullptr, nullptr, nullptr);
        SendMessageW(hLblLayoutPrimary, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBold), TRUE);

        HWND hLblLayoutSub = CreateWindowExW(0, L"STATIC",
            L"Displays installed games in a high-performance structured table (Title, Platform, Size, Rating, Developer, Released) with live cover art and metadata in the right details pane.",
            WS_CHILD | WS_VISIBLE,
            36, card1Y + 54, 560, 36, hwnd, nullptr, nullptr, nullptr);
        SendMessageW(hLblLayoutSub, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        // Card 2: Installed Storefront Discovery & Manual Local Games
        constexpr int card2Y = card1Y + 95 + 12;
        HWND hLblDiscoveryInfo = CreateWindowExW(0, L"STATIC",
            L"Auto-detects Steam, Epic, GOG, itch.io, Battle.net, Ubisoft, Rockstar, EA, Amazon, Xbox, and Square Enix.",
            WS_CHILD | WS_VISIBLE, 36, card2Y + 30, 560, 36, hwnd, nullptr, nullptr, nullptr);
        SendMessageW(hLblDiscoveryInfo, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        g_hwndBtnAddLocalGame = CreateWindowExW(0, L"BUTTON", L"\u2795 Add Local Game...",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            36, card2Y + 74, 160, 32, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_ADD_LOCAL_GAME)), nullptr, nullptr);
        SendMessageW(g_hwndBtnAddLocalGame, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBold), TRUE);

        g_hwndBtnPopulateLibrary = CreateWindowExW(0, L"BUTTON", L"\u21BB Populate Library",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            206, card2Y + 74, 175, 32, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_POPULATE_LIBRARY)), nullptr, nullptr);
        SendMessageW(g_hwndBtnPopulateLibrary, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBold), TRUE);

        g_hwndBtnOpenMetadataDir = CreateWindowExW(0, L"BUTTON", L"Open Metadata Folder",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            391, card2Y + 74, 165, 32, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_OPEN_METADATA_DIR)), nullptr, nullptr);
        SendMessageW(g_hwndBtnOpenMetadataDir, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        // Card 3: Metadata API Keys & Cloud Sync
        constexpr int card3Y = card2Y + 122 + 12;
        HWND hLblSgdb = CreateWindowExW(0, L"STATIC", L"SteamGridDB API Key:",
            WS_CHILD | WS_VISIBLE, 36, card3Y + 34, 160, 20, hwnd, nullptr, nullptr, nullptr);
        SendMessageW(hLblSgdb, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        g_hwndEditSgdb = CreateWindowExW(0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 208, card3Y + 33, 336, 22, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EDIT_SGDB_KEY)), nullptr, nullptr);
        SendMessageW(g_hwndEditSgdb, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        HWND hLblIgdb = CreateWindowExW(0, L"STATIC", L"IGDB Client ID:",
            WS_CHILD | WS_VISIBLE, 36, card3Y + 64, 160, 20, hwnd, nullptr, nullptr, nullptr);
        SendMessageW(hLblIgdb, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        g_hwndEditIgdb = CreateWindowExW(0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 208, card3Y + 63, 336, 22, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EDIT_IGDB_ID)), nullptr, nullptr);
        SendMessageW(g_hwndEditIgdb, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        HWND hLblIgdbSecret = CreateWindowExW(0, L"STATIC", L"IGDB Client Secret:",
            WS_CHILD | WS_VISIBLE, 36, card3Y + 94, 160, 20, hwnd, nullptr, nullptr, nullptr);
        SendMessageW(hLblIgdbSecret, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        g_hwndEditIgdbSecret = CreateWindowExW(0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_PASSWORD | ES_AUTOHSCROLL, 208, card3Y + 93, 336, 22, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EDIT_IGDB_SECRET)), nullptr, nullptr);
        SendMessageW(g_hwndEditIgdbSecret, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        g_hwndChkOneDrive = CreateWindowExW(0, L"BUTTON", L"Decentralized profile && save state cloud backup to local OneDrive folder",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 36, card3Y + 126, 560, 22, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CHK_ONEDRIVE)), nullptr, nullptr);
        SendMessageW(g_hwndChkOneDrive, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);

        // Apply Dark Mode UxTheme Explorer styles to common controls
        const wchar_t* themeName = g_isDarkMode ? L"DarkMode_Explorer" : L"Explorer";
        SetWindowTheme(g_hwndChkOneDrive, themeName, nullptr);

        DarkMode::ApplyWindowTheme(g_hwndEditSgdb, g_isDarkMode);
        DarkMode::ApplyWindowTheme(g_hwndEditIgdb, g_isDarkMode);
        DarkMode::ApplyWindowTheme(g_hwndEditIgdbSecret, g_isDarkMode);

        // Save Button (Owner-drawn with system accent color)
        constexpr int saveY = card3Y + 160 + 16;
        HWND hBtnSave = CreateWindowExW(0, L"BUTTON", L"Save Configuration",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            20, saveY, 190, 38, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_SAVE)), nullptr, nullptr);
        SendMessageW(hBtnSave, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBold), TRUE);

        LoadConfigValues();
        return 0;
    }

    case WM_NCACTIVATE:
        return DefWindowProcW(hwnd, WM_NCACTIVATE, wParam, -1);

    case WM_NCPAINT:
        return 0;

    case WM_NCCALCSIZE:
        if (wParam == TRUE) {
            // Remove standard window title bar while keeping window border drop shadow and sizing
            return 0;
        }
        break;

    case WM_NCHITTEST: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hwnd, &pt);

        RECT rc;
        GetClientRect(hwnd, &rc);
        const int w = rc.right - rc.left;
        const int h = rc.bottom - rc.top;

        // Title Bar region
        if (pt.y >= 0 && pt.y < TITLE_BAR_HEIGHT) {
            // Stylized nh-reader window controls pill pod
            RECT rcPill = GetPillRect(w);
            if (PtInRect(&rcPill, pt)) {
                return HTCLIENT;
            }
            return HTCAPTION; // Draggable title bar
        }

        // Window border resize hit testing
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

        // Double buffer
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, rcClient.bottom);
        HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

        // Fill Main Window Background
        FillRect(memDC, &rcClient, g_hBgBrush);

        // =====================================================================
        // STYLIZED WINDOWS 11 CUSTOM TITLE BAR
        // =====================================================================
        RECT rcTitleBar = { 0, 0, w, TITLE_BAR_HEIGHT };
        HBRUSH titleBrush = CreateSolidBrush(g_palette.drawerBg);
        FillRect(memDC, &rcTitleBar, titleBrush);
        DeleteObject(titleBrush);

        // 1px Bottom Border Line
        HPEN borderPen = CreatePen(PS_SOLID, 1, g_palette.divider);
        HGDIOBJ oldPen = SelectObject(memDC, borderPen);
        MoveToEx(memDC, 0, TITLE_BAR_HEIGHT - 1, nullptr);
        LineTo(memDC, w, TITLE_BAR_HEIGHT - 1);
        SelectObject(memDC, oldPen);
        DeleteObject(borderPen);

        // Embedded Purple Game Controller Icon
        if (g_hAppIcon != nullptr) {
            DrawIconEx(memDC, 12, 11, g_hAppIcon, 16, 16, 0, nullptr, DI_NORMAL);
        }

        // Title Bar Label
        SetBkMode(memDC, TRANSPARENT);
        SetTextColor(memDC, g_palette.textPrimary);
        SelectObject(memDC, g_hFontBold);
        RECT rcText = { 36, 10, w - 110, TITLE_BAR_HEIGHT };
        DrawTextW(memDC, L"Windows Game Explorer \u2014 Settings", -1, &rcText, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

        // =====================================================================
        // NH-READER STYLIZED WINDOW CONTROLS PILL POD (Min / Max / Close)
        // =====================================================================
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

        // Render Minimize Button
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

        // Render Maximize Button
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
            if (IsZoomed(hwnd)) {
                Rectangle(memDC, cx - 3, cy - 5, cx + 5, cy + 3);
                Rectangle(memDC, cx - 5, cy - 3, cx + 3, cy + 5);
            } else {
                Rectangle(memDC, cx - 4, cy - 4, cx + 5, cy + 5);
            }
            SelectObject(memDC, oB);
            SelectObject(memDC, oP);
            DeleteObject(glyphPen);
        }

        // Render Close Button (Turns Fluent red on hover)
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

        // =====================================================================
        // AUTHENTIC WINDOWS 11 FLUENT SECTION CARDS
        // =====================================================================
        constexpr int card0Y = TITLE_BAR_HEIGHT + 12;
        constexpr int card1Y = card0Y + 105 + 12;
        constexpr int card2Y = card1Y + 95 + 12;
        constexpr int card3Y = card2Y + 122 + 12;

        const struct CardSpec {
            int y, h;
            const wchar_t* title;
        } cards[] = {
            { card0Y, 105, L"File Explorer Shell Integration" },
            { card1Y, 95, L"Library Presentation Model" },
            { card2Y, 122, L"Storefront Discovery & Manual Local Games" },
            { card3Y, 160, L"Metadata API Keys & Cloud Sync" }
        };

        for (const auto& c : cards) {
            RECT rcCard = { 20, c.y, w - 20, c.y + c.h };
            HBRUSH cardBrush = CreateSolidBrush(g_palette.cardBg);
            HPEN cardPen = CreatePen(PS_SOLID, 1, g_palette.cardBorder);
            HGDIOBJ oB = SelectObject(memDC, cardBrush);
            HGDIOBJ oP = SelectObject(memDC, cardPen);

            RoundRect(memDC, rcCard.left, rcCard.top, rcCard.right, rcCard.bottom, 8, 8);

            SelectObject(memDC, oP);
            SelectObject(memDC, oB);
            DeleteObject(cardPen);
            DeleteObject(cardBrush);

            // Card Header Text
            SetBkMode(memDC, TRANSPARENT);
            SetTextColor(memDC, g_palette.textPrimary);
            SelectObject(memDC, g_hFontBold);
            RECT rcCardTitle = { 36, c.y + 10, w - 40, c.y + 28 };
            DrawTextW(memDC, c.title, -1, &rcCardTitle, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
        }

        // Sleek Fluent borders for edit controls in Card 3
        const int editYPositions[] = { card3Y + 31, card3Y + 61, card3Y + 91 };
        for (int ey : editYPositions) {
            RECT rcEditBorder = { 206, ey, 546, ey + 26 };
            HPEN ePen = CreatePen(PS_SOLID, 1, g_palette.cardBorder);
            HBRUSH eBr = CreateSolidBrush(g_isDarkMode ? RGB(32, 32, 32) : RGB(255, 255, 255));
            HGDIOBJ oB = SelectObject(memDC, eBr);
            HGDIOBJ oP = SelectObject(memDC, ePen);
            RoundRect(memDC, rcEditBorder.left, rcEditBorder.top, rcEditBorder.right, rcEditBorder.bottom, 4, 4);
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

        if (pDis->CtlID == IDC_BTN_TOGGLE_INTEGRATION) {
            const bool enabled = IsIntegrationEnabled();
            COLORREF btnBg;
            COLORREF txtColor;
            if (enabled) {
                btnBg = isPressed ? g_palette.accentPressed : g_palette.accent;
                txtColor = g_palette.accentText;
            } else {
                btnBg = isPressed ? (g_isDarkMode ? RGB(65, 65, 65) : RGB(210, 210, 210))
                                  : (g_isDarkMode ? RGB(52, 52, 52) : RGB(230, 230, 230));
                txtColor = g_palette.textPrimary;
            }
            HBRUSH hBr = CreateSolidBrush(btnBg);
            HPEN hPen = CreatePen(PS_SOLID, 1, enabled ? g_palette.accentHover : g_palette.cardBorder);
            HGDIOBJ oldBr = SelectObject(hdcBtn, hBr);
            HGDIOBJ oldPen = SelectObject(hdcBtn, hPen);
            RoundRect(hdcBtn, rcBtn.left, rcBtn.top, rcBtn.right, rcBtn.bottom, 6, 6);
            SelectObject(hdcBtn, oldBr);
            SelectObject(hdcBtn, oldPen);
            DeleteObject(hBr);
            DeleteObject(hPen);

            SetBkMode(hdcBtn, TRANSPARENT);
            SetTextColor(hdcBtn, txtColor);
            SelectObject(hdcBtn, g_hFontBold);
            const wchar_t* label = enabled ? L"Disable Integration" : L"Enable Integration";
            DrawTextW(hdcBtn, label, -1, &rcBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return TRUE;
        }

        if (pDis->CtlID == IDC_BTN_OPEN_EXPLORER) {
            COLORREF btnBg = isPressed ? (g_isDarkMode ? RGB(65, 65, 65) : RGB(210, 210, 210))
                                       : (g_isDarkMode ? RGB(52, 52, 52) : RGB(230, 230, 230));
            HBRUSH hBr = CreateSolidBrush(btnBg);
            HPEN hPen = CreatePen(PS_SOLID, 1, g_palette.cardBorder);
            HGDIOBJ oldBr = SelectObject(hdcBtn, hBr);
            HGDIOBJ oldPen = SelectObject(hdcBtn, hPen);
            RoundRect(hdcBtn, rcBtn.left, rcBtn.top, rcBtn.right, rcBtn.bottom, 6, 6);
            SelectObject(hdcBtn, oldBr);
            SelectObject(hdcBtn, oldPen);
            DeleteObject(hBr);
            DeleteObject(hPen);

            SetBkMode(hdcBtn, TRANSPARENT);
            SetTextColor(hdcBtn, g_palette.textPrimary);
            SelectObject(hdcBtn, g_hFont);
            DrawTextW(hdcBtn, L"Open Games in File Explorer", -1, &rcBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return TRUE;
        }

        if (pDis->CtlID == IDC_BTN_ADD_LOCAL_GAME) {
            COLORREF btnBg = isPressed ? g_palette.accentPressed : g_palette.accent;
            HBRUSH hBr = CreateSolidBrush(btnBg);
            HPEN hPen = CreatePen(PS_SOLID, 1, isPressed ? g_palette.accentPressed : g_palette.accentHover);
            HGDIOBJ oldBr = SelectObject(hdcBtn, hBr);
            HGDIOBJ oldPen = SelectObject(hdcBtn, hPen);
            RoundRect(hdcBtn, rcBtn.left, rcBtn.top, rcBtn.right, rcBtn.bottom, 6, 6);
            SelectObject(hdcBtn, oldBr);
            SelectObject(hdcBtn, oldPen);
            DeleteObject(hBr);
            DeleteObject(hPen);

            SetBkMode(hdcBtn, TRANSPARENT);
            SetTextColor(hdcBtn, g_palette.accentText);
            SelectObject(hdcBtn, g_hFontBold);
            DrawTextW(hdcBtn, L"\u2795 Add Local Game...", -1, &rcBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return TRUE;
        }

        if (pDis->CtlID == IDC_BTN_POPULATE_LIBRARY) {
            COLORREF btnBg = isPressed ? (g_isDarkMode ? RGB(65, 65, 65) : RGB(210, 210, 210))
                                       : (g_isDarkMode ? RGB(52, 52, 52) : RGB(230, 230, 230));
            HBRUSH hBr = CreateSolidBrush(btnBg);
            HPEN hPen = CreatePen(PS_SOLID, 1, g_palette.cardBorder);
            HGDIOBJ oldBr = SelectObject(hdcBtn, hBr);
            HGDIOBJ oldPen = SelectObject(hdcBtn, hPen);
            RoundRect(hdcBtn, rcBtn.left, rcBtn.top, rcBtn.right, rcBtn.bottom, 6, 6);
            SelectObject(hdcBtn, oldBr);
            SelectObject(hdcBtn, oldPen);
            DeleteObject(hBr);
            DeleteObject(hPen);

            SetBkMode(hdcBtn, TRANSPARENT);
            SetTextColor(hdcBtn, g_palette.textPrimary);
            SelectObject(hdcBtn, g_hFont);
            DrawTextW(hdcBtn, L"\u21BB Populate Library", -1, &rcBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return TRUE;
        }

        if (pDis->CtlID == IDC_BTN_OPEN_METADATA_DIR) {
            COLORREF btnBg = isPressed ? (g_isDarkMode ? RGB(65, 65, 65) : RGB(210, 210, 210))
                                       : (g_isDarkMode ? RGB(52, 52, 52) : RGB(230, 230, 230));
            HBRUSH hBr = CreateSolidBrush(btnBg);
            HPEN hPen = CreatePen(PS_SOLID, 1, g_palette.cardBorder);
            HGDIOBJ oldBr = SelectObject(hdcBtn, hBr);
            HGDIOBJ oldPen = SelectObject(hdcBtn, hPen);
            RoundRect(hdcBtn, rcBtn.left, rcBtn.top, rcBtn.right, rcBtn.bottom, 6, 6);
            SelectObject(hdcBtn, oldBr);
            SelectObject(hdcBtn, oldPen);
            DeleteObject(hBr);
            DeleteObject(hPen);

            SetBkMode(hdcBtn, TRANSPARENT);
            SetTextColor(hdcBtn, g_palette.textPrimary);
            SelectObject(hdcBtn, g_hFont);
            DrawTextW(hdcBtn, L"Open Metadata Folder", -1, &rcBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return TRUE;
        }

        if (pDis->CtlID == IDC_BTN_SAVE) {
            COLORREF btnBg = isPressed ? g_palette.accentPressed : g_palette.accent;
            HBRUSH hBr = CreateSolidBrush(btnBg);
            HPEN hPen = CreatePen(PS_SOLID, 1, isPressed ? g_palette.accentPressed : g_palette.accentHover);
            HGDIOBJ oldBr = SelectObject(hdcBtn, hBr);
            HGDIOBJ oldPen = SelectObject(hdcBtn, hPen);
            RoundRect(hdcBtn, rcBtn.left, rcBtn.top, rcBtn.right, rcBtn.bottom, 6, 6);
            SelectObject(hdcBtn, oldBr);
            SelectObject(hdcBtn, oldPen);
            DeleteObject(hBr);
            DeleteObject(hPen);

            SetBkMode(hdcBtn, TRANSPARENT);
            SetTextColor(hdcBtn, g_palette.accentText);
            SelectObject(hdcBtn, g_hFontBold);
            DrawTextW(hdcBtn, L"Save Configuration", -1, &rcBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return TRUE;
        }
        break;
    }

    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        if (id == IDC_BTN_TOGGLE_INTEGRATION) {
            ToggleIntegration(hwnd);
            return 0;
        }
        if (id == IDC_BTN_SAVE) {
            SaveConfigValues(hwnd);
            return 0;
        }
        if (id == IDC_BTN_OPEN_EXPLORER) {
            const std::wstring cmd = std::wstring(L"::") + SZ_CLSID_GAME_EXPLORER;
            ShellExecuteW(hwnd, L"open", L"explorer.exe", cmd.c_str(), nullptr, SW_SHOWNORMAL);
            return 0;
        }
        if (id == IDC_BTN_ADD_LOCAL_GAME) {
            wchar_t szFile[MAX_PATH] = { 0 };
            OPENFILENAMEW ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hwnd;
            ofn.lpstrFilter = L"Game Executables & Shortcuts (*.exe;*.bat;*.cmd;*.lnk)\0*.exe;*.bat;*.cmd;*.lnk\0All Files (*.*)\0*.*\0";
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = ARRAYSIZE(szFile);
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            ofn.lpstrTitle = L"Select Game Executable or Shortcut";

            if (GetOpenFileNameW(&ofn)) {
                if (StorefrontScanner::AddCustomLocalGame(szFile)) {
                    MessageBoxW(hwnd, L"Local game successfully added to your library!\nA per-game metadata.json file has been created in your metadata folder for manual customization and provider enrichment.", L"Windows Game Explorer", MB_OK | MB_ICONINFORMATION);
                } else {
                    MessageBoxW(hwnd, L"Could not add the selected game. Please ensure the file exists.", L"Error", MB_OK | MB_ICONERROR);
                }
            }
            return 0;
        }
        if (id == IDC_BTN_POPULATE_LIBRARY) {
            HCURSOR hOldCursor = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
            auto games = StorefrontScanner::ScanAllInstalledGames();
            for (const auto& g : games) {
                StorefrontScanner::SavePerGameMetadata(g);
            }
            Storage::EnsureDirectoriesCreated();
            SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_PATHW, Storage::GetUserRootPath().c_str(), nullptr);
            SetCursor(hOldCursor);

            wchar_t msg[256];
            swprintf_s(msg, L"Library scan complete! Successfully indexed %zu installed and custom games across all storefronts and drives.", games.size());
            MessageBoxW(hwnd, msg, L"Windows Game Explorer", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        if (id == IDC_BTN_OPEN_METADATA_DIR) {
            Storage::EnsureDirectoriesCreated();
            const fs::path metaDir = Storage::GetMetadataPath();
            ShellExecuteW(hwnd, L"open", metaDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
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

    case WM_CTLCOLOREDIT: {
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

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, PWSTR /*lpCmdLine*/, int nCmdShow) {
    INITCOMMONCONTROLSEX icex{};
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icex);

    DarkMode::Initialize();
    g_isDarkMode = DarkMode::IsSystemDarkMode();
    g_palette = DarkMode::GetPalette(g_isDarkMode);
    g_hBgBrush = CreateSolidBrush(g_palette.canvasBg);
    g_hCardBrush = CreateSolidBrush(g_palette.cardBg);
    g_hEditBrush = CreateSolidBrush(g_isDarkMode ? RGB(32, 32, 32) : RGB(255, 255, 255));

    const wchar_t* CLASS_NAME = L"GameExplorerConfigWnd";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = ConfigWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = g_hBgBrush;
    wc.lpszClassName = CLASS_NAME;
    RegisterClassExW(&wc);

    // Modern Frameless Window with native sizing and DWM shadow
    HWND hwnd = CreateWindowExW(
        0, CLASS_NAME, L"Windows Game Explorer — Configuration & Settings",
        WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        CW_USEDEFAULT, CW_USEDEFAULT, 640, 700,
        nullptr, nullptr, hInstance, nullptr
    );

    if (hwnd == nullptr) return 1;

    // Apply Windows 11 rounded corners and DWM dark mode
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

    return static_cast<int>(msg.wParam);
}
