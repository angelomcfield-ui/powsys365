#include "IconProvider.h"

#include <QGuiApplication>
#include <QPixmap>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QSvgRenderer>
#include <QPainter>

namespace powsys365 {
namespace icon {

// ============================================================
// Constructor
// ============================================================
IconProvider::IconProvider()
    : QQuickImageProvider(QQuickImageProvider::Pixmap,
                          QQuickImageProvider::ForceAsynchronousImageLoading) {
}

// ============================================================
// requestPixmap - Main entry point from QML
// ============================================================
QPixmap IconProvider::requestPixmap(const QString& id, QSize* size, const QSize& requestedSize) {
    QMutexLocker locker(&m_mutex);

    ParsedId parsed = parseRequestId(id);

    // Override size if requested from QML
    if (requestedSize.isValid() && !requestedSize.isEmpty()) {
        parsed.size = requestedSize;
    }

    if (size) {
        *size = parsed.size;
    }

    // Build render options
    RenderOptions opts;
    opts.state = parsed.state;
    opts.animation = parsed.animation;
    opts.customColor = parsed.customColor;
    opts.opacity = parsed.opacity;
    opts.badgeNumber = parsed.badgeNumber;
    opts.shadowEnabled = parsed.shadow;
    opts.rotation = parsed.rotation;

    // Render via IconRenderer
    auto& renderer = IconRenderer::instance();

    if (parsed.badgeNumber >= 0) {
        return renderer.renderIconWithBadge(parsed.iconId, parsed.size,
                                             parsed.badgeNumber, opts);
    }

    return renderer.renderIcon(parsed.iconId, parsed.size, opts);
}

// ============================================================
// State Parsing
// ============================================================
IconState IconProvider::parseState(const QString& stateStr) {
    return stateFromString(stateStr);
}

AnimationType IconProvider::parseAnimation(const QString& animStr) {
    return animationFromString(animStr);
}

// ============================================================
// Request ID Parsing
// ============================================================
IconProvider::ParsedId IconProvider::parseRequestId(const QString& id) {
    ParsedId result;
    if (id.isEmpty()) return result;

    // Format: "iconId|state|size|options"
    // or:     "iconId" (uses defaults)
    // or:     "iconId|state" (uses default size 24)
    // or:     "iconId|state|size"

    QStringList parts = id.split('|');
    if (parts.isEmpty()) return result;

    // Part 0: icon ID (always present)
    result.iconId = parts[0].trimmed();

    // Part 1: state (optional, default INACTIVE)
    if (parts.size() > 1) {
        result.state = parseState(parts[1].trimmed());
    }

    // Part 2: size (optional, default 24)
    if (parts.size() > 2) {
        QString sizeStr = parts[2].trimmed();
        bool ok;
        int s = sizeStr.toInt(&ok);
        if (ok && s > 0) {
            result.size = QSize(s, s);
        }
    }

    // Part 3+: options (key:value pairs)
    for (int i = 3; i < parts.size(); ++i) {
        QString opt = parts[i].trimmed();
        if (opt.isEmpty()) continue;

        // Badge option: "b:3" or "badge:5"
        if (opt.startsWith("b:") || opt.startsWith("badge:")) {
            int sep = opt.indexOf(':');
            if (sep >= 0) {
                bool ok;
                int badge = opt.mid(sep + 1).toInt(&ok);
                if (ok) result.badgeNumber = badge;
            }
        }
        // Animation option: "a:pulse" or "anim:spin"
        else if (opt.startsWith("a:") || opt.startsWith("anim:")) {
            int sep = opt.indexOf(':');
            if (sep >= 0) {
                result.animation = parseAnimation(opt.mid(sep + 1));
            }
        }
        // Custom color: "c:#FF6B35" or "color:#FF6B35"
        else if (opt.startsWith("c:") || opt.startsWith("color:")) {
            int sep = opt.indexOf(':');
            if (sep >= 0) {
                result.customColor = QColor(opt.mid(sep + 1));
            }
        }
        // Opacity: "o:0.5" or "opacity:80"
        else if (opt.startsWith("o:") || opt.startsWith("opacity:")) {
            int sep = opt.indexOf(':');
            if (sep >= 0) {
                bool ok;
                qreal op = opt.mid(sep + 1).toDouble(&ok);
                if (ok) {
                    result.opacity = qBound(0.0, op, 1.0);
                }
            }
        }
        // Shadow: "s:true" or "shadow"
        else if (opt == "s" || opt == "shadow" || opt.startsWith("s:true")) {
            result.shadow = true;
        }
        // Rotation: "r:90" or "rot:180"
        else if (opt.startsWith("r:") || opt.startsWith("rot:")) {
            int sep = opt.indexOf(':');
            if (sep >= 0) {
                bool ok;
                int rot = opt.mid(sep + 1).toInt(&ok);
                if (ok) result.rotation = rot;
            }
        }
    }

    return result;
}

// ============================================================
// QML Registration
// ============================================================
void registerIconProvider(QQmlEngine* engine) {
    if (!engine) return;
    engine->addImageProvider("powsys365", new IconProvider());
}

// ============================================================
// URL Builder
// ============================================================
QString buildIconUrl(const QString& iconId,
                     IconState state,
                     int size,
                     AnimationType anim,
                     int badge) {
    QString url = "image://powsys365/" + iconId;
    url += "|" + stateToString(state).toLower();
    url += "|" + QString::number(size);

    if (anim != AnimationType::NONE) {
        url += "|a:" + QString::number(static_cast<int>(anim));
    }
    if (badge >= 0) {
        url += "|b:" + QString::number(badge);
    }

    return url;
}

} // namespace icon
} // namespace powsys365
