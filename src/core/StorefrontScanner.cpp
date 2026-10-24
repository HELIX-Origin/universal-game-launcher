#include "GameExplorerCore/StorefrontScanner.h"
#include "GameExplorerCore/Database.h"
#include "GameExplorerCore/Settings.h"
#include "GameExplorerCore/Storage.h"
#include "GameExplorerCore/Types.h"
#include "sqlite3.h"
#include <windows.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <urlmon.h>
#include <winhttp.h>
#include <winver.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <set>

namespace fs = std::filesystem;

namespace GameExplorer {

namespace {

std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    const int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (sizeNeeded <= 0) return {};
    std::string utf8(sizeNeeded, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8.data(), sizeNeeded, nullptr, nullptr);
    return utf8;
}

std::wstring Utf8ToWide(const char* utf8) {
    if (utf8 == nullptr || utf8[0] == '\0') return {};
    const int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (sizeNeeded <= 1) return {};
    std::wstring wide(sizeNeeded - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide.data(), sizeNeeded);
    return wide;
}

std::wstring UnescapeJsonOrVdfString(const std::wstring& raw) {
    std::wstring out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == L'\\' && i + 1 < raw.size()) {
            const wchar_t next = raw[i + 1];
            if (next == L'\\' || next == L'"' || next == L'/') {
                out.push_back(next);
                ++i;
                continue;
            }
            if (next == L'n' || next == L'r' || next == L't') {
                out.push_back(L' ');
                ++i;
                continue;
            }
        }
        out.push_back(raw[i]);
    }
    return out;
}

std::wstring EscapeJsonString(const std::wstring& raw) {
    std::wstring out;
    out.reserve(raw.size() + 8);
    for (const wchar_t ch : raw) {
        if (ch == L'\\') out += L"\\\\";
        else if (ch == L'"') out += L"\\\"";
        else if (ch == L'\n' || ch == L'\r') out += L' ';
        else out.push_back(ch);
    }
    return out;
}

size_t FindNextUnescapedQuote(const std::wstring& s, size_t startPos) {
    for (size_t i = startPos; i < s.size(); ++i) {
        if (s[i] == L'\\') {
            ++i; // Skip escaped char
            continue;
        }
        if (s[i] == L'"') return i;
    }
    return std::wstring::npos;
}

bool ParseKeyValueLine(const std::wstring& line, std::wstring& outKey, std::wstring& outVal) {
    const size_t firstQuote = FindNextUnescapedQuote(line, 0);
    if (firstQuote == std::wstring::npos) return false;
    const size_t secondQuote = FindNextUnescapedQuote(line, firstQuote + 1);
    if (secondQuote == std::wstring::npos) return false;

    outKey = UnescapeJsonOrVdfString(line.substr(firstQuote + 1, secondQuote - firstQuote - 1));

    const size_t thirdQuote = FindNextUnescapedQuote(line, secondQuote + 1);
    if (thirdQuote == std::wstring::npos) return false;
    const size_t fourthQuote = FindNextUnescapedQuote(line, thirdQuote + 1);
    if (fourthQuote == std::wstring::npos) return false;

    outVal = UnescapeJsonOrVdfString(line.substr(thirdQuote + 1, fourthQuote - thirdQuote - 1));
    return true;
}

std::wstring NormalizeDirKey(const std::wstring& dirPath) {
    if (dirPath.empty()) return L"";
    std::wstring norm = dirPath;
    std::replace(norm.begin(), norm.end(), L'/', L'\\');
    // Collapse double backslashes
    std::wstring collapsed;
    collapsed.reserve(norm.size());
    for (size_t i = 0; i < norm.size(); ++i) {
        if (norm[i] == L'\\' && !collapsed.empty() && collapsed.back() == L'\\') {
            continue;
        }
        collapsed.push_back(static_cast<wchar_t>(::towlower(norm[i])));
    }
    // Ensure drive letter has backslash (e.g., "e:games" -> "e:\games")
    if (collapsed.size() >= 2 && collapsed[1] == L':' && (collapsed.size() == 2 || collapsed[2] != L'\\')) {
        collapsed.insert(2, 1, L'\\');
    }
    while (collapsed.size() > 3 && collapsed.back() == L'\\') {
        collapsed.pop_back();
    }
    return collapsed;
}

std::wstring CacheRemoteArtwork(const std::wstring& appId, const std::wstring& role, const std::wstring& url) {
    if (url.empty()) return L"";
    if (url.rfind(L"http://", 0) != 0 && url.rfind(L"https://", 0) != 0) {
        return url;
    }

    // Preserve exact image extension (.gif, .png, .jpg, .jpeg, .webp, .bmp, .ico)
    std::wstring lowerUrl = url;
    std::transform(lowerUrl.begin(), lowerUrl.end(), lowerUrl.begin(), ::towlower);
    std::wstring ext = L".jpg";
    if (lowerUrl.find(L".gif") != std::wstring::npos) ext = L".gif";
    else if (lowerUrl.find(L".png") != std::wstring::npos) ext = L".png";
    else if (lowerUrl.find(L".webp") != std::wstring::npos) ext = L".webp";
    else if (lowerUrl.find(L".bmp") != std::wstring::npos) ext = L".bmp";
    else if (lowerUrl.find(L".ico") != std::wstring::npos) ext = L".ico";

    Storage::EnsureDirectoriesCreated();
    const fs::path artDir = Storage::GetArtPath();
    std::error_code ec;
    fs::create_directories(artDir, ec);

    std::wstring safeId = appId;
    for (auto& ch : safeId) {
        if (!iswalnum(ch) && ch != L'_' && ch != L'-') ch = L'_';
    }

    const fs::path localPath = artDir / (safeId + L"_" + role + ext);
    if (fs::exists(localPath, ec) && fs::file_size(localPath, ec) > 64) {
        return localPath.wstring();
    }

    if (SUCCEEDED(URLDownloadToFileW(nullptr, url.c_str(), localPath.c_str(), 0, nullptr))) {
        if (fs::exists(localPath, ec) && fs::file_size(localPath, ec) > 64) {
            return localPath.wstring();
        }
    }
    return url;
}

std::wstring GetSystemSteamApiLanguage() {
    wchar_t localeBuf[LOCALE_NAME_MAX_LENGTH] = { 0 };
    if (GetUserDefaultLocaleName(localeBuf, LOCALE_NAME_MAX_LENGTH) <= 0) {
        return L"english";
    }
    std::wstring loc = localeBuf;
    std::transform(loc.begin(), loc.end(), loc.begin(), ::towlower);

    if (loc.rfind(L"ja", 0) == 0) return L"japanese";
    if (loc.rfind(L"de", 0) == 0) return L"german";
    if (loc.rfind(L"fr", 0) == 0) return L"french";
    if (loc == L"es-419" || loc == L"es-mx" || loc == L"es-ar" || loc == L"es-cl" || loc == L"es-co") return L"latam";
    if (loc.rfind(L"es", 0) == 0) return L"spanish";
    if (loc == L"pt-br") return L"brazilian";
    if (loc.rfind(L"pt", 0) == 0) return L"portuguese";
    if (loc.rfind(L"ru", 0) == 0) return L"russian";
    if (loc == L"zh-tw" || loc == L"zh-hk" || loc == L"zh-hant") return L"tchinese";
    if (loc.rfind(L"zh", 0) == 0) return L"schinese";
    if (loc.rfind(L"ko", 0) == 0) return L"koreana";
    if (loc.rfind(L"pl", 0) == 0) return L"polish";
    if (loc.rfind(L"it", 0) == 0) return L"italian";
    if (loc.rfind(L"nl", 0) == 0) return L"Dutch";
    if (loc.rfind(L"tr", 0) == 0) return L"turkish";
    if (loc.rfind(L"ar", 0) == 0) return L"arabic";
    if (loc.rfind(L"th", 0) == 0) return L"thai";
    if (loc.rfind(L"vi", 0) == 0) return L"vietnamese";
    if (loc.rfind(L"uk", 0) == 0) return L"ukrainian";
    if (loc.rfind(L"cs", 0) == 0) return L"czech";
    if (loc.rfind(L"hu", 0) == 0) return L"hungarian";
    if (loc.rfind(L"ro", 0) == 0) return L"romanian";
    if (loc.rfind(L"sv", 0) == 0) return L"swedish";
    if (loc.rfind(L"da", 0) == 0) return L"danish";
    if (loc.rfind(L"fi", 0) == 0) return L"finnish";
    if (loc.rfind(L"nb", 0) == 0 || loc.rfind(L"nn", 0) == 0 || loc.rfind(L"no", 0) == 0) return L"norwegian";
    return L"english";
}

void EnrichFromExecutableVersionInfo(GameItem& item) {
    if (item.executablePath.empty() || !fs::exists(item.executablePath)) return;
    if (!item.developer.empty() && !item.publisher.empty()) return;

    DWORD handle = 0;
    const DWORD verSize = GetFileVersionInfoSizeW(item.executablePath.c_str(), &handle);
    if (verSize == 0) return;

    std::vector<BYTE> verData(verSize);
    if (!GetFileVersionInfoW(item.executablePath.c_str(), handle, verSize, verData.data())) {
        return;
    }

    struct LANGANDCODEPAGE {
        WORD wLanguage;
        WORD wCodePage;
    }* lpTranslate = nullptr;
    UINT cbTranslate = 0;

    if (!VerQueryValueW(verData.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<LPVOID*>(&lpTranslate), &cbTranslate) ||
        cbTranslate < sizeof(LANGANDCODEPAGE) || lpTranslate == nullptr) {
        return;
    }

    const size_t numTrans = cbTranslate / sizeof(LANGANDCODEPAGE);
    const LANGID userLang = GetUserDefaultUILanguage();
    size_t bestIdx = 0;
    for (size_t i = 0; i < numTrans; ++i) {
        if (lpTranslate[i].wLanguage == userLang) {
            bestIdx = i;
            break;
        }
        if (PRIMARYLANGID(lpTranslate[i].wLanguage) == PRIMARYLANGID(userLang)) {
            bestIdx = i;
        }
    }

    auto queryStr = [&](const wchar_t* fieldName) -> std::wstring {
        wchar_t subBlock[128] = { 0 };
        wsprintfW(subBlock, L"\\StringFileInfo\\%04x%04x\\%s", lpTranslate[bestIdx].wLanguage, lpTranslate[bestIdx].wCodePage, fieldName);
        wchar_t* lpBuffer = nullptr;
        UINT dwBytes = 0;
        if (VerQueryValueW(verData.data(), subBlock, reinterpret_cast<LPVOID*>(&lpBuffer), &dwBytes) && lpBuffer != nullptr && dwBytes > 1) {
            return std::wstring(lpBuffer);
        }
        return L"";
    };

    const std::wstring company = queryStr(L"CompanyName");
    if (!company.empty()) {
        if (item.developer.empty()) item.developer = company;
        if (item.publisher.empty()) item.publisher = company;
    }
    if (item.synopsis.empty()) {
        const std::wstring desc = queryStr(L"FileDescription");
        if (!desc.empty() && _wcsicmp(desc.c_str(), item.title.c_str()) != 0) {
            item.synopsis = desc;
        }
    }
}

uint64_t CalculateDirectoryDiskSize(const fs::path& dirPath) {
    if (dirPath.empty() || !fs::exists(dirPath)) return 0;
    uint64_t total = 0;
    std::error_code ec;
    auto iter = fs::recursive_directory_iterator(dirPath, fs::directory_options::skip_permission_denied, ec);
    for (const auto& entry : iter) {
        if (iter.depth() > 3) {
            iter.pop();
            continue;
        }
        if (entry.is_regular_file(ec)) {
            total += entry.file_size(ec);
        }
    }
    return total;
}

