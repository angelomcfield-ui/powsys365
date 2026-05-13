#include "ui_config.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdlib>

namespace powsys365::config {

/* ================================================================
   String helpers
   ================================================================ */

std::string UIConfigPanel::themeToString(ThemeMode t) {
    switch (t) {
        case ThemeMode::System:       return "system";
        case ThemeMode::Light:        return "light";
        case ThemeMode::Dark:         return "dark";
        case ThemeMode::HighContrast: return "highcontrast";
    }
    return "system";
}

UIConfigPanel::ThemeMode UIConfigPanel::stringToTheme(const std::string& s) {
    std::string l;
    for (char c : s) l += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (l == "light")        return ThemeMode::Light;
    if (l == "dark")         return ThemeMode::Dark;
    if (l == "highcontrast") return ThemeMode::HighContrast;
    return ThemeMode::System;
}

std::string UIConfigPanel::accentToString(AccentColor a) {
    switch (a) {
        case AccentColor::Blue:    return "blue";
        case AccentColor::Green:   return "green";
        case AccentColor::Orange:  return "orange";
        case AccentColor::Purple:  return "purple";
        case AccentColor::Red:     return "red";
        case AccentColor::Teal:    return "teal";
        case AccentColor::Custom:  return "custom";
    }
    return "blue";
}

UIConfigPanel::AccentColor UIConfigPanel::stringToAccent(const std::string& s) {
    std::string l;
    for (char c : s) l += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (l == "green")  return AccentColor::Green;
    if (l == "orange") return AccentColor::Orange;
    if (l == "purple") return AccentColor::Purple;
    if (l == "red")    return AccentColor::Red;
    if (l == "teal")   return AccentColor::Teal;
    if (l == "custom") return AccentColor::Custom;
    return AccentColor::Blue;
}

std::string UIConfigPanel::layoutToString(LayoutMode l) {
    switch (l) {
        case LayoutMode::Compact:  return "compact";
        case LayoutMode::Standard: return "standard";
        case LayoutMode::Expanded: return "expanded";
        case LayoutMode::Custom:   return "custom";
    }
    return "standard";
}

UIConfigPanel::LayoutMode UIConfigPanel::stringToLayout(const std::string& s) {
    std::string l;
    for (char c : s) l += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (l == "compact")  return LayoutMode::Compact;
    if (l == "expanded") return LayoutMode::Expanded;
    if (l == "custom")   return LayoutMode::Custom;
    return LayoutMode::Standard;
}

std::string UIConfigPanel::animationToString(AnimationLevel a) {
    switch (a) {
        case AnimationLevel::None:     return "none";
        case AnimationLevel::Minimal:  return "minimal";
        case AnimationLevel::Standard: return "standard";
        case AnimationLevel::Full:     return "full";
    }
    return "standard";
}

UIConfigPanel::AnimationLevel UIConfigPanel::stringToAnimation(const std::string& s) {
    std::string l;
    for (char c : s) l += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (l == "none")    return AnimationLevel::None;
    if (l == "minimal") return AnimationLevel::Minimal;
    if (l == "full")    return AnimationLevel::Full;
    return AnimationLevel::Standard;
}

/* ================================================================
   Color helpers
   ================================================================ */

std::string UIConfigPanel::Color::toHex() const {
    std::ostringstream oss;
    oss << '#' << std::hex << std::setfill('0')
        << std::setw(2) << static_cast<int>(r)
        << std::setw(2) << static_cast<int>(g)
        << std::setw(2) << static_cast<int>(b)
        << std::setw(2) << static_cast<int>(a);
    return oss.str();
}

UIConfigPanel::Color
UIConfigPanel::Color::fromHex(const std::string& hex) {
    Color c{0, 0, 0, 255};
    std::string h = hex;
    if (h.empty()) return c;
    if (h[0] == '#') h = h.substr(1);
    if (h.size() >= 6) {
        c.r = static_cast<uint8_t>(std::strtol(h.substr(0, 2).c_str(), nullptr, 16));
        c.g = static_cast<uint8_t>(std::strtol(h.substr(2, 2).c_str(), nullptr, 16));
        c.b = static_cast<uint8_t>(std::strtol(h.substr(4, 2).c_str(), nullptr, 16));
    }
    if (h.size() >= 8) {
        c.a = static_cast<uint8_t>(std::strtol(h.substr(6, 2).c_str(), nullptr, 16));
    }
    return c;
}

/* ================================================================
   PIMPL
   ================================================================ */

class UIConfigPanel::Impl {
public:
    ThemeMode       theme_        = ThemeMode::System;
    AccentColor     accentColor_  = AccentColor::Blue;
    Color           customAccent_ = {59, 130, 246, 255}; // Tailwind blue-500
    ColorPalette    palette_;
    std::string     language_     = "en";
    FontSpec        font_         = {"Inter", 10, 400, false};
    FontSpec        monoFont_     = {"JetBrains Mono", 10, 400, false};
    LayoutMode      layoutMode_   = LayoutMode::Standard;
    bool            sidebarVisible_ = true;
    int             sidebarWidth_   = 240;
    bool            toolbarVisible_ = true;
    bool            statusBarVisible_ = true;
    AnimationLevel  animationLevel_ = AnimationLevel::Standard;
    int             animationDuration_ = 200; // ms
    bool            enableTransitions_ = true;
    bool            enableBlur_        = true;
    bool            hasChanges_        = false;

