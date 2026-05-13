#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <mutex>
#include <chrono>
#include <optional>
#include <functional>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <regex>

namespace powsys365::legal {

// ============================================================================
// Tipos GDPR (EU 2016/679)
// ============================================================================

// Articulo 4: Categorias de datos personales
enum class PersonalDataCategory {
    BASIC,               // Nombre, email, telefono
    IDENTIFICATION,      // DNI, pasaporte, NIE
    FINANCIAL,           // Datos bancarios, tarjetas
    SENSITIVE,           // Raza, religion, opiniones politicas (Art. 9)
    HEALTH,              // Datos de salud (Art. 9)
    BIOMETRIC,           // Huellas, reconocimiento facial
    GENETIC,             // Datos geneticos (Art. 9)
    LOCATION,            // Datos de geolocalizacion
    BEHAVIORAL,          // Historial de navegacion, cookies
    CHILD,               // Datos de menores (Art. 8)
    CRIMINAL,            // Antecedentes penales (Art. 10)
    PROFESSIONAL         // CV, experiencia laboral
};

// Articulo 6: Base legal para el procesamiento
enum class LegalBasis {
    CONSENT,             // Art. 6(1)(a)
    CONTRACT,            // Art. 6(1)(b) - Contrato
    LEGAL_OBLIGATION,    // Art. 6(1)(c) - Obligacion legal
    VITAL_INTEREST,      // Art. 6(1)(d) - Interes vital
    PUBLIC_INTEREST,     // Art. 6(1)(e) - Interes publico
    LEGITIMATE_INTEREST  // Art. 6(1)(f) - Interes legitimo
};

// Articulo 6: Estados del consentimiento
enum class ConsentStatus {
    PENDING,             // Pendiente de decision
    GRANTED,             // Concedido
    REVOKED,             // Revocado
    EXPIRED,             // Caducado
    WITHDRAWN_PARTIALLY  // Retirado parcialmente
};

// Articulo 15-22: Derechos del titular
enum class DataSubjectRight {
    ACCESS,              // Art. 15 - Derecho de acceso
    RECTIFICATION,       // Art. 16 - Derecho de rectificacion
    ERASURE,             // Art. 17 - Derecho al olvido
    RESTRICTION,         // Art. 18 - Derecho a limitacion
    PORTABILITY,         // Art. 20 - Derecho a portabilidad
    OBJECTION,           // Art. 21 - Derecho de oposicion
    AUTOMATED_DECISION   // Art. 22 - Derecho a no ser objeto de decisiones automatizadas
};

// Articulo 33: Tipos de brechas
enum class BreachSeverity {
    LOW,       // Riesgo limitado
    MEDIUM,    // Riesgo significativo
    HIGH,      // Riesgo alto - notificar a autoridad
    CRITICAL   // Riesgo critico - notificar a titulares
};

// Estado de una solicitud de derechos
enum class RequestStatus {
    RECEIVED,
    UNDER_REVIEW,
    ACTION_TAKEN,
    PARTIALLY_COMPLETED,
    REJECTED,
    CLOSED
};

// ============================================================================
// Estructuras de datos
// ============================================================================

struct ConsentRecord {
    int    consentId;
    int    userId;
    std::string purpose;               // Finalidad especifica (Art. 5(1)(b))
    std::string description;
    LegalBasis legalBasis;             // Base legal
    std::set<PersonalDataCategory> dataCategories;
    ConsentStatus status;
    bool   explicitConsent;            // Art. 9(2)(a) - Consentimiento explicito para datos sensibles
    std::chrono::system_clock::time_point grantedAt;
    std::chrono::system_clock::time_point expiresAt;
    std::chrono::system_clock::time_point revokedAt;
    std::string consentVersion;        // Version de los terminos aceptados
    std::string ipAddress;             // IP del usuario al dar consentimiento
    std::string userAgent;             // Navegador
    bool   isActive() const {
        if (status != ConsentStatus::GRANTED) return false;
        return std::chrono::system_clock::now() < expiresAt;
    }
};

struct DataProcessingActivity {
    int    activityId;
    std::string name;
    std::string description;
    LegalBasis legalBasis;
    std::set<PersonalDataCategory> dataCategories;
    std::string purpose;               // Art. 5(1)(b)
    std::string retentionPeriod;       // Art. 5(1)(e)
    std::string recipients;            // Destinatarios (Art. 13(1)(e))
    bool   crossBorderTransfer;        // Transferencia fuera de EEA
    std::string thirdCountry;          // Pais destino (Art. 44-49)
    std::string safeguards;            // Salvaguardas aplicadas
    std::chrono::system_clock::time_point createdAt;
};

struct DataSubjectRequest {
    int    requestId;
    int    userId;
    DataSubjectRight right;
    std::string description;
    RequestStatus status;
    std::chrono::system_clock::time_point submittedAt;
    std::chrono::system_clock::time_point resolvedAt;
    std::chrono::system_clock::time_point deadlineAt; // 30 dias segun Art. 12(3)
    std::string response;
    std::string denialReason;          // Si se rechaza, justificacion
    std::string processedBy;           // DPO que proceso la solicitud
    std::string format;                // Formato de entrega (JSON, XML, etc.)
};

struct DataBreachRecord {
    int    breachId;
    std::string description;
    BreachSeverity severity;
    std::set<PersonalDataCategory> affectedCategories;
    int    estimatedAffectedUsers;
    std::chrono::system_clock::time_point detectedAt;
    std::chrono::system_clock::time_point reportedToDPA_At;   // Art. 33(1) - 72 horas
    std::chrono::system_clock::time_point notifiedUsers_At;   // Art. 34
    std::string mitigationMeasures;
    std::string dpaNotificationRef;    // Referencia de la notificacion a AEPD
    bool   dpaNotified;
    bool   subjectsNotified;
};

struct UserDataInventory {
    int    userId;
    std::map<std::string, std::string> personalData;     // Campo -> valor
    std::map<std::string, std::string> activityLogs;     // Log de actividades
    std::map<std::string, std::string> consentVersions;  // Versiones aceptadas
    std::chrono::system_clock::time_point createdAt;
    std::chrono::system_clock::time_point lastAccessed;
    std::set<int> consentIds;
};

struct ComplianceReport {
    std::string reportId;
    std::string period;
    std::chrono::system_clock::time_point generatedAt;
    std::string generatedBy;
    int    totalUsers;
    int    totalDataSubjectRequests;
    int    totalBreaches;
    int    pendingRequests;
    int    overdueRequests;
    double avgResolutionDays;
    std::map<std::string, int> requestTypeBreakdown;
    std::map<std::string, int> consentStatusBreakdown;
    std::map<std::string, int> breachSeverityBreakdown;
    std::string recommendations;
    std::string riskAssessment;
};

// DPO (Delegado de Proteccion de Datos) - Art. 37
struct DataProtectionOfficer {
    std::string name;
    std::string email;
    std::string phone;
    std::string dpaRegistrationNumber; // Registro AEPD
};

// ============================================================================
// GDPREngine
// ============================================================================

class GDPREngine {
public:
    GDPREngine();

