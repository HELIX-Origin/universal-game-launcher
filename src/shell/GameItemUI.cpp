#include "GameItemUI.h"
#include <shellapi.h>
#include <propvarutil.h>
#include <strsafe.h>
#include <gdiplus.h>
#include <filesystem>
#include <cwctype>

namespace fs = std::filesystem;

extern std::atomic<ULONG> g_dllRefCount;
extern HINSTANCE g_hInstance;

namespace GameExplorer {

namespace {

static const PROPERTYKEY s_supportedKeys[] = {
    PKEY_ItemNameDisplay,
    PKEY_Company,
    PKEY_Author,
    PKEY_Rating,
    PKEY_Size,
    PKEY_Media_Year,
    PKEY_ItemTypeText
};

ULONG ParseYear(const std::wstring& dateStr) {
    for (size_t i = 0; i + 3 < dateStr.size(); ++i) {
        if (iswdigit(dateStr[i]) && iswdigit(dateStr[i + 1]) &&
            iswdigit(dateStr[i + 2]) && iswdigit(dateStr[i + 3])) {
            return static_cast<ULONG>(_wtoi(dateStr.substr(i, 4).c_str()));
        }
    }
    return 0;
}

} // namespace

GameItemUI::GameItemUI(GameItem item, uint16_t itemKind)
    : m_item(std::move(item)), m_itemKind(itemKind) {
    ++g_dllRefCount;
}

GameItemUI::~GameItemUI() {
    --g_dllRefCount;
}

IFACEMETHODIMP GameItemUI::QueryInterface(REFIID riid, void** ppv) {
    if (ppv == nullptr) return E_POINTER;
    *ppv = nullptr;

    if (riid == IID_IUnknown || riid == IID_IThumbnailProvider) {
        *ppv = static_cast<IThumbnailProvider*>(this);
    } else if (riid == IID_IShellItemImageFactory) {
        *ppv = static_cast<IShellItemImageFactory*>(this);
    } else if (riid == IID_IPropertyStore) {
        *ppv = static_cast<IPropertyStore*>(this);
    } else if (riid == IID_IExtractIconW) {
        *ppv = static_cast<IExtractIconW*>(this);
    } else if (riid == IID_IQueryInfo) {
        *ppv = static_cast<IQueryInfo*>(this);
    }

    if (*ppv != nullptr) {
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

IFACEMETHODIMP_(ULONG) GameItemUI::AddRef() {
    return ++m_refCount;
}

IFACEMETHODIMP_(ULONG) GameItemUI::Release() {
    const ULONG c = --m_refCount;
    if (c == 0) delete this;
    return c;
}

HBITMAP GameItemUI::CreateThumbnailBitmap(UINT targetW, UINT targetH) {
    if (targetW == 0) targetW = 256;
    if (targetH == 0) targetH = 256;

    // Determine candidate image path (Cover Art first, then Hero Art) for games
    std::wstring imgPath;
    if (m_itemKind == 1) {
        imgPath = m_item.coverArtPath;
        if (imgPath.empty() || !fs::exists(imgPath)) {
            imgPath = m_item.heroArtPath;
        }
    }

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = static_cast<LONG>(targetW);
    bi.bmiHeader.biHeight = static_cast<LONG>(targetH); // bottom-up
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HDC hdcScreen = GetDC(nullptr);
    HBITMAP hDIB = CreateDIBSection(hdcScreen, &bi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    ReleaseDC(nullptr, hdcScreen);

    if (hDIB == nullptr) return nullptr;

    HDC hdcMem = CreateCompatibleDC(nullptr);
    HGDIOBJ hOld = SelectObject(hdcMem, hDIB);

    if (!imgPath.empty() && fs::exists(imgPath)) {
        Gdiplus::Bitmap bmp(imgPath.c_str());
        if (bmp.GetLastStatus() == Gdiplus::Ok && bmp.GetWidth() > 0 && bmp.GetHeight() > 0) {
            Gdiplus::Graphics g(hdcMem);
            g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            g.Clear(Gdiplus::Color(0, 0, 0, 0)); // transparent background

            // Fit aspect ratio inside targetW x targetH
            const float srcW = static_cast<float>(bmp.GetWidth());
            const float srcH = static_cast<float>(bmp.GetHeight());
            const float scale = (std::min)(static_cast<float>(targetW) / srcW, static_cast<float>(targetH) / srcH);
            const float drawW = srcW * scale;
            const float drawH = srcH * scale;
            const float drawX = (static_cast<float>(targetW) - drawW) / 2.0f;
            const float drawY = (static_cast<float>(targetH) - drawH) / 2.0f;

            g.DrawImage(&bmp, drawX, drawY, drawW, drawH);
            SelectObject(hdcMem, hOld);
            DeleteDC(hdcMem);
            return hDIB;
        }
    }

    // Fallback: draw icon centered
    HICON hIcon = nullptr;
    if (!m_item.executablePath.empty() && fs::exists(m_item.executablePath)) {
        ExtractIconExW(m_item.executablePath.c_str(), 0, &hIcon, nullptr, 1);
    }
    if (hIcon == nullptr) {
        hIcon = static_cast<HICON>(LoadImageW(
            g_hInstance, MAKEINTRESOURCEW(100), IMAGE_ICON,
            (std::min)(static_cast<int>(targetW), 128),
            (std::min)(static_cast<int>(targetH), 128),
            LR_DEFAULTCOLOR
        ));
    }

    if (hIcon != nullptr) {
        RECT rc{ 0, 0, static_cast<LONG>(targetW), static_cast<LONG>(targetH) };
        FillRect(hdcMem, &rc, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        const int iconSize = (std::min)(static_cast<int>(targetW), 128);
        const int ix = (static_cast<int>(targetW) - iconSize) / 2;
        const int iy = (static_cast<int>(targetH) - iconSize) / 2;
        DrawIconEx(hdcMem, ix, iy, hIcon, iconSize, iconSize, 0, nullptr, DI_NORMAL);
        DestroyIcon(hIcon);
    }

    SelectObject(hdcMem, hOld);
    DeleteDC(hdcMem);
    return hDIB;
}

// IThumbnailProvider
IFACEMETHODIMP GameItemUI::GetThumbnail(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha) {
    if (phbmp == nullptr || pdwAlpha == nullptr) return E_POINTER;
    *phbmp = nullptr;
    *pdwAlpha = WTSAT_UNKNOWN;

    // Use cx for width; cover art aspect ratio is ~2:3 so height is ~1.5 * cx
    const UINT h = (cx * 3) / 2;
    HBITMAP hbm = CreateThumbnailBitmap(cx, h);
    if (hbm == nullptr) return E_FAIL;

    *phbmp = hbm;
    *pdwAlpha = WTSAT_ARGB;
    return S_OK;
}

// IShellItemImageFactory
IFACEMETHODIMP GameItemUI::GetImage(SIZE size, SIIGBF /*flags*/, HBITMAP* phbm) {
    if (phbm == nullptr) return E_POINTER;
    *phbm = nullptr;

    const UINT w = static_cast<UINT>((std::max)(1L, size.cx));
    const UINT h = static_cast<UINT>((std::max)(1L, size.cy));
    HBITMAP hbm = CreateThumbnailBitmap(w, h);
    if (hbm == nullptr) return E_FAIL;

    *phbm = hbm;
    return S_OK;
}

// IPropertyStore
IFACEMETHODIMP GameItemUI::GetCount(DWORD* cProps) {
    if (cProps == nullptr) return E_POINTER;
    *cProps = ARRAYSIZE(s_supportedKeys);
    return S_OK;
}

IFACEMETHODIMP GameItemUI::GetAt(DWORD iProp, PROPERTYKEY* pkey) {
    if (pkey == nullptr) return E_POINTER;
    if (iProp >= ARRAYSIZE(s_supportedKeys)) return E_INVALIDARG;
    *pkey = s_supportedKeys[iProp];
    return S_OK;
}

IFACEMETHODIMP GameItemUI::GetValue(REFPROPERTYKEY key, PROPVARIANT* pv) {
    if (pv == nullptr) return E_POINTER;
    PropVariantInit(pv);

    if (IsEqualPropertyKey(key, PKEY_ItemNameDisplay)) {
        return InitPropVariantFromString(m_item.title.c_str(), pv);
    }
    if (IsEqualPropertyKey(key, PKEY_Company)) {
        return InitPropVariantFromString(m_item.publisher.empty() ? L"Unknown Publisher" : m_item.publisher.c_str(), pv);
    }
    if (IsEqualPropertyKey(key, PKEY_Author)) {
        return InitPropVariantFromString(m_item.developer.empty() ? L"Unknown Developer" : m_item.developer.c_str(), pv);
    }
    if (IsEqualPropertyKey(key, PKEY_Rating)) {
        return InitPropVariantFromString(m_item.rating.empty() ? L"Unrated" : m_item.rating.c_str(), pv);
    }
    if (IsEqualPropertyKey(key, PKEY_Size)) {
        return InitPropVariantFromUInt64(m_item.diskSizeBytes, pv);
    }
    if (IsEqualPropertyKey(key, PKEY_Media_Year)) {
        const ULONG year = ParseYear(m_item.releaseDate);
        if (year > 0) {
            return InitPropVariantFromUInt32(year, pv);
        }
    }
    if (IsEqualPropertyKey(key, PKEY_ItemTypeText)) {
        return InitPropVariantFromString((m_itemKind == 0) ? L"Game Providers" : L"Games", pv);
    }

    return S_OK; // empty variant
}

IFACEMETHODIMP GameItemUI::SetValue(REFPROPERTYKEY /*key*/, REFPROPVARIANT /*propvar*/) {
    return STG_E_ACCESSDENIED; // Read-only property store
}

IFACEMETHODIMP GameItemUI::Commit() {
    return S_OK;
}

// IExtractIconW
IFACEMETHODIMP GameItemUI::GetIconLocation(
    UINT /*uFlags*/, PWSTR pszIconFile, UINT cchMax, int* piIndex, UINT* pwFlags) {
    if (pszIconFile == nullptr || piIndex == nullptr || pwFlags == nullptr) {
        return E_POINTER;
    }

    if (!m_item.executablePath.empty() && fs::exists(m_item.executablePath)) {
        StringCchCopyW(pszIconFile, cchMax, m_item.executablePath.c_str());
        *piIndex = 0;
        *pwFlags = 0;
        return S_OK;
    }

    wchar_t modulePath[MAX_PATH];
    if (GetModuleFileNameW(g_hInstance, modulePath, MAX_PATH) != 0) {
        StringCchCopyW(pszIconFile, cchMax, modulePath);
        *piIndex = -100;
        *pwFlags = GIL_NOTFILENAME | GIL_PERINSTANCE;
        return S_OK;
    }

    return E_FAIL;
}

IFACEMETHODIMP GameItemUI::Extract(
    PCWSTR pszFile, UINT nIconIndex, HICON* phiconLarge, HICON* phiconSmall, UINT nIconSize) {
    if (pszFile != nullptr && fs::exists(pszFile)) {
        const UINT count = ExtractIconExW(pszFile, static_cast<int>(nIconIndex), phiconLarge, phiconSmall, 1);
        if (count > 0 && ((phiconLarge && *phiconLarge) || (phiconSmall && *phiconSmall))) {
            return S_OK;
        }
    }

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

// IQueryInfo
IFACEMETHODIMP GameItemUI::GetInfoTip(DWORD /*dwFlags*/, PWSTR* ppwszTip) {
    if (ppwszTip == nullptr) return E_POINTER;
    *ppwszTip = nullptr;

    wchar_t tip[512];
    if (m_itemKind == 0) {
        swprintf_s(tip, L"%s\nGame Provider\nCompany: %s",
            m_item.title.c_str(),
            m_item.publisher.empty() ? L"Storefront" : m_item.publisher.c_str()
        );
    } else {
        const std::wstring platformStr(PlatformToString(m_item.platform));
        swprintf_s(tip, L"%s\nPlatform: %s\nRating: %s\nDeveloper: %s",
            m_item.title.c_str(),
            platformStr.c_str(),
            m_item.rating.empty() ? L"Unrated" : m_item.rating.c_str(),
            m_item.developer.empty() ? L"Unknown" : m_item.developer.c_str()
        );
    }

    const size_t len = wcslen(tip) + 1;
    auto* pOut = static_cast<PWSTR>(CoTaskMemAlloc(len * sizeof(wchar_t)));
    if (pOut == nullptr) return E_OUTOFMEMORY;
    StringCchCopyW(pOut, len, tip);
    *ppwszTip = pOut;
    return S_OK;
}

IFACEMETHODIMP GameItemUI::GetInfoFlags(DWORD* pdwFlags) {
    if (pdwFlags == nullptr) return E_POINTER;
    *pdwFlags = 0;
    return S_OK;
}

} // namespace GameExplorer