std::string FetchHttpsUrlUtf8(const wchar_t* host, const std::wstring& path) {
    std::string response;
    HINTERNET hSession = WinHttpOpen(L"WindowsGameExplorer/2.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return response;

    WinHttpSetTimeouts(hSession, 1500, 1500, 2000, 2000);
    HINTERNET hConnect = WinHttpConnect(hSession, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (hConnect) {
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (hRequest) {
            if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(hRequest, nullptr)) {
                DWORD dwSize = 0;
                do {
                    if (!WinHttpQueryDataAvailable(hRequest, &dwSize) || dwSize == 0) break;
                    std::string chunk(dwSize, '\0');
                    DWORD dwDownloaded = 0;
                    if (WinHttpReadData(hRequest, chunk.data(), dwSize, &dwDownloaded) && dwDownloaded > 0) {
                        chunk.resize(dwDownloaded);
                        response.append(chunk);
                    }
                } while (dwSize > 0);
            }
            WinHttpCloseHandle(hRequest);
        }
        WinHttpCloseHandle(hConnect);
    }
    WinHttpCloseHandle(hSession);
    return response;
}

std::wstring ExtractJsonStringField(const std::wstring& json, const std::wstring& fieldKey, size_t startSearchPos = 0) {
    const std::wstring needle = L"\"" + fieldKey + L"\":";
    size_t pos = json.find(needle, startSearchPos);
    if (pos == std::wstring::npos) return L"";
    pos += needle.size();
    while (pos < json.size() && (json[pos] == L' ' || json[pos] == L'\t')) ++pos;
    if (pos >= json.size() || json[pos] != L'"') return L"";
    const size_t endQuote = FindNextUnescapedQuote(json, pos + 1);
    if (endQuote == std::wstring::npos) return L"";
    return UnescapeJsonOrVdfString(json.substr(pos + 1, endQuote - (pos + 1)));
}

std::wstring ExtractJsonStringArrayFirstOrJoined(const std::wstring& json, const std::wstring& arrayKey) {
    const std::wstring needle = L"\"" + arrayKey + L"\":[";
    size_t pos = json.find(needle);
    if (pos == std::wstring::npos) {
        const std::wstring needleSp = L"\"" + arrayKey + L"\": [";
        pos = json.find(needleSp);
        if (pos == std::wstring::npos) return L"";
        pos += needleSp.size();
    } else {
        pos += needle.size();
    }

    const size_t closeBracket = json.find(L']', pos);
    if (closeBracket == std::wstring::npos) return L"";

    std::wstring joined;
    size_t cur = pos;
    while (cur < closeBracket) {
        const size_t q1 = FindNextUnescapedQuote(json, cur);
        if (q1 == std::wstring::npos || q1 >= closeBracket) break;
        const size_t q2 = FindNextUnescapedQuote(json, q1 + 1);
        if (q2 == std::wstring::npos || q2 >= closeBracket) break;
        const std::wstring item = UnescapeJsonOrVdfString(json.substr(q1 + 1, q2 - (q1 + 1)));
        if (!item.empty()) {
            if (!joined.empty()) joined += L", ";
            joined += item;
        }
        cur = q2 + 1;
    }
    return joined;
}

void EnrichSteamMetadataFromStoreApi(const std::wstring& rawAppId, GameItem& item) {
    if (!item.developer.empty() && !item.rating.empty() && !item.synopsis.empty()) {
        return;
    }

    const std::wstring langParam = GetSystemSteamApiLanguage();
    const std::string utf8Json = FetchHttpsUrlUtf8(L"store.steampowered.com", L"/api/appdetails?appids=" + rawAppId + L"&l=" + langParam);
    if (utf8Json.empty()) return;

    const std::wstring json = Utf8ToWide(utf8Json.c_str());
    if (json.find(L"\"success\":true") == std::wstring::npos && json.find(L"\"success\": true") == std::wstring::npos) {
        return;
    }

    if (item.developer.empty()) {
        item.developer = ExtractJsonStringArrayFirstOrJoined(json, L"developers");
    }
    if (item.publisher.empty()) {
        item.publisher = ExtractJsonStringArrayFirstOrJoined(json, L"publishers");
    }
    if (item.synopsis.empty()) {
        item.synopsis = ExtractJsonStringField(json, L"short_description");
    }
    if (item.releaseDate.empty()) {
        const size_t relPos = json.find(L"\"release_date\"");
        if (relPos != std::wstring::npos) {
            item.releaseDate = ExtractJsonStringField(json, L"date", relPos);
        }
    }
    if (item.genres.empty()) {
        const size_t genresPos = json.find(L"\"genres\":");
        if (genresPos != std::wstring::npos) {
            const size_t endGenres = json.find(L']', genresPos);
            if (endGenres != std::wstring::npos) {
                std::wstring gList;
                size_t searchCur = genresPos;
                while (searchCur < endGenres) {
                    const size_t descPos = json.find(L"\"description\":", searchCur);
                    if (descPos == std::wstring::npos || descPos >= endGenres) break;
                    const std::wstring gName = ExtractJsonStringField(json, L"description", descPos - 1);
                    if (!gName.empty()) {
                        if (!gList.empty()) gList += L", ";
                        gList += gName;
                    }
                    searchCur = descPos + 14;
                }
                item.genres = gList;
            }
        }
    }
    if (item.rating.empty()) {
        // 1. Check ESRB rating if present
        std::wstring ratingLabel;
        const size_t esrbPos = json.find(L"\"esrb\":");
        if (esrbPos != std::wstring::npos) {
            std::wstring r = ExtractJsonStringField(json, L"rating", esrbPos);
            std::transform(r.begin(), r.end(), r.begin(), ::towlower);
            if (r == L"e") ratingLabel = L"ESRB: Everyone";
            else if (r == L"e10") ratingLabel = L"ESRB: Everyone 10+";
            else if (r == L"t") ratingLabel = L"ESRB: Teen (13+)";
            else if (r == L"m") ratingLabel = L"ESRB: Mature (17+)";
            else if (r == L"ao") ratingLabel = L"ESRB: Adults Only (18+)";
        }
        // 2. Check Steam content_descriptors (3 = Adult Only Sexual Content, 4 = Frequent Nudity/Sexual Content, 2 = Frequent Violence/Gore)
        if (ratingLabel.empty()) {
            const size_t cdPos = json.find(L"\"content_descriptors\":");
            if (cdPos != std::wstring::npos) {
                const size_t cdEnd = json.find(L'}', cdPos);
                const std::wstring cdBlock = json.substr(cdPos, cdEnd != std::wstring::npos ? (cdEnd - cdPos) : 120);
                if (cdBlock.find(L"3") != std::wstring::npos) ratingLabel = L"Adults Only (18+)";
                else if (cdBlock.find(L"4") != std::wstring::npos || cdBlock.find(L"2") != std::wstring::npos) ratingLabel = L"Mature (17+)";
            }
        }
        // 3. Check DEJUS / Steam Germany / PEGI age rating
        if (ratingLabel.empty()) {
            for (const wchar_t* agency : { L"\"dejus\":", L"\"steam_germany\":", L"\"pegi\":" }) {
                const size_t agPos = json.find(agency);
                if (agPos != std::wstring::npos) {
                    const std::wstring ageStr = ExtractJsonStringField(json, L"rating", agPos);
                    const int age = _wtoi(ageStr.c_str());
                    if (age >= 18) { ratingLabel = L"Mature (17+)"; break; }
                    if (age >= 12) { ratingLabel = L"Teen (13+)"; break; }
                    if (age >= 10) { ratingLabel = L"Everyone 10+"; break; }
                    if (!ageStr.empty()) { ratingLabel = L"Everyone"; break; }
                }
            }
        }
        if (ratingLabel.empty()) {
            ratingLabel = L"Teen (13+)";
        }
        item.rating = ratingLabel;
    }
}

std::wstring ReadRegistryString(HKEY hRoot, const wchar_t* subKey, const wchar_t* valueName) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(hRoot, subKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return L"";
    }

    wchar_t buffer[MAX_PATH] = { 0 };
    DWORD bufSize = sizeof(buffer);
    DWORD type = 0;
    const LSTATUS status = RegQueryValueExW(hKey, valueName, nullptr, &type, reinterpret_cast<BYTE*>(buffer), &bufSize);
    RegCloseKey(hKey);

    if (status == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ)) {
        return std::wstring(buffer);
    }
    return L"";
}

// Recursively searches for the primary game executable (up to 3 levels deep)
fs::path FindPrimaryGameExecutable(const fs::path& rootDir) {
    if (!fs::exists(rootDir)) return {};

    fs::path preferredExe;
    uintmax_t maxExeSize = 0;

    std::error_code ec;
    auto iter = fs::recursive_directory_iterator(
        rootDir,
        fs::directory_options::skip_permission_denied,
        ec
    );

    for (const auto& entry : iter) {
        if (iter.depth() > 3) {
            iter.pop();
            continue;
        }

        if (!entry.is_regular_file()) continue;

        if (entry.path().extension() == L".exe") {
            const auto fn = entry.path().filename().wstring();
            if (StorefrontScanner::IsBlacklistedExecutable(fn)) continue;

            // Highly prioritized canonical game executable names
            if (_wcsicmp(fn.c_str(), L"Game.exe") == 0 ||
                _wcsicmp(fn.c_str(), L"ffxivboot64.exe") == 0 ||
                _wcsicmp(fn.c_str(), L"ffxivboot.exe") == 0) {
                return entry.path();
            }

            const uintmax_t sz = entry.file_size(ec);
            if (sz > maxExeSize) {
                maxExeSize = sz;
                preferredExe = entry.path();
            }
        }
    }

    return preferredExe;
}

std::wstring CleanTitleFromFolderName(const std::wstring& folderName) {
    std::wstring title = folderName;
    // Replace hyphens and underscores with spaces
    for (auto& ch : title) {
        if (ch == L'-' || ch == L'_') ch = L' ';
    }

    // Capitalize first letter of each word
    bool newWord = true;
    for (auto& ch : title) {
        if (ch == L' ') {
            newWord = true;
        } else if (newWord) {
            ch = static_cast<wchar_t>(::towupper(ch));
            newWord = false;
        }
    }

    return title;
}

} // namespace

bool StorefrontScanner::IsBlacklistedExecutable(const std::wstring& filename) {
    std::wstring lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);

    const wchar_t* blacklisted[] = {
        L"notification_helper.exe",
        L"nwjc.exe",
        L"unitycrashhandler64.exe",
        L"unitycrashhandler32.exe",
        L"crashreport.exe",
        L"crashpad_handler.exe",
        L"unins000.exe",
        L"uninstall.exe",
        L"dxsetup.exe",
        L"vcredist_x64.exe",
        L"vcredist_x86.exe",
        L"update.exe",
        L"setup.exe",
        L"battle.net.exe",
        L"battle.net launcher.exe",
        L"battlenet.exe",
        L"battlenet.overlay.runtime.exe",
        L"agent.exe",
        L"blizzarderror.exe",
        L"blizzarderror64.exe",
        L"ubisoftconnect.exe",
        L"ubisoftgamelauncher.exe",
        L"ubisoftgamelauncher64.exe",
        L"upc.exe",
        L"uplay.exe"
    };

    for (const auto* b : blacklisted) {
        if (lower == b) return true;
    }
    return false;
}

std::vector<std::wstring> StorefrontScanner::GetAllMountedDrives() {
    std::vector<std::wstring> drives;
    wchar_t driveStrings[512] = { 0 };
    const DWORD len = GetLogicalDriveStringsW(511, driveStrings);
    if (len > 0) {
        const wchar_t* p = driveStrings;
        while (*p) {
            const UINT type = GetDriveTypeW(p);
            if (type == DRIVE_FIXED || type == DRIVE_REMOVABLE) {
                std::wstring root = p;
                if (!root.empty() && root.back() != L'\\') {
                    root.push_back(L'\\'); // Ensure "C:\" so fs::path(drive) / L"Games" is "C:\Games"
                }
                drives.push_back(root);
            }
            p += wcslen(p) + 1;
        }
    }
    return drives;
}

