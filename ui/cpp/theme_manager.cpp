#include "theme_manager.h"

ThemeManager::ThemeManager(QObject* parent)
    : QObject(parent), m_themeName("Light")
{
}

QString ThemeManager::themeName() const
{
    return m_themeName;
}

void ThemeManager::setThemeName(const QString& themeName)
{
    if (m_themeName != themeName) {
        m_themeName = themeName;
        emit themeChanged();
    }
}