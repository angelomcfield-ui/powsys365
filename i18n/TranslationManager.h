#pragma once

#include <QLocale>
#include <QMap>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QTranslator>
#include <QCoreApplication>

#include <memory>
#include <vector>

namespace powsys365 {
namespace i18n {

// ============================================================
// Supported Locales
// ============================================================
enum class SupportedLocale {
    ES,  // Spanish (default)
    EN,  // English
    FR,  // French
    DE,  // German
    PT,  // Portuguese
    ZH,  // Chinese (Simplified)
    JA,  // Japanese
    KO,  // Korean
    AR,  // Arabic
    HI   // Hindi
};

QString localeToCode(SupportedLocale locale);
QString localeToName(SupportedLocale locale);
SupportedLocale codeToLocale(const QString& code);

// ============================================================
// Number / Date / Currency Format Info
// ============================================================
struct LocaleFormat {
    QString decimalSeparator = ".";
    QString thousandsSeparator = ",";
    QString dateFormat = "yyyy-MM-dd";
    QString dateTimeFormat = "yyyy-MM-dd hh:mm:ss";
    QString timeFormat = "hh:mm:ss";
    QString currencySymbol = "$";
    QString currencyCode = "USD";
    bool currencySymbolBefore = true;
    int currencyDecimals = 2;
    Qt::LayoutDirection layoutDirection = Qt::LeftToRight;
};

LocaleFormat getFormatForLocale(SupportedLocale locale);

// ============================================================
// TranslationManager - Singleton for i18n
// ============================================================
class TranslationManager : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString currentLocale READ currentLocaleCode WRITE setLocale NOTIFY localeChanged)
    Q_PROPERTY(QString currentLanguage READ currentLanguage NOTIFY localeChanged)
    Q_PROPERTY(bool isRTL READ isRTL NOTIFY localeChanged)

public:
    explicit TranslationManager(QObject* parent = nullptr);
    ~TranslationManager() override;

    // Singleton
    static TranslationManager& instance();

    // --- Locale Management ---
    bool loadLocale(SupportedLocale locale);
    bool loadLocale(const QString& localeCode);

    QString currentLocaleCode() const;
    SupportedLocale currentLocale() const;
    QString currentLanguage() const;

    // --- Translation ---
    QString tr(const QString& context, const QString& text) const;
    QString tr(const QString& context, const QString& text, const QString& disambiguation) const;

    // --- Formatting ---
    QString formatNumber(double value, int decimals = 2) const;
    QString formatInteger(int value) const;
    QString formatDate(const QDate& date) const;
    QString formatDateTime(const QDateTime& dateTime) const;
    QString formatTime(const QTime& time) const;
    QString formatCurrency(double amount, const QString& currencyCode = QString()) const;
    QString formatPercentage(double value, int decimals = 1) const;

    // --- Locale Properties ---
    bool isRTL() const;
    Qt::LayoutDirection layoutDirection() const;
    LocaleFormat format() const;

    // --- Available Locales ---
    std::vector<SupportedLocale> availableLocales() const;
    QStringList availableLocaleCodes() const;
    QStringList availableLocaleNames() const;

    // --- QML Registration ---
    static void registerQmlTypes();

    // --- System Locale Detection ---
    static SupportedLocale detectSystemLocale();

signals:
    void localeChanged(SupportedLocale locale);
    void translationsLoaded();

private:
    SupportedLocale m_currentLocale = SupportedLocale::ES;
    std::unique_ptr<QTranslator> m_translator;
    QLocale m_qtLocale;
    mutable QMutex m_mutex;
    bool m_loaded = false;

    // Translation catalog (fallback when .qm files are not available)
    QMap<QString, QMap<QString, QString>> m_fallbackTranslations;

    bool installTranslator(SupportedLocale locale);
    void loadFallbackTranslations();
    QString lookupFallback(const QString& context, const QString& text) const;
    QString translatorDir() const;
};

// ============================================================
// Macro for convenient translation
// ============================================================
#define POWSYS365_TR(ctx, text) \
    powsys365::i18n::TranslationManager::instance().tr(ctx, text)

} // namespace i18n
} // namespace powsys365
