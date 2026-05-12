#include "ThemeManager.h"
#include "IconProvider.h"

#include <QGuiApplication>
#include <QPalette>
#include <QStyleHints>

namespace powsys365 {
namespace icon {

// ============================================================
// Constructor
// ============================================================
ThemeManager::ThemeManager(QObject* parent)
    : QObject(parent) {
    // Auto-detect system theme
    autoDetectSystemTheme();
}

ThemeManager& ThemeManager::instance() {
    static ThemeManager mgr;
    return mgr;
}

// ============================================================
// Dark Mode
// ============================================================
void ThemeManager::setDarkMode(bool dark) {
    if (m_darkMode == dark) return;

    m_darkMode = dark;

    if (m_darkMode) {
        buildDarkPalette();
    } else {
        buildLightPalette();
    }

    emit themeChanged(m_darkMode);
    emit paletteChanged();

    // Invalidate icon cache on theme change
    IconRenderer::instance().clearCache();
}

void ThemeManager::toggleDarkMode() {
    setDarkMode(!m_darkMode);
}

// ============================================================
// Auto-detect System Theme
// ============================================================
void ThemeManager::autoDetectSystemTheme() {
    bool systemDark = false;

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (QGuiApplication::styleHints()) {
        systemDark = (QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark);
    }
#else
    // Fallback: check application palette
    const QPalette& pal = QGuiApplication::palette();
    QColor windowColor = pal.color(QPalette::Window);
    int luminance = (windowColor.red() * 299 + windowColor.green() * 587 + windowColor.blue() * 114) / 1000;
    systemDark = luminance < 128;
#endif

    m_darkMode = systemDark;

    if (m_darkMode) {
        buildDarkPalette();
    } else {
        buildLightPalette();
    }
}

// ============================================================
// Light Palette (Apple-inspired)
// ============================================================
void ThemeManager::buildLightPalette() {
    m_palette.brand            = QColor("#007AFF");
    m_palette.brandSecondary   = QColor("#5856D6");

    m_palette.background       = QColor("#F2F2F7");
    m_palette.surface          = QColor("#FFFFFF");
    m_palette.elevated         = QColor("#FFFFFF");
    m_palette.overlay          = QColor("#000000");

    m_palette.textPrimary      = QColor("#000000");
    m_palette.textSecondary    = QColor("#3C3C43");
    m_palette.textTertiary     = QColor("#8E8E93");
    m_palette.textDisabled     = QColor("#C7C7CC");

    m_palette.accent           = QColor("#007AFF");
    m_palette.accentHover      = QColor("#0056D6");
    m_palette.accentPressed    = QColor("#0044AA");

    m_palette.border           = QColor("#E5E5EA");
    m_palette.borderFocus      = QColor("#007AFF");
    m_palette.divider          = QColor("#E5E5EA");

    // Icon state colors (Apple system colors)
    m_palette.iconActive       = QColor(AppleColors::RED);      // #FF3B30
    m_palette.iconInactive     = QColor(AppleColors::GREEN);    // #34C759
    m_palette.iconDisabled     = QColor(AppleColors::GRAY);     // #8E8E93
    m_palette.iconLoading      = QColor(AppleColors::YELLOW);   // #FFCC00
    m_palette.iconWarning      = QColor(AppleColors::ORANGE);   // #FF9500
    m_palette.iconDiagnostic   = QColor(AppleColors::BLUE);     // #007AFF
    m_palette.iconSuccess      = QColor(AppleColors::DARK_GREEN);// #30D158
    m_palette.iconCritical     = QColor(AppleColors::DARK_RED); // #FF2D55
    m_palette.iconInfo         = QColor(AppleColors::CYAN);     // #5AC8FA

    // Status colors
    m_palette.statusOnline     = QColor(AppleColors::GREEN);
    m_palette.statusOffline    = QColor(AppleColors::GRAY);
    m_palette.statusWarning    = QColor(AppleColors::ORANGE);
    m_palette.statusError      = QColor(AppleColors::RED);

    // Semantic
    m_palette.destructive      = QColor(AppleColors::RED);
    m_palette.constructive     = QColor(AppleColors::GREEN);
}

// ============================================================
// Dark Palette (Apple-inspired)
// ============================================================
void ThemeManager::buildDarkPalette() {
    m_palette.brand            = QColor("#0A84FF");
    m_palette.brandSecondary   = QColor("#5E5CE6");

    m_palette.background       = QColor("#000000");
    m_palette.surface          = QColor("#1C1C1E");
    m_palette.elevated         = QColor("#2C2C2E");
    m_palette.overlay          = QColor("#FFFFFF");

    m_palette.textPrimary      = QColor("#FFFFFF");
    m_palette.textSecondary    = QColor("#EBEBF5");
    m_palette.textTertiary     = QColor("#8E8E93");
    m_palette.textDisabled     = QColor("#48484A");

    m_palette.accent           = QColor("#0A84FF");
    m_palette.accentHover      = QColor("#3399FF");
    m_palette.accentPressed    = QColor("#0066CC");

    m_palette.border           = QColor("#38383A");
    m_palette.borderFocus      = QColor("#0A84FF");
    m_palette.divider          = QColor("#38383A");

    // Icon state colors (Apple dark mode system colors - slightly brighter)
    m_palette.iconActive       = QColor("#FF453A");   // Brighter red
    m_palette.iconInactive     = QColor("#30D158");   // Brighter green
    m_palette.iconDisabled     = QColor("#636366");   // Darker gray
    m_palette.iconLoading      = QColor("#FFD60A");   // Brighter yellow
    m_palette.iconWarning      = QColor("#FF9F0A");   // Brighter orange
    m_palette.iconDiagnostic   = QColor("#0A84FF");   // Brighter blue
    m_palette.iconSuccess      = QColor("#30D158");   // Bright green
    m_palette.iconCritical     = QColor("#FF375F");   // Bright pink-red
    m_palette.iconInfo         = QColor("#64D2FF");   // Bright cyan

    // Status colors
    m_palette.statusOnline     = QColor("#30D158");
    m_palette.statusOffline    = QColor("#636366");
    m_palette.statusWarning    = QColor("#FF9F0A");
    m_palette.statusError      = QColor("#FF453A");

    // Semantic
    m_palette.destructive      = QColor("#FF453A");
    m_palette.constructive     = QColor("#30D158");
}

// ============================================================
// Color For State
// ============================================================
QColor ThemeManager::colorForState(IconState state) const {
    switch (state) {
        case IconState::ACTIVE:      return m_palette.iconActive;
        case IconState::INACTIVE:    return m_palette.iconInactive;
        case IconState::DISABLED:    return m_palette.iconDisabled;
        case IconState::LOADING:     return m_palette.iconLoading;
        case IconState::WARNING:     return m_palette.iconWarning;
        case IconState::DIAGNOSTIC:  return m_palette.iconDiagnostic;
        case IconState::SUCCESS:     return m_palette.iconSuccess;
        case IconState::CRITICAL:    return m_palette.iconCritical;
        case IconState::INFO:        return m_palette.iconInfo;
        case IconState::CUSTOM:      return m_palette.accent;
    }
    return m_palette.iconInactive;
}

QColor ThemeManager::statusColor(const QString& status) const {
    QString s = status.toLower();
    if (s == "online" || s == "ok" || s == "good" || s == "passed") {
        return m_palette.statusOnline;
    }
    if (s == "offline" || s == "unknown" || s == "idle") {
        return m_palette.statusOffline;
    }
    if (s == "warning" || s == "caution" || s == "attention") {
        return m_palette.statusWarning;
    }
    if (s == "error" || s == "critical" || s == "failed" || s == "fault") {
        return m_palette.statusError;
    }
    return m_palette.statusOffline;
}

// ============================================================
// Custom Palette
// ============================================================
void ThemeManager::setCustomPalette(const ColorPalette& palette) {
    m_palette = palette;
    emit paletteChanged();
    IconRenderer::instance().clearCache();
}

void ThemeManager::resetToDefaultPalette() {
    if (m_darkMode) {
        buildDarkPalette();
    } else {
        buildLightPalette();
    }
    emit paletteChanged();
    IconRenderer::instance().clearCache();
}

// ============================================================
// QML Helpers
// ============================================================
QColor ThemeManager::stateColor(const QString& stateName) const {
    IconState state = stateFromString(stateName);
    return colorForState(state);
}

QString ThemeManager::iconUrl(const QString& iconId, const QString& state, int size) const {
    return buildIconUrl(iconId, stateFromString(state), size);
}

} // namespace icon
} // namespace powsys365