    Impl() {
        // Set default palette
        palette_.primary       = {59, 130, 246, 255};
        palette_.secondary     = {107, 114, 128, 255};
        palette_.accent        = {59, 130, 246, 255};
        palette_.background    = {255, 255, 255, 255};
        palette_.surface       = {249, 250, 251, 255};
        palette_.error         = {239, 68, 68, 255};
        palette_.warning       = {245, 158, 11, 255};
        palette_.success       = {34, 197, 94, 255};
        palette_.textPrimary   = {17, 24, 39, 255};
        palette_.textSecondary = {107, 114, 128, 255};
        palette_.border        = {229, 231, 235, 255};
        palette_.divider       = {229, 231, 235, 255};
    }
};

/* ================================================================
   Construction
   ================================================================ */

UIConfigPanel::UIConfigPanel() : pImpl(std::make_unique<Impl>()) {}
UIConfigPanel::~UIConfigPanel() = default;

/* ================================================================
   Theme
   ================================================================ */

void UIConfigPanel::setTheme(ThemeMode m)       { pImpl->theme_ = m; pImpl->hasChanges_ = true; }
UIConfigPanel::ThemeMode UIConfigPanel::theme() const { return pImpl->theme_; }

void UIConfigPanel::setAccentColor(AccentColor a)  { pImpl->accentColor_ = a; pImpl->hasChanges_ = true; }
UIConfigPanel::AccentColor UIConfigPanel::accentColor() const { return pImpl->accentColor_; }

void UIConfigPanel::setCustomAccentColor(const Color& c) { pImpl->customAccent_ = c; pImpl->hasChanges_ = true; }
UIConfigPanel::Color UIConfigPanel::customAccentColor() const { return pImpl->customAccent_; }

void UIConfigPanel::setColorPalette(const ColorPalette& p) { pImpl->palette_ = p; pImpl->hasChanges_ = true; }
UIConfigPanel::ColorPalette UIConfigPanel::colorPalette() const { return pImpl->palette_; }

/* ================================================================
   Language
   ================================================================ */

void UIConfigPanel::setLanguage(const std::string& l) { pImpl->language_ = l; pImpl->hasChanges_ = true; }
std::string UIConfigPanel::language() const { return pImpl->language_; }

std::vector<std::string> UIConfigPanel::availableLanguages() const {
    return {"en", "es", "fr", "de", "it", "pt", "zh", "ja", "ko", "ar", "hi", "ru", "pl", "tr", "nl"};
}

/* ================================================================
   Font
   ================================================================ */

void UIConfigPanel::setFont(const FontSpec& f)       { pImpl->font_ = f; pImpl->hasChanges_ = true; }
UIConfigPanel::FontSpec UIConfigPanel::font() const   { return pImpl->font_; }

void UIConfigPanel::setFontSize(int s)               { pImpl->font_.pointSize = s; pImpl->hasChanges_ = true; }
int  UIConfigPanel::fontSize() const                 { return pImpl->font_.pointSize; }

void UIConfigPanel::setFontFamily(const std::string& f) { pImpl->font_.family = f; pImpl->hasChanges_ = true; }
std::string UIConfigPanel::fontFamily() const          { return pImpl->font_.family; }

void UIConfigPanel::setMonospaceFont(const FontSpec& f) { pImpl->monoFont_ = f; pImpl->hasChanges_ = true; }
UIConfigPanel::FontSpec UIConfigPanel::monospaceFont() const { return pImpl->monoFont_; }

/* ================================================================
   Layout
   ================================================================ */

void UIConfigPanel::setLayoutMode(LayoutMode m)      { pImpl->layoutMode_ = m; pImpl->hasChanges_ = true; }
UIConfigPanel::LayoutMode UIConfigPanel::layoutMode() const { return pImpl->layoutMode_; }

void UIConfigPanel::setSidebarVisible(bool v)        { pImpl->sidebarVisible_ = v; pImpl->hasChanges_ = true; }
bool UIConfigPanel::sidebarVisible() const           { return pImpl->sidebarVisible_; }

void UIConfigPanel::setSidebarWidth(int w)           { pImpl->sidebarWidth_ = w; pImpl->hasChanges_ = true; }
int  UIConfigPanel::sidebarWidth() const             { return pImpl->sidebarWidth_; }

void UIConfigPanel::setToolbarVisible(bool v)        { pImpl->toolbarVisible_ = v; pImpl->hasChanges_ = true; }
bool UIConfigPanel::toolbarVisible() const           { return pImpl->toolbarVisible_; }

void UIConfigPanel::setStatusBarVisible(bool v)      { pImpl->statusBarVisible_ = v; pImpl->hasChanges_ = true; }
bool UIConfigPanel::statusBarVisible() const         { return pImpl->statusBarVisible_; }

/* ================================================================
   Animation
   ================================================================ */

void UIConfigPanel::setAnimationLevel(AnimationLevel a) { pImpl->animationLevel_ = a; pImpl->hasChanges_ = true; }
UIConfigPanel::AnimationLevel UIConfigPanel::animationLevel() const { return pImpl->animationLevel_; }

void UIConfigPanel::setAnimationDuration(int d)      { pImpl->animationDuration_ = d; pImpl->hasChanges_ = true; }
int  UIConfigPanel::animationDuration() const        { return pImpl->animationDuration_; }

void UIConfigPanel::setEnableTransitions(bool e)     { pImpl->enableTransitions_ = e; pImpl->hasChanges_ = true; }
bool UIConfigPanel::enableTransitions() const        { return pImpl->enableTransitions_; }

void UIConfigPanel::setEnableBlur(bool e)            { pImpl->enableBlur_ = e; pImpl->hasChanges_ = true; }
bool UIConfigPanel::enableBlur() const               { return pImpl->enableBlur_; }

/* ================================================================
   JSON Serialisation
   ================================================================ */

namespace {
    std::string jesc(const std::string& s) {
        std::string r;
        for (char c : s) {
            switch (c) { case '"': r += "\\\""; break; case '\\': r += "\\\\"; break;
            case '\n': r += "\\n"; break; case '\r': r += "\\r"; break; case '\t': r += "\\t"; break;
            default: r += c; break; }
        }
        return r;
    }
    std::string colJson(const UIConfigPanel::Color& c) {
        return "{\\"r\\":" + std::to_string(c.r) + ",\\"g\\":" + std::to_string(c.g) +
               ",\\"b\\":" + std::to_string(c.b) + ",\\"a\\":" + std::to_string(c.a) + "}";
    }
}

std::string UIConfigPanel::toJSON() const {
    std::ostringstream oss;
    oss << "{";
    oss << "\\"theme\\":\\"" << themeToString(pImpl->theme_) << "\\",";
    oss << "\\"accentColor\\":\\"" << accentToString(pImpl->accentColor_) << "\\",";
    oss << "\\"customAccent\\":" << colJson(pImpl->customAccent_) << ",";
    oss << "\\"palette\\":{";
    oss << "\\"primary\\":" << colJson(pImpl->palette_.primary) << ",";
    oss << "\\"secondary\\":" << colJson(pImpl->palette_.secondary) << ",";
    oss << "\\"accent\\":" << colJson(pImpl->palette_.accent) << ",";
    oss << "\\"background\\":" << colJson(pImpl->palette_.background) << ",";
    oss << "\\"surface\\":" << colJson(pImpl->palette_.surface) << ",";
    oss << "\\"error\\":" << colJson(pImpl->palette_.error) << ",";
    oss << "\\"warning\\":" << colJson(pImpl->palette_.warning) << ",";
    oss << "\\"success\\":" << colJson(pImpl->palette_.success) << ",";
    oss << "\\"textPrimary\\":" << colJson(pImpl->palette_.textPrimary) << ",";
    oss << "\\"textSecondary\\":" << colJson(pImpl->palette_.textSecondary) << ",";
    oss << "\\"border\\":" << colJson(pImpl->palette_.border) << ",";
    oss << "\\"divider\\":" << colJson(pImpl->palette_.divider);
    oss << "},";
    oss << "\\"language\\":\\"" << jesc(pImpl->language_) << "\\",";
    oss << "\\"font\\":{\\"family\\":\\"" << jesc(pImpl->font_.family) << "\\",\\"size\\":"
        << pImpl->font_.pointSize << ",\\"weight\\":" << pImpl->font_.weight
        << ",\\"italic\\":" << (pImpl->font_.italic ? "true" : "false") << "},";
    oss << "\\"monospaceFont\\":{\\"family\\":\\"" << jesc(pImpl->monoFont_.family)
        << "\\",\\"size\\":" << pImpl->monoFont_.pointSize << "},";
    oss << "\\"layoutMode\\":\\"" << layoutToString(pImpl->layoutMode_) << "\\",";
    oss << "\\"sidebarVisible\\":" << (pImpl->sidebarVisible_ ? "true" : "false") << ",";
    oss << "\\"sidebarWidth\\":" << pImpl->sidebarWidth_ << ",";
    oss << "\\"toolbarVisible\\":" << (pImpl->toolbarVisible_ ? "true" : "false") << ",";
    oss << "\\"statusBarVisible\\":" << (pImpl->statusBarVisible_ ? "true" : "false") << ",";
    oss << "\\"animationLevel\\":\\"" << animationToString(pImpl->animationLevel_) << "\\",";
    oss << "\\"animationDuration\\":" << pImpl->animationDuration_ << ",";
    oss << "\\"enableTransitions\\":" << (pImpl->enableTransitions_ ? "true" : "false") << ",";
    oss << "\\"enableBlur\\":" << (pImpl->enableBlur_ ? "true" : "false");
    oss << "}";
    return oss.str();
}

