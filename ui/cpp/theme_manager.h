#ifndef THEME_MANAGER_H
#define THEME_MANAGER_H

#include <QObject>
#include <QQmlEngine>
#include <QColor>

/**
 * @brief ThemeManager - Centralized Apple-style theme management
 *
 * Provides dark/light mode toggle, semantic color definitions,
 * and persistent preference storage. All colors follow Apple's
 * Human Interface Guidelines for macOS/iOS.
 */
class ThemeManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // ── Mode ───────────────────────────────────────────────────────────────
    Q_PROPERTY(bool darkMode READ darkMode WRITE setDarkMode NOTIFY darkModeChanged)

    // ── Semantic colors (auto-switched by mode) ──────────────────────────
    Q_PROPERTY(QColor backgroundPrimary READ backgroundPrimary NOTIFY colorsChanged)
    Q_PROPERTY(QColor backgroundSecondary READ backgroundSecondary NOTIFY colorsChanged)
    Q_PROPERTY(QColor backgroundTertiary READ backgroundTertiary NOTIFY colorsChanged)
    Q_PROPERTY(QColor backgroundElevated READ backgroundElevated NOTIFY colorsChanged)
    Q_PROPERTY(QColor backgroundGlass READ backgroundGlass NOTIFY colorsChanged)
    Q_PROPERTY(QColor separator READ separator NOTIFY colorsChanged)
    Q_PROPERTY(QColor fillPrimary READ fillPrimary NOTIFY colorsChanged)
    Q_PROPERTY(QColor fillSecondary READ fillSecondary NOTIFY colorsChanged)
    Q_PROPERTY(QColor fillTertiary READ fillTertiary NOTIFY colorsChanged)

    // ── Text colors ───────────────────────────────────────────────────────
    Q_PROPERTY(QColor textPrimary READ textPrimary NOTIFY colorsChanged)
    Q_PROPERTY(QColor textSecondary READ textSecondary NOTIFY colorsChanged)
    Q_PROPERTY(QColor textTertiary READ textTertiary NOTIFY colorsChanged)
    Q_PROPERTY(QColor textPlaceholder READ textPlaceholder NOTIFY colorsChanged)

    // ── Accent colors (Apple system colors) ──────────────────────────────
    Q_PROPERTY(QColor accentBlue READ accentBlue NOTIFY colorsChanged)
    Q_PROPERTY(QColor accentGreen READ accentGreen NOTIFY colorsChanged)
    Q_PROPERTY(QColor accentRed READ accentRed NOTIFY colorsChanged)
    Q_PROPERTY(QColor accentOrange READ accentOrange NOTIFY colorsChanged)
    Q_PROPERTY(QColor accentYellow READ accentYellow NOTIFY colorsChanged)
    Q_PROPERTY(QColor accentPurple READ accentPurple NOTIFY colorsChanged)
    Q_PROPERTY(QColor accentTeal READ accentTeal NOTIFY colorsChanged)
    Q_PROPERTY(QColor accentIndigo READ accentIndigo NOTIFY colorsChanged)

    // ── Component-specific colors ────────────────────────────────────────
    Q_PROPERTY(QColor buttonBackground READ buttonBackground NOTIFY colorsChanged)
    Q_PROPERTY(QColor buttonHovered READ buttonHovered NOTIFY colorsChanged)
    Q_PROPERTY(QColor buttonPressed READ buttonPressed NOTIFY colorsChanged)
    Q_PROPERTY(QColor buttonDisabled READ buttonDisabled NOTIFY colorsChanged)
    Q_PROPERTY(QColor inputBackground READ inputBackground NOTIFY colorsChanged)
    Q_PROPERTY(QColor inputBorder READ inputBorder NOTIFY colorsChanged)
    Q_PROPERTY(QColor inputFocusedBorder READ inputFocusedBorder NOTIFY colorsChanged)
    Q_PROPERTY(QColor tableBackground READ tableBackground NOTIFY colorsChanged)
    Q_PROPERTY(QColor tableAltRow READ tableAltRow NOTIFY colorsChanged)
    Q_PROPERTY(QColor tableSelected READ tableSelected NOTIFY colorsChanged)
    Q_PROPERTY(QColor toolbarBackground READ toolbarBackground NOTIFY colorsChanged)
    Q_PROPERTY(QColor sidebarBackground READ sidebarBackground NOTIFY colorsChanged)
    Q_PROPERTY(QColor tooltipBackground READ tooltipBackground NOTIFY colorsChanged)
    Q_PROPERTY(QColor overlay READ overlay NOTIFY colorsChanged)

    // ── Power system specific colors ─────────────────────────────────────
    Q_PROPERTY(QColor busPQ READ busPQ NOTIFY colorsChanged)
    Q_PROPERTY(QColor busPV READ busPV NOTIFY colorsChanged)
    Q_PROPERTY(QColor busSlack READ busSlack NOTIFY colorsChanged)
    Q_PROPERTY(QColor lineNormal READ lineNormal NOTIFY colorsChanged)
    Q_PROPERTY(QColor lineWarning READ lineWarning NOTIFY colorsChanged)
    Q_PROPERTY(QColor lineOverload READ lineOverload NOTIFY colorsChanged)
    Q_PROPERTY(QColor generatorOnline READ generatorOnline NOTIFY colorsChanged)
    Q_PROPERTY(QColor generatorOffline READ generatorOffline NOTIFY colorsChanged)
    Q_PROPERTY(QColor loadActive READ loadActive NOTIFY colorsChanged)
    Q_PROPERTY(QColor violation READ violation NOTIFY colorsChanged)

