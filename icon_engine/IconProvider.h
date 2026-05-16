#pragma once

#include "IconRenderer.h"

#include <QQuickImageProvider>
#include <QMutex>
#include <QPixmap>
#include <QString>

// Forward declaration
class QQmlEngine;

namespace powsys365 {
namespace icon {

// ============================================================
// IconProvider - QQuickImageProvider for QML integration
//
// Image source URL format: "image://powsys365/<iconId>|<state>|<size>|<options>"
//
// Examples:
//   "image://powsys365/status_online|active|32"
//   "image://powsys365/toolbar_save|inactive|24|b:3"    (with badge 3)
//   "image://powsys365/alarm_critical|critical|48|a:pulse" (with pulse animation)
//   "image://powsys365/sidebar_sld|diagnostic|24|c:#FF6B35" (custom color)
// ============================================================

class IconProvider : public QQuickImageProvider {
public:
    explicit IconProvider();
    ~IconProvider() override = default;

    // QQuickImageProvider interface
    QPixmap requestPixmap(const QString& id, QSize* size, const QSize& requestedSize) override;

    // --- Parse helpers ---
    static IconState parseState(const QString& stateStr);
    static AnimationType parseAnimation(const QString& animStr);

    // --- Request ID Parsing ---
    struct ParsedId {
        QString iconId;
        IconState state = IconState::INACTIVE;
        QSize size = QSize(24, 24);
        AnimationType animation = AnimationType::NONE;
        int badgeNumber = -1;
        QColor customColor;
        qreal opacity = 1.0;
        bool shadow = false;
        int rotation = 0;
    };

    static ParsedId parseRequestId(const QString& id);

    // --- Provider info ---
    static QString providerId() { return "powsys365"; }
    static QString providerUrlScheme() { return "image://powsys365/"; }

private:
    mutable QMutex m_mutex;
};

// ============================================================
// QML Registration Helper
// ============================================================
void registerIconProvider(QQmlEngine* engine);

// ============================================================
// URL Builder
// ============================================================
QString buildIconUrl(const QString& iconId,
                     IconState state = IconState::INACTIVE,
                     int size = 24,
                     AnimationType anim = AnimationType::NONE,
                     int badge = -1);

} // namespace icon
} // namespace powsys365
