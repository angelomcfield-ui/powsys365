#ifndef THEME_MANAGER_H
#define THEME_MANAGER_H

#include <QObject>
#include <QString>

class ThemeManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString themeName READ themeName WRITE setThemeName NOTIFY themeChanged)

public:
    explicit ThemeManager(QObject* parent = nullptr);

    QString themeName() const;
    void setThemeName(const QString& themeName);

signals:
    void themeChanged();

private:
    QString m_themeName;
};

#endif // THEME_MANAGER_H