void UIConfigPanel::fromJSON(const std::string& json) {
    auto extractStr = [&](const std::string& k) -> std::string {
        size_t p = json.find("\\"" + k + "\\":");
        if (p == std::string::npos) return "";
        p = json.find('"', p + k.length() + 3);
        if (p == std::string::npos) { p = json.find(':', p); return json.substr(p+1, 10); }
        ++p; size_t e = json.find('"', p);
        return (e == std::string::npos) ? "" : json.substr(p, e - p);
    };
    auto extractBool = [&](const std::string& k) -> bool {
        size_t p = json.find("\\"" + k + "\\":");
        if (p == std::string::npos) return false;
        p = json.find(':', p + k.length() + 3); if (p == std::string::npos) return false;
        ++p; while (p < json.size() && json[p] == ' ') ++p;
        return json.substr(p, 4) == "true";
    };
    auto extractInt = [&](const std::string& k) -> int {
        size_t p = json.find("\\"" + k + "\\":");
        if (p == std::string::npos) return 0;
        p = json.find(':', p + k.length() + 3); if (p == std::string::npos) return 0;
        ++p; return std::atoi(json.c_str() + p);
    };
    auto extractColor = [&](const std::string& k) -> Color {
        Color c{0,0,0,255};
        size_t p = json.find("\\"" + k + "\\":");
        if (p == std::string::npos) return c;
        p = json.find("\"r\":", p);
        if (p != std::string::npos) { p += 4; c.r = static_cast<uint8_t>(std::atoi(json.c_str() + p)); }
        p = json.find("\"g\":", p);
        if (p != std::string::npos) { p += 4; c.g = static_cast<uint8_t>(std::atoi(json.c_str() + p)); }
        p = json.find("\"b\":", p);
        if (p != std::string::npos) { p += 4; c.b = static_cast<uint8_t>(std::atoi(json.c_str() + p)); }
        p = json.find("\"a\":", p);
        if (p != std::string::npos) { p += 4; c.a = static_cast<uint8_t>(std::atoi(json.c_str() + p)); }
        return c;
    };

    std::string t = extractStr("theme");       if (!t.empty()) pImpl->theme_ = stringToTheme(t);
    std::string a = extractStr("accentColor"); if (!a.empty()) pImpl->accentColor_ = stringToAccent(a);
    pImpl->customAccent_ = extractColor("customAccent");
    pImpl->palette_.primary       = extractColor("primary");
    pImpl->palette_.secondary     = extractColor("secondary");
    pImpl->palette_.accent        = extractColor("accent");
    pImpl->palette_.background    = extractColor("background");
    pImpl->palette_.surface       = extractColor("surface");
    pImpl->palette_.error         = extractColor("error");
    pImpl->palette_.warning       = extractColor("warning");
    pImpl->palette_.success       = extractColor("success");
    pImpl->palette_.textPrimary   = extractColor("textPrimary");
    pImpl->palette_.textSecondary = extractColor("textSecondary");
    pImpl->palette_.border        = extractColor("border");
    pImpl->palette_.divider       = extractColor("divider");

    std::string lang = extractStr("language"); if (!lang.empty()) pImpl->language_ = lang;
    std::string ff = extractStr("family");     if (!ff.empty()) pImpl->font_.family = ff;
    int fs = extractInt("size");               if (fs > 0) pImpl->font_.pointSize = fs;
    int fw = extractInt("weight");             if (fw > 0) pImpl->font_.weight = fw;

    std::string l = extractStr("layoutMode");  if (!l.empty()) pImpl->layoutMode_ = stringToLayout(l);
    pImpl->sidebarVisible_  = extractBool("sidebarVisible");
    pImpl->sidebarWidth_    = extractInt("sidebarWidth"); if (pImpl->sidebarWidth_ < 50) pImpl->sidebarWidth_ = 240;
    pImpl->toolbarVisible_  = extractBool("toolbarVisible");
    pImpl->statusBarVisible_= extractBool("statusBarVisible");

    std::string al = extractStr("animationLevel"); if (!al.empty()) pImpl->animationLevel_ = stringToAnimation(al);
    pImpl->animationDuration_ = extractInt("animationDuration"); if (pImpl->animationDuration_ < 0) pImpl->animationDuration_ = 200;
    pImpl->enableTransitions_ = extractBool("enableTransitions");
    pImpl->enableBlur_        = extractBool("enableBlur");

    pImpl->hasChanges_ = false;
}

/* ================================================================
   Validation
   ================================================================ */

std::vector<std::string> UIConfigPanel::validate() const {
    std::vector<std::string> errors;
    if (pImpl->font_.pointSize < 6 || pImpl->font_.pointSize > 72) {
        errors.push_back("Font size must be between 6 and 72");
    }
    if (pImpl->sidebarWidth_ < 50 || pImpl->sidebarWidth_ > 600) {
        errors.push_back("Sidebar width must be between 50 and 600");
    }
    if (pImpl->animationDuration_ < 0 || pImpl->animationDuration_ > 5000) {
        errors.push_back("Animation duration must be between 0 and 5000 ms");
    }
    auto langs = availableLanguages();
    if (std::find(langs.begin(), langs.end(), pImpl->language_) == langs.end()) {
        errors.push_back("Unsupported language code: " + pImpl->language_);
    }
    return errors;
}

void UIConfigPanel::resetToDefaults() { pImpl = std::make_unique<Impl>(); }
bool UIConfigPanel::hasChanges() const { return pImpl->hasChanges_; }
void UIConfigPanel::markSaved()        { pImpl->hasChanges_ = false; }

} // namespace powsys365::config