size_t StorefrontScanner::ScanAll(Database& db) {
    size_t total = 0;
    const auto allGames = ScanAllInstalledGames();

    std::set<std::wstring> validAppIds;
    for (const auto& game : allGames) {
        validAppIds.insert(game.appId);
    }

    const auto existing = db.GetAllGames();

    db.BeginTransaction();
    for (auto game : allGames) {
        for (const auto& ex : existing) {
            if (ex.appId == game.appId && ex.isFavorite) {
                game.isFavorite = true;
                break;
            }
        }
        if (db.UpsertGame(game)) {
            ++total;
        }
    }

    // Prune stale, uninstalled, or obsolete ghost games from the database
    for (const auto& ex : existing) {
        if (validAppIds.count(ex.appId) == 0) {
            db.DeleteGame(ex.id);
        }
    }
    db.CommitTransaction();

    return total;
}

std::vector<std::wstring> StorefrontScanner::GetSteamLibraryFolders(const std::wstring& steamPath) {
    std::vector<std::wstring> folders;
    std::set<std::wstring> seenNormalized;

    auto tryAddFolder = [&](const std::wstring& rawFolder) {
        if (rawFolder.empty() || !fs::exists(rawFolder)) return;
        const std::wstring key = NormalizeDirKey(rawFolder);
        if (!key.empty() && seenNormalized.insert(key).second) {
            folders.push_back(fs::path(rawFolder).lexically_normal().wstring());
        }
    };

    tryAddFolder(steamPath);

    const fs::path vdfPath = fs::path(steamPath) / L"steamapps" / L"libraryfolders.vdf";
    if (fs::exists(vdfPath)) {
        std::wifstream stream(vdfPath);
        if (stream.is_open()) {
            std::wstring line;
            while (std::getline(stream, line)) {
                std::wstring key, val;
                if (ParseKeyValueLine(line, key, val)) {
                    if (key == L"path") {
                        std::replace(val.begin(), val.end(), L'/', L'\\');
                        tryAddFolder(val);
                    }
                }
            }
        }
    }

    // Also scan all mounted drives for non-standard or external Steam libraries
    const auto drives = GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path candidates[] = {
            fs::path(drive) / L"SteamLibrary",
            fs::path(drive) / L"Games" / L"SteamLibrary",
            fs::path(drive) / L"Steam"
        };
        for (const auto& cand : candidates) {
            if (fs::exists(cand / L"steamapps")) {
                tryAddFolder(cand.wstring());
            }
        }
    }

    return folders;
}

void StorefrontScanner::DiscoverSteamGridArtwork(
    const std::wstring& steamPath,
    const std::wstring& appId,
    std::wstring& outCover,
    std::wstring& outHero,
    std::wstring& outIcon
) {
    // 1. Check modern Steam librarycache: steamPath/appcache/librarycache/<appId>/ (including hashed subfolders)
    const fs::path libCacheDir = fs::path(steamPath) / L"appcache" / L"librarycache" / appId;
    if (fs::exists(libCacheDir)) {
        const fs::path cover600 = libCacheDir / L"library_600x900.jpg";
        const fs::path coverPng = libCacheDir / L"library_600x900.png";
        const fs::path coverCap = libCacheDir / L"library_capsule.jpg";
        if (outCover.empty()) {
            if (fs::exists(cover600)) outCover = cover600.wstring();
            else if (fs::exists(coverPng)) outCover = coverPng.wstring();
            else if (fs::exists(coverCap)) outCover = coverCap.wstring();
        }
        if (outHero.empty()) {
            const fs::path heroJpg = libCacheDir / L"library_hero.jpg";
            const fs::path heroPng = libCacheDir / L"library_hero.png";
            const fs::path headerJpg = libCacheDir / L"header.jpg";
            if (fs::exists(heroJpg)) outHero = heroJpg.wstring();
            else if (fs::exists(heroPng)) outHero = heroPng.wstring();
            else if (fs::exists(headerJpg)) outHero = headerJpg.wstring();
        }

        // Scan hashed subdirectories (Steam 2024+ librarycache layout) and SHA1 icon files
        std::error_code ec;
        for (const auto& entry : fs::recursive_directory_iterator(libCacheDir, fs::directory_options::skip_permission_denied, ec)) {
            if (!entry.is_regular_file()) continue;
            const auto fn = entry.path().filename().wstring();
            if (outCover.empty() && (fn == L"library_600x900.jpg" || fn == L"library_600x900.png" || fn == L"library_capsule.jpg" || fn == L"library_capsule.png")) {
                outCover = entry.path().wstring();
            } else if (outHero.empty() && (fn == L"library_hero.jpg" || fn == L"library_hero.png" || fn == L"library_header.jpg" || fn == L"header.jpg")) {
                outHero = entry.path().wstring();
            } else if (outIcon.empty() && fn.size() == 44 && entry.path().extension() == L".jpg") {
                // 40-char SHA1 hex filename + ".jpg" is Steam's cached square game icon
                outIcon = entry.path().wstring();
            }
        }

        if (outIcon.empty()) {
            const fs::path logoPng = libCacheDir / L"logo.png";
            if (fs::exists(logoPng)) outIcon = logoPng.wstring();
        }
        if (outCover.empty() && !outHero.empty()) {
            outCover = outHero;
        }
    }

    // 2. Also check userdata/<userId>/config/grid/ (custom/overridden grid art)
    const fs::path userdataPath = fs::path(steamPath) / L"userdata";
    if (fs::exists(userdataPath)) {
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(userdataPath, ec)) {
            if (!entry.is_directory()) continue;

            const fs::path gridDir = entry.path() / L"config" / L"grid";
            if (!fs::exists(gridDir)) continue;

            if (outCover.empty()) {
                const fs::path coverPng = gridDir / (appId + L"p.png");
                const fs::path coverJpg = gridDir / (appId + L"p.jpg");
                if (fs::exists(coverPng)) outCover = coverPng.wstring();
                else if (fs::exists(coverJpg)) outCover = coverJpg.wstring();
            }

            if (outHero.empty()) {
                const fs::path heroPng = gridDir / (appId + L"_hero.png");
                const fs::path heroJpg = gridDir / (appId + L"_hero.jpg");
                if (fs::exists(heroPng)) outHero = heroPng.wstring();
                else if (fs::exists(heroJpg)) outHero = heroJpg.wstring();
            }

            if (outIcon.empty()) {
                const fs::path iconIco = gridDir / (appId + L"_icon.ico");
                const fs::path iconPng = gridDir / (appId + L"_icon.png");
                const fs::path logoPng = gridDir / (appId + L"_logo.png");
                if (fs::exists(iconIco)) outIcon = iconIco.wstring();
                else if (fs::exists(iconPng)) outIcon = iconPng.wstring();
                else if (fs::exists(logoPng)) outIcon = logoPng.wstring();
            }

            if (!outCover.empty() && !outHero.empty() && !outIcon.empty()) {
                break;
            }
        }
    }
}

std::vector<GameItem> StorefrontScanner::ScanSteam() {
    std::vector<GameItem> games;
    std::set<std::wstring> seenSteamAppIds;

    std::wstring steamPath = ReadRegistryString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
    if (steamPath.empty()) {
        steamPath = ReadRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath");
    }
    if (steamPath.empty()) {
        steamPath = ReadRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath");
    }
    if (steamPath.empty() || !fs::exists(steamPath)) {
        return games;
    }

    const auto libraryFolders = GetSteamLibraryFolders(steamPath);

    for (const auto& lib : libraryFolders) {
        const fs::path steamappsDir = fs::path(lib) / L"steamapps";
        if (!fs::exists(steamappsDir)) continue;

        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(steamappsDir, ec)) {
            if (!entry.is_regular_file()) continue;

            const auto filename = entry.path().filename().wstring();
            if (filename.rfind(L"appmanifest_", 0) != 0 || entry.path().extension() != L".acf") {
                continue;
            }

            std::wifstream stream(entry.path());
            if (!stream.is_open()) continue;

            std::wstring rawAppId, gameName, installDir, sizeOnDiskStr, lastPlayedStr;
            std::wstring line;
            while (std::getline(stream, line)) {
                std::wstring key, val;
                if (ParseKeyValueLine(line, key, val)) {
                    if (key == L"appid") rawAppId = val;
                    else if (key == L"name") gameName = val;
                    else if (key == L"installdir") installDir = val;
                    else if (key == L"SizeOnDisk") sizeOnDiskStr = val;
                    else if (key == L"LastPlayed") lastPlayedStr = val;
                }
            }

            if (rawAppId.empty() || gameName.empty()) continue;
            if (!seenSteamAppIds.insert(rawAppId).second) continue;

            // Filter out tools, runtimes, and redistributables
            if (rawAppId == L"228980" ||
                gameName.find(L"SteamVR") != std::wstring::npos ||
                gameName.find(L"Dedicated Server") != std::wstring::npos ||
                gameName.find(L"Steamworks") != std::wstring::npos) {
                continue;
            }

            GameItem item{};
            item.appId = L"steam_" + rawAppId;
            item.title = gameName;
            item.platform = PlatformType::Steam;
            item.launchUri = L"steam://rungameid/" + rawAppId;

            if (!sizeOnDiskStr.empty()) {
                item.diskSizeBytes = _wcstoui64(sizeOnDiskStr.c_str(), nullptr, 10);
            }

            if (!lastPlayedStr.empty()) {
                item.lastPlayedTimestamp = _wcstoui64(lastPlayedStr.c_str(), nullptr, 10);
            }

            const fs::path commonDir = steamappsDir / L"common" / installDir;
            if (fs::exists(commonDir)) {
                item.workingDirectory = commonDir.wstring();
                const auto exe = FindPrimaryGameExecutable(commonDir);
                if (!exe.empty()) {
                    item.executablePath = exe.wstring();
                }
            }

            DiscoverSteamGridArtwork(steamPath, rawAppId, item.coverArtPath, item.heroArtPath, item.iconPath);

            // Load any cached/custom per-game metadata first; if developer/rating/synopsis missing, enrich from Steam Store API and persist
            LoadPerGameMetadata(item);
            if (item.developer.empty() || item.rating.empty() || item.synopsis.empty()) {
                EnrichSteamMetadataFromStoreApi(rawAppId, item);
                SavePerGameMetadata(item);
            }

            games.push_back(std::move(item));
        }
    }

    return games;
}

