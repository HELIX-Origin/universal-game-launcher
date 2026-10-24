#pragma once

#include <windows.h>

namespace GameExplorer {

struct ThemePalette {
    COLORREF canvasBg;
    COLORREF drawerBg;
    COLORREF cardBg;
    COLORREF cardBorder;
    COLORREF cardSelectedBg;
    COLORREF cardSelectedBorder;
    COLORREF textPrimary;
    COLORREF textSecondary;
    COLORREF divider;
    COLORREF buttonBg;
    COLORREF buttonText;
    COLORREF accent;
    COLORREF accentHover;
    COLORREF accentPressed;
    COLORREF accentText;
};

class DarkMode {
public:
    static void Initialize();
    static bool IsSystemDarkMode();
    static COLORREF GetSystemAccentColor();
    static void ApplyWindowTheme(HWND hwnd, bool darkMode);
    static ThemePalette GetPalette(bool isDarkMode);
};

} // namespace GameExplorer
