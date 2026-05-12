#include "theme_manager.h"
#include <QSettings>
#include <QGuiApplication>
#include <QPalette>

ThemeManager::ThemeManager(QObject *parent)
    : QObject(parent)
{
    loadPreference();
}

ThemeManager *ThemeManager::create(QQmlEngine *engine, QJSEngine *scriptEngine)
{
    Q_UNUSED(engine)
    Q_UNUSED(scriptEngine)
    return new ThemeManager();
}

void ThemeManager::loadPreference()
{
    QSettings settings("POWSYS365", "POWSYS365");
    m_darkMode = settings.value("darkMode", true).toBool();
}

void ThemeManager::savePreference()
{
    QSettings settings("POWSYS365", "POWSYS365");
    settings.setValue("darkMode", m_darkMode);
}

void ThemeManager::setDarkMode(bool dark)
{
    if (m_darkMode != dark) {
        m_darkMode = dark;
        savePreference();
        emit darkModeChanged();
        emit colorsChanged();
    }
}

void ThemeManager::toggleTheme()
{
    setDarkMode(!m_darkMode);
}

void ThemeManager::syncWithSystem()
{
    // Could detect system theme here
    // For now, keep current preference
}

// ── Background colors ─────────────────────────────────────────────────────
QColor ThemeManager::backgroundPrimary() const
{
    return m_darkMode ? QColor("#1C1C1E") : QColor("#F5F5F7");
}

QColor ThemeManager::backgroundSecondary() const
{
    return m_darkMode ? QColor("#2C2C2E") : QColor("#FFFFFF");
}

QColor ThemeManager::backgroundTertiary() const
{
    return m_darkMode ? QColor("#3A3A3C") : QColor("#E5E5EA");
}

QColor ThemeManager::backgroundElevated() const
{
    return m_darkMode ? QColor("#1C1C1E") : QColor("#FFFFFF");
}

QColor ThemeManager::backgroundGlass() const
{
    return m_darkMode ? QColor(40, 40, 42, 180) : QColor(255, 255, 255, 180);
}

QColor ThemeManager::separator() const
{
    return m_darkMode ? QColor("#38383A") : QColor("#E5E5EA");
}

QColor ThemeManager::fillPrimary() const
{
    return m_darkMode ? QColor(120, 120, 128, 36) : QColor(120, 120, 128, 20);
}

QColor ThemeManager::fillSecondary() const
{
    return m_darkMode ? QColor(120, 120, 128, 32) : QColor(120, 120, 128, 16);
}

QColor ThemeManager::fillTertiary() const
{
    return m_darkMode ? QColor(118, 118, 128, 24) : QColor(118, 118, 128, 12);
}

// ── Text colors ───────────────────────────────────────────────────────────
QColor ThemeManager::textPrimary() const
{
    return m_darkMode ? QColor("#FFFFFF") : QColor("#1D1D1F");
}

QColor ThemeManager::textSecondary() const
{
    return m_darkMode ? QColor("#98989D") : QColor("#86868B");
}

QColor ThemeManager::textTertiary() const
{
    return m_darkMode ? QColor("#636366") : QColor("#A1A1A6");
}

QColor ThemeManager::textPlaceholder() const
{
    return m_darkMode ? QColor("#636366") : QColor("#C7C7CC");
}

// ── Accent colors ─────────────────────────────────────────────────────────
QColor ThemeManager::accentBlue() const
{
    return QColor("#0A84FF");
}

QColor ThemeManager::accentGreen() const
{
    return QColor("#32D74B");
}

QColor ThemeManager::accentRed() const
{
    return QColor("#FF453A");
}

QColor ThemeManager::accentOrange() const
{
    return QColor("#FF9F0A");
}

QColor ThemeManager::accentYellow() const
{
    return QColor("#FFD60A");
}

QColor ThemeManager::accentPurple() const
{
    return QColor("#BF5AF2");
}

QColor ThemeManager::accentTeal() const
{
    return QColor("#64D2FF");
}

QColor ThemeManager::accentIndigo() const
{
    return QColor("#5E5CE6");
}

// ── Component colors ──────────────────────────────────────────────────────
QColor ThemeManager::buttonBackground() const
{
    return m_darkMode ? QColor("#3A3A3C") : QColor("#E5E5EA");
}

QColor ThemeManager::buttonHovered() const
{
    return m_darkMode ? QColor("#48484A") : QColor("#D1D1D6");
}

QColor ThemeManager::buttonPressed() const
{
    return m_darkMode ? QColor("#525254") : QColor("#C7C7CC");
}

QColor ThemeManager::buttonDisabled() const
{
    return m_darkMode ? QColor("#2C2C2E") : QColor("#F2F2F7");
}

QColor ThemeManager::inputBackground() const
{
    return m_darkMode ? QColor("#1C1C1E") : QColor("#FFFFFF");
}

QColor ThemeManager::inputBorder() const
{
    return m_darkMode ? QColor("#38383A") : QColor("#D1D1D6");
}

QColor ThemeManager::inputFocusedBorder() const
{
    return accentBlue();
}

QColor ThemeManager::tableBackground() const
{
    return m_darkMode ? QColor("#2C2C2E") : QColor("#FFFFFF");
}

QColor ThemeManager::tableAltRow() const
{
    return m_darkMode ? QColor("#323234") : QColor("#F5F5F7");
}

QColor ThemeManager::tableSelected() const
{
    return QColor(10, 132, 255, 60);
}

QColor ThemeManager::toolbarBackground() const
{
    return m_darkMode ? QColor(44, 44, 46, 240) : QColor(255, 255, 255, 240);
}

QColor ThemeManager::sidebarBackground() const
{
    return m_darkMode ? QColor(30, 30, 32, 245) : QColor(245, 245, 247, 245);
}

QColor ThemeManager::tooltipBackground() const
{
    return m_darkMode ? QColor("#3A3A3C") : QColor("#1D1D1F");
}

QColor ThemeManager::overlay() const
{
    return QColor(0, 0, 0, 80);
}