std::vector<GameItem> StorefrontScanner::ScanItch() {
    std::vector<GameItem> games;
    std::set<std::wstring> discoveredFolderNames;

    auto extractItchSubdomainAuthor = [](const std::wstring& storeUrl) -> std::wstring {
        const size_t schemePos = storeUrl.find(L"://");
        if (schemePos == std::wstring::npos) return L"";
        const size_t dotPos = storeUrl.find(L".itch.io", schemePos + 3);
        if (dotPos == std::wstring::npos) return L"";
        return CleanTitleFromFolderName(storeUrl.substr(schemePos + 3, dotPos - (schemePos + 3)));
    };

    auto classifyItchRating = [](const std::wstring& title, const std::wstring& desc) -> std::wstring {
        std::wstring combined = title + L" " + desc;
        std::transform(combined.begin(), combined.end(), combined.begin(), ::towlower);
        const wchar_t* adultKeywords[] = {
            L"18+", L"nsfw", L"\U0001F51E", L"adult", L"h-game", L"hentai", L"lewd", L"kinky", L"harem", L"eroge", L"ntr", L"futa", L"hookers"
        };
        for (const auto* kw : adultKeywords) {
            if (combined.find(kw) != std::wstring::npos) return L"Adults Only (18+)";
        }
        return L"Teen (13+)";
    };

    // 1. Check butler.db in %APPDATA%\itch\db\butler.db
    wchar_t appData[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH) > 0) {
        const fs::path dbPath = fs::path(appData) / L"itch" / L"db" / L"butler.db";
        if (fs::exists(dbPath)) {
            sqlite3* pItchDb = nullptr;
            const std::string utf8Path = WideToUtf8(dbPath.wstring());
            if (sqlite3_open_v2(utf8Path.c_str(), &pItchDb, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK) {
                const char* sql =
                    "SELECT c.id, g.id, g.title, g.cover_url, g.still_cover_url, g.url, l.path, c.install_folder_name, "
                    "u.display_name, u.username, g.short_text, g.published_at, c.installed_size, c.seconds_run "
                    "FROM caves c "
                    "JOIN games g ON c.game_id = g.id "
                    "JOIN install_locations l ON c.install_location_id = l.id "
                    "LEFT JOIN users u ON g.user_id = u.id;";

                sqlite3_stmt* stmt = nullptr;
                if (sqlite3_prepare_v2(pItchDb, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                    while (sqlite3_step(stmt) == SQLITE_ROW) {
                        const int64_t gameId = sqlite3_column_int64(stmt, 1);
                        const auto* titleUtf8 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                        const auto* coverUrlUtf8 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
                        const auto* stillCoverUtf8 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
                        const auto* urlUtf8 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
                        const auto* locPathUtf8 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
                        const auto* folderUtf8 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
                        const auto* dispNameUtf8 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
                        const auto* userNameUtf8 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
                        const auto* shortTextUtf8 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
                        const auto* pubAtUtf8 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11));
                        const int64_t installedSize = sqlite3_column_int64(stmt, 12);
                        const int64_t secondsRun = sqlite3_column_int64(stmt, 13);

                        const std::wstring title = Utf8ToWide(titleUtf8);
                        const std::wstring locPath = Utf8ToWide(locPathUtf8);
                        const std::wstring folderName = Utf8ToWide(folderUtf8);

                        if (!locPath.empty() && !folderName.empty()) {
                            const fs::path gameDir = fs::path(locPath) / folderName;
                            std::wstring lowerFolder = folderName;
                            std::transform(lowerFolder.begin(), lowerFolder.end(), lowerFolder.begin(), ::towlower);
                            discoveredFolderNames.insert(lowerFolder);

                            GameItem item{};
                            item.appId = L"itch_" + std::to_wstring(gameId);
                            item.title = title.empty() ? CleanTitleFromFolderName(folderName) : title;
                            item.platform = PlatformType::Itch;
                            item.storePageUrl = Utf8ToWide(urlUtf8);

                            std::wstring dev = Utf8ToWide(dispNameUtf8);
                            if (dev.empty()) dev = Utf8ToWide(userNameUtf8);
                            if (dev.empty()) dev = extractItchSubdomainAuthor(item.storePageUrl);
                            item.developer = dev;
                            item.publisher = dev.empty() ? L"itch.io" : dev;

                            item.synopsis = Utf8ToWide(shortTextUtf8);
                            const std::wstring pubAt = Utf8ToWide(pubAtUtf8);
                            if (pubAt.size() >= 10) {
                                item.releaseDate = pubAt.substr(0, 10);
                            }
                            item.genres = L"Indie";
                            item.rating = classifyItchRating(item.title, item.synopsis);

                            if (installedSize > 0) {
                                item.diskSizeBytes = static_cast<uint64_t>(installedSize);
                            }
                            if (secondsRun > 0) {
                                item.playtimeMinutes = static_cast<uint32_t>((secondsRun + 59) / 60);
                            }

                            // Find primary executable for direct execution without the itch app
                            const fs::path exePath = FindPrimaryGameExecutable(gameDir);
                            if (!exePath.empty()) {
                                item.executablePath = exePath.wstring();
                                item.workingDirectory = exePath.parent_path().wstring();
                            } else {
                                item.workingDirectory = gameDir.wstring();
                            }

                            item.launchUri = L"";

                            std::wstring coverUrl = Utf8ToWide(coverUrlUtf8);
                            if (coverUrl.empty()) coverUrl = Utf8ToWide(stillCoverUtf8);
                            if (!coverUrl.empty()) {
                                item.coverArtPath = CacheRemoteArtwork(item.appId, L"cover", coverUrl);
                            }

                            LoadPerGameMetadata(item);
                            SavePerGameMetadata(item);
                            games.push_back(std::move(item));
                        }
                    }
                    sqlite3_finalize(stmt);
                }
                sqlite3_close(pItchDb);
            }
        }
    }

    // 2. Scan all mounted drives for itch game folders (e.g. E:\Games\itch\games)
    const auto drives = GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path itchDirs[] = {
            fs::path(drive) / L"Games" / L"itch" / L"games",
            fs::path(drive) / L"itch" / L"games",
        };

        for (const auto& itchDir : itchDirs) {
            if (!fs::exists(itchDir)) continue;

            std::error_code ec;
            for (const auto& entry : fs::directory_iterator(itchDir, ec)) {
                if (!entry.is_directory()) continue;

                const auto fn = entry.path().filename().wstring();
                std::wstring lowerFn = fn;
                std::transform(lowerFn.begin(), lowerFn.end(), lowerFn.begin(), ::towlower);
                if (lowerFn == L"downloads" || lowerFn == L".itch" || lowerFn == L"games" || discoveredFolderNames.count(lowerFn) > 0) {
                    continue;
                }

                const fs::path exePath = FindPrimaryGameExecutable(entry.path());
                if (!exePath.empty()) {
                    GameItem item{};
                    item.appId = L"itch_" + fn;
                    item.title = CleanTitleFromFolderName(fn);
                    item.platform = PlatformType::Itch;
                    item.executablePath = exePath.wstring();
                    item.workingDirectory = exePath.parent_path().wstring();
                    item.launchUri = L"";
                    games.push_back(std::move(item));
                    discoveredFolderNames.insert(lowerFn);
                }
            }
        }
    }

    return games;
}

std::vector<GameItem> StorefrontScanner::ScanGOG() {
    std::vector<GameItem> games;

    const wchar_t* gogRegKeys[] = {
        L"SOFTWARE\\WOW6432Node\\GOG.com\\Games",
        L"SOFTWARE\\GOG.com\\Games"
    };

    for (const auto* rootSubKey : gogRegKeys) {
        HKEY hParent = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, rootSubKey, 0, KEY_READ, &hParent) != ERROR_SUCCESS) {
            continue;
        }

        wchar_t subKeyName[256];
        DWORD subKeyLen = 256;
        DWORD index = 0;

        while (RegEnumKeyExW(hParent, index++, subKeyName, &subKeyLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
            subKeyLen = 256;
            const std::wstring fullSubKey = std::wstring(rootSubKey) + L"\\" + subKeyName;

            const std::wstring gameId = ReadRegistryString(HKEY_LOCAL_MACHINE, fullSubKey.c_str(), L"gameID");
            const std::wstring gameName = ReadRegistryString(HKEY_LOCAL_MACHINE, fullSubKey.c_str(), L"gameName");
            const std::wstring exePath = ReadRegistryString(HKEY_LOCAL_MACHINE, fullSubKey.c_str(), L"exe");
            const std::wstring installPath = ReadRegistryString(HKEY_LOCAL_MACHINE, fullSubKey.c_str(), L"path");

            if (!gameName.empty()) {
                GameItem item{};
                item.appId = L"gog_" + (gameId.empty() ? std::wstring(subKeyName) : gameId);
                item.title = gameName;
                item.platform = PlatformType::GOG;
                item.workingDirectory = installPath;
                item.executablePath = exePath.empty() ? (installPath + L"\\" + subKeyName + L".exe") : exePath;
                games.push_back(std::move(item));
            }
        }

        RegCloseKey(hParent);
    }

    return games;
}

std::vector<GameItem> StorefrontScanner::ScanEpic() {
    std::vector<GameItem> games;

    wchar_t progData[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableW(L"PROGRAMDATA", progData, MAX_PATH) == 0) {
        wcscpy_s(progData, L"C:\\ProgramData");
    }
    const fs::path manifestsDir = fs::path(progData) / L"Epic" / L"EpicGamesLauncher" / L"Data" / L"Manifests";
    if (!fs::exists(manifestsDir)) return games;

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(manifestsDir, ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != L".item") {
            continue;
        }

        std::wifstream stream(entry.path());
        if (!stream.is_open()) continue;

        std::wstring line;
        std::wstring displayName, appName, installLocation, launchExe;

        while (std::getline(stream, line)) {
            std::wstring key, val;
            if (ParseKeyValueLine(line, key, val)) {
                if (key == L"DisplayName") displayName = val;
                else if (key == L"AppName") appName = val;
                else if (key == L"InstallLocation") installLocation = val;
                else if (key == L"LaunchExecutable") launchExe = val;
            }
        }

        if (!displayName.empty() && !appName.empty()) {
            GameItem item{};
            item.appId = L"epic_" + appName;
            item.title = displayName;
            item.platform = PlatformType::Epic;
            item.workingDirectory = installLocation;
            if (!launchExe.empty()) {
                item.executablePath = (fs::path(installLocation) / launchExe).wstring();
            }
            item.launchUri = L"com.epicgames.launcher://apps/" + appName + L"?action=launch&silent=true";
            EnrichFromExecutableVersionInfo(item);
            games.push_back(std::move(item));
        }
    }

    return games;
}

std::vector<GameItem> StorefrontScanner::ScanBattleNet() {
    std::vector<GameItem> games;
    std::set<std::wstring> seenDirs;

    const HKEY roots[] = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };
    const wchar_t* uninstallPaths[] = {
        L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"
    };

    for (HKEY root : roots) {
        for (const auto* uninstallSubKey : uninstallPaths) {
            HKEY hKey = nullptr;
            if (RegOpenKeyExW(root, uninstallSubKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) continue;
            wchar_t subName[256];
            DWORD idx = 0, nameLen = 256;
            while (RegEnumKeyExW(hKey, idx++, subName, &nameLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
                nameLen = 256;
                const std::wstring fullSub = std::wstring(uninstallSubKey) + L"\\" + subName;
                const std::wstring pub = ReadRegistryString(root, fullSub.c_str(), L"Publisher");
                const std::wstring displayName = ReadRegistryString(root, fullSub.c_str(), L"DisplayName");
                const std::wstring installLoc = ReadRegistryString(root, fullSub.c_str(), L"InstallLocation");
                if (pub.find(L"Blizzard") != std::wstring::npos && !displayName.empty() &&
                    displayName.find(L"Battle.net") == std::wstring::npos && !installLoc.empty() && fs::exists(installLoc)) {
                    const std::wstring normKey = NormalizeDirKey(installLoc);
                    if (!normKey.empty() && seenDirs.insert(normKey).second) {
                        const auto exe = FindPrimaryGameExecutable(installLoc);
                        GameItem item{};
                        item.appId = L"bnet_" + std::wstring(subName);
                        item.title = displayName;
                        item.platform = PlatformType::BattleNet;
                        item.developer = L"Blizzard Entertainment";
                        item.publisher = L"Blizzard Entertainment";
                        item.workingDirectory = installLoc;
                        if (!exe.empty()) item.executablePath = exe.wstring();
                        games.push_back(std::move(item));
                    }
                }
            }
            RegCloseKey(hKey);
        }
    }

    const auto drives = GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path bnetDirs[] = {
            fs::path(drive) / L"Games" / L"Battle.net",
            fs::path(drive) / L"Battle.net"
        };
        for (const auto& bnetDir : bnetDirs) {
            if (!fs::exists(bnetDir)) continue;
            std::error_code ec;
            for (const auto& entry : fs::directory_iterator(bnetDir, ec)) {
                if (!entry.is_directory()) continue;
                const std::wstring folderName = entry.path().filename().wstring();
                std::wstring lowerFolder = folderName;
                std::transform(lowerFolder.begin(), lowerFolder.end(), lowerFolder.begin(), ::towlower);
                if (lowerFolder.rfind(L"battle.net", 0) == 0 ||
                    lowerFolder == L".battle.net" ||
                    lowerFolder == L"blizzard error" ||
                    lowerFolder == L"agent" ||
                    lowerFolder == L"games") {
                    continue;
                }
                const std::wstring normKey = NormalizeDirKey(entry.path().wstring());
                if (normKey.empty() || !seenDirs.insert(normKey).second) continue;

                const auto exe = FindPrimaryGameExecutable(entry.path());
                if (!exe.empty()) {
                    GameItem item{};
                    item.appId = L"bnet_" + folderName;
                    item.title = folderName;
                    item.platform = PlatformType::BattleNet;
                    item.developer = L"Blizzard Entertainment";
                    item.publisher = L"Blizzard Entertainment";
                    item.executablePath = exe.wstring();
                    item.workingDirectory = entry.path().wstring();
                    games.push_back(std::move(item));
                }
            }
        }
    }

    return games;
}

