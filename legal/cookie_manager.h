#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <mutex>
#include <chrono>
#include <optional>
#include <functional>
#include <sstream>
#include <atomic>

namespace powsys365::legal {

// ============================================================================
// Tipos de Cookies (ePrivacy Directive 2002/58/EC + GDPR)
// ============================================================================

// Categorias segun guia AEPD/EDPB
enum class CookieCategory {
    NECESSARY,       // Estrictamente necesarias (exentas de consentimiento previo)
    PREFERENCES,     // Preferencias/funcionalidad
    STATISTICS,      // Estadisticas/analitica
    MARKETING,       // Marketing/publicidad
    THIRD_PARTY      // Cookies de terceros
};

// Nivel de riesgo para la privacidad
enum class CookieRiskLevel {
    MINIMAL,     // Necesarias
    LOW,         // Preferencias
    MEDIUM,      // Estadisticas anonimizadas
    HIGH         // Marketing, tracking, terceros
};

// Estado del consentimiento de cookies
enum class CookieConsentStatus {
    PENDING,           // Usuario aun no ha decidido
    ACCEPTED_ALL,      // Acepto todas
    ACCEPTED_SELECTED, // Acepto seleccionadas
    REJECTED_ALL       // Rechazo todas excepto necesarias
};

// ============================================================================
// Estructuras
// ============================================================================

struct CookieDefinition {
    std::string name;
    std::string domain;
    std::string description;
    CookieCategory category;
    CookieRiskLevel riskLevel;
    int maxAgeSeconds;               // Duracion
    bool httpOnly;
    bool secure;
    std::string sameSite;            // Strict, Lax, None
    std::string purpose;             // Finalidad especifica
    std::string thirdPartyInfo;      // Info del tercero si aplica
    bool sessionCookie;              // true = se elimina al cerrar navegador
    std::string dataCollected;       // Que datos recopila
};

struct CookieConsentRecord {
    int    userId;
    CookieConsentStatus overallStatus;
    std::map<CookieCategory, bool> categoryConsent; // true = aceptado
    std::chrono::system_clock::time_point consentGivenAt;
    std::chrono::system_clock::time_point expiresAt;
    std::string consentVersion;
    std::string ipAddress;
    std::string userAgent;
    bool   isActive() const {
        return overallStatus == CookieConsentStatus::ACCEPTED_ALL ||
               overallStatus == CookieConsentStatus::ACCEPTED_SELECTED;
    }
};

struct CookieBannerConfig {
    std::string title;
    std::string message;
    std::string acceptAllText;
    std::string rejectAllText;
    std::string customizeText;
    std::string privacyPolicyUrl;
    std::string cookiePolicyUrl;
    bool showRejectAllButton;
    bool granularConsent;        // Permitir seleccion por categorias
    bool showThirdPartyList;
    int bannerPosition;          // 0=center, 1=bottom, 2=top
    std::string brandColor;
    std::string companyName;
};

struct CookieScanResult {
    int totalCookies;
    int necessaryCount;
    int preferencesCount;
    int statisticsCount;
    int marketingCount;
    int thirdPartyCount;
    std::vector<CookieDefinition> cookies;
    std::string riskAssessment;
};

// ============================================================================
// CookieManager
// ============================================================================

class CookieManager {
public:
    CookieManager();

    // --- Registro de cookies ---
    void registerCookie(const CookieDefinition& cookie);
    void registerNecessaryCookie(const std::string& name,
                                  const std::string& description,
                                  const std::string& purpose);
    void registerPreferenceCookie(const std::string& name,
                                   const std::string& description,
                                   const std::string& purpose);
    void registerStatisticsCookie(const std::string& name,
                                   const std::string& description,
                                   const std::string& purpose,
                                   const std::string& dataCollected = "");
    void registerMarketingCookie(const std::string& name,
                                  const std::string& description,
                                  const std::string& purpose,
                                  const std::string& thirdPartyInfo = "");

    // --- Consentimiento ---
    CookieConsentRecord acceptAllCookies(int userId,
                                          const std::string& ipAddress = "",
                                          const std::string& userAgent = "");
    CookieConsentRecord rejectAllCookies(int userId,
                                          const std::string& ipAddress = "",
                                          const std::string& userAgent = "");
    CookieConsentRecord acceptSelectedCookies(int userId,
                                               const std::map<CookieCategory, bool>& selections,
                                               const std::string& ipAddress = "",
                                               const std::string& userAgent = "");
    CookieConsentRecord updateCookieConsent(int userId,
                                             const std::map<CookieCategory, bool>& selections);
    bool revokeCookieConsent(int userId);

    // --- Consultas ---
    bool hasCookieConsent(int userId, CookieCategory category) const;
    bool isCookieAllowed(const std::string& cookieName, int userId) const;
    std::optional<CookieConsentRecord> getConsentRecord(int userId) const;
    std::vector<CookieDefinition> getCookiesByCategory(CookieCategory category) const;
    std::vector<CookieDefinition> getThirdPartyCookies() const;
    int getCookieCount() const;
    int getThirdPartyCookieCount() const;

    // --- Banner de consentimiento ---
    void configureBanner(const CookieBannerConfig& config);
    CookieBannerConfig getBannerConfig() const;
    std::string generateBannerHtml() const;
    std::string generateCookiePolicyHtml() const;
    std::string generateCookieDeclarationJson() const;

    // --- Escaneo y cumplimiento ---
    CookieScanResult scanCookies() const;
    std::string generateComplianceReport() const;
    bool isGdprCompliant() const;

    // --- Callbacks ---
    using ConsentChangeCallback = std::function<void(int userId, const CookieConsentRecord&)>;
    void onConsentChange(ConsentChangeCallback callback);

    // --- Utilidades ---
    static std::string categoryToString(CookieCategory cat);
    static std::string categoryToSpanish(CookieCategory cat);
    static std::string categoryDescription(CookieCategory cat);
    static CookieRiskLevel categoryToRiskLevel(CookieCategory cat);

private:
    mutable std::mutex cookiesMutex_;
    mutable std::mutex consentMutex_;
    mutable std::mutex bannerMutex_;
    mutable std::mutex callbacksMutex_;

    std::vector<CookieDefinition> cookies_;
    std::map<int, CookieConsentRecord> consentRecords_;
    CookieBannerConfig bannerConfig_;
    ConsentChangeCallback consentCallback_;

    std::string consentVersion_ = "1.0";

    // --- Utilidades ---
    bool cookieExists(const std::string& name) const;
    void notifyConsentChange(int userId, const CookieConsentRecord& record);
    std::string escapeHtml(const std::string& text) const;
    std::string generateCategorySection(CookieCategory cat) const;
};

} // namespace powsys365::legal
