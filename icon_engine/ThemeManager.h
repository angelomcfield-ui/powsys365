#pragma once

#include "IconRenderer.h"

#include <QColor>
#include <QMap>
#include <QMutex>
#include <QObject>
#include <QString>

namespace powsys365 {
namespace icon {

// ============================================================
// Color Palette - Apple Design inspired
// ============================================================
struct ColorPalette {
    // Brand
    QColor brand;
    QColor brandSecondary;

    // Background hierarchy
    QColor background;
    QColor surface;
    QColor elevated;
    QColor overlay;

    // Text
    QColor textPrimary;
    QColor textSecondary;
    QColor textTertiary;
    QColor textDisabled;

    // Accent
    QColor accent;
    QColor accentHover;
    QColor accentPressed;

    // Border
    QColor border;
    QColor borderFocus;
    QColor divider;

    // Icon colors by state
    QColor iconActive;
    QColor iconInactive;
    QColor iconDisabled;
    QColor iconLoading;
    QColor iconWarning;
    QColor iconDiagnostic;
    QColor iconSuccess;
    QColor iconCritical;
    QColor iconInfo;

    // Status colors
    QColor statusOnline;
    QColor statusOffline;
    QColor statusWarning;
    QColor statusError;

    // Semantic
    QColor destructive;
    QColor constructive;
};

// ============================================================
// ThemeManager - Centralized theme/palette management
// ============================================================
class ThemeManager : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool darkMode READ darkMode WRITE setDarkMode NOTIFY themeChanged)
    Q_PROPERTY(QColor iconActiveColor READ iconActiveColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor iconInactiveColor READ iconInactiveColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor iconDisabledColor READ iconDisabledColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor iconLoadingColor READ iconLoadingColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor surfaceColor READ surfaceColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor textColor READ textColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor accentColor READ accentColor NOTIFY paletteChanged)

public:
    explicit ThemeManager(QObject* parent = nullptr);
    ~ThemeManager() override = default;

    // Singleton
    static ThemeManager& instance();

    // --- Dark Mode ---
    bool darkMode() const { return m_darkMode; }
    void setDarkMode(bool dark);
    void toggleDarkMode();

    // --- Palette Access ---
    const ColorPalette& palette() const { return m_palette; }

    // Icon colors
    QColor iconActiveColor() const   { return m_palette.iconActive; }
    QColor iconInactiveColor() const { return m_palette.iconInactive; }
    QColor iconDisabledColor() const { return m_palette.iconDisabled; }
    QColor iconLoadingColor() const  { return m_palette.iconLoading; }

    // Background / surface
    QColor backgroundColor() const { return m_palette.background; }
    QColor surfaceColor() const    { return m_palette.surface; }
    QColor elevatedColor() const   { return m_palette.elevated; }

    // Text
    QColor textColor() const          { return m_palette.textPrimary; }
    QColor textSecondaryColor() const { return m_palette.textSecondary; }

    // Accent
    QColor accentColor() const { return m_palette.accent; }

    // --- Palette Queries ---
    QColor colorForState(IconState state) const;
    QColor statusColor(const QString& status) const;

    // --- Custom Palettes ---
    void setCustomPalette(const ColorPalette& palette);
    void resetToDefaultPalette();

    // --- Auto-detect System Theme ---
    void autoDetectSystemTheme();

    // --- QML Helpers ---
    Q_INVOKABLE QColor stateColor(const QString& stateName) const;
    Q_INVOKABLE QString iconUrl(const QString& iconId, const QString& state, int size) const;

signals:
    void themeChanged(bool darkMode);
    void paletteChanged();

private:
    bool m_darkMode = false;
    ColorPalette m_palette;

    void buildLightPalette();
    void buildDarkPalette();
};

} // namespace icon
} // namespace powsys365