std::vector<GameItem> StorefrontScanner::ScanLegacyGames() {
    std::vector<GameItem> games;
    std::set<std::wstring> seenDirs;

    wchar_t appData[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH) > 0) {
        const fs::path statePath = fs::path(appData) / L"legacy-games-launcher" / L"app-state.json";
        if (fs::exists(statePath)) {
            std::ifstream stream(statePath, std::ios::binary);
            if (stream.is_open()) {
                std::string raw((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
                const std::wstring json = Utf8ToWide(raw.c_str());
                size_t pos = 0;
                while ((pos = json.find(L"\"game_install_dir\"", pos)) != std::wstring::npos) {
                    const size_t q1 = FindNextUnescapedQuote(json, pos + 18);
                    const size_t q2 = (q1 != std::wstring::npos) ? FindNextUnescapedQuote(json, q1 + 1) : std::wstring::npos;
                    if (q1 != std::wstring::npos && q2 != std::wstring::npos) {
                        const std::wstring installDir = UnescapeJsonOrVdfString(json.substr(q1 + 1, q2 - q1 - 1));
                        if (!installDir.empty() && fs::exists(installDir)) {
                            const std::wstring normKey = NormalizeDirKey(installDir);
                            if (seenDirs.insert(normKey).second) {
                                const auto exe = FindPrimaryGameExecutable(installDir);
                                if (!exe.empty()) {
                                    GameItem item{};
                                    item.appId = L"legacy_" + fs::path(installDir).filename().wstring();
                                    item.title = fs::path(installDir).filename().wstring();
                                    item.platform = PlatformType::LegacyGames;
                                    item.publisher = L"Legacy Games";
                                    item.workingDirectory = installDir;
                                    item.executablePath = exe.wstring();
                                    EnrichFromExecutableVersionInfo(item);
                                    games.push_back(std::move(item));
                                }
                            }
                        }
                    }
                    pos += 18;
                }
            }
        }
    }

    return games;
}

std::vector<GameItem> StorefrontScanner::ScanJagex() {
    std::vector<GameItem> games;
    wchar_t localAppData[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH) == 0) return games;

    const fs::path runeliteExe = fs::path(localAppData) / L"RuneLite" / L"RuneLite.exe";
    if (fs::exists(runeliteExe)) {
        GameItem item{};
        item.appId = L"jagex_runelite";
        item.title = L"Old School RuneScape (RuneLite)";
        item.platform = PlatformType::Jagex;
        item.developer = L"Jagex / RuneLite";
        item.publisher = L"Jagex Ltd";
        item.rating = L"ESRB: Teen (13+)";
        item.genres = L"MMORPG, Adventure, Free to Play";
        item.executablePath = runeliteExe.wstring();
        item.workingDirectory = runeliteExe.parent_path().wstring();
        games.push_back(std::move(item));
    }

    const fs::path osrsJagexExe = fs::path(localAppData) / L"Jagex" / L"Old School RuneScape" / L"osclient.exe";
    if (fs::exists(osrsJagexExe)) {
        GameItem item{};
        item.appId = L"jagex_osrs";
        item.title = L"Old School RuneScape";
        item.platform = PlatformType::Jagex;
        item.developer = L"Jagex Ltd";
        item.publisher = L"Jagex Ltd";
        item.rating = L"ESRB: Teen (13+)";
        item.genres = L"MMORPG, Adventure";
        item.executablePath = osrsJagexExe.wstring();
        item.workingDirectory = osrsJagexExe.parent_path().wstring();
        games.push_back(std::move(item));
    }

    return games;
}

std::vector<GameItem> StorefrontScanner::ScanXIVLauncher() {
    std::vector<GameItem> games;
    wchar_t appData[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH) == 0) return games;

    const fs::path cfgPath = fs::path(appData) / L"XIVLauncher" / L"launcherConfigV3.json";
    if (!fs::exists(cfgPath)) return games;

    std::ifstream stream(cfgPath, std::ios::binary);
    if (!stream.is_open()) return games;
    std::string raw((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    const std::wstring json = Utf8ToWide(raw.c_str());

    const size_t pos = json.find(L"\"GamePath\"");
    if (pos != std::wstring::npos) {
        const size_t q1 = FindNextUnescapedQuote(json, pos + 10);
        const size_t q2 = (q1 != std::wstring::npos) ? FindNextUnescapedQuote(json, q1 + 1) : std::wstring::npos;
        if (q1 != std::wstring::npos && q2 != std::wstring::npos) {
            const std::wstring gameDir = UnescapeJsonOrVdfString(json.substr(q1 + 1, q2 - q1 - 1));
            if (!gameDir.empty() && fs::exists(gameDir)) {
                const fs::path boot64 = fs::path(gameDir) / L"boot" / L"ffxivboot64.exe";
                const fs::path game64 = fs::path(gameDir) / L"game" / L"ffxiv_dx11.exe";
                GameItem item{};
                item.appId = L"xivlauncher_ffxiv";
                item.title = L"FINAL FANTASY XIV Online (XIVLauncher)";
                item.platform = PlatformType::XIVLauncher;
                item.developer = L"Square Enix";
                item.publisher = L"Square Enix";
                item.rating = L"ESRB: Teen (13+)";
                item.releaseDate = L"Aug 27, 2013";
                item.genres = L"MMORPG, Role-Playing";
                item.synopsis = L"Take part in an epic and ever-changing FINAL FANTASY as you adventure and explore with friends from around the world.";
                item.workingDirectory = gameDir;
                item.executablePath = fs::exists(boot64) ? boot64.wstring() : (fs::exists(game64) ? game64.wstring() : L"");
                item.coverArtPath = CacheRemoteArtwork(item.appId, L"cover", L"https://cdn.cloudflare.steamstatic.com/steam/apps/39210/library_600x900.jpg");
                item.heroArtPath = CacheRemoteArtwork(item.appId, L"hero", L"https://cdn.cloudflare.steamstatic.com/steam/apps/39210/library_hero.jpg");
                games.push_back(std::move(item));
            }
        }
    }

    return games;
}

std::vector<GameItem> StorefrontScanner::ScanGuildWars2() {
    std::vector<GameItem> games;
    std::set<std::wstring> seenExes;

    auto addGw2Exe = [&](const fs::path& exePath) {
        if (!fs::exists(exePath)) return;
        const std::wstring norm = NormalizeDirKey(exePath.wstring());
        if (!seenExes.insert(norm).second) return;

        GameItem item{};
        item.appId = L"gw2_main";
        item.title = L"Guild Wars 2";
        item.platform = PlatformType::GuildWars2;
        item.developer = L"ArenaNet";
        item.publisher = L"NCSoft";
        item.rating = L"ESRB: Teen (13+)";
        item.releaseDate = L"Aug 28, 2012";
        item.genres = L"MMORPG, Action RPG";
        item.synopsis = L"Guild Wars 2 is an online role-playing game with fast-paced action combat, a rich and detailed universe of stories, and awe-inspiring landscapes to explore.";
        item.executablePath = exePath.wstring();
        item.workingDirectory = exePath.parent_path().wstring();
        item.coverArtPath = CacheRemoteArtwork(item.appId, L"cover", L"https://cdn.cloudflare.steamstatic.com/steam/apps/1284210/library_600x900.jpg");
        item.heroArtPath = CacheRemoteArtwork(item.appId, L"hero", L"https://cdn.cloudflare.steamstatic.com/steam/apps/1284210/library_hero.jpg");
        games.push_back(std::move(item));
    };

    wchar_t appData[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH) > 0) {
        const fs::path gfxXml = fs::path(appData) / L"Guild Wars 2" / L"GFXSettings.Gw2-64.exe.xml";
        if (fs::exists(gfxXml)) {
            std::wifstream stream(gfxXml);
            std::wstring line;
            while (std::getline(stream, line)) {
                const size_t pos = line.find(L"INSTALLPATH");
                if (pos != std::wstring::npos) {
                    const size_t vPos = line.find(L"Value=\"", pos);
                    if (vPos != std::wstring::npos) {
                        const size_t endQ = line.find(L'"', vPos + 7);
                        if (endQ != std::wstring::npos) {
                            const std::wstring dir = line.substr(vPos + 7, endQ - (vPos + 7));
                            addGw2Exe(fs::path(dir) / L"Gw2-64.exe");
                        }
                    }
                }
            }
        }
    }

    for (const auto& drive : GetAllMountedDrives()) {
        addGw2Exe(fs::path(drive) / L"Games" / L"Guild Wars 2" / L"Gw2-64.exe");
        addGw2Exe(fs::path(drive) / L"Guild Wars 2" / L"Gw2-64.exe");
        addGw2Exe(fs::path(drive) / L"Program Files" / L"Guild Wars 2" / L"Gw2-64.exe");
    }

    return games;
}

