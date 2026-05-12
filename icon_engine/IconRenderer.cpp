#include "IconRenderer.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QSvgRenderer>
#include <QtConcurrent>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <algorithm>
#include <cmath>

namespace powsys365 {
namespace icon {

// ============================================================
// State / Color Mapping
// ============================================================
QColor stateToColor(IconState state, const QColor& customColor) {
    switch (state) {
        case IconState::INACTIVE:    return QColor(AppleColors::GREEN);
        case IconState::ACTIVE:      return QColor(AppleColors::RED);
        case IconState::DISABLED:    return QColor(AppleColors::GRAY);
        case IconState::LOADING:     return QColor(AppleColors::YELLOW);
        case IconState::WARNING:     return QColor(AppleColors::ORANGE);
        case IconState::DIAGNOSTIC:  return QColor(AppleColors::BLUE);
        case IconState::SUCCESS:     return QColor(AppleColors::DARK_GREEN);
        case IconState::CRITICAL:    return QColor(AppleColors::DARK_RED);
        case IconState::INFO:        return QColor(AppleColors::CYAN);
        case IconState::CUSTOM:      return customColor.isValid() ? customColor : QColor(AppleColors::BLUE);
    }
    return QColor(AppleColors::GRAY);
}

QString stateToString(IconState state) {
    switch (state) {
        case IconState::INACTIVE:    return "INACTIVE";
        case IconState::ACTIVE:      return "ACTIVE";
        case IconState::DISABLED:    return "DISABLED";
        case IconState::LOADING:     return "LOADING";
        case IconState::WARNING:     return "WARNING";
        case IconState::DIAGNOSTIC:  return "DIAGNOSTIC";
        case IconState::SUCCESS:     return "SUCCESS";
        case IconState::CRITICAL:    return "CRITICAL";
        case IconState::INFO:        return "INFO";
        case IconState::CUSTOM:      return "CUSTOM";
    }
    return "UNKNOWN";
}

IconState stateFromString(const QString& s) {
    QString upper = s.toUpper();
    if (upper == "INACTIVE")    return IconState::INACTIVE;
    if (upper == "ACTIVE")      return IconState::ACTIVE;
    if (upper == "DISABLED")    return IconState::DISABLED;
    if (upper == "LOADING")     return IconState::LOADING;
    if (upper == "WARNING")     return IconState::WARNING;
    if (upper == "DIAGNOSTIC")  return IconState::DIAGNOSTIC;
    if (upper == "SUCCESS")     return IconState::SUCCESS;
    if (upper == "CRITICAL")    return IconState::CRITICAL;
    if (upper == "INFO")        return IconState::INFO;
    if (upper == "CUSTOM")      return IconState::CUSTOM;
    return IconState::INACTIVE;
}

QString categoryToString(IconCategory cat) {
    switch (cat) {
        case IconCategory::TOOLBAR:     return "TOOLBAR";
        case IconCategory::SIDEBAR:     return "SIDEBAR";
        case IconCategory::STATUS:      return "STATUS";
        case IconCategory::EQUIPMENT:   return "EQUIPMENT";
        case IconCategory::ALARM:       return "ALARM";
        case IconCategory::NAVIGATION:  return "NAVIGATION";
    }
    return "UNKNOWN";
}

AnimationType animationFromString(const QString& s) {
    QString upper = s.toUpper();
    if (upper == "PULSE")   return AnimationType::PULSE;
    if (upper == "FADE")    return AnimationType::FADE;
    if (upper == "SPIN")    return AnimationType::SPIN;
    if (upper == "BOUNCE")  return AnimationType::BOUNCE;
    if (upper == "SHAKE")   return AnimationType::SHAKE;
    if (upper == "GLOW")    return AnimationType::GLOW;
    return AnimationType::NONE;
}

// ============================================================
// RenderOptions cache key
// ============================================================
QString RenderOptions::cacheKey(const QString& iconId, const QSize& size) const {
    QString key = iconId + "_" +
                  QString::number(size.width()) + "x" + QString::number(size.height()) + "_" +
                  stateToString(state);
    if (badgeNumber >= 0) key += "_b" + QString::number(badgeNumber);
    if (animation != AnimationType::NONE) key += "_" + QString::number(static_cast<int>(animation));
    if (!customColor.isValid() && state == IconState::CUSTOM) {
        key += "_" + customColor.name();
    }
    if (opacity < 1.0) key += "_o" + QString::number(static_cast<int>(opacity * 100));
    return key;
}

// ============================================================
// IconCache Implementation
// ============================================================
IconCache::IconCache(size_t maxSize) : m_maxSize(maxSize) {}

void IconCache::insert(const QString& key, const QPixmap& pixmap) {
    QMutexLocker locker(&m_mutex);

    // Remove existing entry if present
    auto it = m_cacheMap.find(key);
    if (it != m_cacheMap.end()) {
        m_cacheList.erase(it->second);
        m_cacheMap.erase(it);
    }

    // Insert at front (MRU)
    m_cacheList.emplace_front(key, pixmap);
    m_cacheMap[key] = m_cacheList.begin();

    // Evict if over capacity
    while (m_cacheMap.size() > m_maxSize) {
        const QString& evictKey = m_cacheList.back().key;
        m_cacheMap.erase(evictKey);
        m_cacheList.pop_back();
    }
}

std::optional<QPixmap> IconCache::get(const QString& key) {
    QMutexLocker locker(&m_mutex);

    auto it = m_cacheMap.find(key);
    if (it == m_cacheMap.end()) {
        return std::nullopt;
    }

    // Move to front (MRU)
    auto listIt = it->second;
    listIt->timestamp = std::chrono::steady_clock::now();
    listIt->accessCount++;

    if (listIt != m_cacheList.begin()) {
        m_cacheList.splice(m_cacheList.begin(), m_cacheList, listIt);
        it->second = m_cacheList.begin();
    }

    return listIt->pixmap;
}

void IconCache::invalidate(const QString& key) {
    QMutexLocker locker(&m_mutex);

    auto it = m_cacheMap.find(key);
    if (it != m_cacheMap.end()) {
        m_cacheList.erase(it->second);
        m_cacheMap.erase(it);
    }
}

void IconCache::clear() {
    QMutexLocker locker(&m_mutex);
    m_cacheList.clear();
    m_cacheMap.clear();
}

void IconCache::warmCache(const std::vector<QString>& iconIds,
                            IconState state,
                            const QSize& size) {
    // Pre-render icons on background thread
    RenderOptions opts;
    opts.state = state;

    for (const auto& iconId : iconIds) {
        QString key = opts.cacheKey(iconId, size);
        {
            QMutexLocker locker(&m_mutex);
            if (m_cacheMap.count(key)) continue; // Already cached
        }

        // Would use QtConcurrent in production
        // For now, just render synchronously
        // IconRenderer::instance().renderIcon(iconId, size, opts);
    }
}

size_t IconCache::size() const {
    QMutexLocker locker(&m_mutex);
    return m_cacheMap.size();
}

void IconCache::setMaxSize(size_t maxSize) {
    QMutexLocker locker(&m_mutex);
    m_maxSize = maxSize;
    while (m_cacheMap.size() > m_maxSize) {
        const QString& evictKey = m_cacheList.back().key;
        m_cacheMap.erase(evictKey);
        m_cacheList.pop_back();
    }
}

// ============================================================
// IconTemplateDatabase
// ============================================================
IconTemplateDatabase& IconTemplateDatabase::instance() {
    static IconTemplateDatabase db;
    return db;
}

IconTemplateDatabase::IconTemplateDatabase() {
    registerBuiltInIcons();
}

void IconTemplateDatabase::registerIcon(const QString& iconId, const QString& svgTemplate) {
    m_templates[iconId] = svgTemplate;
}

QString IconTemplateDatabase::getTemplate(const QString& iconId) const {
    auto it = m_templates.find(iconId);
    if (it != m_templates.end()) return it->second;
    return QString();
}

bool IconTemplateDatabase::hasTemplate(const QString& iconId) const {
    return m_templates.count(iconId) > 0;
}

std::vector<QString> IconTemplateDatabase::allIconIds() const {
    std::vector<QString> ids;
    for (const auto& [id, _] : m_templates) {
        ids.push_back(id);
    }
    return ids;
}

void IconTemplateDatabase::registerBuiltInIcons() {
    // --- Toolbar Icons ---
    registerIcon("toolbar_new",
        R"(<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">"
        "<path d='M19 13h-6v6h-2v-6H5v-2h6V5h2v6h6v2z' fill='%1'/>"
        "</svg>)");

    registerIcon("toolbar_open",
        R"(<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">"
        "<path d='M20 6h-8l-2-2H4c-1.1 0-2 .9-2 2v12c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V8c0-1.1-.9-2-2-2zm0 12H4V8h16v10z' fill='%1'/>"
        "</svg>)");

    registerIcon("toolbar_save",
        R"(<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">"
        "<path d='M17 3H5a2 2 0 00-2 2v14a2 2 0 002 2h14a2 2 0 002-2V7l-4-4zm-5 16c-1.66 0-3-1.34-3-3s1.34-3 3-3 3 1.34 3 3-1.34 3-3 3zm3-10H5V5h10v4z' fill='%1'/>"
        "</svg>)");

    registerIcon("toolbar_print",
        R"(<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">"
        "<path d='M19 8H5c-1.66 0-3 1.34-3 3v6h4v4h12v-4h4v-6c0-1.66-1.34-3-3-3zm-3 11H8v-5h8v5zm3-7c-.55 0-1-.45-1-1s.45-1 1-1 1 .45 1 1-.45 1-1 1zm-1-9H6v4h12V3z' fill='%1'/>"
        "</svg>)");

    registerIcon("toolbar_export",
        R"(<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">"
        "<path d='M19 12v7H5v-7H3v7c0 1.1.9 2 2 2h14c1.1 0 2-.9 2-2v-7h-2zm-6 .67l2.59-2.58L17 11.5l-5 5-5-5 1.41-1.41L11 12.67V3h2v9.67z' fill='%1'/>"
        "</svg>)");

    // --- Sidebar Icons ---
    registerIcon("sidebar_sld",
        R"(<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">"
        "<circle cx='8' cy='8' r='3' stroke='%1' stroke-width='1.5' fill='none'/>"
        "<circle cx='16' cy='8' r='3' stroke='%1' stroke-width='1.5' fill='none'/>"
        "<circle cx='8' cy='16' r='3' stroke='%1' stroke-width='1.5' fill='none'/>"
        "<circle cx='16' cy='16' r='3' stroke='%1' stroke-width='1.5' fill='none'/>"
        "<line x1='11' y1='8' x2='13' y2='8' stroke='%1' stroke-width='1.5'/>"
        "<line x1='8' y1='11' x2='8' y2='13' stroke='%1' stroke-width='1.5'/>"
        "<line x1='11' y1='16' x2='13' y2='16' stroke='%1' stroke-width='1.5'/>"
        "<line x1='16' y1='11' x2='16' y2='13' stroke='%1' stroke-width='1.5'/>"
        "</svg>)");

    registerIcon("sidebar_protection",
        R"(<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">"
        "<path d='M12 1L3 5v6c0 5.55 3.84 10.74 9 12 5.16-1.26 9-6.45 9-12V5l-9-4z' stroke='%1' stroke-width='1.5' fill='none'/>"
        "<path d='M9 12l2 2 4-4' stroke='%1' stroke-width='1.5' fill='none' stroke-linecap='round' stroke-linejoin='round'/>"
        "</svg>)");

    registerIcon("sidebar_arcflash",
        R"(<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">"
        "<path d='M13 2L3 14h9l-1 8 10-12h-9l1-8z' stroke='%1' stroke-width='1.5' fill='none' stroke-linecap='round' stroke-linejoin='round'/>"
        "</svg>)");

    registerIcon("sidebar_harmonic",
        R"(<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">"
        "<path d='M3 17h2v-4H3v4zm4 0h2V7H7v10zm4 0h2v-6h-2v6zm4 0h2v-8h-2v8zm4 0h2v-2h-2v2z' fill='%1'/>"
        "</svg>)");

    registerIcon("sidebar_motor",
        R"(<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">"
        "<circle cx='12' cy='12' r='8' stroke='%1' stroke-width='1.5' fill='none'/>"
        "<circle cx='12' cy='12' r='3' stroke='%1' stroke-width='1.5' fill='none'/>"
        "<path d='M12 4v3M12 17v3M4 12h3M17 12h3' stroke='%1' stroke-width='1.5' stroke-linecap='round'/>"
        "</svg>)");

    // --- Status Icons ---
    registerIcon("status_online",
        R"(<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">"
        "<circle cx='12' cy='12' r='10' stroke='%1' stroke-width='2' fill='none'/>"
        "<circle cx='12' cy='12' r='5' fill='%1'/>"
        "</svg>)");

    registerIcon("status_offline",
        R"(<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">"
        "<circle cx='12' cy='12' r='10' stroke='%1' stroke-width='2' fill='none'/>"
        "<line x1='7' y1='7' x2='17' y2='17' stroke='%1' stroke-width='2' stroke-linecap='round'/>"
        "<line x1='17' y1='7' x2='7' y2='17' stroke='%1' stroke-width='2' stroke-linecap='round'/>"
        "</svg>)");

    registerIcon("status_syncing",
        R"(<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">"
        "<path d='M12 4V1L8 5l4 4V6c3.31 0 6 2.69 6 6 0 1.01-.25 1.97-.7 2.8l1.46 1.46A7.93 7.93 0 0020 12c0-4.42-3.58-8-8-8zm0 14c-3.31 0-6-2.69-6-6 0-1.01.25-1.97.7-2.8L5.24 7.74A7.93 7.93 0 004 12c0 4.42 3.58 8 8 8v3l4-4-4-4v3z' fill='%1'/>"
        "</svg>)");

    // --- Equipment Icons ---
    registerIcon("eq_transformer",
        R"(<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">"
        "<circle cx='8' cy='12' r='5' stroke='%1' stroke-width='1.5' fill='none'/>"
        "<circle cx='16' cy='12' r='5' stroke='%1' stroke-width='1.5' fill='none'/>"
        "<line x1='8' y1='7' x2='16' y2='7' stroke='%1' stroke-width='1' stroke-dasharray='2,2'/>"
        "<line x1='8' y1='17' x2='16' y2='17' stroke='%1' stroke-width='1' stroke-dasharray='2,2'/>"
        "</svg>)");

    registerIcon("eq_breaker",
        R"(<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">"
        "<rect x='4' y='8' width='6' height='8' rx='1' stroke='%1' stroke-width='1.5' fill='none'/>"
        "<rect x='14' y='8' width='6' height='8' rx='1' stroke='%1' stroke-width='1.5' fill='none'/>"
        "<line x1='10' y1='12' x2='14' y2='12' stroke='%1' stroke-width='1.5'/>"
        "</svg>)");

    registerIcon("eq_bus",
        R"(<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">"
        "<rect x='3' y='10' width='18' height='4' rx='1' fill='%1'/>"
        "<line x1='6' y1='14' x2='6' y2='20' stroke='%1' stroke-width='1.5'/>"
        "<line x1='12' y1='14' x2='12' y2='20' stroke='%1' stroke-width='1.5'/>"
        "<line x1='18' y1='14' x2='18' y2='20' stroke='%1' stroke-width='1.5'/>"
        "</svg>)");

    registerIcon("eq_generator",
        R"(<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">"
        "<circle cx='12' cy='12' r='8' stroke='%1' stroke-width='1.5' fill='none'/>"
        "<path d='M12 7v5l3 3' stroke='%1' stroke-width='1.5' fill='none' stroke-linecap='round'/>"
        "<path d='M2 12h3M19 12h3' stroke='%1' stroke-width='1.5' stroke-linecap='round'/>"
        "</svg>)");

    registerIcon("eq_load",
        R"(<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">"
        "<rect x='6' y='4' width='12' height='16' rx='2' stroke='%1' stroke-width='1.5' fill='none'/>"
        "<line x1='6' y1='9' x2='18' y2='9' stroke='%1' stroke-width='1'/>"
        "<line x1='9' y1='13' x2='15' y2='13' stroke='%1' stroke-width='1.5' stroke-linecap='round'/>"
        "<line x1='9' y1='16' x2='15' y2='16' stroke='%1' stroke-width='1.5' stroke-linecap='round'/>"
        "</svg>)");

    // --- Alarm Icons ---
    registerIcon("alarm_critical",
        R"(<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">"
        "<path d='M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm1 15h-2v-2h2v2zm0-4h-2V7h2v6z' fill='%1'/>"
        "</svg>)");

    registerIcon("alarm_warning",
        R"(<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">"
        "<path d='M1 21h22L12 2 1 21zm12-3h-2v-2h2v2zm0-4h-2v-4h2v4z' fill='%1'/>"
        "</svg>)");

    registerIcon("alarm_info",
        R"(<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">"
        "<path d='M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm1 15h-2v-6h2v6zm0-8h-2V7h2v2z' fill='%1'/>"
        "</svg>)");

    // --- Navigation Icons ---
    registerIcon("nav_back",
        R"(<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">"
        "<path d='M20 11H7.83l5.59-5.59L12 4l-8 8 8 8 1.41-1.41L7.83 13H20v-2z' fill='%1'/>"
        "</svg>)");

    registerIcon("nav_forward",
        R"(<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">"
        "<path d='M12 4l-1.41 1.41L16.17 11H4v2h12.17l-5.58 5.59L12 20l8-8z' fill='%1'/>"
        "</svg>)");

    registerIcon("nav_home",
        R"(<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">"
        "<path d='M10 20v-6h4v6h5v-8h3L12 3 2 12h3v8z' fill='%1'/>"
        "</svg>)");

    registerIcon("nav_settings",
        R"(<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">"
        "<path d='M19.14 12.94c.04-.3.06-.61.06-.94 0-.32-.02-.64-.07-.94l2.03-1.58a.49.49 0 00.12-.61l-1.92-3.32a.488.488 0 00-.59-.22l-2.39.96c-.5-.38-1.03-.7-1.62-.94l-.36-2.54a.484.484 0 00-.48-.41h-3.84c-.24 0-.43.17-.47.41l-.36 2.54c-.59.24-1.13.57-1.62.94l-2.39-.96c-.22-.08-.47 0-.59.22L3.16 8.87c-.12.21-.08.47.12.61l2.03 1.58c-.05.3-.09.63-.09.94s.02.64.07.94l-2.03 1.58a.49.49 0 00-.12.61l1.92 3.32c.12.22.37.29.59.22l2.39-.96c.5.38 1.03.7 1.62.94l.36 2.54c.05.24.24.41.48.41h3.84c.24 0 .44-.17.47-.41l.36-2.54c.59-.24 1.13-.58 1.62-.94l2.39.96c.22.08.47 0 .59-.22l1.92-3.32c.12-.22.07-.47-.12-.61l-2.01-1.58zM12 15.6c-1.98 0-3.6-1.62-3.6-3.6s1.62-3.6 3.6-3.6 3.6 1.62 3.6 3.6-1.62 3.6-3.6 3.6z' fill='%1'/>"
        "</svg>)");

    registerIcon("nav_help",
        R"(<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">"
        "<path d='M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm1 17h-2v-2h2v2zm2.07-7.75l-.9.92C13.45 12.9 13 13.5 13 15h-2v-.5c0-1.1.45-2.1 1.17-2.83l1.24-1.26c.37-.36.59-.86.59-1.41 0-1.1-.9-2-2-2s-2 .9-2 2H8c0-2.21 1.79-4 4-4s4 1.79 4 4c0 .88-.36 1.68-.93 2.25z' fill='%1'/>"
        "</svg>)");

    registerIcon("nav_logout",
        R"(<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">"
        "<path d='M17 7l-1.41 1.41L18.17 11H8v2h10.17l-2.58 2.58L17 17l5-5zM4 5h8V3H4c-1.1 0-2 .9-2 2v14c0 1.1.9 2 2 2h8v-2H4V5z' fill='%1'/>"
        "</svg>)");

    // --- Power System Icons ---
    registerIcon("busbar_h",
        R"(<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">"
        "<rect x='2' y='11' width='20' height='2' rx='0.5' fill='%1'/>"
        "<circle cx='6' cy='12' r='1' fill='%1'/>"
        "<circle cx='12' cy='12' r='1' fill='%1'/>"
        "<circle cx='18' cy='12' r='1' fill='%1'/>"
        "</svg>)");

    registerIcon("busbar_v",
        R"(<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">"
        "<rect x='11' y='2' width='2' height='20' rx='0.5' fill='%1'/>"
        "<circle cx='12' cy='6' r='1' fill='%1'/>"
        "<circle cx='12' cy='12' r='1' fill='%1'/>"
        "<circle cx='12' cy='18' r='1' fill='%1'/>"
        "</svg>)");
}

// ============================================================
// IconRenderer Implementation
// ============================================================

IconRenderer::IconRenderer() {}

IconRenderer& IconRenderer::instance() {
    static IconRenderer inst;
    return inst;
}

QPixmap IconRenderer::renderIcon(const QString& iconId,
                                  const QSize& size,
                                  const RenderOptions& options) {
    QString key = options.cacheKey(iconId, size);

    // Check cache
    auto cached = m_cache.get(key);
    if (cached.has_value()) {
        QPixmap pix = cached.value();
        // Apply dynamic animation if needed
        if (options.animation != AnimationType::NONE) {
            qreal progress = static_cast<qreal>(
                QGuiApplication::applicationState() == Qt::ApplicationActive ? 1.0 : 0.5);
            return applyAnimation(pix, options.animation, progress);
        }
        return pix;
    }

    // Render and cache
    QPixmap pix = renderInternal(iconId, size, options);
    if (!pix.isNull()) {
        m_cache.insert(key, pix);
    }
    return pix;
}

QPixmap IconRenderer::renderIconWithBadge(const QString& iconId,
                                            const QSize& size,
                                            int badgeNumber,
                                            const RenderOptions& options) {
    RenderOptions badgeOpts = options;
    badgeOpts.badgeNumber = badgeNumber;
    QString key = badgeOpts.cacheKey(iconId, size);

    auto cached = m_cache.get(key);
    if (cached.has_value()) return cached.value();

    QPixmap base = renderInternal(iconId, size, options);
    if (base.isNull()) return QPixmap();

    QPixmap result = addBadge(base, badgeNumber);
    m_cache.insert(key, result);
    return result;
}

std::map<QString, QPixmap> IconRenderer::renderIconSet(
    const std::vector<QString>& iconIds,
    const QSize& size,
    const RenderOptions& options) {

    std::map<QString, QPixmap> results;
    for (const auto& id : iconIds) {
        results[id] = renderIcon(id, size, options);
    }
    return results;
}

QPixmap IconRenderer::renderInternal(const QString& iconId,
                                      const QSize& size,
                                      const RenderOptions& options) {
    auto& db = IconTemplateDatabase::instance();
    QString template_ = db.getTemplate(iconId);

    if (template_.isEmpty()) {
        // Return a fallback "missing icon" pixmap
        QPixmap fallback(size);
        fallback.fill(Qt::transparent);
        QPainter p(&fallback);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor(AppleColors::GRAY), 1, Qt::DashLine));
        p.drawRect(1, 1, size.width() - 2, size.height() - 2);
        p.setPen(QColor(AppleColors::GRAY));
        QFont f = p.font();
        f.setPointSize(8);
        p.setFont(f);
        p.drawText(fallback.rect(), Qt::AlignCenter, "?");
        p.end();
        return fallback;
    }