    // --- Registro de consentimiento (Art. 7) ---
    int  recordConsent(int userId,
                       const std::string& purpose,
                       const std::string& description,
                       LegalBasis legalBasis,
                       const std::set<PersonalDataCategory>& dataCategories,
                       bool explicitConsent = false,
                       int validityDays = 365,
                       const std::string& ipAddress = "",
                       const std::string& userAgent = "",
                       const std::string& consentVersion = "1.0");
    bool revokeConsent(int consentId);
    bool revokeAllConsent(int userId);
    bool partialWithdrawal(int consentId, const std::set<PersonalDataCategory>& categoriesToWithdraw);
    std::optional<ConsentRecord> getConsent(int consentId) const;
    std::vector<ConsentRecord> getUserConsents(int userId) const;
    std::vector<ConsentRecord> getActiveConsents(int userId) const;
    bool hasValidConsent(int userId, const std::string& purpose,
                         PersonalDataCategory category) const;

    // --- Derechos del titular (Arts. 15-22) ---
    int  processDataRequest(int userId,
                            DataSubjectRight right,
                            const std::string& description);
    int  submitAccessRequest(int userId, const std::string& format = "JSON");
    int  submitRectificationRequest(int userId,
                                     const std::map<std::string, std::string>& corrections);
    int  submitErasureRequest(int userId, const std::string& reason = "");
    int  submitPortabilityRequest(int userId, const std::string& format = "JSON");
    int  submitObjectionRequest(int userId, const std::string& processingActivity);

    bool processAccessRequest(int requestId);      // Art. 15
    bool processRectificationRequest(int requestId, const std::map<std::string, std::string>& data);
    bool processErasureRequest(int requestId);     // Art. 17
    bool processPortabilityRequest(int requestId, const std::string& outputPath);
    bool processRestrictionRequest(int requestId);  // Art. 18
    bool processObjectionRequest(int requestId);    // Art. 21

    bool fulfillRequest(int requestId, const std::string& response);
    bool rejectRequest(int requestId, const std::string& reason);