std::vector<GameItem> StorefrontScanner::ScanRiot() {
    std::vector<GameItem> games;
    std::set<std::wstring> seenDirs;

    wchar_t progData[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableW(L"PROGRAMDATA", progData, MAX_PATH) > 0) {
        const fs::path metaDir = fs::path(progData) / L"Riot Games" / L"Metadata";
        if (fs::exists(metaDir)) {
            std::error_code ec;
            for (const auto& entry : fs::directory_iterator(metaDir, ec)) {
                if (!entry.is_directory()) continue;
                for (const auto& file : fs::directory_iterator(entry.path(), ec)) {
                    if (file.path().extension() == L".yaml") {
                        std::wifstream stream(file.path());
                        std::wstring line;
                        while (std::getline(stream, line)) {
                            const size_t pos = line.find(L"product_install_full_path:");
                            if (pos != std::wstring::npos) {
                                std::wstring val = line.substr(pos + 26);
                                while (!val.empty() && (val.front() == L' ' || val.front() == L'"' || val.front() == L'\'')) val.erase(val.begin());
                                while (!val.empty() && (val.back() == L' ' || val.back() == L'"' || val.back() == L'\'' || val.back() == L'\r')) val.pop_back();
                                if (!val.empty() && fs::exists(val)) {
                                    const std::wstring norm = NormalizeDirKey(val);
                                    if (seenDirs.insert(norm).second) {
                                        const auto exe = FindPrimaryGameExecutable(val);
                                        if (!exe.empty()) {
                                            GameItem item{};
                                            item.appId = L"riot_" + fs::path(val).filename().wstring();
                                            item.title = fs::path(val).filename().wstring();
                                            item.platform = PlatformType::Riot;
                                            item.developer = L"Riot Games";
                                            item.publisher = L"Riot Games";
                                            item.workingDirectory = val;
                                            item.executablePath = exe.wstring();
                                            games.push_back(std::move(item));
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    return games;
}

std::vector<GameItem> StorefrontScanner::ScanHoYoPlay() {
    std::vector<GameItem> games;
    std::set<std::wstring> seenDirs;

    for (const auto& drive : GetAllMountedDrives()) {
        const fs::path hoyoDirs[] = {
            fs::path(drive) / L"Games" / L"HoYoPlay" / L"games",
            fs::path(drive) / L"Program Files" / L"HoYoPlay" / L"games",
            fs::path(drive) / L"Program Files" / L"Genshin Impact" / L"Genshin Impact game",
            fs::path(drive) / L"Program Files" / L"Star Rail" / L"Game",
            fs::path(drive) / L"Program Files" / L"ZenlessZoneZero Game"
        };
        for (const auto& dir : hoyoDirs) {
            if (!fs::exists(dir)) continue;
            const std::wstring norm = NormalizeDirKey(dir.wstring());
            if (!seenDirs.insert(norm).second) continue;
            const auto exe = FindPrimaryGameExecutable(dir);
            if (!exe.empty()) {
                GameItem item{};
                item.appId = L"hoyo_" + exe.stem().wstring();
                item.title = exe.stem().wstring();
                item.platform = PlatformType::HoYoPlay;
                item.developer = L"HoYoverse";
                item.publisher = L"COGNOSPHERE";
                item.workingDirectory = exe.parent_path().wstring();
                item.executablePath = exe.wstring();
                EnrichFromExecutableVersionInfo(item);
                games.push_back(std::move(item));
            }
        }
    }

    return games;
}

std::vector<GameItem> StorefrontScanner::ScanStandaloneAndDrives() {
    std::vector<GameItem> games;
    const auto drives = GetAllMountedDrives();

    for (const auto& drive : drives) {
        const fs::path ffxivDir = fs::path(drive) / L"Games" / L"Square Enix" / L"FINAL FANTASY XIV - A Realm Reborn";
        if (fs::exists(ffxivDir)) {
            const fs::path boot64 = ffxivDir / L"boot" / L"ffxivboot64.exe";
            const fs::path boot = ffxivDir / L"boot" / L"ffxivboot.exe";

            GameItem item{};
            item.appId = L"sqex_ffxiv";
            item.title = L"FINAL FANTASY XIV Online";
            item.platform = PlatformType::SquareEnix;
            item.developer = L"Square Enix";
            item.publisher = L"Square Enix";
            item.rating = L"ESRB: Teen (13+)";
            item.releaseDate = L"Aug 27, 2013";
            item.genres = L"MMORPG, Role-Playing";
            item.synopsis = L"Take part in an epic and ever-changing FINAL FANTASY as you adventure and explore with friends from around the world. Experience all the hallmarks of the best-selling franchise—an unforgettable story, exhilarating battles, and a myriad of diverse environments to explore.";
            item.workingDirectory = (fs::exists(boot64) || fs::exists(boot)) ? (ffxivDir / L"boot").wstring() : ffxivDir.wstring();
            item.executablePath = fs::exists(boot64) ? boot64.wstring() : (fs::exists(boot) ? boot.wstring() : L"");
            item.coverArtPath = CacheRemoteArtwork(item.appId, L"cover", L"https://cdn.cloudflare.steamstatic.com/steam/apps/39210/library_600x900.jpg");
            item.heroArtPath = CacheRemoteArtwork(item.appId, L"hero", L"https://cdn.cloudflare.steamstatic.com/steam/apps/39210/library_hero.jpg");
            games.push_back(std::move(item));
        }
    }

    return games;
}

std::vector<GameItem> StorefrontScanner::ScanUbisoft() {
    std::vector<GameItem> games;
    std::set<std::wstring> seenDirs;

    auto checkInstall = [&](HKEY root, const wchar_t* subKey) {
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(root, subKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            wchar_t name[256];
            DWORD idx = 0;
            DWORD nameLen = ARRAYSIZE(name);
            while (RegEnumKeyExW(hKey, idx, name, &nameLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
                const std::wstring itemSubKey = std::wstring(subKey) + L"\\" + name;
                std::wstring installDir = ReadRegistryString(root, itemSubKey.c_str(), L"InstallDir");
                if (!installDir.empty() && fs::exists(installDir)) {
                    std::wstring lower = installDir;
                    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
                    if (seenDirs.insert(lower).second) {
                        const auto exe = FindPrimaryGameExecutable(installDir);
                        GameItem item{};
                        item.appId = L"uplay_" + std::wstring(name);
                        item.title = fs::path(installDir).filename().wstring();
                        item.platform = PlatformType::Ubisoft;
                        item.workingDirectory = installDir;
                        if (!exe.empty()) {
                            item.executablePath = exe.wstring();
                        }
                        item.launchUri = L"uplay://launch/" + std::wstring(name) + L"/0";
                        games.push_back(std::move(item));
                    }
                }
                ++idx;
                nameLen = ARRAYSIZE(name);
            }
            RegCloseKey(hKey);
        }
    };

    checkInstall(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Ubisoft\\Launcher\\Installs");
    checkInstall(HKEY_CURRENT_USER, L"Software\\Ubisoft\\Launcher\\Installs");

    // Scan all mounted drives for Ubisoft library folders
    const auto drives = GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path ubiDirs[] = {
            fs::path(drive) / L"Games" / L"Ubisoft",
            fs::path(drive) / L"Ubisoft Games",
            fs::path(drive) / L"Ubisoft" / L"games"
        };
        for (const auto& dir : ubiDirs) {
            if (!fs::exists(dir)) continue;
            std::error_code ec;
            for (const auto& entry : fs::directory_iterator(dir, ec)) {
                if (!entry.is_directory()) continue;
                const std::wstring folderName = entry.path().filename().wstring();
                std::wstring lowerFolder = folderName;
                std::transform(lowerFolder.begin(), lowerFolder.end(), lowerFolder.begin(), ::towlower);
                if (lowerFolder == L"ubisoft game launcher" ||
                    lowerFolder.rfind(L"ubisoft", 0) == 0 ||
                    lowerFolder == L"games") {
                    continue;
                }
                std::wstring lower = entry.path().wstring();
                std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
                if (seenDirs.insert(lower).second) {
                    const auto exe = FindPrimaryGameExecutable(entry.path());
                    if (!exe.empty()) {
                        GameItem item{};
                        item.appId = L"uplay_" + entry.path().filename().wstring();
                        item.title = entry.path().filename().wstring();
                        item.platform = PlatformType::Ubisoft;
                        item.executablePath = exe.wstring();
                        item.workingDirectory = entry.path().wstring();
                        games.push_back(std::move(item));
                    }
                }
            }
        }
    }

    return games;
}

std::vector<GameItem> StorefrontScanner::ScanRockstar() {
    std::vector<GameItem> games;
    std::set<std::wstring> seenDirs;

    auto checkRockstar = [&](HKEY root, const wchar_t* subKey) {
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(root, subKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            wchar_t name[256];
            DWORD idx = 0;
            DWORD nameLen = ARRAYSIZE(name);
            while (RegEnumKeyExW(hKey, idx, name, &nameLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
                if (_wcsicmp(name, L"Launcher") != 0) {
                    const std::wstring itemSubKey = std::wstring(subKey) + L"\\" + name;
                    std::wstring installFolder = ReadRegistryString(root, itemSubKey.c_str(), L"InstallFolder");
                    if (installFolder.empty()) {
                        installFolder = ReadRegistryString(root, itemSubKey.c_str(), L"InstallDir");
                    }
                    if (!installFolder.empty() && fs::exists(installFolder)) {
                        std::wstring lower = installFolder;
                        std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
                        if (seenDirs.insert(lower).second) {
                            const auto exe = FindPrimaryGameExecutable(installFolder);
                            GameItem item{};
                            item.appId = L"rockstar_" + std::wstring(name);
                            item.title = name;
                            item.platform = PlatformType::Rockstar;
                            item.workingDirectory = installFolder;
                            if (!exe.empty()) {
                                item.executablePath = exe.wstring();
                            }
                            games.push_back(std::move(item));
                        }
                    }
                }
                ++idx;
                nameLen = ARRAYSIZE(name);
            }
            RegCloseKey(hKey);
        }
    };

    checkRockstar(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Rockstar Games");
    checkRockstar(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Rockstar Games");

    // Scan all mounted drives for Rockstar games
    const auto drives = GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path rDirs[] = {
            fs::path(drive) / L"Games" / L"Rockstar Games",
            fs::path(drive) / L"Rockstar Games",
            fs::path(drive) / L"Program Files" / L"Rockstar Games"
        };
        for (const auto& dir : rDirs) {
            if (!fs::exists(dir)) continue;
            std::error_code ec;
            for (const auto& entry : fs::directory_iterator(dir, ec)) {
                if (!entry.is_directory()) continue;
                if (_wcsicmp(entry.path().filename().c_str(), L"Launcher") == 0) continue;
                std::wstring lower = entry.path().wstring();
                std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
                if (seenDirs.insert(lower).second) {
                    const auto exe = FindPrimaryGameExecutable(entry.path());
                    if (!exe.empty()) {
                        GameItem item{};
                        item.appId = L"rockstar_" + entry.path().filename().wstring();
                        item.title = entry.path().filename().wstring();
                        item.platform = PlatformType::Rockstar;
                        item.executablePath = exe.wstring();
                        item.workingDirectory = entry.path().wstring();
                        games.push_back(std::move(item));
                    }
                }
            }
        }
    }

    return games;
}

std::vector<GameItem> StorefrontScanner::ScanEA() {
    std::vector<GameItem> games;
    std::set<std::wstring> seenDirs;

    // Check registry for EA Games
    auto checkEA = [&](HKEY root, const wchar_t* subKey) {
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(root, subKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            wchar_t name[256];
            DWORD idx = 0;
            DWORD nameLen = ARRAYSIZE(name);
            while (RegEnumKeyExW(hKey, idx, name, &nameLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
                const std::wstring itemSubKey = std::wstring(subKey) + L"\\" + name;
                std::wstring installDir = ReadRegistryString(root, itemSubKey.c_str(), L"Install Dir");
                if (installDir.empty()) {
                    installDir = ReadRegistryString(root, itemSubKey.c_str(), L"InstallDir");
                }
                if (!installDir.empty() && fs::exists(installDir)) {
                    std::wstring lower = installDir;
                    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
                    if (seenDirs.insert(lower).second) {
                        const auto exe = FindPrimaryGameExecutable(installDir);
                        GameItem item{};
                        item.appId = L"ea_" + std::wstring(name);
                        item.title = name;
                        item.platform = PlatformType::EAApp;
                        item.workingDirectory = installDir;
                        if (!exe.empty()) {
                            item.executablePath = exe.wstring();
                        }
                        games.push_back(std::move(item));
                    }
                }
                ++idx;
                nameLen = ARRAYSIZE(name);
            }
            RegCloseKey(hKey);
        }
    };

    checkEA(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\EA Games");
    checkEA(HKEY_LOCAL_MACHINE, L"SOFTWARE\\EA Games");
    checkEA(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Electronic Arts\\EA Games");

    // Scan all mounted drives for EA Games library folders
    const auto drives = GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path eaDirs[] = {
            fs::path(drive) / L"Games" / L"EA Games",
            fs::path(drive) / L"EA Games",
            fs::path(drive) / L"Program Files" / L"EA Games"
        };
        for (const auto& dir : eaDirs) {
            if (!fs::exists(dir)) continue;
            std::error_code ec;
            for (const auto& entry : fs::directory_iterator(dir, ec)) {
                if (!entry.is_directory()) continue;
                const std::wstring folderName = entry.path().filename().wstring();
                std::wstring lowerFolder = folderName;
                std::transform(lowerFolder.begin(), lowerFolder.end(), lowerFolder.begin(), ::towlower);
                if (lowerFolder == L"electronic arts" ||
                    lowerFolder == L"ea desktop" ||
                    lowerFolder == L"origin" ||
                    lowerFolder == L"games") {
                    continue;
                }
                std::wstring lower = entry.path().wstring();
                std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
                if (seenDirs.insert(lower).second) {
                    const auto exe = FindPrimaryGameExecutable(entry.path());
                    if (!exe.empty()) {
                        GameItem item{};
                        item.appId = L"ea_" + entry.path().filename().wstring();
                        item.title = entry.path().filename().wstring();
                        item.platform = PlatformType::EAApp;
                        item.executablePath = exe.wstring();
                        item.workingDirectory = entry.path().wstring();
                        games.push_back(std::move(item));
                    }
                }
            }
        }
    }

    return games;
}

std::vector<GameItem> StorefrontScanner::ScanAmazon() {
    std::vector<GameItem> games;
    std::set<std::wstring> seenDirs;

    // Amazon Games SQLite database
    wchar_t localAppData[MAX_PATH];
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH) > 0) {
        const fs::path dbPath = fs::path(localAppData) / L"Amazon Games" / L"Data" / L"Games" / L"Sql" / L"GameInstallInfo.sqlite";
        if (fs::exists(dbPath)) {
            sqlite3* db = nullptr;
            const std::string utf8Path = WideToUtf8(dbPath.wstring());
            if (sqlite3_open_v2(utf8Path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK) {
                sqlite3_stmt* stmt = nullptr;
                const char* sql = "SELECT Id, ProductTitle, InstallDirectory FROM GameInstallInfo;";
                if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                    while (sqlite3_step(stmt) == SQLITE_ROW) {
                        const char* rawId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                        const char* rawTitle = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                        const char* rawDir = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));

                        std::wstring id = Utf8ToWide(rawId);
                        std::wstring title = Utf8ToWide(rawTitle);
                        std::wstring dir = Utf8ToWide(rawDir);

                        if (!dir.empty() && fs::exists(dir)) {
                            std::wstring lower = dir;
                            std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
                            if (seenDirs.insert(lower).second) {
                                const auto exe = FindPrimaryGameExecutable(dir);
                                GameItem item{};
                                item.appId = L"amzn_" + id;
                                item.title = title.empty() ? fs::path(dir).filename().wstring() : title;
                                item.platform = PlatformType::Amazon;
                                item.workingDirectory = dir;
                                if (!exe.empty()) {
                                    item.executablePath = exe.wstring();
                                }
                                item.launchUri = L"amazon-games://play/" + id;
                                games.push_back(std::move(item));
                            }
                        }
                    }
                    sqlite3_finalize(stmt);
                }
                sqlite3_close(db);
            }
        }
    }

    // Also scan all mounted drives for Amazon Games folders
    const auto drives = GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path amznDirs[] = {
            fs::path(drive) / L"Games" / L"Amazon Games",
            fs::path(drive) / L"Amazon Games"
        };
        for (const auto& dir : amznDirs) {
            if (!fs::exists(dir)) continue;
            std::error_code ec;
            for (const auto& entry : fs::directory_iterator(dir, ec)) {
                if (!entry.is_directory()) continue;
                const std::wstring folderName = entry.path().filename().wstring();
                std::wstring lowerFolder = folderName;
                std::transform(lowerFolder.begin(), lowerFolder.end(), lowerFolder.begin(), ::towlower);
                if (lowerFolder == L"amazon games" ||
                    lowerFolder == L"app" ||
                    lowerFolder == L"games") {
                    continue;
                }
                std::wstring lower = entry.path().wstring();
                std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
                if (seenDirs.insert(lower).second) {
                    const auto exe = FindPrimaryGameExecutable(entry.path());
                    if (!exe.empty()) {
                        GameItem item{};
                        item.appId = L"amzn_" + entry.path().filename().wstring();
                        item.title = entry.path().filename().wstring();
                        item.platform = PlatformType::Amazon;
                        item.executablePath = exe.wstring();
                        item.workingDirectory = entry.path().wstring();
                        games.push_back(std::move(item));
                    }
                }
            }
        }
    }

    return games;
}

std::vector<GameItem> StorefrontScanner::ScanXbox() {
    std::vector<GameItem> games;
    std::set<std::wstring> seenDirs;

    // 1. Read <Drive>:\.GamingRoot on every mounted drive to discover the user's configured XboxGames root folder
    const auto drives = GetAllMountedDrives();
    for (const auto& drive : drives) {
        std::vector<fs::path> xboxRoots = { fs::path(drive) / L"XboxGames", fs::path(drive) / L"Games" / L"XboxGames" };
        const fs::path gamingRootFile = fs::path(drive) / L".GamingRoot";
        if (fs::exists(gamingRootFile)) {
            std::ifstream grStream(gamingRootFile, std::ios::binary);
            if (grStream.is_open()) {
                std::vector<char> bytes((std::istreambuf_iterator<char>(grStream)), std::istreambuf_iterator<char>());
                if (bytes.size() > 8 && bytes[0] == 'R' && bytes[1] == 'G' && bytes[2] == 'B' && bytes[3] == 'X') {
                    const wchar_t* widePtr = reinterpret_cast<const wchar_t*>(bytes.data() + 8);
                    const size_t wideLen = (bytes.size() - 8) / sizeof(wchar_t);
                    std::wstring relPath(widePtr, wideLen);
                    while (!relPath.empty() && relPath.back() == L'\0') relPath.pop_back();
                    if (!relPath.empty()) {
                        xboxRoots.insert(xboxRoots.begin(), fs::path(drive) / relPath);
                    }
                }
            }
        }

        for (const auto& xboxDir : xboxRoots) {
            if (!fs::exists(xboxDir)) continue;
            std::error_code ec;
            for (const auto& entry : fs::directory_iterator(xboxDir, ec)) {
                if (!entry.is_directory()) continue;
                const auto gameDir = entry.path();
                const std::wstring fn = gameDir.filename().wstring();
                if (_wcsicmp(fn.c_str(), L"GameSave") == 0) continue;

                const std::wstring lower = NormalizeDirKey(gameDir.wstring());
                if (!seenDirs.insert(lower).second) continue;

                const fs::path contentDir = gameDir / L"Content";
                const fs::path searchDir = fs::exists(contentDir) ? contentDir : gameDir;

                const fs::path launcherHelper = searchDir / L"gamelaunchhelper.exe";
                fs::path targetExe = fs::exists(launcherHelper) ? launcherHelper : FindPrimaryGameExecutable(searchDir);

                if (!targetExe.empty()) {
                    GameItem item{};
                    item.appId = L"xbox_" + fn;
                    item.title = fn;
                    item.platform = PlatformType::Xbox;
                    item.publisher = L"Xbox Game Studios";
                    item.executablePath = targetExe.wstring();
                    item.workingDirectory = targetExe.parent_path().wstring();
                    EnrichFromExecutableVersionInfo(item);
                    games.push_back(std::move(item));
                }
            }
        }
    }

    // 2. Scan AppX/MSIX packages in HKCU\Software\Classes\Local Settings\Software\Microsoft\Windows\CurrentVersion\AppModel\Repository\Packages
    const wchar_t* packagesKeyPath = L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\AppModel\\Repository\\Packages";
    HKEY hPackages = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, packagesKeyPath, 0, KEY_READ, &hPackages) == ERROR_SUCCESS) {
        wchar_t pkgName[256];
        DWORD idx = 0, pkgLen = 256;
        while (RegEnumKeyExW(hPackages, idx++, pkgName, &pkgLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
            pkgLen = 256;
            const std::wstring pkgStr = pkgName;
            if (pkgStr.rfind(L"Microsoft.GamingApp_", 0) == 0 ||
                pkgStr.rfind(L"Microsoft.XboxGamingOverlay_", 0) == 0 ||
                pkgStr.rfind(L"Microsoft.XboxGameOverlay_", 0) == 0 ||
                pkgStr.rfind(L"Microsoft.XboxIdentityProvider_", 0) == 0 ||
                pkgStr.rfind(L"Microsoft.XboxSpeechToTextOverlay_", 0) == 0 ||
                pkgStr.rfind(L"Microsoft.Xbox.TCUI_", 0) == 0 ||
                pkgStr.rfind(L"Microsoft.GamingServices_", 0) == 0) {
                continue;
            }

            const std::wstring fullSub = std::wstring(packagesKeyPath) + L"\\" + pkgStr;
            const std::wstring rootFolder = ReadRegistryString(HKEY_CURRENT_USER, fullSub.c_str(), L"PackageRootFolder");
            if (rootFolder.empty() || !fs::exists(rootFolder)) continue;

            const fs::path rootPath(rootFolder);
            const bool isXboxGame =
                fs::exists(rootPath / L"xboxservices.config") ||
                fs::exists(rootPath / L"XboxServices.config") ||
                fs::exists(rootPath / L"MicrosoftGame.config") ||
                fs::exists(rootPath / L"Microsoft.Xbox.Services.dll");
            if (!isXboxGame) continue;

            const std::wstring norm = NormalizeDirKey(rootFolder);
            if (!seenDirs.insert(norm).second) continue;

            std::wstring displayName, publisherName, appIdAttr = L"App", exeRelPath;
            const fs::path manifestPath = rootPath / L"AppxManifest.xml";
            if (fs::exists(manifestPath)) {
                std::ifstream mStream(manifestPath, std::ios::binary);
                std::string rawXml((std::istreambuf_iterator<char>(mStream)), std::istreambuf_iterator<char>());
                const std::wstring xml = Utf8ToWide(rawXml.c_str());

                auto extractTag = [&](const std::wstring& tag) -> std::wstring {
                    const size_t openPos = xml.find(L"<" + tag + L">");
                    const size_t closePos = (openPos != std::wstring::npos) ? xml.find(L"</" + tag + L">", openPos) : std::wstring::npos;
                    if (openPos != std::wstring::npos && closePos != std::wstring::npos) {
                        const size_t start = openPos + tag.size() + 2;
                        return xml.substr(start, closePos - start);
                    }
                    return L"";
                };
                displayName = extractTag(L"DisplayName");
                publisherName = extractTag(L"PublisherDisplayName");

                const size_t appPos = xml.find(L"<Application ");
                if (appPos != std::wstring::npos) {
                    const size_t endApp = xml.find(L">", appPos);
                    const std::wstring appTag = xml.substr(appPos, endApp - appPos);
                    const size_t idPos = appTag.find(L"Id=\"");
                    if (idPos != std::wstring::npos) {
                        const size_t idEnd = appTag.find(L'"', idPos + 4);
                        if (idEnd != std::wstring::npos) appIdAttr = appTag.substr(idPos + 4, idEnd - (idPos + 4));
                    }
                    const size_t exePos = appTag.find(L"Executable=\"");
                    if (exePos != std::wstring::npos) {
                        const size_t exeEnd = appTag.find(L'"', exePos + 12);
                        if (exeEnd != std::wstring::npos) exeRelPath = appTag.substr(exePos + 12, exeEnd - (exePos + 12));
                    }
                }
            }

            const size_t firstUnderscore = pkgStr.find(L'_');
            const std::wstring pkgIdPrefix = (firstUnderscore != std::wstring::npos) ? pkgStr.substr(0, firstUnderscore) : pkgStr;
            const size_t lastUnderscore = pkgStr.rfind(L'_');
            const std::wstring pkgFamilyName = (firstUnderscore != std::wstring::npos && lastUnderscore != std::wstring::npos && lastUnderscore > firstUnderscore)
                ? (pkgIdPrefix + pkgStr.substr(lastUnderscore))
                : pkgStr;

            if (displayName.empty() || displayName.rfind(L"ms-resource:", 0) == 0) {
                if (pkgIdPrefix == L"Microsoft.MicrosoftSolitaireCollection") {
                    displayName = L"Microsoft Solitaire Collection";
                } else {
                    const size_t dot = pkgIdPrefix.rfind(L'.');
                    displayName = (dot != std::wstring::npos) ? pkgIdPrefix.substr(dot + 1) : pkgIdPrefix;
                }
            }

            fs::path bestCover, bestHero, bestIcon;
            uintmax_t bestCoverSize = 0, bestHeroSize = 0, bestIconSize = 0;
            std::error_code ec;
            for (const auto& entry : fs::recursive_directory_iterator(rootPath, fs::directory_options::skip_permission_denied, ec)) {
                if (!entry.is_regular_file()) continue;
                const std::wstring ext = entry.path().extension().wstring();
                if (_wcsicmp(ext.c_str(), L".png") != 0 && _wcsicmp(ext.c_str(), L".jpg") != 0) continue;
                const std::wstring fn = entry.path().filename().wstring();
                if (fn.find(L"contrast-") != std::wstring::npos) continue;
                // Skip transparent SplashScreen overlays so full-bleed tiles are chosen for Cover & Hero
                if (fn.find(L"SplashScreen") != std::wstring::npos) continue;

                const uintmax_t sz = entry.file_size(ec);
                if (fn.find(L"Wide") != std::wstring::npos) {
                    if (sz > bestHeroSize) {
                        bestHeroSize = sz;
                        bestHero = entry.path();
                    }
                } else {
                    if (sz > bestCoverSize) {
                        bestCoverSize = sz;
                        bestCover = entry.path();
                    }
                    if (fn.find(L"AppList") != std::wstring::npos && sz > bestIconSize) {
                        bestIconSize = sz;
                        bestIcon = entry.path();
                    }
                }
            }

            GameItem item{};
            item.appId = L"xbox_" + pkgIdPrefix;
            item.title = displayName;
            item.platform = PlatformType::Xbox;
            item.developer = publisherName.empty() ? L"Xbox Game Studios" : publisherName;
            item.publisher = publisherName.empty() ? L"Microsoft Studios" : publisherName;
            item.rating = L"ESRB: Everyone";
            item.genres = L"Xbox Live, Casual";
            item.workingDirectory = rootFolder;
            if (!exeRelPath.empty() && fs::exists(rootPath / exeRelPath)) {
                item.executablePath = (rootPath / exeRelPath).wstring();
            } else {
                const auto exe = FindPrimaryGameExecutable(rootPath);
                if (!exe.empty()) item.executablePath = exe.wstring();
            }
            item.launchUri = L"shell:AppsFolder\\" + pkgFamilyName + L"!" + appIdAttr;
            if (!bestCover.empty()) item.coverArtPath = bestCover.wstring();
            if (!bestHero.empty()) item.heroArtPath = bestHero.wstring();
            if (!bestIcon.empty()) item.iconPath = bestIcon.wstring();
            games.push_back(std::move(item));
        }
        RegCloseKey(hPackages);
    }

    return games;
}

std::vector<GameItem> StorefrontScanner::ScanCustomGames() {
    std::vector<GameItem> games;
    const fs::path metaDir = Storage::GetMetadataPath();
    if (!fs::exists(metaDir)) return games;

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(metaDir, ec)) {
        if (!entry.is_regular_file()) continue;
        const auto filename = entry.path().filename().wstring();
        if (filename.rfind(L"custom_", 0) == 0 && entry.path().extension() == L".json") {
            GameItem item{};
            item.appId = entry.path().stem().wstring();
            item.platform = PlatformType::CustomExecutable;
            if (LoadPerGameMetadata(item) && !item.executablePath.empty() && fs::exists(item.executablePath)) {
                if (item.title.empty()) {
                    item.title = fs::path(item.executablePath).stem().wstring();
                }
                EnrichFromExecutableVersionInfo(item);
                games.push_back(std::move(item));
            }
        }
    }

    return games;
}

bool StorefrontScanner::AddCustomLocalGame(const std::wstring& executablePath, const std::wstring& customTitle) {
    if (executablePath.empty() || !fs::exists(executablePath)) return false;

    Storage::EnsureDirectoriesCreated();
    const auto now = GetTickCount64();
    GameItem item{};
    item.appId = L"custom_" + std::to_wstring(now);
    item.title = customTitle.empty() ? fs::path(executablePath).stem().wstring() : customTitle;
    item.platform = PlatformType::CustomExecutable;
    item.executablePath = executablePath;
    item.workingDirectory = fs::path(executablePath).parent_path().wstring();
    EnrichFromExecutableVersionInfo(item);

    SavePerGameMetadata(item);
    SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_PATHW, Storage::GetUserRootPath().c_str(), nullptr);
    return true;
}

bool StorefrontScanner::RemoveCustomLocalGame(const std::wstring& appId) {
    if (appId.empty()) return false;
    const fs::path metaDir = Storage::GetMetadataPath();
    const fs::path jsonPath = metaDir / (appId + L".json");
    std::error_code ec;
    if (fs::exists(jsonPath, ec) && fs::remove(jsonPath, ec)) {
        SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_PATHW, Storage::GetUserRootPath().c_str(), nullptr);
        return true;
    }
    return false;
}

bool StorefrontScanner::LoadPerGameMetadata(GameItem& item) {
    const fs::path metaDir = Storage::GetMetadataPath();
    const fs::path jsonPath = metaDir / (item.appId + L".json");
    if (!fs::exists(jsonPath)) return false;

    std::wifstream stream(jsonPath);
    if (!stream.is_open()) return false;

    auto isValidPathOrUrl = [](const std::wstring& val) -> bool {
        if (val.empty()) return false;
        if (val.rfind(L"http://", 0) == 0 || val.rfind(L"https://", 0) == 0) return true;
        std::error_code ec;
        return fs::exists(val, ec);
    };

    std::wstring line;
    while (std::getline(stream, line)) {
        std::wstring key, val;
        if (ParseKeyValueLine(line, key, val)) {
            if (key == L"title" && !val.empty()) item.title = val;
            else if (key == L"developer" && !val.empty()) item.developer = val;
            else if (key == L"publisher" && !val.empty()) item.publisher = val;
            else if (key == L"releaseDate" && !val.empty()) item.releaseDate = val;
            else if ((key == L"rating" || key == L"score") && !val.empty()) item.rating = val;
            else if ((key == L"synopsis" || key == L"description") && !val.empty()) item.synopsis = val;
            else if (key == L"genres" && !val.empty()) item.genres = val;
            else if ((key == L"coverArtPath" || key == L"cover") && isValidPathOrUrl(val)) item.coverArtPath = val;
            else if ((key == L"heroArtPath" || key == L"hero") && isValidPathOrUrl(val)) item.heroArtPath = val;
            else if ((key == L"iconPath" || key == L"icon") && isValidPathOrUrl(val)) item.iconPath = val;
            else if ((key == L"launchString" || key == L"launchCommand") && !val.empty()) item.launchCommand = val;
            else if (key == L"launchArguments" && !val.empty()) item.launchArguments = val;
            else if (key == L"executablePath" && !val.empty() && fs::exists(val)) item.executablePath = val;
            else if (key == L"workingDirectory" && !val.empty() && fs::exists(val)) item.workingDirectory = val;
        }
    }
    return true;
}

bool StorefrontScanner::SavePerGameMetadata(const GameItem& item) {
    Storage::EnsureDirectoriesCreated();
    const fs::path metaDir = Storage::GetMetadataPath();
    const fs::path jsonPath = metaDir / (item.appId + L".json");

    std::wofstream out(jsonPath);
    if (!out.is_open()) return false;

    const auto platStr = PlatformToString(item.platform);

    out << L"{\n";
    out << L"  \"appId\": \"" << EscapeJsonString(item.appId) << L"\",\n";
    out << L"  \"title\": \"" << EscapeJsonString(item.title) << L"\",\n";
    out << L"  \"platform\": \"" << EscapeJsonString(std::wstring(platStr)) << L"\",\n";
    out << L"  \"description\": \"" << EscapeJsonString(item.synopsis) << L"\",\n";
    out << L"  \"score\": \"" << EscapeJsonString(item.rating) << L"\",\n";
    out << L"  \"developer\": \"" << EscapeJsonString(item.developer) << L"\",\n";
    out << L"  \"publisher\": \"" << EscapeJsonString(item.publisher) << L"\",\n";
    out << L"  \"releaseDate\": \"" << EscapeJsonString(item.releaseDate) << L"\",\n";
    out << L"  \"genres\": \"" << EscapeJsonString(item.genres) << L"\",\n";
    out << L"  \"launchString\": \"" << EscapeJsonString(item.launchCommand.empty() ? item.launchUri : item.launchCommand) << L"\",\n";
    out << L"  \"launchArguments\": \"" << EscapeJsonString(item.launchArguments) << L"\",\n";
    out << L"  \"executablePath\": \"" << EscapeJsonString(item.executablePath) << L"\",\n";
    out << L"  \"workingDirectory\": \"" << EscapeJsonString(item.workingDirectory) << L"\",\n";
    out << L"  \"iconPath\": \"" << EscapeJsonString(item.iconPath) << L"\",\n";
    out << L"  \"coverArtPath\": \"" << EscapeJsonString(item.coverArtPath) << L"\",\n";
    out << L"  \"heroArtPath\": \"" << EscapeJsonString(item.heroArtPath) << L"\"\n";
    out << L"}\n";
    return true;
}

std::vector<GameItem> StorefrontScanner::ScanAllInstalledGames() {
    Storage::EnsureDirectoriesCreated();
    std::vector<GameItem> allGames;

    auto appendAll = [&](std::vector<GameItem>&& items) {
        allGames.insert(allGames.end(), std::make_move_iterator(items.begin()), std::make_move_iterator(items.end()));
    };

    appendAll(ScanSteam());
    appendAll(ScanEpic());
    appendAll(ScanGOG());
    appendAll(ScanXbox());
    appendAll(ScanEA());
    appendAll(ScanUbisoft());
    appendAll(ScanBattleNet());
    appendAll(ScanItch());
    appendAll(ScanAmazon());
    appendAll(ScanLegacyGames());
    appendAll(ScanJagex());
    appendAll(ScanXIVLauncher());
    appendAll(ScanGuildWars2());
    appendAll(ScanRockstar());
    appendAll(ScanRiot());
    appendAll(ScanHoYoPlay());
    appendAll(ScanCustomGames());
    appendAll(ScanStandaloneAndDrives());

    std::vector<GameItem> deduped;
    std::set<std::wstring> seenAppIds;
    std::set<std::wstring> seenExePaths;
    std::set<std::wstring> seenPlatformTitles;

    for (auto& game : allGames) {
        if (!game.appId.empty() && !seenAppIds.insert(game.appId).second) {
            continue;
        }

        if (!game.executablePath.empty()) {
            const std::wstring normExe = NormalizeDirKey(game.executablePath);
            if (!normExe.empty() && !seenExePaths.insert(normExe).second) {
                continue;
            }
        }

        std::wstring platTitleKey = std::wstring(PlatformToString(game.platform)) + L"|" + game.title;
        std::transform(platTitleKey.begin(), platTitleKey.end(), platTitleKey.begin(), ::towlower);
        if (!seenPlatformTitles.insert(platTitleKey).second) {
            continue;
        }

        game.id = static_cast<int64_t>(deduped.size() + 1);
        LoadPerGameMetadata(game);
        if (game.developer.empty() || game.publisher.empty()) {
            EnrichFromExecutableVersionInfo(game);
        }
        SavePerGameMetadata(game);
        deduped.push_back(std::move(game));
    }

    return deduped;
}

} // namespace GameExplorer
