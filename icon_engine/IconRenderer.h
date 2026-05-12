#pragma once

#include <QColor>
#include <QMutex>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QSharedPointer>
#include <QString>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <chrono>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace powsys365 {
namespace icon {

// ============================================================
// IconState - Color states following Apple Design guidelines
// ============================================================
enum class IconState {
    INACTIVE    = 0,   // Verde #34C759
    ACTIVE      = 1,   // Rojo #FF3B30
    DISABLED    = 2,   // Gris #8E8E93
    LOADING     = 3,   // Amarillo #FFCC00
    WARNING     = 4,   // Naranja #FF9500
    DIAGNOSTIC  = 5,   // Azul #007AFF
    SUCCESS     = 6,   // Verde oscuro #30D158
    CRITICAL    = 7,   // Rojo oscuro #FF2D55
    INFO        = 8,   // Cyan #5AC8FA
    CUSTOM      = 9    // Color personalizado
};

QColor stateToColor(IconState state, const QColor& customColor = QColor());
QString stateToString(IconState state);
IconState stateFromString(const QString& s);

// ============================================================
// IconCategory
// ============================================================
enum class IconCategory {
    TOOLBAR     = 0,
    SIDEBAR     = 1,
    STATUS      = 2,
    EQUIPMENT   = 3,
    ALARM       = 4,
    NAVIGATION  = 5
};

QString categoryToString(IconCategory cat);

// ============================================================
// AnimationType
// ============================================================
enum class AnimationType {
    NONE    = 0,
    PULSE   = 1,
    FADE    = 2,
    SPIN    = 3,
    BOUNCE  = 4,
    SHAKE   = 5,
    GLOW    = 6
};

AnimationType animationFromString(const QString& s);

// ============================================================
// Apple Design Colors
// ============================================================
namespace AppleColors {
    constexpr const char* RED          = "#FF3B30";
    constexpr const char* GREEN        = "#34C759";
    constexpr const char* GRAY         = "#8E8E93";
    constexpr const char* YELLOW       = "#FFCC00";
    constexpr const char* ORANGE       = "#FF9500";
    constexpr const char* BLUE         = "#007AFF";
    constexpr const char* DARK_GREEN   = "#30D158";
    constexpr const char* DARK_RED     = "#FF2D55";
    constexpr const char* CYAN         = "#5AC8FA";
    constexpr const char* INDIGO       = "#5856D6";
    constexpr const char* PURPLE       = "#AF52DE";
    constexpr const char* TEAL         = "#59ADC4";
}

// ============================================================
// LRU Cache Entry
// ============================================================
struct CacheEntry {
    QString key;
    QPixmap pixmap;
    std::chrono::steady_clock::time_point timestamp;
    int accessCount = 1;

    CacheEntry(const QString& k, const QPixmap& p)
        : key(k), pixmap(p), timestamp(std::chrono::steady_clock::now()) {}
};

// ============================================================
// IconCache - Thread-safe LRU cache
// ============================================================
class IconCache {
public:
    explicit IconCache(size_t maxSize = 256);
    ~IconCache() = default;

    // No copy
    IconCache(const IconCache&) = delete;
    IconCache& operator=(const IconCache&) = delete;

    void insert(const QString& key, const QPixmap& pixmap);
    std::optional<QPixmap> get(const QString& key);
    void invalidate(const QString& key);
    void clear();
    void warmCache(const std::vector<QString>& iconIds,
                   IconState state,
                   const QSize& size);

    size_t size() const;
    size_t maxSize() const { return m_maxSize; }
    void setMaxSize(size_t maxSize);

private:
    mutable QMutex m_mutex;
    std::list<CacheEntry> m_cacheList;  // MRU at front
    std::map<QString, typename std::list<CacheEntry>::iterator> m_cacheMap;
    size_t m_maxSize;
};

// ============================================================
// SVG Template Database
// ============================================================
class IconTemplateDatabase {
public:
    static IconTemplateDatabase& instance();

    void registerIcon(const QString& iconId, const QString& svgTemplate);
    QString getTemplate(const QString& iconId) const;
    bool hasTemplate(const QString& iconId) const;
    std::vector<QString> allIconIds() const;

    // Pre-register built-in icons
    void registerBuiltInIcons();

private:
    IconTemplateDatabase();
    std::map<QString, QString> m_templates;
};

// ============================================================
// Render Options
// ============================================================
struct RenderOptions {
    IconState state = IconState::INACTIVE;
    IconCategory category = IconCategory::TOOLBAR;
    AnimationType animation = AnimationType::NONE;
    QColor customColor;
    qreal opacity = 1.0;
    int badgeNumber = -1;       // -1 = no badge
    bool shadowEnabled = false;
    qreal shadowBlur = 3.0;
    int rotation = 0;           // Degrees
    qreal scale = 1.0;

    QString cacheKey(const QString& iconId, const QSize& size) const;
};

// ============================================================
// IconRenderer - Main rendering engine
// ============================================================
class IconRenderer {
public:
    IconRenderer();
    ~IconRenderer() = default;

    // Singleton for global cache access
    static IconRenderer& instance();

    // --- Core Rendering ---
    QPixmap renderIcon(const QString& iconId,
                        const QSize& size,
                        const RenderOptions& options = RenderOptions());

    QPixmap renderIconWithBadge(const QString& iconId,
                                 const QSize& size,
                                 int badgeNumber,
                                 const RenderOptions& options = RenderOptions());

    // --- Batch Rendering ---
    std::map<QString, QPixmap> renderIconSet(
        const std::vector<QString>& iconIds,
        const QSize& size,
        const RenderOptions& options = RenderOptions());

    // --- Cache Management ---
    void warmCache(const std::vector<QString>& iconIds,
                   IconState state,
                   const QSize& size);
    void invalidateCache(const QString& iconId);
    void clearCache();
    size_t cacheSize() const;

    // --- SVG Processing ---
    static QString tintSvg(const QString& svgData, const QColor& color);
    static QString addAnimationToSvg(const QString& svgData, AnimationType anim);
    static QPixmap svgToPixmap(const QString& svgData, const QSize& size);
    static QString applyOpacity(const QString& svgData, qreal opacity);

    // --- Badge Rendering ---
    static QPixmap addBadge(const QPixmap& source, int number, const QColor& badgeColor = QColor("#FF3B30"));

private:
    IconCache m_cache;

    QPixmap renderInternal(const QString& iconId,
                            const QSize& size,
                            const RenderOptions& options);
    QPixmap applyAnimation(const QPixmap& source, AnimationType anim, qreal progress);
    QPixmap applyShadow(const QPixmap& source, qreal blurRadius);
    QString processSvgTemplate(const QString& template_, const RenderOptions& options);
};

} // namespace icon
} // namespace powsys365