    std::optional<DataSubjectRequest> getRequest(int requestId) const;
    std::vector<DataSubjectRequest> getUserRequests(int userId) const;
    std::vector<DataSubjectRequest> getPendingRequests() const;
    std::vector<DataSubjectRequest> getOverdueRequests() const;

    // --- Inventario de datos ---
    void registerUserData(int userId, const std::map<std::string, std::string>& data,
                          const std::set<PersonalDataCategory>& categories);
    UserDataInventory getUserDataInventory(int userId) const;
    bool updateUserData(int userId, const std::string& field, const std::string& value);
    bool deleteUserData(int userId);                  // Derecho al olvido completo
    bool anonymizeUserData(int userId);               // Anonimizacion (Art. 4(5))
    std::vector<std::string> getUserDataCategories(int userId) const;

    // --- Actividades de procesamiento (Registro de Actividades - Art. 30) ---
    int  registerProcessingActivity(const std::string& name,
                                     const std::string& description,
                                     LegalBasis legalBasis,
                                     const std::set<PersonalDataCategory>& categories,
                                     const std::string& purpose,
                                     const std::string& retentionPeriod,
                                     const std::string& recipients,
                                     bool crossBorder = false,
                                     const std::string& thirdCountry = "",
                                     const std::string& safeguards = "");
    std::optional<DataProcessingActivity> getProcessingActivity(int activityId) const;
    std::vector<DataProcessingActivity> listProcessingActivities() const;

    // --- Brechas de datos (Arts. 33-34) ---
    int  recordDataBreach(const std::string& description,
                          BreachSeverity severity,
                          const std::set<PersonalDataCategory>& affectedCategories,
                          int estimatedAffectedUsers,
                          const std::string& mitigationMeasures);
    bool notifyDPA(int breachId);                     // Art. 33 - 72 horas
    bool notifyAffectedSubjects(int breachId);         // Art. 34
    std::vector<DataBreachRecord> getActiveBreaches() const;

    // --- Reportes de cumplimiento ---
    ComplianceReport generateComplianceReport(const std::string& period,
                                               const std::string& generatedBy = "DPO") const;
    bool exportComplianceReport(const ComplianceReport& report,
                                 const std::string& outputPath) const;

    // --- Privacidad por Diseno y por Defecto (Art. 25) ---
    bool validatePrivacyByDesign(const std::string& featureName,
                                  const std::set<PersonalDataCategory>& dataCategories,
                                  LegalBasis basis) const;
    std::string generatePrivacyImpactAssessment(const std::string& projectName,
                                                 const std::vector<int>& processingActivityIds) const;

    // --- Configuracion DPO (Art. 37) ---
    void setDPO(const DataProtectionOfficer& dpo);
    DataProtectionOfficer getDPO() const;
    std::string getDPOPrivacyNotice() const;  // Texto legal para politica de privacidad

    // --- Persistencia ---
    bool saveToDisk(const std::string& path) const;
    bool loadFromDisk(const std::string& path);

    // --- Estadisticas ---
    int  getTotalUsers() const;
    int  getPendingRequestCount() const;
    int  getOverdueRequestCount() const;
    double getAverageResolutionDays() const;

private:
    mutable std::mutex consentMutex_;
    mutable std::mutex requestMutex_;
    mutable std::mutex inventoryMutex_;
    mutable std::mutex activityMutex_;
    mutable std::mutex breachMutex_;
    mutable std::mutex dpoMutex_;

    std::map<int, ConsentRecord> consents_;                 // consentId -> record
    std::map<int, DataSubjectRequest> requests_;            // requestId -> request
    std::map<int, UserDataInventory> dataInventory_;        // userId -> inventory
    std::map<int, DataProcessingActivity> processingActivities_; // activityId -> activity
    std::map<int, DataBreachRecord> breaches_;              // breachId -> record
    DataProtectionOfficer dpo_;

    std::atomic<int> nextConsentId_{1};
    std::atomic<int> nextRequestId_{1};
    std::atomic<int> nextActivityId_{1};
    std::atomic<int> nextBreachId_{1};

    // --- Utilidades ---
    void calculateDeadline(DataSubjectRequest& request);
    std::string rightToString(DataSubjectRight right) const;
    std::string basisToString(LegalBasis basis) const;
    std::string categoryToString(PersonalDataCategory cat) const;
    std::string statusToString(RequestStatus status) const;
    std::string consentStatusToString(ConsentStatus status) const;
    bool requiresExplicitConsent(PersonalDataCategory cat) const;
};

} // namespace powsys365::legal