public:
    explicit ThemeManager(QObject *parent = nullptr);

    static ThemeManager *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    // ── Q_PROPERTY getters ────────────────────────────────────────────────
    bool darkMode() const { return m_darkMode; }

    QColor backgroundPrimary() const;
    QColor backgroundSecondary() const;
    QColor backgroundTertiary() const;
    QColor backgroundElevated() const;
    QColor backgroundGlass() const;
    QColor separator() const;
    QColor fillPrimary() const;
    QColor fillSecondary() const;
    QColor fillTertiary() const;

    QColor textPrimary() const;
    QColor textSecondary() const;
    QColor textTertiary() const;
    QColor textPlaceholder() const;

    QColor accentBlue() const;
    QColor accentGreen() const;
    QColor accentRed() const;
    QColor accentOrange() const;
    QColor accentYellow() const;
    QColor accentPurple() const;
    QColor accentTeal() const;
    QColor accentIndigo() const;

    QColor buttonBackground() const;
    QColor buttonHovered() const;
    QColor buttonPressed() const;
    QColor buttonDisabled() const;
    QColor inputBackground() const;
    QColor inputBorder() const;
    QColor inputFocusedBorder() const;
    QColor tableBackground() const;
    QColor tableAltRow() const;
    QColor tableSelected() const;
    QColor toolbarBackground() const;
    QColor sidebarBackground() const;
    QColor tooltipBackground() const;
    QColor overlay() const;

    QColor busPQ() const { return accentBlue(); }
    QColor busPV() const { return accentGreen(); }
    QColor busSlack() const { return accentRed(); }
    QColor lineNormal() const { return accentGreen(); }
    QColor lineWarning() const { return accentYellow(); }
    QColor lineOverload() const { return accentRed(); }
    QColor generatorOnline() const { return accentGreen(); }
    QColor generatorOffline() const { return QColor("#8E8E93"); }
    QColor loadActive() const { return accentOrange(); }
    QColor violation() const { return accentRed(); }

public slots:
    void setDarkMode(bool dark);
    void toggleTheme();
    void syncWithSystem();

signals:
    void darkModeChanged();
    void colorsChanged();

private:
    bool m_darkMode = true;

    void loadPreference();
    void savePreference();
};

#endif // THEME_MANAGER_H
