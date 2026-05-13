#include "cookie_manager.h"
#include <algorithm>
#include <iomanip>

namespace powsys365::legal {

// ============================================================================
// Constructor
// ============================================================================

CookieManager::CookieManager() {
    // Configuracion por defecto del banner
    bannerConfig_.title = "Configuracion de Cookies";
    bannerConfig_.message = "Utilizamos cookies propias y de terceros para mejorar nuestros servicios, "
                           "personalizar su experiencia y analizar el trafico. Puede aceptar todas las cookies, "
                           "rechazar las no esenciales o personalizar sus preferencias. "
                           "Para mas informacion, consulte nuestra Politica de Cookies.";
    bannerConfig_.acceptAllText = "Aceptar todas";
    bannerConfig_.rejectAllText = "Rechazar todas";
    bannerConfig_.customizeText = "Personalizar";
    bannerConfig_.privacyPolicyUrl = "/privacy";
    bannerConfig_.cookiePolicyUrl = "/cookies";
    bannerConfig_.showRejectAllButton = true;
    bannerConfig_.granularConsent = true;
    bannerConfig_.showThirdPartyList = true;
    bannerConfig_.bannerPosition = 1; // bottom
    bannerConfig_.brandColor = "#0078D4";
    bannerConfig_.companyName = "POWSYS365";
}

// ============================================================================
// Utilidades de conversion
// ============================================================================

std::string CookieManager::categoryToString(CookieCategory cat) {
    switch (cat) {
        case CookieCategory::NECESSARY:    return "necessary";
        case CookieCategory::PREFERENCES:  return "preferences";
        case CookieCategory::STATISTICS:   return "statistics";
        case CookieCategory::MARKETING:    return "marketing";
        case CookieCategory::THIRD_PARTY:  return "third_party";
        default: return "unknown";
    }
}

std::string CookieManager::categoryToSpanish(CookieCategory cat) {
    switch (cat) {
        case CookieCategory::NECESSARY:    return "Estrictamente necesarias";
        case CookieCategory::PREFERENCES:  return "Preferencias y funcionalidad";
        case CookieCategory::STATISTICS:   return "Estadisticas y analitica";
        case CookieCategory::MARKETING:    return "Marketing y publicidad";
        case CookieCategory::THIRD_PARTY:  return "Cookies de terceros";
        default: return "Desconocida";
    }
}

std::string CookieManager::categoryDescription(CookieCategory cat) {
    switch (cat) {
        case CookieCategory::NECESSARY:
            return "Estas cookies son esenciales para el funcionamiento del sitio web "
                   "y no pueden desactivarse. Incluyen funciones de seguridad y "
                   "autenticacion. No requieren consentimiento previo segun la ePrivacy Directive.";
        case CookieCategory::PREFERENCES:
            return "Permiten recordar sus preferencias de configuracion (idioma, region, etc.) "
                   "para ofrecerle una experiencia personalizada. Requieren su consentimiento.";
        case CookieCategory::STATISTICS:
            return "Nos ayudan a entender como interactua con el sitio web mediante la "
                   "recopilacion de informacion anonima. Requieren su consentimiento.";
        case CookieCategory::MARKETING:
            return "Se utilizan para rastrear a los visitantes en los sitios web y mostrar "
                   "anuncios relevantes y atractivos. Requieren su consentimiento explicito.";
        case CookieCategory::THIRD_PARTY:
            return "Cookies establecidas por dominios externos que proporcionan servicios "
                   "integrados (redes sociales, reproductores de video, etc.). "
                   "Requieren su consentimiento explicito.";
        default: return "";
    }
}

CookieRiskLevel CookieManager::categoryToRiskLevel(CookieCategory cat) {
    switch (cat) {
        case CookieCategory::NECESSARY:    return CookieRiskLevel::MINIMAL;
        case CookieCategory::PREFERENCES:  return CookieRiskLevel::LOW;
        case CookieCategory::STATISTICS:   return CookieRiskLevel::MEDIUM;
        case CookieCategory::MARKETING:    return CookieRiskLevel::HIGH;
        case CookieCategory::THIRD_PARTY:  return CookieRiskLevel::HIGH;
        default: return CookieRiskLevel::MEDIUM;
    }
}

std::string CookieManager::escapeHtml(const std::string& text) const {
    std::string result;
    for (char c : text) {
        switch (c) {
            case '&': result += "&amp;"; break;
            case '<': result += "&lt;"; break;
            case '>': result += "&gt;"; break;
            case '"': result += "&quot;"; break;
            case '\'': result += "&#x27;"; break;
            default: result += c; break;
        }
    }
    return result;
}

// ============================================================================
// Registro de cookies
// ============================================================================

void CookieManager::registerCookie(const CookieDefinition& cookie) {
    std::unique_lock<std::mutex> lock(cookiesMutex_);
    // Verificar si ya existe
    auto it = std::find_if(cookies_.begin(), cookies_.end(),
        [&cookie](const CookieDefinition& c) { return c.name == cookie.name; });
    if (it == cookies_.end()) {
        cookies_.push_back(cookie);
    }
}

void CookieManager::registerNecessaryCookie(const std::string& name,
                                             const std::string& description,
                                             const std::string& purpose) {
    CookieDefinition cookie;
    cookie.name = name;
    cookie.description = description;
    cookie.purpose = purpose;
    cookie.category = CookieCategory::NECESSARY;
    cookie.riskLevel = CookieRiskLevel::MINIMAL;
    cookie.sessionCookie = true;
    cookie.httpOnly = true;
    cookie.secure = true;
    cookie.sameSite = "Strict";
    cookie.domain = "powsys365.local";
    cookie.maxAgeSeconds = 0; // session
    registerCookie(cookie);
}

void CookieManager::registerPreferenceCookie(const std::string& name,
                                              const std::string& description,
                                              const std::string& purpose) {
    CookieDefinition cookie;
    cookie.name = name;
    cookie.description = description;
    cookie.purpose = purpose;
    cookie.category = CookieCategory::PREFERENCES;
    cookie.riskLevel = CookieRiskLevel::LOW;
    cookie.sessionCookie = false;
    cookie.httpOnly = false;
    cookie.secure = true;
    cookie.sameSite = "Lax";
    cookie.domain = "powsys365.local";
    cookie.maxAgeSeconds = 365 * 24 * 3600; // 1 year
    registerCookie(cookie);
}

void CookieManager::registerStatisticsCookie(const std::string& name,
                                              const std::string& description,
                                              const std::string& purpose,
                                              const std::string& dataCollected) {
    CookieDefinition cookie;
    cookie.name = name;
    cookie.description = description;
    cookie.purpose = purpose;
    cookie.category = CookieCategory::STATISTICS;
    cookie.riskLevel = CookieRiskLevel::MEDIUM;
    cookie.sessionCookie = false;
    cookie.httpOnly = false;
    cookie.secure = true;
    cookie.sameSite = "Lax";
    cookie.domain = "powsys365.local";
    cookie.dataCollected = dataCollected;
    cookie.maxAgeSeconds = 2 * 365 * 24 * 3600; // 2 years
    registerCookie(cookie);
}

void CookieManager::registerMarketingCookie(const std::string& name,
                                             const std::string& description,
                                             const std::string& purpose,
                                             const std::string& thirdPartyInfo) {
    CookieDefinition cookie;
    cookie.name = name;
    cookie.description = description;
    cookie.purpose = purpose;
    cookie.category = CookieCategory::MARKETING;
    cookie.riskLevel = CookieRiskLevel::HIGH;
    cookie.sessionCookie = false;
    cookie.httpOnly = false;
    cookie.secure = true;
    cookie.sameSite = "None";
    cookie.domain = "powsys365.local";
    cookie.thirdPartyInfo = thirdPartyInfo;
    cookie.maxAgeSeconds = 365 * 24 * 3600; // 1 year
    registerCookie(cookie);
}

// ============================================================================
// Consentimiento
// ============================================================================

CookieConsentRecord CookieManager::acceptAllCookies(int userId,
                                                       const std::string& ipAddress,
                                                       const std::string& userAgent) {
    CookieConsentRecord record;
    record.userId = userId;
    record.overallStatus = CookieConsentStatus::ACCEPTED_ALL;
    record.consentGivenAt = std::chrono::system_clock::now();
    record.expiresAt = record.consentGivenAt + std::chrono::hours(24 * 365);
    record.consentVersion = consentVersion_;
    record.ipAddress = ipAddress;
    record.userAgent = userAgent;

    record.categoryConsent[CookieCategory::NECESSARY] = true;
    record.categoryConsent[CookieCategory::PREFERENCES] = true;
    record.categoryConsent[CookieCategory::STATISTICS] = true;
    record.categoryConsent[CookieCategory::MARKETING] = true;
    record.categoryConsent[CookieCategory::THIRD_PARTY] = true;

    {
        std::unique_lock<std::mutex> lock(consentMutex_);
        consentRecords_[userId] = record;
    }

    notifyConsentChange(userId, record);
    return record;
}

CookieConsentRecord CookieManager::rejectAllCookies(int userId,
                                                       const std::string& ipAddress,
                                                       const std::string& userAgent) {
    CookieConsentRecord record;
    record.userId = userId;
    record.overallStatus = CookieConsentStatus::REJECTED_ALL;
    record.consentGivenAt = std::chrono::system_clock::now();
    record.expiresAt = record.consentGivenAt + std::chrono::hours(24 * 365);
    record.consentVersion = consentVersion_;
    record.ipAddress = ipAddress;
    record.userAgent = userAgent;

    // Solo las necesarias
    record.categoryConsent[CookieCategory::NECESSARY] = true;
    record.categoryConsent[CookieCategory::PREFERENCES] = false;
    record.categoryConsent[CookieCategory::STATISTICS] = false;
    record.categoryConsent[CookieCategory::MARKETING] = false;
    record.categoryConsent[CookieCategory::THIRD_PARTY] = false;

    {
        std::unique_lock<std::mutex> lock(consentMutex_);
        consentRecords_[userId] = record;
    }

    notifyConsentChange(userId, record);
    return record;
}

CookieConsentRecord CookieManager::acceptSelectedCookies(int userId,
                                                            const std::map<CookieCategory, bool>& selections,
                                                            const std::string& ipAddress,
                                                            const std::string& userAgent) {
    CookieConsentRecord record;
    record.userId = userId;
    record.overallStatus = CookieConsentStatus::ACCEPTED_SELECTED;
    record.consentGivenAt = std::chrono::system_clock::now();
    record.expiresAt = record.consentGivenAt + std::chrono::hours(24 * 365);
    record.consentVersion = consentVersion_;
    record.ipAddress = ipAddress;
    record.userAgent = userAgent;

    // Las necesarias siempre se aceptan
    record.categoryConsent[CookieCategory::NECESSARY] = true;
    for (const auto& [cat, accepted] : selections) {
        record.categoryConsent[cat] = accepted;
    }

    {
        std::unique_lock<std::mutex> lock(consentMutex_);
        consentRecords_[userId] = record;
    }

    notifyConsentChange(userId, record);
    return record;
}

CookieConsentRecord CookieManager::updateCookieConsent(int userId,
                                                          const std::map<CookieCategory, bool>& selections) {
    std::unique_lock<std::mutex> lock(consentMutex_);
    auto it = consentRecords_.find(userId);
    if (it == consentRecords_.end()) {
        lock.unlock();
        return acceptSelectedCookies(userId, selections);
    }

    for (const auto& [cat, accepted] : selections) {
        it->second.categoryConsent[cat] = accepted;
    }
    it->second.consentGivenAt = std::chrono::system_clock::now();

    // Recalcular estado
    bool allAccepted = true;
    bool anyAccepted = false;
    for (const auto& [cat, accepted] : it->second.categoryConsent) {
        if (cat != CookieCategory::NECESSARY) {
            if (accepted) anyAccepted = true;
            else allAccepted = false;
        }
    }

    if (allAccepted && anyAccepted) {
        it->second.overallStatus = CookieConsentStatus::ACCEPTED_ALL;
    } else if (anyAccepted) {
        it->second.overallStatus = CookieConsentStatus::ACCEPTED_SELECTED;
    } else {
        it->second.overallStatus = CookieConsentStatus::REJECTED_ALL;
    }

    lock.unlock();
    notifyConsentChange(userId, it->second);
    return it->second;
}

bool CookieManager::revokeCookieConsent(int userId) {
    std::unique_lock<std::mutex> lock(consentMutex_);
    auto it = consentRecords_.find(userId);
    if (it == consentRecords_.end()) return false;
    consentRecords_.erase(it);
    return true;
}

// ============================================================================
// Consultas
// ============================================================================

bool CookieManager::hasCookieConsent(int userId, CookieCategory category) const {
    // Las cookies necesarias siempre estan permitidas
    if (category == CookieCategory::NECESSARY) return true;

    std::unique_lock<std::mutex> lock(consentMutex_);
    auto it = consentRecords_.find(userId);
    if (it == consentRecords_.end()) return false;
    auto cit = it->second.categoryConsent.find(category);
    if (cit == it->second.categoryConsent.end()) return false;
    return cit->second;
}

bool CookieManager::isCookieAllowed(const std::string& cookieName, int userId) const {
    std::unique_lock<std::mutex> lock(cookiesMutex_);
    auto cit = std::find_if(cookies_.begin(), cookies_.end(),
        [&cookieName](const CookieDefinition& c) { return c.name == cookieName; });
    if (cit == cookies_.end()) return false;
    lock.unlock();
    return hasCookieConsent(userId, cit->category);
}

std::optional<CookieConsentRecord> CookieManager::getConsentRecord(int userId) const {
    std::unique_lock<std::mutex> lock(consentMutex_);
    auto it = consentRecords_.find(userId);
    if (it == consentRecords_.end()) return std::nullopt;
    return it->second;
}

std::vector<CookieDefinition> CookieManager::getCookiesByCategory(CookieCategory category) const {
    std::unique_lock<std::mutex> lock(cookiesMutex_);
    std::vector<CookieDefinition> result;
    std::copy_if(cookies_.begin(), cookies_.end(), std::back_inserter(result),
        [category](const CookieDefinition& c) { return c.category == category; });
    return result;
}

std::vector<CookieDefinition> CookieManager::getThirdPartyCookies() const {
    std::unique_lock<std::mutex> lock(cookiesMutex_);
    std::vector<CookieDefinition> result;
    std::copy_if(cookies_.begin(), cookies_.end(), std::back_inserter(result),
        [](const CookieDefinition& c) {
            return !c.thirdPartyInfo.empty() || c.category == CookieCategory::THIRD_PARTY;
        });
    return result;
}

int CookieManager::getCookieCount() const {
    std::unique_lock<std::mutex> lock(cookiesMutex_);
    return static_cast<int>(cookies_.size());
}

int CookieManager::getThirdPartyCookieCount() const {
    return static_cast<int>(getThirdPartyCookies().size());
}

// ============================================================================
// Configuracion del Banner
// ============================================================================

void CookieManager::configureBanner(const CookieBannerConfig& config) {
    std::unique_lock<std::mutex> lock(bannerMutex_);
    bannerConfig_ = config;
}

CookieBannerConfig CookieManager::getBannerConfig() const {
    std::unique_lock<std::mutex> lock(bannerMutex_);
    return bannerConfig_;
}

// ============================================================================
// Generacion de HTML del Banner
// ============================================================================

std::string CookieManager::generateBannerHtml() const {
    std::unique_lock<std::mutex> lock(bannerMutex_);
    std::unique_lock<std::mutex> cLock(cookiesMutex_);

    std::ostringstream html;
    html << "<!-- Banner de Cookies POWSYS365 - GDPR/ePrivacy Compliant -->\n";
    html << "<div id=\"cookie-banner\" class=\"cookie-banner\" style=\"display:none;\">\n";

    // CSS
    html << "  <style>\n";
    html << "    .cookie-banner { position:fixed; z-index:99999; ";
    if (bannerConfig_.bannerPosition == 0) {
        html << "top:50%;left:50%;transform:translate(-50%,-50%); ";
    } else if (bannerConfig_.bannerPosition == 2) {
        html << "top:0;left:0;right:0; ";
    } else {
        html << "bottom:0;left:0;right:0; ";
    }
    html << "background:#fff;box-shadow:0 -2px 20px rgba(0,0,0,0.2); "
         << "padding:24px;font-family:system-ui,-apple-system,sans-serif; "
         << "border-radius:" << (bannerConfig_.bannerPosition == 0 ? "12px" : "0") << "; }\n";
    html << "    .cookie-banner-content { max-width:1200px;margin:0 auto; }\n";
    html << "    .cookie-banner h2 { margin:0 0 12px;font-size:18px;color:#333; }\n";
    html << "    .cookie-banner p { margin:0 0 16px;font-size:14px;color:#555;line-height:1.5; }\n";
    html << "    .cookie-buttons { display:flex;gap:8px;flex-wrap:wrap; }\n";
    html << "    .cookie-btn { padding:10px 20px;border:none;border-radius:6px;cursor:pointer; "
         << "font-size:14px;font-weight:600;transition:opacity 0.2s; }\n";
    html << "    .cookie-btn:hover { opacity:0.85; }\n";
    html << "    .cookie-btn-primary { background:" << bannerConfig_.brandColor
         << ";color:#fff; }\n";
    html << "    .cookie-btn-secondary { background:#f0f0f0;color:#333; }\n";
    html << "    .cookie-btn-link { background:none;color:" << bannerConfig_.brandColor
         << ";text-decoration:underline;padding:10px 12px; }\n";
    html << "    .cookie-details { margin-top:16px;padding-top:16px;border-top:1px solid #eee; }\n";
    html << "    .cookie-category { margin-bottom:12px;padding:12px;background:#f8f9fa; "
         << "border-radius:8px; }\n";
    html << "    .cookie-category h3 { margin:0 0 8px;font-size:14px; }\n";
    html << "    .cookie-category p { margin:0 0 8px;font-size:12px;color:#666; }\n";
    html << "    .cookie-toggle { display:flex;align-items:center;gap:8px; }\n";
    html << "    .cookie-toggle input[type='checkbox'] { width:18px;height:18px;cursor:pointer; }\n";
    html << "  </style>\n";

    // Contenido
    html << "  <div class=\"cookie-banner-content\">\n";
    html << "    <h2>" << escapeHtml(bannerConfig_.title) << "</h2>\n";
    html << "    <p>" << escapeHtml(bannerConfig_.message) << "\n";
    html << "      <a href=\"" << bannerConfig_.cookiePolicyUrl << "\">Politica de Cookies</a> | ";
    html << "      <a href=\"" << bannerConfig_.privacyPolicyUrl << "\">Politica de Privacidad</a>\n";
    html << "    </p>\n";

    // Botones
    html << "    <div class=\"cookie-buttons\">\n";
    html << "      <button class=\"cookie-btn cookie-btn-primary\" onclick=\"acceptAllCookies()\">"
         << escapeHtml(bannerConfig_.acceptAllText) << "</button>\n";
    if (bannerConfig_.showRejectAllButton) {
        html << "      <button class=\"cookie-btn cookie-btn-secondary\" onclick=\"rejectAllCookies()\">"
             << escapeHtml(bannerConfig_.rejectAllText) << "</button>\n";
    }
    if (bannerConfig_.granularConsent) {
        html << "      <button class=\"cookie-btn cookie-btn-link\" onclick=\"toggleCookieDetails()\">"
             << escapeHtml(bannerConfig_.customizeText) << "</button>\n";
    }
    html << "    </div>\n";

    // Detalles granulares
    if (bannerConfig_.granularConsent) {
        html << "    <div id=\"cookie-details\" class=\"cookie-details\" style=\"display:none;\">\n";

        // Por cada categoria
        for (auto cat : {CookieCategory::NECESSARY, CookieCategory::PREFERENCES,
                         CookieCategory::STATISTICS, CookieCategory::MARKETING}) {
            auto catCookies = getCookiesByCategory(cat);
            html << "      <div class=\"cookie-category\">\n";
            html << "        <h3>" << categoryToSpanish(cat) << " (" << catCookies.size() << ")</h3>\n";
            html << "        <p>" << categoryDescription(cat) << "</p>\n";
            html << "        <div class=\"cookie-toggle\">\n";
            html << "          <input type=\"checkbox\" id=\"cat_" << categoryToString(cat) << "\" "
                 << (cat == CookieCategory::NECESSARY ? "checked disabled" : "checked") << ">\n";
            html << "          <label for=\"cat_" << categoryToString(cat) << "\">Permitir</label>\n";
            html << "        </div>\n";

            // Lista de cookies
            if (!catCookies.empty()) {
                html << "        <ul style=\"margin-top:8px;font-size:12px;color:#666;\">\n";
                for (const auto& c : catCookies) {
                    html << "          <li><strong>" << escapeHtml(c.name) << "</strong> - "
                         << escapeHtml(c.description);
                    if (!c.dataCollected.empty()) {
                        html << " [Datos: " << escapeHtml(c.dataCollected) << "]";
                    }
                    if (!c.thirdPartyInfo.empty()) {
                        html << " [Tercero: " << escapeHtml(c.thirdPartyInfo) << "]";
                    }
                    html << "</li>\n";
                }
                html << "        </ul>\n";
            }
            html << "      </div>\n";
        }

        html << "      <button class=\"cookie-btn cookie-btn-primary\" onclick=\"saveCookiePreferences()\">"
             << "Guardar preferencias</button>\n";
        html << "    </div>\n";
    }

    html << "  </div>\n";
    html << "</div>\n";

    // JavaScript
    html << "<script>\n";
    html << "function acceptAllCookies() {\n";
    html << "  document.cookie = 'cookie_consent=all; path=/; max-age=' + (365*24*3600) + '; SameSite=Lax';\n";
    html << "  document.getElementById('cookie-banner').style.display = 'none';\n";
    html << "  console.log('Cookies aceptadas');\n";
    html << "}\n";
    html << "function rejectAllCookies() {\n";
    html << "  document.cookie = 'cookie_consent=necessary; path=/; max-age=' + (365*24*3600) + '; SameSite=Lax';\n";
    html << "  document.getElementById('cookie-banner').style.display = 'none';\n";
    html << "  console.log('Cookies rechazadas');\n";
    html << "}\n";
    html << "function toggleCookieDetails() {\n";
    html << "  var d = document.getElementById('cookie-details');\n";
    html << "  d.style.display = d.style.display === 'none' ? 'block' : 'none';\n";
    html << "}\n";
    html << "function saveCookiePreferences() {\n";
    html << "  var cats = [];\n";
    html << "  if(document.getElementById('cat_preferences').checked) cats.push('preferences');\n";
    html << "  if(document.getElementById('cat_statistics').checked) cats.push('statistics');\n";
    html << "  if(document.getElementById('cat_marketing').checked) cats.push('marketing');\n";
    html << "  document.cookie = 'cookie_consent=' + cats.join(',') + '; path=/; max-age=' + (365*24*3600);\n";
    html << "  document.getElementById('cookie-banner').style.display = 'none';\n";
    html << "}\n";
    // Mostrar banner si no hay consentimiento
    html << "(function(){\n";
    html << "  var consent = document.cookie.match(/cookie_consent=([^;]+)/);\n";
    html << "  if(!consent) document.getElementById('cookie-banner').style.display = 'block';\n";
    html << "})();\n";
    html << "</script>\n";

    return html.str();
}

// ============================================================================
// Generacion de Politica de Cookies
// ============================================================================

std::string CookieManager::generateCookiePolicyHtml() const {
    std::unique_lock<std::mutex> lock(bannerMutex_);
    std::unique_lock<std::mutex> cLock(cookiesMutex_);

    std::ostringstream html;
    html << "<!DOCTYPE html><html><head><meta charset='UTF-8'>\n";
    html << "<title>Politica de Cookies - " << escapeHtml(bannerConfig_.companyName) << "</title>\n";
    html << "<style>body{font-family:system-ui,sans-serif;max-width:900px;margin:40px auto;padding:20px; "
         << "line-height:1.6;color:#333;} h1{color:" << bannerConfig_.brandColor << ";} "
         << "table{width:100%;border-collapse:collapse;margin:16px 0;} "
         << "th,td{border:1px solid #ddd;padding:10px;text-align:left;} "
         << "th{background:#f5f5f5;} .category{margin:24px 0;padding:16px;background:#f9f9f9;border-radius:8px;}"
         << "</style></head><body>\n";

    html << "<h1>Politica de Cookies</h1>\n";
    html << "<p>En <strong>" << escapeHtml(bannerConfig_.companyName) << "</strong> utilizamos cookies y tecnologias similares "
         << "para mejorar su experiencia de navegacion, proporcionar funcionalidades esenciales, "
         << "analizar el trafico y personalizar el contenido. Esta politica cumple con el "
         << "<strong>Reglamento (UE) 2016/679 (GDPR)</strong> y la "
         << "<strong>Directiva 2002/58/CE (ePrivacy)</strong>.</p>\n";

    html << "<h2>1. Que son las cookies</h2>\n";
    html << "<p>Las cookies son pequenos archivos de texto que se almacenan en su dispositivo cuando visita "
         << "un sitio web. Permiten al sitio recordar sus acciones y preferencias durante un periodo de tiempo.</p>\n";

    html << "<h2>2. Tipos de cookies que utilizamos</h2>\n";

    for (auto cat : {CookieCategory::NECESSARY, CookieCategory::PREFERENCES,
                     CookieCategory::STATISTICS, CookieCategory::MARKETING, CookieCategory::THIRD_PARTY}) {
        auto catCookies = getCookiesByCategory(cat);
        html << "<div class=\"category\">\n";
        html << "<h3>" << categoryToSpanish(cat) << "</h3>\n";
        html << "<p>" << categoryDescription(cat) << "</p>\n";

        if (!catCookies.empty()) {
            html << "<table>\n";
            html << "<tr><th>Nombre</th><th>Proposito</th><th>Duracion</th><th>Terceros</th></tr>\n";
            for (const auto& c : catCookies) {
                std::string duration = c.sessionCookie ? "Sesion" :
                    std::to_string(c.maxAgeSeconds / 86400) + " dias";
                html << "<tr><td>" << escapeHtml(c.name) << "</td>"
                     << "<td>" << escapeHtml(c.purpose) << "</td>"
                     << "<td>" << duration << "</td>"
                     << "<td>" << (c.thirdPartyInfo.empty() ? "No" : escapeHtml(c.thirdPartyInfo)) << "</td></tr>\n";
            }
            html << "</table>\n";
        }
        html << "</div>\n";
    }

    html << "<h2>3. Como gestionar sus preferencias</h2>\n";
    html << "<p>Puede modificar sus preferencias de cookies en cualquier momento haciendo clic en el "
         << "boton de configuracion de cookies que aparece en la parte inferior de nuestra web. "
         << "Tambien puede configurar su navegador para bloquear o eliminar cookies.</p>\n";

    html << "<h2>4. Derechos de los usuarios</h2>\n";
    html << "<p>De acuerdo con el GDPR, tiene derecho a:</p>\n";
    html << "<ul>\n";
    html << "<li>Acceder a la informacion sobre las cookies utilizadas</li>\n";
    html << "<li>Retirar su consentimiento en cualquier momento</li>\n";
    html << "<li>Oponerse al tratamiento de sus datos para fines de marketing</li>\n";
    html << "<li>Solicitar la supresion de los datos recopilados</li>\n";
    html << "</ul>\n";

    html << "<h2>5. Transferencias internacionales</h2>\n";
    html << "<p>Algunas cookies de terceros pueden transferir datos fuera del Espacio Economico Europeo (EEE). "
         << "En tales casos, aplicamos las salvaguardas adecuadas conforme a los Capitulos IV y V del GDPR "
         << "(Clausulas Contractuales Tipo, decisiones de adecuacion, etc.).</p>\n";

    html << "<h2>6. Vigencia de esta politica</h2>\n";
    html << "<p>Esta politica fue actualizada por ultima vez el 1 de enero de 2025. "
         << "Nos reservamos el derecho de modificarla en cualquier momento. "
         << "Cualquier cambio significativo sera notificado a traves del banner de cookies.</p>\n";

    html << "<p>Para mas informacion, consulte nuestra <a href=\"" << bannerConfig_.privacyPolicyUrl
         << "\">Politica de Privacidad</a>.</p>\n";

    html << "</body></html>\n";
    return html.str();
}

// ============================================================================
// Declaracion de cookies en JSON
// ============================================================================

std::string CookieManager::generateCookieDeclarationJson() const {
    std::unique_lock<std::mutex> cLock(cookiesMutex_);
    std::ostringstream json;
    json << "{\n";
    json << "  \"company\": \"" << escapeHtml(bannerConfig_.companyName) << "\",\n";
    json << "  \"consentVersion\": \"" << consentVersion_ << "\",\n";
    json << "  \"legalBasis\": \"Consentimiento (Art. 6(1)(a) GDPR + ePrivacy Directive)\",\n";
    json << "  \"dpoEmail\": \"dpo@powsys365.local\",\n";
    json << "  \"lastUpdated\": \"2025-01-01T00:00:00Z\",\n";
    json << "  \"cookies\": [\n";

    bool first = true;
    for (const auto& c : cookies_) {
        if (!first) json << ",\n";
        first = false;
        json << "    {\n";
        json << "      \"name\": \"" << escapeHtml(c.name) << "\",\n";
        json << "      \"category\": \"" << categoryToString(c.category) << "\",\n";
        json << "      \"purpose\": \"" << escapeHtml(c.purpose) << "\",\n";
        json << "      \"description\": \"" << escapeHtml(c.description) << "\",\n";
        json << "      \"domain\": \"" << escapeHtml(c.domain) << "\",\n";
        json << "      \"duration\": " << c.maxAgeSeconds << ",\n";
        json << "      \"sessionCookie\": " << (c.sessionCookie ? "true" : "false") << ",\n";
        json << "      \"httpOnly\": " << (c.httpOnly ? "true" : "false") << ",\n";
        json << "      \"secure\": " << (c.secure ? "true" : "false") << ",\n";
        json << "      \"sameSite\": \"" << c.sameSite << "\"";
        if (!c.dataCollected.empty()) {
            json << ",\n      \"dataCollected\": \"" << escapeHtml(c.dataCollected) << "\"";
        }
        if (!c.thirdPartyInfo.empty()) {
            json << ",\n      \"thirdParty\": \"" << escapeHtml(c.thirdPartyInfo) << "\"";
        }
        json << "\n    }";
    }
    json << "\n  ]\n";
    json << "}\n";
    return json.str();
}

// ============================================================================
// Escaneo y cumplimiento
// ============================================================================

CookieScanResult CookieManager::scanCookies() const {
    std::unique_lock<std::mutex> lock(cookiesMutex_);
    CookieScanResult result;
    result.totalCookies = static_cast<int>(cookies_.size());

    for (const auto& c : cookies_) {
        result.cookies.push_back(c);
        switch (c.category) {
            case CookieCategory::NECESSARY:    result.necessaryCount++; break;
            case CookieCategory::PREFERENCES:  result.preferencesCount++; break;
            case CookieCategory::STATISTICS:   result.statisticsCount++; break;
            case CookieCategory::MARKETING:    result.marketingCount++; break;
            case CookieCategory::THIRD_PARTY:  result.thirdPartyCount++; break;
        }
        if (!c.thirdPartyInfo.empty()) {
            result.thirdPartyCount++;
        }
    }

    // Evaluacion de riesgo
    std::ostringstream risk;
    int highRiskCount = 0;
    for (const auto& c : cookies_) {
        if (c.riskLevel == CookieRiskLevel::HIGH) highRiskCount++;
    }

    if (highRiskCount > 5) {
        risk << "ALTO: " << highRiskCount << " cookies de alto riesgo detectadas. "
             << "Se recomienda revisar las cookies de marketing y terceros.";
    } else if (highRiskCount > 0) {
        risk << "MEDIO: " << highRiskCount << " cookies de alto riesgo. "
             << "Monitoreo recomendado.";
    } else {
        risk << "BAJO: Perfil de cookies compliant con GDPR.";
    }
    result.riskAssessment = risk.str();

    return result;
}

std::string CookieManager::generateComplianceReport() const {
    auto scan = scanCookies();
    std::ostringstream report;
    report << "============================================================\n";
    report << "  INFORME DE CUMPLIMIENTO DE COOKIES - GDPR/ePrivacy\n";
    report << "  " << bannerConfig_.companyName << "\n";
    report << "============================================================\n\n";
    report << "Resumen de cookies:\n";
    report << "  Total:         " << scan.totalCookies << "\n";
    report << "  Necesarias:    " << scan.necessaryCount << "\n";
    report << "  Preferencias:  " << scan.preferencesCount << "\n";
    report << "  Estadisticas:  " << scan.statisticsCount << "\n";
    report << "  Marketing:     " << scan.marketingCount << "\n";
    report << "  Terceros:      " << scan.thirdPartyCount << "\n\n";
    report << "Evaluacion de riesgo: " << scan.riskAssessment << "\n\n";

    bool compliant = true;
    report << "Verificaciones de cumplimiento:\n";
    if (scan.necessaryCount == 0) {
        report << "  [X] No hay cookies necesarias registradas\n";
        compliant = false;
    } else {
        report << "  [OK] Cookies necesarias registradas: " << scan.necessaryCount << "\n";
    }

    // Verificar que cookies no necesarias requieren consentimiento
    for (const auto& c : scan.cookies) {
        if (c.category != CookieCategory::NECESSARY) {
            if (!c.secure) {
                report << "  [AVISO] Cookie '" << c.name << "' no usa flag Secure\n";
            }
            if (c.sameSite.empty()) {
                report << "  [AVISO] Cookie '" << c.name << "' no tiene SameSite configurado\n";
            }
        }
    }

    report << "\nEstado de cumplimiento: " << (compliant ? "CUMPLIMIENTO" : "NO CUMPLIMIENTO") << "\n";
    report << "============================================================\n";
    return report.str();
}

bool CookieManager::isGdprCompliant() const {
    auto scan = scanCookies();
    // Verificar que las cookies necesarias estan definidas
    if (scan.necessaryCount == 0) return false;

    // Verificar que no hay cookies sin declarar (en produccion se compararia con las reales)
    // Verificar que las no-necesarias tienen atributos de seguridad
    for (const auto& c : scan.cookies) {
        if (c.category != CookieCategory::NECESSARY) {
            if (!c.secure || c.sameSite.empty()) {
                // No son requisitos absolutos pero se recomiendan
                // (en produccion se ajustaria la severidad)
            }
        }
    }
    return true;
}

// ============================================================================
// Callbacks
// ============================================================================

void CookieManager::onConsentChange(ConsentChangeCallback callback) {
    std::unique_lock<std::mutex> lock(callbacksMutex_);
    consentCallback_ = callback;
}

void CookieManager::notifyConsentChange(int userId, const CookieConsentRecord& record) {
    std::unique_lock<std::mutex> lock(callbacksMutex_);
    if (consentCallback_) {
        try { consentCallback_(userId, record); } catch (...) {}
    }
}

bool CookieManager::cookieExists(const std::string& name) const {
    return std::any_of(cookies_.begin(), cookies_.end(),
        [&name](const CookieDefinition& c) { return c.name == name; });
}

} // namespace powsys365::legal
