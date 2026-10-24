#include "ShellView.h"
#include "ShellFolder.h"
#include "GameExplorerCore/Guid.h"
#include "GameExplorerCore/Database.h"
#include "GameExplorerCore/Storage.h"
#include "GameExplorerCore/Settings.h"
#include "GameExplorerCore/StorefrontScanner.h"
#include "GameExplorerCore/PlatformLauncher.h"
#include "GameExplorerCore/Localization.h"

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.UI.Xaml.Input.h>
#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>

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

namespace wux = winrt::Windows::UI::Xaml;
namespace wuxc = winrt::Windows::UI::Xaml::Controls;
namespace wuxm = winrt::Windows::UI::Xaml::Media;
namespace wuxmi = winrt::Windows::UI::Xaml::Media::Imaging;
namespace wuxi = winrt::Windows::UI::Xaml::Input;
namespace wf = winrt::Windows::Foundation;

namespace GameExplorer {

namespace {

constexpr const wchar_t* VIEW_CLASS_NAME = L"GameExplorerXamlHostViewClass";

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

bool GetPngEncoderClsid(CLSID* pClsid) {
    UINT num = 0;
    UINT size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return false;

    std::vector<BYTE> buf(size);
    auto* pImageCodecInfo = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
    Gdiplus::GetImageEncoders(num, size, pImageCodecInfo);

    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pImageCodecInfo[j].MimeType, L"image/png") == 0) {
            *pClsid = pImageCodecInfo[j].Clsid;
            return true;
        }
    }
    return false;
}

// Extracts a high-res icon from an .exe or .ico and saves it as a .png in cache/art/ so XAML BitmapImage can load it
std::wstring EnsureExtractedIconPng(const std::wstring& cacheKey, const std::wstring& sourceExeOrIco) {
    if (sourceExeOrIco.empty() || !fs::exists(sourceExeOrIco)) return L"";

    const auto ext = fs::path(sourceExeOrIco).extension().wstring();
    if (_wcsicmp(ext.c_str(), L".png") == 0 ||
        _wcsicmp(ext.c_str(), L".jpg") == 0 ||
        _wcsicmp(ext.c_str(), L".jpeg") == 0 ||
        _wcsicmp(ext.c_str(), L".gif") == 0 ||
        _wcsicmp(ext.c_str(), L".webp") == 0 ||
        _wcsicmp(ext.c_str(), L".bmp") == 0) {
        return sourceExeOrIco;
    }

    Storage::EnsureDirectoriesCreated();
    const fs::path outDir = Storage::GetArtPath();
    std::error_code ec;
    fs::create_directories(outDir, ec);

    std::wstring safeKey = cacheKey;
    for (auto& ch : safeKey) {
        if (!iswalnum(ch) && ch != L'_' && ch != L'-') ch = L'_';
    }
    const fs::path pngPath = outDir / (L"icon_" + safeKey + L".png");
    if (fs::exists(pngPath, ec) && fs::file_size(pngPath, ec) > 0) {
        return pngPath.wstring();
    }

    HICON hIcon = nullptr;
    if (FAILED(SHDefExtractIconW(sourceExeOrIco.c_str(), 0, 0, &hIcon, nullptr, 128)) || hIcon == nullptr) {
        ExtractIconExW(sourceExeOrIco.c_str(), 0, &hIcon, nullptr, 1);
    }
    if (hIcon == nullptr) return L"";

    constexpr int iconDim = 128;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = iconDim;
    bi.bmiHeader.biHeight = iconDim;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HDC hdcScreen = GetDC(nullptr);
    HBITMAP hDib = CreateDIBSection(hdcScreen, &bi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    ReleaseDC(nullptr, hdcScreen);

    if (hDib != nullptr) {
        HDC hdcMem = CreateCompatibleDC(nullptr);
        HGDIOBJ hOld = SelectObject(hdcMem, hDib);
        DrawIconEx(hdcMem, 0, 0, hIcon, iconDim, iconDim, 0, nullptr, DI_NORMAL);
        SelectObject(hdcMem, hOld);
        DeleteDC(hdcMem);

        Gdiplus::Bitmap bmp(iconDim, iconDim, iconDim * 4, PixelFormat32bppARGB, static_cast<BYTE*>(pBits));
        bmp.RotateFlip(Gdiplus::RotateNoneFlipY);
        CLSID pngClsid{};
        if (GetPngEncoderClsid(&pngClsid)) {
            bmp.Save(pngPath.c_str(), &pngClsid, nullptr);
        }
        DeleteObject(hDib);
    }

    DestroyIcon(hIcon);
    return fs::exists(pngPath, ec) ? pngPath.wstring() : L"";
}

wuxm::SolidColorBrush MakeBrush(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
    return wuxm::SolidColorBrush(winrt::Windows::UI::ColorHelper::FromArgb(a, r, g, b));
}

wuxmi::BitmapImage LoadLocalBitmap(const std::wstring& filePath) {
    if (filePath.empty()) return nullptr;
    const bool isRemoteUri = (filePath.rfind(L"http://", 0) == 0 || filePath.rfind(L"https://", 0) == 0);
    if (!isRemoteUri) {
        std::error_code ec;
        if (!fs::exists(filePath, ec)) return nullptr;
    }
    try {
        wuxmi::BitmapImage bmp;
        bmp.AutoPlay(true);
        bmp.UriSource(wf::Uri(filePath));
        return bmp;
    } catch (...) {
        return nullptr;
    }
}

} // namespace

struct ShellView::XamlState {
    wux::Hosting::WindowsXamlManager xamlManager{ nullptr };
    wux::Hosting::DesktopWindowXamlSource xamlSource{ nullptr };
    wuxc::Grid rootGrid{ nullptr };
    wuxc::Border sidepaneHost{ nullptr };
};

ShellView::ShellView(ShellFolder* pFolder)
    : m_pFolder(pFolder), m_xaml(std::make_unique<XamlState>()) {
    ++g_dllRefCount;
    if (m_pFolder != nullptr) {
        m_pFolder->AddRef();
    }
    EnsureGdiplusInitialized();
    DarkMode::Initialize();
    m_isDarkMode = DarkMode::IsSystemDarkMode();
    m_layoutMode = SettingsManager::GetLayoutMode();
}

ShellView::~ShellView() {
    if (m_pFolder != nullptr) {
        m_pFolder->Release();
        m_pFolder = nullptr;
    }
    --g_dllRefCount;
}

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

IFACEMETHODIMP ShellView::GetWindow(HWND* phwnd) {
    if (phwnd == nullptr) return E_POINTER;
    *phwnd = m_hwndView;
    return S_OK;
}

IFACEMETHODIMP ShellView::ContextSensitiveHelp(BOOL /*fEnterMode*/) {
    return E_NOTIMPL;
}

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
    if (uState != SVUIA_DEACTIVATE && oldState == SVUIA_DEACTIVATE && m_pShellBrowser != nullptr) {
        m_pShellBrowser->OnViewWindowActive(this);
    }
    return S_OK;
}