    // Process SVG template
    QString processedSvg = processSvgTemplate(template_, options);

    // Apply opacity
    if (options.opacity < 1.0) {
        processedSvg = applyOpacity(processedSvg, options.opacity);
    }

    // Convert to pixmap
    QPixmap pix = svgToPixmap(processedSvg, size);

    // Apply shadow
    if (options.shadowEnabled) {
        pix = applyShadow(pix, options.shadowBlur);
    }

    // Apply rotation
    if (options.rotation != 0) {
        QTransform t;
        t.rotate(options.rotation);
        pix = pix.transformed(t, Qt::SmoothTransformation);
    }

    // Apply scale
    if (options.scale != 1.0) {
        QSize newSize(static_cast<int>(size.width() * options.scale),
                      static_cast<int>(size.height() * options.scale));
        pix = pix.scaled(newSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    return pix;
}

QString IconRenderer::processSvgTemplate(const QString& template_,
                                          const RenderOptions& options) {
    QColor color = stateToColor(options.state, options.customColor);
    QString colorStr = color.name();

    // Replace %1 placeholder with the color
    QString result = template_;
    result.replace("%1", colorStr);

    return result;
}

// ============================================================
// SVG Processing
// ============================================================
QString IconRenderer::tintSvg(const QString& svgData, const QColor& color) {
    QString result = svgData;
    // Replace fill='%1' or fill="%1" patterns
    QString colorStr = color.name();

    // Simple replacement - in production would use proper XML parsing
    QRegularExpression fillRe(R"(fill=['"]([^'"]*)['"])");
    QRegularExpression strokeRe(R"(stroke=['"]([^'"]*)['"])");

    result.replace(fillRe, "fill=\"" + colorStr + "\"");
    result.replace(strokeRe, "stroke=\"" + colorStr + "\"");

    return result;
}

QString IconRenderer::addAnimationToSvg(const QString& svgData, AnimationType anim) {
    if (anim == AnimationType::NONE) return svgData;

    QString animElement;
    switch (anim) {
        case AnimationType::PULSE:
            animElement = R"(
                <style>
                    @keyframes pulse { 0%,100% { opacity: 1; } 50% { opacity: 0.4; } }
                    .anim-pulse { animation: pulse 2s ease-in-out infinite; }
                </style>
                <g class="anim-pulse">)";
            break;
        case AnimationType::FADE:
            animElement = R"(
                <style>
                    @keyframes fade { 0%,100% { opacity: 0.3; } 50% { opacity: 1; } }
                    .anim-fade { animation: fade 1.5s ease-in-out infinite; }
                </style>
                <g class="anim-fade">)";
            break;
        case AnimationType::SPIN:
            animElement = R"(
                <style>
                    @keyframes spin { from { transform: rotate(0deg); } to { transform: rotate(360deg); } }
                    .anim-spin { animation: spin 2s linear infinite; transform-origin: center; }
                </style>
                <g class="anim-spin">)";
            break;
        case AnimationType::BOUNCE:
            animElement = R"(
                <style>
                    @keyframes bounce { 0%,100% { transform: translateY(0); } 50% { transform: translateY(-3px); } }
                    .anim-bounce { animation: bounce 0.6s ease-in-out infinite; }
                </style>
                <g class="anim-bounce">)";
            break;
        case AnimationType::SHAKE:
            animElement = R"(
                <style>
                    @keyframes shake { 0%,100% { transform: translateX(0); } 25% { transform: translateX(-2px); } 75% { transform: translateX(2px); } }
                    .anim-shake { animation: shake 0.3s ease-in-out infinite; }
                </style>
                <g class="anim-shake">)";
            break;
        case AnimationType::GLOW:
            animElement = R"(
                <style>
                    @keyframes glow { 0%,100% { filter: drop-shadow(0 0 2px currentColor); } 50% { filter: drop-shadow(0 0 6px currentColor); } }
                    .anim-glow { animation: glow 2s ease-in-out infinite; }
                </style>
                <g class="anim-glow">)";
            break;
        default:
            return svgData;
    }

    // Insert animation wrapper before closing </svg>
    QString result = svgData;
    int insertPos = result.lastIndexOf("</svg>");
    if (insertPos >= 0) {
        result.insert(insertPos, animElement + "</g>");
    }
    return result;
}

