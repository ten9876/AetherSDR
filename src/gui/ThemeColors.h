#pragma once

// Centralized color palette definitions and theme management for AetherSDR.
// Two built-in palettes — Dark (default, matches existing look) and Light.
// Use ThemeManager::instance() to get the active palette and connect to
// themeChanged() for live updates.

#include <QColor>
#include <QObject>
#include <QString>

namespace AetherSDR {

struct ThemePalette {
    // Core
    QColor appBackground;       // Main window dark surface
    QColor textPrimary;         // Standard labels, values
    QColor textSecondary;       // Dim labels, slider row labels
    QColor textTertiary;        // Section keywords
    QColor textScale;           // EQ dB scale, minor annotations
    QColor textInactive;        // Disabled indicators
    QColor accent;              // Slider handles, active fills
    QColor titleBarText;        // Applet title labels

    // Borders & Surfaces
    QColor buttonBase;          // Default button/combo bg
    QColor buttonHover;         // Hover state
    QColor buttonAltHover;      // Hover variant (P/CW)
    QColor borderStandard;      // Button/combo borders
    QColor borderSubtle;        // Secondary borders
    QColor insetBackground;     // Value readout boxes
    QColor insetBorder;         // Readout box borders, dividers
    QColor titleGradientTop;
    QColor titleGradientMid;
    QColor titleGradientBot;
    QColor titleBorder;         // Title bar bottom border
    QColor groove;              // Slider groove bg

    // Window chrome
    QColor menuBarBg;           // Menu bar background
    QColor statusBarBg;         // Status bar background
    QColor listBg;              // List widget background
    QColor listAltBg;           // Alternate row background
    QColor tabBg;               // Inactive tab background
    QColor tabBorder;           // Tab and pane border
    QColor splitterHandle;      // Splitter handle color

    // Active/checked button states (same in both themes)
    QColor greenBg;
    QColor greenText;
    QColor greenBorder;
    QColor blueBg;
    QColor blueText;
    QColor blueBorder;
    QColor amberBg;
    QColor amberText;
    QColor amberBorder;
};

// Dark palette — pixel-identical to the existing hardcoded colors.
inline ThemePalette darkPalette()
{
    ThemePalette p;
    p.appBackground     = QColor("#0f0f1a");
    p.textPrimary       = QColor("#c8d8e8");
    p.textSecondary     = QColor("#8090a0");
    p.textTertiary      = QColor("#708090");
    p.textScale          = QColor("#607080");
    p.textInactive       = QColor("#405060");
    p.accent             = QColor("#00b4d8");
    p.titleBarText       = QColor("#8aa8c0");

    p.buttonBase         = QColor("#1a2a3a");
    p.buttonHover        = QColor("#203040");
    p.buttonAltHover     = QColor("#204060");
    p.borderStandard     = QColor("#205070");
    p.borderSubtle       = QColor("#203040");
    p.insetBackground    = QColor("#0a0a18");
    p.insetBorder        = QColor("#1e2e3e");
    p.titleGradientTop   = QColor("#3a4a5a");
    p.titleGradientMid   = QColor("#2a3a4a");
    p.titleGradientBot   = QColor("#1a2a38");
    p.titleBorder        = QColor("#0a1a28");
    p.groove             = QColor("#203040");

    p.menuBarBg          = QColor("#0a0a14");
    p.statusBarBg        = QColor("#0a0a14");
    p.listBg             = QColor("#111120");
    p.listAltBg          = QColor("#161626");
    p.tabBg              = QColor("#1a2a3a");
    p.tabBorder          = QColor("#304050");
    p.splitterHandle     = QColor("#203040");

    p.greenBg            = QColor("#006040");
    p.greenText          = QColor("#00ff88");
    p.greenBorder        = QColor("#00a060");
    p.blueBg             = QColor("#0070c0");
    p.blueText           = QColor("#ffffff");
    p.blueBorder         = QColor("#0090e0");
    p.amberBg            = QColor("#604000");
    p.amberText          = QColor("#ffb800");
    p.amberBorder        = QColor("#906000");

    return p;
}

// Light palette — readable interface with sufficient contrast.
// Spectrum and waterfall widgets ignore this and stay dark.
inline ThemePalette lightPalette()
{
    ThemePalette p;
    p.appBackground     = QColor("#e8edf2");
    p.textPrimary       = QColor("#1a1a2a");
    p.textSecondary     = QColor("#4a5568");
    p.textTertiary      = QColor("#5a6a7a");
    p.textScale          = QColor("#6a7a8a");
    p.textInactive       = QColor("#9aa0a8");
    p.accent             = QColor("#0090b0");  // slightly deeper for light bg
    p.titleBarText       = QColor("#2a4a60");

    p.buttonBase         = QColor("#d0d8e0");
    p.buttonHover        = QColor("#bcc6d0");
    p.buttonAltHover     = QColor("#b0c0d0");
    p.borderStandard     = QColor("#9ab0c0");
    p.borderSubtle       = QColor("#c0c8d0");
    p.insetBackground    = QColor("#f4f6f8");
    p.insetBorder        = QColor("#b8c4d0");
    p.titleGradientTop   = QColor("#c8d0d8");
    p.titleGradientMid   = QColor("#b8c4d0");
    p.titleGradientBot   = QColor("#aab4c0");
    p.titleBorder        = QColor("#98a4b0");
    p.groove             = QColor("#c0c8d0");

    p.menuBarBg          = QColor("#dce2e8");
    p.statusBarBg        = QColor("#dce2e8");
    p.listBg             = QColor("#f0f2f4");
    p.listAltBg          = QColor("#e4e8ec");
    p.tabBg              = QColor("#d0d8e0");
    p.tabBorder          = QColor("#a0b0c0");
    p.splitterHandle     = QColor("#b0b8c0");

    // Active/checked states: keep same hue, slightly adjusted for contrast
    p.greenBg            = QColor("#d0f0e0");
    p.greenText          = QColor("#006840");
    p.greenBorder        = QColor("#40b070");
    p.blueBg             = QColor("#d0e8ff");
    p.blueText           = QColor("#004080");
    p.blueBorder         = QColor("#4090d0");
    p.amberBg            = QColor("#fff0d0");
    p.amberText          = QColor("#805000");
    p.amberBorder        = QColor("#c09040");

    return p;
}

// Singleton that holds the active palette and notifies widgets on change.
class ThemeManager : public QObject {
    Q_OBJECT

public:
    static ThemeManager& instance()
    {
        static ThemeManager s;
        return s;
    }

    const ThemePalette& palette() const { return m_palette; }
    QString themeName() const { return m_name; }
    bool isDark() const { return m_name == QStringLiteral("Dark"); }

    void setTheme(const QString& name)
    {
        if (name == m_name) return;
        m_name = name;
        m_palette = (name == QStringLiteral("Light")) ? lightPalette() : darkPalette();
        emit themeChanged();
    }

signals:
    void themeChanged();

private:
    ThemeManager() : m_name("Dark"), m_palette(darkPalette()) {}
    ThemeManager(const ThemeManager&) = delete;
    ThemeManager& operator=(const ThemeManager&) = delete;

    QString m_name;
    ThemePalette m_palette;
};

} // namespace AetherSDR
