#pragma once

#include <string>
#include <vector>
#include <array>

namespace powsys365::config {

/**
 * @brief UI panel configuration – theme, language, fonts, layout,
 *        custom colors, animations.
 *
 * Qt6-compatible: stores Qt-agnostic types; conversion helpers
 * for QColor, QFont, QLocale available in .cpp.
 */
class UIConfigPanel {
public:
    /**
     * @brief Application theme mode.
     */
    enum class ThemeMode {
        System,
        Light,
        Dark,
        HighContrast
    };

    static std::string themeToString(ThemeMode t);
    static ThemeMode   stringToTheme(const std::string& s);

    /**
     * @brief Accent color preset.
     */
    enum class AccentColor {
        Blue, Green, Orange, Purple, Red, Teal, Custom
    };

    static std::string accentToString(AccentColor a);
    static AccentColor stringToAccent(const std::string& s);

    /**
     * @brief Layout mode.
     */
    enum class LayoutMode {
        Compact,    // Minimal chrome
        Standard,   // Balanced
        Expanded,   // Full feature visible
        Custom
    };

    static std::string layoutToString(LayoutMode l);
    static LayoutMode  stringToLayout(const std::string& s);

    /**
     * @brief Animation level.
     */
    enum class AnimationLevel {
        None,
        Minimal,
        Standard,
        Full
    };

    static std::string animationToString(AnimationLevel a);
    static AnimationLevel stringToAnimation(const std::string& s);

    /**
     * @brief Color definition (RGBA, Qt-compatible).
     */
    struct Color {
        uint8_t r = 0, g = 0, b = 0, a = 255;
        std::string toHex() const;
        static Color fromHex(const std::string& hex);
    };

    /**
     * @brief Font specification.
     */
    struct FontSpec {
        std::string family = "Inter";
        int         pointSize = 10;
        int         weight    = 400; // QFont::Normal
        bool        italic    = false;
    };

    /**
     * @brief Custom color palette override.
     */
    struct ColorPalette {
        Color primary;
        Color secondary;
        Color accent;
        Color background;
        Color surface;
        Color error;
        Color warning;
        Color success;
        Color textPrimary;
        Color textSecondary;
        Color border;
        Color divider;
    };

    UIConfigPanel();
    ~UIConfigPanel() = default;

    UIConfigPanel(const UIConfigPanel&) = default;
    UIConfigPanel& operator=(const UIConfigPanel&) = default;
    UIConfigPanel(UIConfigPanel&&) noexcept = default;
    UIConfigPanel& operator=(UIConfigPanel&&) noexcept = default;

    // ----------------------------------------------------------------
    //  Theme
    // ----------------------------------------------------------------

    void setTheme(ThemeMode mode);
    ThemeMode theme() const;

    void setAccentColor(AccentColor color);
    AccentColor accentColor() const;

    void setCustomAccentColor(const Color& color);
    Color customAccentColor() const;

    void setColorPalette(const ColorPalette& palette);
    ColorPalette colorPalette() const;

    // ----------------------------------------------------------------
    //  Language
    // ----------------------------------------------------------------

    void setLanguage(const std::string& langCode); // "en", "es", "fr"...
    std::string language() const;

    std::vector<std::string> availableLanguages() const;

    // ----------------------------------------------------------------
    //  Font
    // ----------------------------------------------------------------

    void setFont(const FontSpec& font);
    FontSpec font() const;

    void setFontSize(int pointSize);
    int  fontSize() const;

    void setFontFamily(const std::string& family);
    std::string fontFamily() const;

    void setMonospaceFont(const FontSpec& font);
    FontSpec monospaceFont() const;

    // ----------------------------------------------------------------
    //  Layout
    // ----------------------------------------------------------------

    void setLayoutMode(LayoutMode mode);
    LayoutMode layoutMode() const;

    void setSidebarVisible(bool visible);
    bool sidebarVisible() const;

    void setSidebarWidth(int width);
    int  sidebarWidth() const;

    void setToolbarVisible(bool visible);
    bool toolbarVisible() const;

    void setStatusBarVisible(bool visible);
    bool statusBarVisible() const;

    // ----------------------------------------------------------------
    //  Animation
    // ----------------------------------------------------------------

    void setAnimationLevel(AnimationLevel level);
    AnimationLevel animationLevel() const;

    void setAnimationDuration(int ms);
    int  animationDuration() const;

    void setEnableTransitions(bool enabled);
    bool enableTransitions() const;

    void setEnableBlur(bool enabled);
    bool enableBlur() const;

    // ----------------------------------------------------------------
    //  Serialisation
    // ----------------------------------------------------------------

    std::string toJSON() const;
    void fromJSON(const std::string& json);

    // ----------------------------------------------------------------
    //  Validation
    // ----------------------------------------------------------------

    std::vector<std::string> validate() const;

    void resetToDefaults();
    bool hasChanges() const;
    void markSaved();

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

} // namespace powsys365::config