QPixmap IconRenderer::svgToPixmap(const QString& svgData, const QSize& size) {
    QSvgRenderer renderer(svgData.toUtf8());
    if (!renderer.isValid()) {
        QPixmap fallback(size);
        fallback.fill(Qt::transparent);
        return fallback;
    }

    QPixmap pix(size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    renderer.render(&p);
    p.end();

    return pix;
}

QString IconRenderer::applyOpacity(const QString& svgData, qreal opacity) {
    QString result = svgData;
    int svgPos = result.indexOf("<svg");
    if (svgPos >= 0) {
        int tagEnd = result.indexOf(">", svgPos);
        if (tagEnd > svgPos) {
            QString opacityAttr = QString(" opacity=\"%1\"").arg(opacity, 0, 'f', 2);
            result.insert(tagEnd, opacityAttr);
        }
    }
    return result;
}

// ============================================================
// Badge Rendering
// ============================================================
QPixmap IconRenderer::addBadge(const QPixmap& source, int number, const QColor& badgeColor) {
    if (number < 0) return source;

    QPixmap result = source.copy();
    QPainter p(&result);
    p.setRenderHint(QPainter::Antialiasing);

    int badgeSize = qMin(source.width(), source.height()) / 2;
    int badgeRadius = badgeSize / 2;
    int badgeX = source.width() - badgeSize - 1;
    int badgeY = 1;

    // Draw badge background
    p.setBrush(badgeColor);
    p.setPen(Qt::NoPen);
    p.drawEllipse(badgeX, badgeY, badgeSize, badgeSize);

    // Draw number
    p.setPen(Qt::white);
    QFont font = p.font();
    font.setPointSizeF(qMax(6.0, badgeSize * 0.5));
    font.setBold(true);
    p.setFont(font);

    QString text = (number > 99) ? "99+" : QString::number(number);
    QRect badgeRect(badgeX, badgeY, badgeSize, badgeSize);
    p.drawText(badgeRect, Qt::AlignCenter, text);

    p.end();
    return result;
}

// ============================================================
// Animation Effects
// ============================================================
QPixmap IconRenderer::applyAnimation(const QPixmap& source, AnimationType anim, qreal progress) {
    if (anim == AnimationType::NONE) return source;

    QPixmap result(source.size());
    result.fill(Qt::transparent);
    QPainter p(&result);
    p.setRenderHint(QPainter::Antialiasing);

    switch (anim) {
        case AnimationType::PULSE: {
            qreal scale = 1.0 + 0.1 * std::sin(progress * M_PI * 2);
            QPoint center = source.rect().center();
            p.translate(center);
            p.scale(scale, scale);
            p.translate(-center);
            p.drawPixmap(0, 0, source);
            break;
        }
        case AnimationType::FADE: {
            qreal opacity = 0.4 + 0.6 * std::sin(progress * M_PI * 2);
            p.setOpacity(opacity);
            p.drawPixmap(0, 0, source);
            break;
        }
        case AnimationType::SPIN: {
            qreal angle = progress * 360.0;
            QPoint center = source.rect().center();
            p.translate(center);
            p.rotate(angle);
            p.translate(-center);
            p.drawPixmap(0, 0, source);
            break;
        }
        case AnimationType::BOUNCE: {
            qreal offset = -3.0 * std::abs(std::sin(progress * M_PI * 2));
            p.translate(0, offset);
            p.drawPixmap(0, 0, source);
            break;
        }
        case AnimationType::SHAKE: {
            qreal offset = 2.0 * std::sin(progress * M_PI * 8);
            p.translate(offset, 0);
            p.drawPixmap(0, 0, source);
            break;
        }
        case AnimationType::GLOW: {
            p.drawPixmap(0, 0, source);
            qreal glowIntensity = 0.3 + 0.2 * std::sin(progress * M_PI * 2);
            p.setCompositionMode(QPainter::CompositionMode_SourceAtop);
            p.fillRect(result.rect(), QColor(255, 255, 255, static_cast<int>(glowIntensity * 255)));
            break;
        }
        default:
            p.drawPixmap(0, 0, source);
    }

    p.end();
    return result;
}

// ============================================================
// Shadow Effect
// ============================================================
QPixmap IconRenderer::applyShadow(const QPixmap& source, qreal blurRadius) {
    QPixmap result(source.size() + QSize(static_cast<int>(blurRadius * 4),
                                          static_cast<int>(blurRadius * 4)));
    result.fill(Qt::transparent);

    QPainter p(&result);
    p.setRenderHint(QPainter::Antialiasing);

    // Draw shadow
    QGraphicsDropShadowEffect shadow;
    shadow.setBlurRadius(blurRadius);
    shadow.setColor(QColor(0, 0, 0, 80));
    shadow.setOffset(1, 1);

    // Simple shadow simulation
    int offset = static_cast<int>(blurRadius);
    p.setOpacity(0.3);
    p.drawPixmap(offset * 2, offset * 2, source);
    p.setOpacity(0.6);
    p.drawPixmap(offset, offset, source);
    p.setOpacity(1.0);
    p.drawPixmap(0, 0, source);

    p.end();
    return result;
}

// ============================================================
// Cache Management
// ============================================================
void IconRenderer::warmCache(const std::vector<QString>& iconIds,
                              IconState state,
                              const QSize& size) {
    m_cache.warmCache(iconIds, state, size);
}

void IconRenderer::invalidateCache(const QString& iconId) {
    // Invalidate all cache entries matching this icon ID
    m_cache.invalidate(iconId);
}

void IconRenderer::clearCache() {
    m_cache.clear();
}

size_t IconRenderer::cacheSize() const {
    return m_cache.size();
}

} // namespace icon
} // namespace powsys365