void ShellView::RefreshData() {
    m_isDarkMode = DarkMode::IsSystemDarkMode();
    m_layoutMode = SettingsManager::GetLayoutMode();

    m_providers.clear();
    auto addProviderIfInstalled = [&](PlatformType plat, const wchar_t* name, const wchar_t* company, const std::wstring& exe, const std::wstring& iconOverride = L"") {
        if (exe.empty() || !fs::exists(exe)) return;
        ProviderViewModel p{};
        p.platform = plat;
        p.name = name;
        p.company = company;
        p.executablePath = exe;
        p.iconPngPath = EnsureExtractedIconPng(std::wstring(L"prov_") + name, iconOverride.empty() ? exe : iconOverride);
        m_providers.push_back(std::move(p));
    };

    addProviderIfInstalled(PlatformType::Steam,       L"Steam",           L"Valve Corporation",      PlatformLauncher::GetSteamExecutablePath());
    addProviderIfInstalled(PlatformType::Epic,        L"Epic Games",      L"Epic Games, Inc.",       PlatformLauncher::GetEpicExecutablePath());
    addProviderIfInstalled(PlatformType::GOG,         L"GOG GALAXY",      L"CD PROJEKT / GOG",       PlatformLauncher::GetGOGGalaxyExecutablePath());
    addProviderIfInstalled(PlatformType::Xbox,        L"Xbox",            L"Microsoft Corporation",  PlatformLauncher::GetXboxExecutablePath(), PlatformLauncher::GetXboxIconPath());
    addProviderIfInstalled(PlatformType::BattleNet,   L"Battle.net",      L"Blizzard Entertainment", PlatformLauncher::GetBattleNetExecutablePath());
    addProviderIfInstalled(PlatformType::Ubisoft,     L"Ubisoft Connect", L"Ubisoft",                PlatformLauncher::GetUbisoftExecutablePath());
    addProviderIfInstalled(PlatformType::EAApp,       L"EA App",          L"Electronic Arts",        PlatformLauncher::GetEAAppExecutablePath());
    addProviderIfInstalled(PlatformType::Itch,        L"itch.io",         L"itch corp.",             PlatformLauncher::GetItchExecutablePath());
    addProviderIfInstalled(PlatformType::Amazon,      L"Amazon Games",    L"Amazon.com, Inc.",       PlatformLauncher::GetAmazonExecutablePath());
    addProviderIfInstalled(PlatformType::LegacyGames, L"Legacy Games",    L"Legacy Games, Inc.",     PlatformLauncher::GetLegacyGamesExecutablePath());
    addProviderIfInstalled(PlatformType::Jagex,       L"Jagex Launcher",  L"Jagex Ltd",              PlatformLauncher::GetJagexExecutablePath());
    addProviderIfInstalled(PlatformType::XIVLauncher, L"XIVLauncher",     L"Goatcorp / FFXIV",       PlatformLauncher::GetXIVLauncherExecutablePath());
    addProviderIfInstalled(PlatformType::GuildWars2,  L"Guild Wars 2",    L"ArenaNet",               PlatformLauncher::GetGuildWars2ExecutablePath());
    addProviderIfInstalled(PlatformType::Rockstar,    L"Rockstar Games",  L"Rockstar Games",         PlatformLauncher::GetRockstarExecutablePath());
    addProviderIfInstalled(PlatformType::Riot,        L"Riot Client",     L"Riot Games",             PlatformLauncher::GetRiotExecutablePath());
    addProviderIfInstalled(PlatformType::HoYoPlay,    L"HoYoPlay",        L"COGNOSPHERE",            PlatformLauncher::GetHoYoPlayExecutablePath());
    addProviderIfInstalled(PlatformType::Paradox,     L"Paradox Launcher",L"Paradox Interactive",    PlatformLauncher::GetParadoxExecutablePath());
    addProviderIfInstalled(PlatformType::ESDE,        L"ES-DE",           L"EmulationStation DE",    PlatformLauncher::GetESDEExecutablePath());

    m_games = StorefrontScanner::ScanAllInstalledGames();
    for (auto& g : m_games) {
        if (g.iconPath.empty() && !g.executablePath.empty()) {
            g.iconPath = EnsureExtractedIconPng(g.appId, g.executablePath);
        } else if (!g.iconPath.empty()) {
            g.iconPath = EnsureExtractedIconPng(g.appId, g.iconPath);
        }
    }

    if (m_selectedGameIndex >= static_cast<int>(m_games.size())) {
        m_selectedGameIndex = -1;
    }
    if (m_selectedProviderIndex >= static_cast<int>(m_providers.size())) {
        m_selectedProviderIndex = -1;
    }
}

void ShellView::LaunchCompanionExe(const wchar_t* exeName, const std::wstring& args) {
    wchar_t dllPath[MAX_PATH] = { 0 };
    if (GetModuleFileNameW(g_hInstance, dllPath, MAX_PATH) > 0) {
        const fs::path dir = fs::path(dllPath).parent_path();
        const fs::path targetExe = dir / exeName;
        if (fs::exists(targetExe)) {
            ShellExecuteW(m_hwndView, L"open", targetExe.c_str(), args.empty() ? nullptr : args.c_str(), dir.c_str(), SW_SHOWNORMAL);
        }
    }
}

void ShellView::SwitchLayoutMode(LayoutMode mode) {
    m_layoutMode = mode;
    SettingsManager::SetLayoutMode(mode);
    BuildXamlViewport();
}

