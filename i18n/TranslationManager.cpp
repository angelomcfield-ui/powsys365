#include "TranslationManager.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibraryInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringBuilder>

namespace powsys365 {
namespace i18n {

// ============================================================
// Locale Code / Name Mapping
// ============================================================
QString localeToCode(SupportedLocale locale) {
    switch (locale) {
        case SupportedLocale::ES: return "es";
        case SupportedLocale::EN: return "en";
        case SupportedLocale::FR: return "fr";
        case SupportedLocale::DE: return "de";
        case SupportedLocale::PT: return "pt";
        case SupportedLocale::ZH: return "zh";
        case SupportedLocale::JA: return "ja";
        case SupportedLocale::KO: return "ko";
        case SupportedLocale::AR: return "ar";
        case SupportedLocale::HI: return "hi";
    }
    return "es";
}

QString localeToName(SupportedLocale locale) {
    switch (locale) {
        case SupportedLocale::ES: return "Espanol";
        case SupportedLocale::EN: return "English";
        case SupportedLocale::FR: return "Francais";
        case SupportedLocale::DE: return "Deutsch";
        case SupportedLocale::PT: return "Portugues";
        case SupportedLocale::ZH: return "Zhongwen";
        case SupportedLocale::JA: return "Nihongo";
        case SupportedLocale::KO: return "Hangugeo";
        case SupportedLocale::AR: return "Arabiyya";
        case SupportedLocale::HI: return "Hindi";
    }
    return "Espanol";
}

SupportedLocale codeToLocale(const QString& code) {
    QString lower = code.toLower().trimmed();
    if (lower == "es" || lower.startsWith("es_")) return SupportedLocale::ES;
    if (lower == "en" || lower.startsWith("en_")) return SupportedLocale::EN;
    if (lower == "fr" || lower.startsWith("fr_")) return SupportedLocale::FR;
    if (lower == "de" || lower.startsWith("de_")) return SupportedLocale::DE;
    if (lower == "pt" || lower.startsWith("pt_")) return SupportedLocale::PT;
    if (lower == "zh" || lower.startsWith("zh_")) return SupportedLocale::ZH;
    if (lower == "ja" || lower.startsWith("ja_")) return SupportedLocale::JA;
    if (lower == "ko" || lower.startsWith("ko_")) return SupportedLocale::KO;
    if (lower == "ar" || lower.startsWith("ar_")) return SupportedLocale::AR;
    if (lower == "hi" || lower.startsWith("hi_")) return SupportedLocale::HI;
    return SupportedLocale::ES; // Default to Spanish
}

// ============================================================
// Format Info per Locale
// ============================================================
LocaleFormat getFormatForLocale(SupportedLocale locale) {
    LocaleFormat fmt;

    switch (locale) {
        case SupportedLocale::ES:
            fmt.decimalSeparator = ",";
            fmt.thousandsSeparator = ".";
            fmt.dateFormat = "dd/MM/yyyy";
            fmt.dateTimeFormat = "dd/MM/yyyy HH:mm:ss";
            fmt.timeFormat = "HH:mm:ss";
            fmt.currencySymbol = "EUR";
            fmt.currencyCode = "EUR";
            fmt.currencySymbolBefore = false;
            break;

        case SupportedLocale::EN:
            fmt.decimalSeparator = ".";
            fmt.thousandsSeparator = ",";
            fmt.dateFormat = "MM/dd/yyyy";
            fmt.dateTimeFormat = "MM/dd/yyyy hh:mm:ss AP";
            fmt.timeFormat = "hh:mm:ss AP";
            fmt.currencySymbol = "$";
            fmt.currencyCode = "USD";
            fmt.currencySymbolBefore = true;
            break;

        case SupportedLocale::FR:
            fmt.decimalSeparator = ",";
            fmt.thousandsSeparator = " ";
            fmt.dateFormat = "dd/MM/yyyy";
            fmt.dateTimeFormat = "dd/MM/yyyy HH:mm:ss";
            fmt.timeFormat = "HH:mm:ss";
            fmt.currencySymbol = "EUR";
            fmt.currencyCode = "EUR";
            fmt.currencySymbolBefore = false;
            break;

        case SupportedLocale::DE:
            fmt.decimalSeparator = ",";
            fmt.thousandsSeparator = ".";
            fmt.dateFormat = "dd.MM.yyyy";
            fmt.dateTimeFormat = "dd.MM.yyyy HH:mm:ss";
            fmt.timeFormat = "HH:mm:ss";
            fmt.currencySymbol = "EUR";
            fmt.currencyCode = "EUR";
            fmt.currencySymbolBefore = false;
            break;

        case SupportedLocale::PT:
            fmt.decimalSeparator = ",";
            fmt.thousandsSeparator = ".";
            fmt.dateFormat = "dd/MM/yyyy";
            fmt.dateTimeFormat = "dd/MM/yyyy HH:mm:ss";
            fmt.timeFormat = "HH:mm:ss";
            fmt.currencySymbol = "R$";
            fmt.currencyCode = "BRL";
            fmt.currencySymbolBefore = true;
            break;

        case SupportedLocale::ZH:
            fmt.decimalSeparator = ".";
            fmt.thousandsSeparator = ",";
            fmt.dateFormat = "yyyy/MM/dd";
            fmt.dateTimeFormat = "yyyy/MM/dd HH:mm:ss";
            fmt.timeFormat = "HH:mm:ss";
            fmt.currencySymbol = "CNY";
            fmt.currencyCode = "CNY";
            fmt.currencySymbolBefore = true;
            break;

        case SupportedLocale::JA:
            fmt.decimalSeparator = ".";
            fmt.thousandsSeparator = ",";
            fmt.dateFormat = "yyyy/MM/dd";
            fmt.dateTimeFormat = "yyyy/MM/dd HH:mm:ss";
            fmt.timeFormat = "HH:mm:ss";
            fmt.currencySymbol = "JPY";
            fmt.currencyCode = "JPY";
            fmt.currencySymbolBefore = true;
            fmt.currencyDecimals = 0;
            break;

        case SupportedLocale::KO:
            fmt.decimalSeparator = ".";
            fmt.thousandsSeparator = ",";
            fmt.dateFormat = "yyyy. MM. dd";
            fmt.dateTimeFormat = "yyyy. MM. dd HH:mm:ss";
            fmt.timeFormat = "HH:mm:ss";
            fmt.currencySymbol = "KRW";
            fmt.currencyCode = "KRW";
            fmt.currencySymbolBefore = true;
            fmt.currencyDecimals = 0;
            break;

        case SupportedLocale::AR:
            fmt.decimalSeparator = ",";
            fmt.thousandsSeparator = ".";
            fmt.dateFormat = "dd/MM/yyyy";
            fmt.dateTimeFormat = "dd/MM/yyyy HH:mm:ss";
            fmt.timeFormat = "HH:mm:ss";
            fmt.currencySymbol = "SAR";
            fmt.currencyCode = "SAR";
            fmt.currencySymbolBefore = false;
            fmt.layoutDirection = Qt::RightToLeft;
            break;

        case SupportedLocale::HI:
            fmt.decimalSeparator = ".";
            fmt.thousandsSeparator = ",";
            fmt.dateFormat = "dd/MM/yyyy";
            fmt.dateTimeFormat = "dd/MM/yyyy HH:mm:ss";
            fmt.timeFormat = "HH:mm:ss";
            fmt.currencySymbol = "INR";
            fmt.currencyCode = "INR";
            fmt.currencySymbolBefore = true;
            break;
    }

    return fmt;
}

// ============================================================
// TranslationManager Implementation
// ============================================================

TranslationManager::TranslationManager(QObject* parent)
    : QObject(parent)
    , m_translator(std::make_unique<QTranslator>()) {

    // Load fallback translations
    loadFallbackTranslations();

    // Auto-detect system locale
    SupportedLocale sysLocale = detectSystemLocale();
    loadLocale(sysLocale);
}

TranslationManager::~TranslationManager() = default;

TranslationManager& TranslationManager::instance() {
    static TranslationManager mgr;
    return mgr;
}

// ============================================================
// Translator Directory
// ============================================================
QString TranslationManager::translatorDir() const {
    // Check multiple locations for .qm files
    QStringList searchPaths;

    // 1. Application directory
    searchPaths << QCoreApplication::applicationDirPath() + "/translations";
    searchPaths << QCoreApplication::applicationDirPath() + "/../translations";
    searchPaths << QCoreApplication::applicationDirPath() + "/../Resources/translations";

    // 2. Standard locations
    searchPaths << QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/translations";
    searchPaths << QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/POWSYS365/translations";

    // 3. Source-relative (development)
    searchPaths << QString(SOURCE_ROOT) + "/i18n";

    for (const QString& path : searchPaths) {
        if (QDir(path).exists()) {
            return path;
        }
    }

    // Return first path even if it doesn't exist (will fail gracefully)
    return searchPaths.first();
}

// ============================================================
// Install Translator
// ============================================================
bool TranslationManager::installTranslator(SupportedLocale locale) {
    QString localeCode = localeToCode(locale);

    // Remove previous translator
    if (m_loaded && m_translator) {
        QCoreApplication::removeTranslator(m_translator.get());
    }

    // Build .qm file path
    QString qmFileName = QString("powsys365_%1.qm").arg(localeCode);
    QString qmPath = translatorDir() + "/" + qmFileName;

    // Try to load .qm file
    if (QFile::exists(qmPath)) {
        m_translator = std::make_unique<QTranslator>();
        if (m_translator->load(qmPath)) {
            QCoreApplication::installTranslator(m_translator.get());
            m_qtLocale = QLocale(localeCode);
            m_loaded = true;
            qDebug() << "[TranslationManager] Loaded translations from:" << qmPath;
            return true;
        }
    }

    // Try loading Qt translations
    QString qtQmPath = QLibraryInfo::path(QLibraryInfo::TranslationsPath) + "/qtbase_" + localeCode + ".qm";
    if (QFile::exists(qtQmPath)) {
        QTranslator* qtTranslator = new QTranslator(qApp);
        if (qtTranslator->load(qtQmPath)) {
            QCoreApplication::installTranslator(qtTranslator);
        }
    }

    // Set Qt locale even if .qm not found (for formatting)
    m_qtLocale = QLocale(localeCode);
    m_loaded = false;

    qDebug() << "[TranslationManager] Using fallback translations for:" << localeCode;
    return true; // We have fallback translations
}

// ============================================================
// Load Locale
// ============================================================
bool TranslationManager::loadLocale(SupportedLocale locale) {
    QMutexLocker locker(&m_mutex);

    if (m_currentLocale == locale && m_loaded) {
        return true; // Already loaded
    }

    m_currentLocale = locale;
    bool success = installTranslator(locale);

    if (success) {
        emit localeChanged(locale);
        emit translationsLoaded();
    }

    return success;
}

bool TranslationManager::loadLocale(const QString& localeCode) {
    SupportedLocale locale = codeToLocale(localeCode);
    return loadLocale(locale);
}

// ============================================================
// Query Current Locale
// ============================================================
QString TranslationManager::currentLocaleCode() const {
    return localeToCode(m_currentLocale);
}

SupportedLocale TranslationManager::currentLocale() const {
    return m_currentLocale;
}

QString TranslationManager::currentLanguage() const {
    return localeToName(m_currentLocale);
}

// ============================================================
// Translation (tr)
// ============================================================
QString TranslationManager::tr(const QString& context, const QString& text) const {
    QMutexLocker locker(&m_mutex);

    if (m_loaded && m_translator) {
        QString translated = m_translator->translate(context.toUtf8().constData(),
                                                       text.toUtf8().constData());
        if (!translated.isEmpty() && translated != text) {
            return translated;
        }
    }

    // Fallback
    return lookupFallback(context, text);
}

QString TranslationManager::tr(const QString& context,
                                const QString& text,
                                const QString& disambiguation) const {
    QMutexLocker locker(&m_mutex);

    if (m_loaded && m_translator) {
        QString translated = m_translator->translate(context.toUtf8().constData(),
                                                       text.toUtf8().constData(),
                                                       disambiguation.toUtf8().constData());
        if (!translated.isEmpty() && translated != text) {
            return translated;
        }
    }

    return lookupFallback(context, text);
}

// ============================================================
// Fallback Translations (embedded for when .qm files missing)
// ============================================================
void TranslationManager::loadFallbackTranslations() {
    // Spanish translations (default - these are the source strings)
    m_fallbackTranslations["es"]["General|New Project"] = "Nuevo Proyecto";
    m_fallbackTranslations["es"]["General|Open Project"] = "Abrir Proyecto";
    m_fallbackTranslations["es"]["General|Save Project"] = "Guardar Proyecto";
    m_fallbackTranslations["es"]["General|Save As"] = "Guardar Como";
    m_fallbackTranslations["es"]["General|Print"] = "Imprimir";
    m_fallbackTranslations["es"]["General|Export"] = "Exportar";
    m_fallbackTranslations["es"]["General|Exit"] = "Salir";
    m_fallbackTranslations["es"]["General|Settings"] = "Configuracion";
    m_fallbackTranslations["es"]["General|Help"] = "Ayuda";
    m_fallbackTranslations["es"]["General|About"] = "Acerca de";

    // English translations
    m_fallbackTranslations["en"]["General|New Project"] = "New Project";
    m_fallbackTranslations["en"]["General|Open Project"] = "Open Project";
    m_fallbackTranslations["en"]["General|Save Project"] = "Save Project";
    m_fallbackTranslations["en"]["General|Save As"] = "Save As";
    m_fallbackTranslations["en"]["General|Print"] = "Print";
    m_fallbackTranslations["en"]["General|Export"] = "Export";
    m_fallbackTranslations["en"]["General|Exit"] = "Exit";
    m_fallbackTranslations["en"]["General|Settings"] = "Settings";
    m_fallbackTranslations["en"]["General|Help"] = "Help";
    m_fallbackTranslations["en"]["General|About"] = "About";

    // French
    m_fallbackTranslations["fr"]["General|New Project"] = "Nouveau Projet";
    m_fallbackTranslations["fr"]["General|Open Project"] = "Ouvrir Projet";
    m_fallbackTranslations["fr"]["General|Save Project"] = "Enregistrer Projet";
    m_fallbackTranslations["fr"]["General|Exit"] = "Quitter";
    m_fallbackTranslations["fr"]["General|Settings"] = "Parametres";
    m_fallbackTranslations["fr"]["General|Help"] = "Aide";

    // German
    m_fallbackTranslations["de"]["General|New Project"] = "Neues Projekt";
    m_fallbackTranslations["de"]["General|Open Project"] = "Projekt Oeffnen";
    m_fallbackTranslations["de"]["General|Save Project"] = "Projekt Speichern";
    m_fallbackTranslations["de"]["General|Exit"] = "Beenden";
    m_fallbackTranslations["de"]["General|Settings"] = "Einstellungen";
    m_fallbackTranslations["de"]["General|Help"] = "Hilfe";

    // Portuguese
    m_fallbackTranslations["pt"]["General|New Project"] = "Novo Projeto";
    m_fallbackTranslations["pt"]["General|Open Project"] = "Abrir Projeto";
    m_fallbackTranslations["pt"]["General|Save Project"] = "Salvar Projeto";
    m_fallbackTranslations["pt"]["General|Exit"] = "Sair";
    m_fallbackTranslations["pt"]["General|Settings"] = "Configuracoes";
    m_fallbackTranslations["pt"]["General|Help"] = "Ajuda";

    // Chinese
    m_fallbackTranslations["zh"]["General|New Project"] = "\u65b0\u9879\u76ee";
    m_fallbackTranslations["zh"]["General|Open Project"] = "\u6253\u5f00\u9879\u76ee";
    m_fallbackTranslations["zh"]["General|Save Project"] = "\u4fdd\u5b58\u9879\u76ee";
    m_fallbackTranslations["zh"]["General|Exit"] = "\u9000\u51fa";
    m_fallbackTranslations["zh"]["General|Settings"] = "\u8bbe\u7f6e";

    // Japanese
    m_fallbackTranslations["ja"]["General|New Project"] = "\u65b0\u898f\u30d7\u30ed\u30b8\u30a7\u30af\u30c8";
    m_fallbackTranslations["ja"]["General|Open Project"] = "\u30d7\u30ed\u30b8\u30a7\u30af\u30c8\u3092\u958b\u304f";
    m_fallbackTranslations["ja"]["General|Save Project"] = "\u30d7\u30ed\u30b8\u30a7\u30af\u30c8\u3092\u4fdd\u5b58";
    m_fallbackTranslations["ja"]["General|Exit"] = "\u7d42\u4e86";
    m_fallbackTranslations["ja"]["General|Settings"] = "\u8a2d\u5b9a";

    // Korean
    m_fallbackTranslations["ko"]["General|New Project"] = "\uc0c8 \ud504\ub85c\uc81d\ud2b8";
    m_fallbackTranslations["ko"]["General|Open Project"] = "\ud504\ub85c\uc81d\ud2b8 \uc5f4\uae30";
    m_fallbackTranslations["ko"]["General|Save Project"] = "\ud504\ub85c\uc81d\ud2b8 \uc800\uc7a5";
    m_fallbackTranslations["ko"]["General|Exit"] = "\uc885\ub8cc";
    m_fallbackTranslations["ko"]["General|Settings"] = "\uc124\uc815";

    // Arabic
    m_fallbackTranslations["ar"]["General|New Project"] = "\u0645\u0634\u0631\u0648\u0639 \u062c\u062f\u064a\u062f";
    m_fallbackTranslations["ar"]["General|Open Project"] = "\u0641\u062a\u062d \u0645\u0634\u0631\u0648\u0639";
    m_fallbackTranslations["ar"]["General|Save Project"] = "\u062d\u0641\u0638 \u0645\u0634\u0631\u0648\u0639";
    m_fallbackTranslations["ar"]["General|Exit"] = "\u062e\u0631\u0648\u062c";
    m_fallbackTranslations["ar"]["General|Settings"] = "\u0625\u0639\u062f\u0627\u062f\u0627\u062a";

    // Hindi
    m_fallbackTranslations["hi"]["General|New Project"] = "\u0928\u092f\u093e \u092a\u094d\u0930\u094b\u091c\u0947\u0915\u094d\u091f";
    m_fallbackTranslations["hi"]["General|Open Project"] = "\u092a\u094d\u0930\u094b\u091c\u0947\u0915\u094d\u091f \u0916\u094b\u0932\u0947\u0902";
    m_fallbackTranslations["hi"]["General|Save Project"] = "\u092a\u094d\u0930\u094b\u091c\u0947\u0915\u094d\u091f \u0938\u0939\u0947\u091c\u0947\u0902";
    m_fallbackTranslations["hi"]["General|Exit"] = "\u092c\u093e\u0939\u0930 \u0928\u093f\u0915\u0932\u0947\u0902";
    m_fallbackTranslations["hi"]["General|Settings"] = "\u0938\u0947\u091f\u093f\u0902\u0917\u094d\u0938";
}

QString TranslationManager::lookupFallback(const QString& context,
                                             const QString& text) const {
    QString localeCode = localeToCode(m_currentLocale);
    QString key = context + "|" + text;

    auto localeIt = m_fallbackTranslations.find(localeCode);
    if (localeIt != m_fallbackTranslations.end()) {
        auto textIt = localeIt->find(key);
        if (textIt != localeIt->end()) {
            return *textIt;
        }
    }

    // Return original text if no translation found
    return text;
}

// ============================================================
// Formatting
// ============================================================
QString TranslationManager::formatNumber(double value, int decimals) const {
    LocaleFormat fmt = getFormatForLocale(m_currentLocale);
    QString numStr = QString::number(value, 'f', decimals);
    numStr.replace(".", fmt.decimalSeparator);
    return numStr;
}

QString TranslationManager::formatInteger(int value) const {
    LocaleFormat fmt = getFormatForLocale(m_currentLocale);
    QString numStr = QString::number(value);
    // Add thousands separators
    QRegularExpression re(R"(\d{1,3}(?=(\d{3})+(?!\d)))");
    numStr.replace(re, "\\1" + fmt.thousandsSeparator);
    return numStr;
}

QString TranslationManager::formatDate(const QDate& date) const {
    LocaleFormat fmt = getFormatForLocale(m_currentLocale);
    return date.toString(fmt.dateFormat);
}

QString TranslationManager::formatDateTime(const QDateTime& dateTime) const {
    LocaleFormat fmt = getFormatForLocale(m_currentLocale);
    return dateTime.toString(fmt.dateTimeFormat);
}

QString TranslationManager::formatTime(const QTime& time) const {
    LocaleFormat fmt = getFormatForLocale(m_currentLocale);
    return time.toString(fmt.timeFormat);
}

QString TranslationManager::formatCurrency(double amount, const QString& currencyCode) const {
    LocaleFormat fmt = getFormatForLocale(m_currentLocale);
    QString code = currencyCode.isEmpty() ? fmt.currencyCode : currencyCode;
    QString symbol = fmt.currencySymbol;

    QString amountStr = formatNumber(amount, fmt.currencyDecimals);

    if (fmt.currencySymbolBefore) {
        return symbol + " " + amountStr + " " + code;
    } else {
        return amountStr + " " + symbol + " (" + code + ")";
    }
}

QString TranslationManager::formatPercentage(double value, int decimals) const {
    QString numStr = formatNumber(value, decimals);
    // Percentage symbol is generally after the number, but RTL locales may differ
    if (isRTL()) {
        return "% " + numStr;
    }
    return numStr + "%";
}

// ============================================================
// Locale Properties
// ============================================================
bool TranslationManager::isRTL() const {
    LocaleFormat fmt = getFormatForLocale(m_currentLocale);
    return fmt.layoutDirection == Qt::RightToLeft;
}

Qt::LayoutDirection TranslationManager::layoutDirection() const {
    return isRTL() ? Qt::RightToLeft : Qt::LeftToRight;
}

LocaleFormat TranslationManager::format() const {
    return getFormatForLocale(m_currentLocale);
}

// ============================================================
// Available Locales
// ============================================================
std::vector<SupportedLocale> TranslationManager::availableLocales() const {
    return {
        SupportedLocale::ES,
        SupportedLocale::EN,
        SupportedLocale::FR,
        SupportedLocale::DE,
        SupportedLocale::PT,
        SupportedLocale::ZH,
        SupportedLocale::JA,
        SupportedLocale::KO,
        SupportedLocale::AR,
        SupportedLocale::HI
    };
}

QStringList TranslationManager::availableLocaleCodes() const {
    QStringList codes;
    for (auto loc : availableLocales()) {
        codes << localeToCode(loc);
    }
    return codes;
}

QStringList TranslationManager::availableLocaleNames() const {
    QStringList names;
    for (auto loc : availableLocales()) {
        names << localeToName(loc);
    }
    return names;
}

// ============================================================
// System Locale Detection
// ============================================================
SupportedLocale TranslationManager::detectSystemLocale() {
    QLocale sysLocale = QLocale::system();
    QString code = sysLocale.name(); // e.g., "es_ES", "en_US"
    return codeToLocale(code);
}

// ============================================================
// QML Registration
// ============================================================
void TranslationManager::registerQmlTypes() {
    qmlRegisterSingletonType<TranslationManager>(
        "POWSYS365.I18n", 1, 0, "I18n",
        [](QQmlEngine*, QJSEngine*) -> QObject* {
            return &TranslationManager::instance();
        });
}

} // namespace i18n
} // namespace powsys365