void ShellView::RebuildSidepane() {
    if (!m_xaml || !m_xaml->sidepaneHost) return;

    const auto bgDrawer    = m_isDarkMode ? MakeBrush(255, 28, 28, 28)   : MakeBrush(255, 246, 246, 246);
    const auto bgCard      = m_isDarkMode ? MakeBrush(255, 38, 38, 38)   : MakeBrush(255, 255, 255, 255);
    const auto borderBrush = m_isDarkMode ? MakeBrush(255, 55, 55, 55)   : MakeBrush(255, 224, 224, 224);
    const auto textPrimary = m_isDarkMode ? MakeBrush(255, 255, 255, 255): MakeBrush(255, 24, 24, 24);
    const auto textSec     = m_isDarkMode ? MakeBrush(255, 175, 175, 175): MakeBrush(255, 96, 96, 96);
    const auto accentBrush = MakeBrush(255, 124, 58, 237);
    const auto whiteBrush  = MakeBrush(255, 255, 255, 255);

    m_xaml->sidepaneHost.Background(bgDrawer);
    m_xaml->sidepaneHost.BorderBrush(borderBrush);
    m_xaml->sidepaneHost.BorderThickness(wux::ThicknessHelper::FromLengths(1, 0, 0, 0));

    wuxc::ScrollViewer scroll;
    scroll.VerticalScrollBarVisibility(wuxc::ScrollBarVisibility::Auto);

    wuxc::StackPanel stack;
    stack.Padding(wux::ThicknessHelper::FromUniformLength(20));
    stack.Spacing(14);

    // Case 1: A Game is selected
    if (m_selectedGameIndex >= 0 && m_selectedGameIndex < static_cast<int>(m_games.size())) {
        const auto& game = m_games[static_cast<size_t>(m_selectedGameIndex)];

        // Artwork Preview Card
        wuxc::Border artCard;
        artCard.CornerRadius(wux::CornerRadiusHelper::FromUniformRadius(8));
        artCard.Background(bgCard);
        artCard.BorderBrush(borderBrush);
        artCard.BorderThickness(wux::ThicknessHelper::FromUniformLength(1));
        artCard.Height(240);

        std::wstring previewPath = !game.coverArtPath.empty() ? game.coverArtPath : (!game.heroArtPath.empty() ? game.heroArtPath : game.iconPath);
        if (auto bmp = LoadLocalBitmap(previewPath)) {
            wuxc::Image img;
            img.Source(bmp);
            if (!game.coverArtPath.empty() || !game.heroArtPath.empty()) {
                img.Stretch(wuxm::Stretch::Uniform);
            } else {
                img.Width(96);
                img.Height(96);
                img.HorizontalAlignment(wux::HorizontalAlignment::Center);
                img.VerticalAlignment(wux::VerticalAlignment::Center);
            }
            artCard.Child(img);
        }
        stack.Children().Append(artCard);

        // Title
        wuxc::TextBlock titleTb;
        titleTb.Text(game.title);
        titleTb.FontSize(20);
        titleTb.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        titleTb.Foreground(textPrimary);
        titleTb.TextWrapping(wux::TextWrapping::Wrap);
        stack.Children().Append(titleTb);

        // Platform Badge
        wuxc::Border pill;
        pill.Background(MakeBrush(45, 124, 58, 237));
        pill.BorderBrush(accentBrush);
        pill.BorderThickness(wux::ThicknessHelper::FromUniformLength(1));
        pill.CornerRadius(wux::CornerRadiusHelper::FromUniformRadius(4));
        pill.Padding(wux::ThicknessHelper::FromLengths(8, 3, 8, 3));
        pill.HorizontalAlignment(wux::HorizontalAlignment::Left);

        wuxc::TextBlock pillTb;
        pillTb.Text(std::wstring(PlatformToString(game.platform)));
        pillTb.FontSize(12);
        pillTb.Foreground(textPrimary);
        pill.Child(pillTb);
        stack.Children().Append(pill);

        // Primary Play Button
        wuxc::Button playBtn;
        playBtn.Content(winrt::box_value(L"\u25B6  Play Game"));
        playBtn.HorizontalAlignment(wux::HorizontalAlignment::Stretch);
        playBtn.Background(accentBrush);
        playBtn.Foreground(whiteBrush);
        playBtn.CornerRadius(wux::CornerRadiusHelper::FromUniformRadius(6));
        playBtn.Padding(wux::ThicknessHelper::FromLengths(12, 9, 12, 9));
        const int idx = m_selectedGameIndex;
        playBtn.Click([this, idx](wf::IInspectable const&, wux::RoutedEventArgs const&) {
            if (idx >= 0 && idx < static_cast<int>(m_games.size())) {
                PlatformLauncher::Launch(m_hwndView, m_games[static_cast<size_t>(idx)]);
            }
        });
        stack.Children().Append(playBtn);

        // Edit in Game Explorer Manager Button
        wuxc::Button editBtn;
        editBtn.Content(winrt::box_value(L"\u270E  Edit in Game Explorer Manager"));
        editBtn.HorizontalAlignment(wux::HorizontalAlignment::Stretch);
        editBtn.Background(bgCard);
        editBtn.Foreground(textPrimary);
        editBtn.BorderBrush(borderBrush);
        editBtn.BorderThickness(wux::ThicknessHelper::FromUniformLength(1));
        editBtn.CornerRadius(wux::CornerRadiusHelper::FromUniformRadius(6));
        editBtn.Padding(wux::ThicknessHelper::FromLengths(12, 8, 12, 8));
        const std::wstring appId = game.appId;
        editBtn.Click([this, appId](wf::IInspectable const&, wux::RoutedEventArgs const&) {
            LaunchCompanionExe(L"GameExplorerManager.exe", appId);
        });
        stack.Children().Append(editBtn);

        // Open Game Folder Button
        if (!game.workingDirectory.empty() && fs::exists(game.workingDirectory)) {
            wuxc::Button dirBtn;
            dirBtn.Content(winrt::box_value(L"\U0001F4C2  Open Install Directory"));
            dirBtn.HorizontalAlignment(wux::HorizontalAlignment::Stretch);
            dirBtn.Background(bgCard);
            dirBtn.Foreground(textPrimary);
            dirBtn.BorderBrush(borderBrush);
            dirBtn.BorderThickness(wux::ThicknessHelper::FromUniformLength(1));
            dirBtn.CornerRadius(wux::CornerRadiusHelper::FromUniformRadius(6));
            dirBtn.Padding(wux::ThicknessHelper::FromLengths(12, 8, 12, 8));
            const std::wstring workDir = game.workingDirectory;
            dirBtn.Click([workDir](wf::IInspectable const&, wux::RoutedEventArgs const&) {
                ShellExecuteW(nullptr, L"open", workDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            });
            stack.Children().Append(dirBtn);
        }

        // Metadata Specs
        auto addMetaRow = [&](const wchar_t* label, const std::wstring& val) {
            if (val.empty()) return;
            wuxc::StackPanel row;
            row.Spacing(2);
            wuxc::TextBlock lbl;
            lbl.Text(label);
            lbl.FontSize(11);
            lbl.Foreground(textSec);
            wuxc::TextBlock vTb;
            vTb.Text(val);
            vTb.FontSize(13);
            vTb.Foreground(textPrimary);
            vTb.TextWrapping(wux::TextWrapping::Wrap);
            row.Children().Append(lbl);
            row.Children().Append(vTb);
            stack.Children().Append(row);
        };

        const auto& loc = Localization::GetStrings();
        addMetaRow(loc.labelDeveloper, game.developer.empty() ? loc.unknownDeveloper : game.developer);
        addMetaRow(loc.labelPublisher, game.publisher.empty() ? std::wstring(PlatformToString(game.platform)) : game.publisher);
        addMetaRow(loc.labelReleaseDate, game.releaseDate);
        addMetaRow(loc.labelGenres, game.genres);
        addMetaRow(loc.labelRating, game.rating.empty() ? loc.unrated : game.rating);
        if (game.playtimeMinutes > 0) {
            const uint32_t hrs = game.playtimeMinutes / 60;
            const uint32_t mins = game.playtimeMinutes % 60;
            const std::wstring playStr = hrs > 0
                ? (std::to_wstring(hrs) + L"h " + std::to_wstring(mins) + L"m")
                : (std::to_wstring(mins) + L"m");
            addMetaRow(loc.labelPlayTime, playStr);
        }
        addMetaRow(L"SIZE ON DISK", FormatDiskSize(game.diskSizeBytes));
        addMetaRow(loc.labelSynopsis, game.synopsis);
    }
    // Case 2: A Game Provider is selected
    else if (m_selectedProviderIndex >= 0 && m_selectedProviderIndex < static_cast<int>(m_providers.size())) {
        const auto& prov = m_providers[static_cast<size_t>(m_selectedProviderIndex)];

        wuxc::Border artCard;
        artCard.CornerRadius(wux::CornerRadiusHelper::FromUniformRadius(8));
        artCard.Background(bgCard);
        artCard.BorderBrush(borderBrush);
        artCard.BorderThickness(wux::ThicknessHelper::FromUniformLength(1));
        artCard.Height(150);

        if (auto bmp = LoadLocalBitmap(prov.iconPngPath)) {
            wuxc::Image img;
            img.Source(bmp);
            img.Width(80);
            img.Height(80);
            img.HorizontalAlignment(wux::HorizontalAlignment::Center);
            img.VerticalAlignment(wux::VerticalAlignment::Center);
            artCard.Child(img);
        }
        stack.Children().Append(artCard);

        wuxc::TextBlock titleTb;
        titleTb.Text(prov.name);
        titleTb.FontSize(20);
        titleTb.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        titleTb.Foreground(textPrimary);
        stack.Children().Append(titleTb);

        wuxc::TextBlock compTb;
        compTb.Text(prov.company);
        compTb.FontSize(13);
        compTb.Foreground(textSec);
        stack.Children().Append(compTb);

        wuxc::Button openBtn;
        openBtn.Content(winrt::box_value(L"\u25B6  Open " + prov.name));
        openBtn.HorizontalAlignment(wux::HorizontalAlignment::Stretch);
        openBtn.Background(accentBrush);
        openBtn.Foreground(whiteBrush);
        openBtn.CornerRadius(wux::CornerRadiusHelper::FromUniformRadius(6));
        openBtn.Padding(wux::ThicknessHelper::FromLengths(12, 9, 12, 9));
        const std::wstring exePath = prov.executablePath;
        const PlatformType provPlat = prov.platform;
        openBtn.Click([this, exePath, provPlat](wf::IInspectable const&, wux::RoutedEventArgs const&) {
            if (provPlat == PlatformType::Xbox) {
                ShellExecuteW(m_hwndView, L"open", L"shell:AppsFolder\\Microsoft.GamingApp_8wekyb3d8bbwe!Microsoft.Xbox.App", nullptr, nullptr, SW_SHOWNORMAL);
            } else if (!exePath.empty() && fs::exists(exePath)) {
                ShellExecuteW(m_hwndView, L"open", exePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
        });
        stack.Children().Append(openBtn);
    }
    // Case 3: Library Overview (Default)
    else {
        wuxc::Border heroBanner;
        heroBanner.CornerRadius(wux::CornerRadiusHelper::FromUniformRadius(10));
        heroBanner.Background(m_isDarkMode ? MakeBrush(255, 38, 28, 56) : MakeBrush(255, 240, 232, 255));
        heroBanner.BorderBrush(accentBrush);
        heroBanner.BorderThickness(wux::ThicknessHelper::FromUniformLength(1));
        heroBanner.Padding(wux::ThicknessHelper::FromUniformLength(18));

        wuxc::StackPanel heroStack;
        heroStack.Spacing(8);
        heroStack.HorizontalAlignment(wux::HorizontalAlignment::Center);

        wchar_t selfDll[MAX_PATH] = { 0 };
        if (GetModuleFileNameW(g_hInstance, selfDll, MAX_PATH) > 0) {
            const std::wstring appIconPng = EnsureExtractedIconPng(L"app_library_icon_v2", selfDll);
            if (auto bmp = LoadLocalBitmap(appIconPng)) {
                wuxc::Image heroImg;
                heroImg.Source(bmp);
                heroImg.Width(72);
                heroImg.Height(72);
                heroImg.HorizontalAlignment(wux::HorizontalAlignment::Center);
                heroStack.Children().Append(heroImg);
            }
        }

        wuxc::TextBlock hTitle;
        hTitle.Text(L"WINDOWS GAME EXPLORER");
        hTitle.FontSize(14);
        hTitle.FontWeight(winrt::Windows::UI::Text::FontWeights::Bold());
        hTitle.Foreground(textPrimary);
        hTitle.HorizontalAlignment(wux::HorizontalAlignment::Center);

        wuxc::TextBlock hSub;
        hSub.Text(L"WinUI XAML Game Library & Storefront Hub");
        hSub.FontSize(12);
        hSub.Foreground(textSec);
        hSub.HorizontalAlignment(wux::HorizontalAlignment::Center);

        heroStack.Children().Append(hTitle);
        heroStack.Children().Append(hSub);
        heroBanner.Child(heroStack);
        stack.Children().Append(heroBanner);

        uint64_t totalBytes = 0;
        for (const auto& g : m_games) totalBytes += g.diskSizeBytes;

        wuxc::TextBlock summaryHdr;
        summaryHdr.Text(L"Library Overview");
        summaryHdr.FontSize(18);
        summaryHdr.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        summaryHdr.Foreground(textPrimary);
        stack.Children().Append(summaryHdr);

        wuxc::TextBlock statsTb;
        statsTb.Text(
            L"\u2022 Installed Providers: " + std::to_wstring(m_providers.size()) + L"\n" +
            L"\u2022 Installed Games: " + std::to_wstring(m_games.size()) + L"\n" +
            L"\u2022 Total Disk Footprint: " + (totalBytes > 0 ? FormatDiskSize(totalBytes) : L"Calculating...")
        );
        statsTb.FontSize(13);
        statsTb.Foreground(textSec);
        statsTb.LineHeight(22);
        stack.Children().Append(statsTb);

        wuxc::Button gemBtn;
        gemBtn.Content(winrt::box_value(L"\u2795  Open Game Explorer Manager"));
        gemBtn.HorizontalAlignment(wux::HorizontalAlignment::Stretch);
        gemBtn.Background(accentBrush);
        gemBtn.Foreground(whiteBrush);
        gemBtn.CornerRadius(wux::CornerRadiusHelper::FromUniformRadius(6));
        gemBtn.Padding(wux::ThicknessHelper::FromLengths(12, 9, 12, 9));
        gemBtn.Click([this](wf::IInspectable const&, wux::RoutedEventArgs const&) {
            LaunchCompanionExe(L"GameExplorerManager.exe");
        });
        stack.Children().Append(gemBtn);

        wuxc::Button cfgBtn;
        cfgBtn.Content(winrt::box_value(L"\u2699  Integration Settings"));
        cfgBtn.HorizontalAlignment(wux::HorizontalAlignment::Stretch);
        cfgBtn.Background(bgCard);
        cfgBtn.Foreground(textPrimary);
        cfgBtn.BorderBrush(borderBrush);
        cfgBtn.BorderThickness(wux::ThicknessHelper::FromUniformLength(1));
        cfgBtn.CornerRadius(wux::CornerRadiusHelper::FromUniformRadius(6));
        cfgBtn.Padding(wux::ThicknessHelper::FromLengths(12, 8, 12, 8));
        cfgBtn.Click([this](wf::IInspectable const&, wux::RoutedEventArgs const&) {
            LaunchCompanionExe(L"GameExplorerConfig.exe");
        });
        stack.Children().Append(cfgBtn);
    }

    scroll.Content(stack);
    m_xaml->sidepaneHost.Child(scroll);
}

void ShellView::BuildXamlViewport() {
    if (!m_xaml || !m_xaml->rootGrid) return;

    m_xaml->rootGrid.Children().Clear();
    m_xaml->rootGrid.RowDefinitions().Clear();
    m_xaml->rootGrid.ColumnDefinitions().Clear();

    const auto bgCanvas    = m_isDarkMode ? MakeBrush(255, 20, 20, 20)   : MakeBrush(255, 250, 250, 250);
    const auto bgToolbar   = m_isDarkMode ? MakeBrush(255, 28, 28, 28)   : MakeBrush(255, 242, 242, 242);
    const auto bgCard      = m_isDarkMode ? MakeBrush(255, 34, 34, 34)   : MakeBrush(255, 255, 255, 255);
    const auto bgCardSel   = m_isDarkMode ? MakeBrush(255, 48, 36, 72)   : MakeBrush(255, 236, 226, 255);
    const auto borderBrush = m_isDarkMode ? MakeBrush(255, 52, 52, 52)   : MakeBrush(255, 220, 220, 220);
    const auto accentBrush = MakeBrush(255, 124, 58, 237);
    const auto textPrimary = m_isDarkMode ? MakeBrush(255, 255, 255, 255): MakeBrush(255, 20, 20, 20);
    const auto textSec     = m_isDarkMode ? MakeBrush(255, 170, 170, 170): MakeBrush(255, 100, 100, 100);
    const auto whiteBrush  = MakeBrush(255, 255, 255, 255);

    m_xaml->rootGrid.RequestedTheme(m_isDarkMode ? wux::ElementTheme::Dark : wux::ElementTheme::Light);
    m_xaml->rootGrid.Background(bgCanvas);

    // Row 0: Top Command & View Switcher Bar (52px)
    // Row 1: Main Split Content (*)
    wuxc::RowDefinition rowTop;
    rowTop.Height(wux::GridLengthHelper::FromPixels(52));
    wuxc::RowDefinition rowMain;
    rowMain.Height(wux::GridLengthHelper::FromValueAndType(1.0, wux::GridUnitType::Star));
    m_xaml->rootGrid.RowDefinitions().Append(rowTop);
    m_xaml->rootGrid.RowDefinitions().Append(rowMain);

    // =========================================================================
    // TOP COMMAND BAR (Layout Mode Switcher + Game Explorer Manager + Refresh)
    // =========================================================================
    wuxc::Border topBar;
    topBar.Background(bgToolbar);
    topBar.BorderBrush(borderBrush);
    topBar.BorderThickness(wux::ThicknessHelper::FromLengths(0, 0, 0, 1));
    topBar.Padding(wux::ThicknessHelper::FromLengths(20, 0, 20, 0));

    wuxc::Grid topGrid;
    wuxc::ColumnDefinition colLeft;
    colLeft.Width(wux::GridLengthHelper::FromValueAndType(1.0, wux::GridUnitType::Star));
    wuxc::ColumnDefinition colRight;
    colRight.Width(wux::GridLengthHelper::Auto());
    topGrid.ColumnDefinitions().Append(colLeft);
    topGrid.ColumnDefinitions().Append(colRight);

    // Left: Library Header + Layout Mode Pill Buttons
    wuxc::StackPanel leftToolbar;
    leftToolbar.Orientation(wuxc::Orientation::Horizontal);
    leftToolbar.VerticalAlignment(wux::VerticalAlignment::Center);
    leftToolbar.Spacing(8);

    auto makeModeBtn = [&](const wchar_t* label, LayoutMode mode) {
        const bool active = (m_layoutMode == mode);
        wuxc::Button btn;
        btn.Content(winrt::box_value(label));
        btn.Background(active ? accentBrush : bgCard);
        btn.Foreground(active ? whiteBrush : textPrimary);
        btn.BorderBrush(active ? accentBrush : borderBrush);
        btn.BorderThickness(wux::ThicknessHelper::FromUniformLength(1));
        btn.CornerRadius(wux::CornerRadiusHelper::FromUniformRadius(6));
        btn.Padding(wux::ThicknessHelper::FromLengths(12, 5, 12, 5));
        btn.Click([this, mode](wf::IInspectable const&, wux::RoutedEventArgs const&) {
            SwitchLayoutMode(mode);
        });
        return btn;
    };

    const auto& loc = Localization::GetStrings();
    leftToolbar.Children().Append(makeModeBtn(loc.viewTiles, LayoutMode::CoverArt));
    leftToolbar.Children().Append(makeModeBtn(L"Hero Banner (~16:9)", LayoutMode::HeroBanner));
    leftToolbar.Children().Append(makeModeBtn(loc.viewDetails, LayoutMode::DetailsView));
    wuxc::Grid::SetColumn(leftToolbar, 0);
    topGrid.Children().Append(leftToolbar);

    // Right: Companion App & Refresh Buttons
    wuxc::StackPanel rightToolbar;
    rightToolbar.Orientation(wuxc::Orientation::Horizontal);
    rightToolbar.VerticalAlignment(wux::VerticalAlignment::Center);
    rightToolbar.Spacing(8);

    wuxc::Button btnManager;
    btnManager.Content(winrt::box_value(std::wstring(L"\u2795 ") + loc.manageMetadataBtn));
    btnManager.Background(bgCard);
    btnManager.Foreground(textPrimary);
    btnManager.BorderBrush(borderBrush);
    btnManager.BorderThickness(wux::ThicknessHelper::FromUniformLength(1));
    btnManager.CornerRadius(wux::CornerRadiusHelper::FromUniformRadius(6));
    btnManager.Padding(wux::ThicknessHelper::FromLengths(12, 5, 12, 5));
    btnManager.Click([this](wf::IInspectable const&, wux::RoutedEventArgs const&) {
        LaunchCompanionExe(L"GameExplorerManager.exe");
    });
    rightToolbar.Children().Append(btnManager);

    wuxc::Button btnSettings;
    btnSettings.Content(winrt::box_value(std::wstring(L"\u2699 ") + loc.settingsBtn));
    btnSettings.Background(bgCard);
    btnSettings.Foreground(textPrimary);
    btnSettings.BorderBrush(borderBrush);
    btnSettings.BorderThickness(wux::ThicknessHelper::FromUniformLength(1));
    btnSettings.CornerRadius(wux::CornerRadiusHelper::FromUniformRadius(6));
    btnSettings.Padding(wux::ThicknessHelper::FromLengths(12, 5, 12, 5));
    btnSettings.Click([this](wf::IInspectable const&, wux::RoutedEventArgs const&) {
        LaunchCompanionExe(L"GameExplorerConfig.exe");
    });
    rightToolbar.Children().Append(btnSettings);

    wuxc::Button btnRefresh;
    btnRefresh.Content(winrt::box_value(std::wstring(L"\u21BB ") + loc.refreshBtn));
    btnRefresh.Background(bgCard);
    btnRefresh.Foreground(textPrimary);
    btnRefresh.BorderBrush(borderBrush);
    btnRefresh.BorderThickness(wux::ThicknessHelper::FromUniformLength(1));
    btnRefresh.CornerRadius(wux::CornerRadiusHelper::FromUniformRadius(6));
    btnRefresh.Padding(wux::ThicknessHelper::FromLengths(12, 5, 12, 5));
    btnRefresh.Click([this](wf::IInspectable const&, wux::RoutedEventArgs const&) {
        Refresh();
    });
    rightToolbar.Children().Append(btnRefresh);

    wuxc::Grid::SetColumn(rightToolbar, 1);
    topGrid.Children().Append(rightToolbar);

    topBar.Child(topGrid);
    wuxc::Grid::SetRow(topBar, 0);
    m_xaml->rootGrid.Children().Append(topBar);

    // =========================================================================
    // MAIN BODY GRID (Column 0: Scrollable Categorized Viewport, Column 1: Sidepane)
    // =========================================================================
    wuxc::Grid bodyGrid;
    wuxc::ColumnDefinition colCanvas;
    colCanvas.Width(wux::GridLengthHelper::FromValueAndType(1.0, wux::GridUnitType::Star));
    wuxc::ColumnDefinition colDrawer;
    colDrawer.Width(wux::GridLengthHelper::FromPixels(340));
    bodyGrid.ColumnDefinitions().Append(colCanvas);
    bodyGrid.ColumnDefinitions().Append(colDrawer);

    wuxc::ScrollViewer mainScroll;
    mainScroll.VerticalScrollBarVisibility(wuxc::ScrollBarVisibility::Auto);

    wuxc::StackPanel mainStack;
    mainStack.Padding(wux::ThicknessHelper::FromLengths(24, 18, 24, 32));
    mainStack.Spacing(18);

    // -------------------------------------------------------------------------
    // SECTION 1: ▾ Game Providers (N)
    // -------------------------------------------------------------------------
    wuxc::Button provHeaderBtn;
    std::wstring provHeaderTitle = (m_providersExpanded ? L"\u25BE  " : L"\u25B8  ") +
                                   std::wstring(loc.providersHeader) + L" (" +
                                   std::to_wstring(m_providers.size()) + L")";
    provHeaderBtn.Content(winrt::box_value(provHeaderTitle));
    provHeaderBtn.Background(MakeBrush(0, 0, 0, 0));
    provHeaderBtn.BorderThickness(wux::ThicknessHelper::FromUniformLength(0));
    provHeaderBtn.Foreground(textPrimary);
    provHeaderBtn.FontSize(16);
    provHeaderBtn.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    provHeaderBtn.Padding(wux::ThicknessHelper::FromLengths(4, 4, 12, 4));
    provHeaderBtn.Click([this](wf::IInspectable const&, wux::RoutedEventArgs const&) {
        m_providersExpanded = !m_providersExpanded;
        BuildXamlViewport();
    });
    mainStack.Children().Append(provHeaderBtn);

    if (m_providersExpanded && !m_providers.empty()) {
        wuxc::VariableSizedWrapGrid provWrap;
        provWrap.Orientation(wuxc::Orientation::Horizontal);
        provWrap.ItemWidth(244);
        provWrap.ItemHeight(78);

        for (size_t i = 0; i < m_providers.size(); ++i) {
            const auto& prov = m_providers[i];
            const bool isSel = (m_selectedProviderIndex == static_cast<int>(i));

            wuxc::Border card;
            card.Width(232);
            card.Height(66);
            card.Margin(wux::ThicknessHelper::FromLengths(0, 0, 12, 12));
            card.CornerRadius(wux::CornerRadiusHelper::FromUniformRadius(8));
            card.Background(isSel ? bgCardSel : bgCard);
            card.BorderBrush(isSel ? accentBrush : borderBrush);
            card.BorderThickness(wux::ThicknessHelper::FromUniformLength(isSel ? 2 : 1));
            card.Padding(wux::ThicknessHelper::FromLengths(12, 8, 12, 8));

            wuxc::StackPanel row;
            row.Orientation(wuxc::Orientation::Horizontal);
            row.Spacing(12);
            row.VerticalAlignment(wux::VerticalAlignment::Center);

            if (auto bmp = LoadLocalBitmap(prov.iconPngPath)) {
                wuxc::Image iconImg;
                iconImg.Source(bmp);
                iconImg.Width(42);
                iconImg.Height(42);
                row.Children().Append(iconImg);
            }

            wuxc::StackPanel txtStack;
            txtStack.VerticalAlignment(wux::VerticalAlignment::Center);

            wuxc::TextBlock nameTb;
            nameTb.Text(prov.name);
            nameTb.FontSize(14);
            nameTb.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
            nameTb.Foreground(textPrimary);

            wuxc::TextBlock compTb;
            compTb.Text(prov.company);
            compTb.FontSize(12);
            compTb.Foreground(textSec);

            txtStack.Children().Append(nameTb);
            txtStack.Children().Append(compTb);
            row.Children().Append(txtStack);
            card.Child(row);

            const int provIdx = static_cast<int>(i);
            const std::wstring exePath = prov.executablePath;
            const PlatformType provPlat = prov.platform;
            card.Tapped([this, provIdx](wf::IInspectable const&, wuxi::TappedRoutedEventArgs const&) {
                m_selectedProviderIndex = provIdx;
                m_selectedGameIndex = -1;
                BuildXamlViewport();
            });
            card.DoubleTapped([this, exePath, provPlat](wf::IInspectable const&, wuxi::DoubleTappedRoutedEventArgs const&) {
                if (provPlat == PlatformType::Xbox) {
                    ShellExecuteW(m_hwndView, L"open", L"shell:AppsFolder\\Microsoft.GamingApp_8wekyb3d8bbwe!Microsoft.Xbox.App", nullptr, nullptr, SW_SHOWNORMAL);
                } else if (!exePath.empty() && fs::exists(exePath)) {
                    ShellExecuteW(m_hwndView, L"open", exePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                }
            });

            provWrap.Children().Append(card);
        }
        mainStack.Children().Append(provWrap);
    }

    // Divider line between Game Providers and Games
    wuxc::Border sectionDiv;
    sectionDiv.Height(1);
    sectionDiv.Background(borderBrush);
    mainStack.Children().Append(sectionDiv);

    // -------------------------------------------------------------------------
    // SECTION 2: ▾ Games (N)
    // -------------------------------------------------------------------------
    wuxc::Button gamesHeaderBtn;
    std::wstring gamesHeaderTitle = (m_gamesExpanded ? L"\u25BE  " : L"\u25B8  ") +
                                    std::wstring(loc.gamesLibraryHeader) + L" (" +
                                    std::to_wstring(m_games.size()) + L")";
    gamesHeaderBtn.Content(winrt::box_value(gamesHeaderTitle));
    gamesHeaderBtn.Background(MakeBrush(0, 0, 0, 0));
    gamesHeaderBtn.BorderThickness(wux::ThicknessHelper::FromUniformLength(0));
    gamesHeaderBtn.Foreground(textPrimary);
    gamesHeaderBtn.FontSize(16);
    gamesHeaderBtn.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    gamesHeaderBtn.Padding(wux::ThicknessHelper::FromLengths(4, 4, 12, 4));
    gamesHeaderBtn.Click([this](wf::IInspectable const&, wux::RoutedEventArgs const&) {
        m_gamesExpanded = !m_gamesExpanded;
        BuildXamlViewport();
    });
    mainStack.Children().Append(gamesHeaderBtn);

    if (m_gamesExpanded) {
        if (m_layoutMode == LayoutMode::DetailsView) {
            // Structured Table List Mode
            wuxc::StackPanel listStack;
            listStack.Spacing(4);

            for (size_t i = 0; i < m_games.size(); ++i) {
                const auto& g = m_games[i];
                const bool isSel = (m_selectedGameIndex == static_cast<int>(i));

                wuxc::Border rowCard;
                rowCard.Height(46);
                rowCard.CornerRadius(wux::CornerRadiusHelper::FromUniformRadius(6));
                rowCard.Background(isSel ? bgCardSel : bgCard);
                rowCard.BorderBrush(isSel ? accentBrush : borderBrush);
                rowCard.BorderThickness(wux::ThicknessHelper::FromUniformLength(isSel ? 2 : 1));
                rowCard.Padding(wux::ThicknessHelper::FromLengths(12, 0, 12, 0));

                wuxc::Grid rowGrid;
                wuxc::ColumnDefinition c0; c0.Width(wux::GridLengthHelper::FromPixels(38));
                wuxc::ColumnDefinition c1; c1.Width(wux::GridLengthHelper::FromValueAndType(1.0, wux::GridUnitType::Star));
                wuxc::ColumnDefinition c2; c2.Width(wux::GridLengthHelper::FromPixels(130));
                wuxc::ColumnDefinition c3; c3.Width(wux::GridLengthHelper::FromPixels(100));
                rowGrid.ColumnDefinitions().Append(c0);
                rowGrid.ColumnDefinitions().Append(c1);
                rowGrid.ColumnDefinitions().Append(c2);
                rowGrid.ColumnDefinitions().Append(c3);

                std::wstring thumbPath = !g.iconPath.empty() ? g.iconPath : g.coverArtPath;
                if (auto bmp = LoadLocalBitmap(thumbPath)) {
                    wuxc::Image ico;
                    ico.Source(bmp);
                    ico.Width(28);
                    ico.Height(28);
                    ico.VerticalAlignment(wux::VerticalAlignment::Center);
                    wuxc::Grid::SetColumn(ico, 0);
                    rowGrid.Children().Append(ico);
                }

                wuxc::TextBlock tTb;
                tTb.Text(g.title);
                tTb.FontSize(14);
                tTb.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
                tTb.Foreground(textPrimary);
                tTb.VerticalAlignment(wux::VerticalAlignment::Center);
                wuxc::Grid::SetColumn(tTb, 1);
                rowGrid.Children().Append(tTb);

                wuxc::TextBlock pTb;
                pTb.Text(std::wstring(PlatformToString(g.platform)));
                pTb.FontSize(12);
                pTb.Foreground(textSec);
                pTb.VerticalAlignment(wux::VerticalAlignment::Center);
                wuxc::Grid::SetColumn(pTb, 2);
                rowGrid.Children().Append(pTb);

                wuxc::TextBlock sTb;
                sTb.Text(FormatDiskSize(g.diskSizeBytes));
                sTb.FontSize(12);
                sTb.Foreground(textSec);
                sTb.VerticalAlignment(wux::VerticalAlignment::Center);
                wuxc::Grid::SetColumn(sTb, 3);
                rowGrid.Children().Append(sTb);

                rowCard.Child(rowGrid);

                const int gIdx = static_cast<int>(i);
                rowCard.Tapped([this, gIdx](wf::IInspectable const&, wuxi::TappedRoutedEventArgs const&) {
                    m_selectedGameIndex = gIdx;
                    m_selectedProviderIndex = -1;
                    BuildXamlViewport();
                });
                rowCard.DoubleTapped([this, gIdx](wf::IInspectable const&, wuxi::DoubleTappedRoutedEventArgs const&) {
                    if (gIdx >= 0 && gIdx < static_cast<int>(m_games.size())) {
                        PlatformLauncher::Launch(m_hwndView, m_games[static_cast<size_t>(gIdx)]);
                    }
                });

                listStack.Children().Append(rowCard);
            }
            mainStack.Children().Append(listStack);
        } else {
            // Grid Mode: CoverArt (~2:3 Vertical Box Art) or HeroBanner (~16:9 Wide Banner)
            const bool isHero = (m_layoutMode == LayoutMode::HeroBanner);
            const double cardW = isHero ? 276.0 : 172.0;
            const double cardH = isHero ? 184.0 : 284.0;
            const double imgH  = isHero ? 132.0 : 228.0;

            wuxc::VariableSizedWrapGrid gamesWrap;
            gamesWrap.Orientation(wuxc::Orientation::Horizontal);
            gamesWrap.ItemWidth(cardW + 14.0);
            gamesWrap.ItemHeight(cardH + 14.0);

            for (size_t i = 0; i < m_games.size(); ++i) {
                const auto& g = m_games[i];
                const bool isSel = (m_selectedGameIndex == static_cast<int>(i));

                wuxc::Border card;
                card.Width(cardW);
                card.Height(cardH);
                card.Margin(wux::ThicknessHelper::FromLengths(0, 0, 14, 14));
                card.CornerRadius(wux::CornerRadiusHelper::FromUniformRadius(8));
                card.Background(isSel ? bgCardSel : bgCard);
                card.BorderBrush(isSel ? accentBrush : borderBrush);
                card.BorderThickness(wux::ThicknessHelper::FromUniformLength(isSel ? 2 : 1));

                wuxc::Grid cardGrid;
                wuxc::RowDefinition rArt;
                rArt.Height(wux::GridLengthHelper::FromPixels(imgH));
                wuxc::RowDefinition rMeta;
                rMeta.Height(wux::GridLengthHelper::FromValueAndType(1.0, wux::GridUnitType::Star));
                cardGrid.RowDefinitions().Append(rArt);
                cardGrid.RowDefinitions().Append(rMeta);

                wuxc::Border artHost;
                artHost.Background(m_isDarkMode ? MakeBrush(255, 25, 25, 30) : MakeBrush(255, 236, 236, 240));
                artHost.CornerRadius(wux::CornerRadiusHelper::FromRadii(6, 6, 0, 0));

                std::wstring primaryArt = isHero
                    ? (!g.heroArtPath.empty() ? g.heroArtPath : g.coverArtPath)
                    : (!g.coverArtPath.empty() ? g.coverArtPath : g.heroArtPath);

                if (auto bmp = LoadLocalBitmap(primaryArt)) {
                    wuxc::Image artImg;
                    artImg.Source(bmp);
                    artImg.Stretch(wuxm::Stretch::UniformToFill);
                    artHost.Child(artImg);
                } else if (auto iconBmp = LoadLocalBitmap(g.iconPath)) {
                    wuxc::Image icoImg;
                    icoImg.Source(iconBmp);
                    icoImg.Width(84);
                    icoImg.Height(84);
                    icoImg.HorizontalAlignment(wux::HorizontalAlignment::Center);
                    icoImg.VerticalAlignment(wux::VerticalAlignment::Center);
                    artHost.Child(icoImg);
                }
                wuxc::Grid::SetRow(artHost, 0);
                cardGrid.Children().Append(artHost);

                wuxc::StackPanel metaPanel;
                metaPanel.Padding(wux::ThicknessHelper::FromLengths(10, 6, 10, 6));
                metaPanel.VerticalAlignment(wux::VerticalAlignment::Center);

                wuxc::TextBlock titleTb;
                titleTb.Text(g.title);
                titleTb.FontSize(13);
                titleTb.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
                titleTb.Foreground(textPrimary);
                titleTb.TextTrimming(wux::TextTrimming::CharacterEllipsis);
                titleTb.MaxLines(1);

                wuxc::TextBlock platTb;
                platTb.Text(std::wstring(PlatformToString(g.platform)));
                platTb.FontSize(11);
                platTb.Foreground(textSec);
                platTb.TextTrimming(wux::TextTrimming::CharacterEllipsis);
                platTb.MaxLines(1);

                metaPanel.Children().Append(titleTb);
                metaPanel.Children().Append(platTb);
                wuxc::Grid::SetRow(metaPanel, 1);
                cardGrid.Children().Append(metaPanel);

                card.Child(cardGrid);

                const int gIdx = static_cast<int>(i);
                card.Tapped([this, gIdx](wf::IInspectable const&, wuxi::TappedRoutedEventArgs const&) {
                    m_selectedGameIndex = gIdx;
                    m_selectedProviderIndex = -1;
                    BuildXamlViewport();
                });
                card.DoubleTapped([this, gIdx](wf::IInspectable const&, wuxi::DoubleTappedRoutedEventArgs const&) {
                    if (gIdx >= 0 && gIdx < static_cast<int>(m_games.size())) {
                        PlatformLauncher::Launch(m_hwndView, m_games[static_cast<size_t>(gIdx)]);
                    }
                });

                gamesWrap.Children().Append(card);
            }

            mainStack.Children().Append(gamesWrap);
        }
    }

    mainScroll.Content(mainStack);
    wuxc::Grid::SetColumn(mainScroll, 0);
    bodyGrid.Children().Append(mainScroll);

    // Column 1: Persistent Right Details Sidepane
    m_xaml->sidepaneHost = wuxc::Border{};
    RebuildSidepane();
    wuxc::Grid::SetColumn(m_xaml->sidepaneHost, 1);
    bodyGrid.Children().Append(m_xaml->sidepaneHost);

    wuxc::Grid::SetRow(bodyGrid, 1);
    m_xaml->rootGrid.Children().Append(bodyGrid);
}

IFACEMETHODIMP ShellView::Refresh() {
    RefreshData();
    BuildXamlViewport();
    return S_OK;
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
        if (m_pShellBrowser->GetControlWindow(1, &hwndParent) != S_OK || hwndParent == nullptr) {
            m_pShellBrowser->GetWindow(&hwndParent);
        }
    }
    if (hwndParent == nullptr) return E_FAIL;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = g_hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = VIEW_CLASS_NAME;
    RegisterClassExW(&wc);

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

    m_hwndView = CreateWindowExW(
        0, VIEW_CLASS_NAME, L"GameExplorerXamlHost",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        viewX, viewY, viewW, viewH,
        hwndParent, nullptr, g_hInstance, this
    );
    if (m_hwndView == nullptr) return E_FAIL;

    // Initialize WinUI XAML Island (DesktopWindowXamlSource) inside m_hwndView
    try {
        m_xaml->xamlManager = wux::Hosting::WindowsXamlManager::InitializeForCurrentThread();
        m_xaml->xamlSource = wux::Hosting::DesktopWindowXamlSource{};

        auto interop = m_xaml->xamlSource.as<IDesktopWindowXamlSourceNative>();
        if (interop) {
            if (SUCCEEDED(interop->AttachToWindow(m_hwndView))) {
                interop->get_WindowHandle(&m_hwndXamlIsland);
                if (m_hwndXamlIsland != nullptr) {
                    SetWindowPos(m_hwndXamlIsland, nullptr, 0, 0, viewW, viewH, SWP_SHOWWINDOW | SWP_NOZORDER);
                }
            }
        }

        m_xaml->rootGrid = wuxc::Grid{};
        m_xaml->xamlSource.Content(m_xaml->rootGrid);

        RefreshData();
        BuildXamlViewport();
    } catch (...) {
        // Fallback safety if XAML initialization encounters an exception
    }

    *phWnd = m_hwndView;
    return S_OK;
}

IFACEMETHODIMP ShellView::DestroyViewWindow() {
    if (m_uState != SVUIA_DEACTIVATE) {
        UIActivate(SVUIA_DEACTIVATE);
    }

    if (m_xaml) {
        try {
            m_xaml->sidepaneHost = nullptr;
            m_xaml->rootGrid = nullptr;
            if (m_xaml->xamlSource) {
                m_xaml->xamlSource.Close();
                m_xaml->xamlSource = nullptr;
            }
            if (m_xaml->xamlManager) {
                m_xaml->xamlManager.Close();
                m_xaml->xamlManager = nullptr;
            }
        } catch (...) {}
    }

    m_hwndXamlIsland = nullptr;
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
                    m_selectedGameIndex = static_cast<int>(i);
                    m_selectedProviderIndex = -1;
                    BuildXamlViewport();
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

LRESULT CALLBACK ShellView::WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    ShellView* pThis = nullptr;

    if (uMsg == WM_NCCREATE) {
        auto* lpcs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        pThis = static_cast<ShellView*>(lpcs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
    } else {
        pThis = reinterpret_cast<ShellView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (pThis != nullptr) {
        switch (uMsg) {
        case WM_SIZE: {
            const int cx = LOWORD(lParam);
            const int cy = HIWORD(lParam);
            if (pThis->m_hwndXamlIsland != nullptr) {
                SetWindowPos(pThis->m_hwndXamlIsland, nullptr, 0, 0, cx, cy, SWP_NOZORDER | SWP_SHOWWINDOW);
            }
            return 0;
        }
        case WM_SETTINGCHANGE: {
            if (lParam != 0 && wcscmp(reinterpret_cast<LPCWSTR>(lParam), L"ImmersiveColorSet") == 0) {
                pThis->Refresh();
            }
            break;
        }
        }
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

} // namespace GameExplorer
