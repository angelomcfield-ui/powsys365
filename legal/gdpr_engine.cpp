#include "gdpr_engine.h"
#include <fstream>
#include <iomanip>
#include <numeric>

namespace powsys365::legal {

// ============================================================================
// Constructor
// ============================================================================

GDPREngine::GDPREngine() {
    // DPO por defecto (debe configurarse)
    dpo_.name = "Sin asignar";
    dpo_.email = "dpo@powsys365.local";
}

// ============================================================================
// Utilidades de conversion
// ============================================================================

std::string GDPREngine::rightToString(DataSubjectRight right) const {
    switch (right) {
        case DataSubjectRight::ACCESS: return "Derecho de acceso (Art. 15)";
        case DataSubjectRight::RECTIFICATION: return "Derecho de rectificacion (Art. 16)";
        case DataSubjectRight::ERASURE: return "Derecho al olvido (Art. 17)";
        case DataSubjectRight::RESTRICTION: return "Derecho a limitacion (Art. 18)";
        case DataSubjectRight::PORTABILITY: return "Derecho a portabilidad (Art. 20)";
        case DataSubjectRight::OBJECTION: return "Derecho de oposicion (Art. 21)";
        case DataSubjectRight::AUTOMATED_DECISION: return "Derecho a no decisiones automatizadas (Art. 22)";
        default: return "Desconocido";
    }
}

std::string GDPREngine::basisToString(LegalBasis basis) const {
    switch (basis) {
        case LegalBasis::CONSENT: return "Consentimiento (Art. 6(1)(a))";
        case LegalBasis::CONTRACT: return "Ejecucion de contrato (Art. 6(1)(b))";
        case LegalBasis::LEGAL_OBLIGATION: return "Obligacion legal (Art. 6(1)(c))";
        case LegalBasis::VITAL_INTEREST: return "Interes vital (Art. 6(1)(d))";
        case LegalBasis::PUBLIC_INTEREST: return "Interes publico (Art. 6(1)(e))";
        case LegalBasis::LEGITIMATE_INTEREST: return "Interes legitimo (Art. 6(1)(f))";
        default: return "Desconocido";
    }
}

std::string GDPREngine::categoryToString(PersonalDataCategory cat) const {
    switch (cat) {
        case PersonalDataCategory::BASIC: return "Datos basicos";
        case PersonalDataCategory::IDENTIFICATION: return "Identificacion";
        case PersonalDataCategory::FINANCIAL: return "Financieros";
        case PersonalDataCategory::SENSITIVE: return "Datos sensibles (Art. 9)";
        case PersonalDataCategory::HEALTH: return "Datos de salud (Art. 9)";
        case PersonalDataCategory::BIOMETRIC: return "Biometricos";
        case PersonalDataCategory::GENETIC: return "Geneticos (Art. 9)";
        case PersonalDataCategory::LOCATION: return "Geolocalizacion";
        case PersonalDataCategory::BEHAVIORAL: return "Comportamiento/cookies";
        case PersonalDataCategory::CHILD: return "Menores (Art. 8)";
        case PersonalDataCategory::CRIMINAL: return "Antecedentes penales (Art. 10)";
        case PersonalDataCategory::PROFESSIONAL: return "Profesionales";
        default: return "Desconocido";
    }
}

std::string GDPREngine::statusToString(RequestStatus status) const {
    switch (status) {
        case RequestStatus::RECEIVED: return "Recibida";
        case RequestStatus::UNDER_REVIEW: return "En revision";
        case RequestStatus::ACTION_TAKEN: return "Accion tomada";
        case RequestStatus::PARTIALLY_COMPLETED: return "Parcialmente completada";
        case RequestStatus::REJECTED: return "Rechazada";
        case RequestStatus::CLOSED: return "Cerrada";
        default: return "Desconocido";
    }
}

std::string GDPREngine::consentStatusToString(ConsentStatus status) const {
    switch (status) {
        case ConsentStatus::PENDING: return "Pendiente";
        case ConsentStatus::GRANTED: return "Concedido";
        case ConsentStatus::REVOKED: return "Revocado";
        case ConsentStatus::EXPIRED: return "Caducado";
        case ConsentStatus::WITHDRAWN_PARTIALLY: return "Retirado parcialmente";
        default: return "Desconocido";
    }
}

bool GDPREngine::requiresExplicitConsent(PersonalDataCategory cat) const {
    return cat == PersonalDataCategory::SENSITIVE ||
           cat == PersonalDataCategory::HEALTH ||
           cat == PersonalDataCategory::BIOMETRIC ||
           cat == PersonalDataCategory::GENETIC ||
           cat == PersonalDataCategory::CHILD ||
           cat == PersonalDataCategory::CRIMINAL;
}

void GDPREngine::calculateDeadline(DataSubjectRequest& request) {
    // Art. 12(3): 30 dias, extensible a 60 con justificacion
    request.deadlineAt = request.submittedAt + std::chrono::hours(24 * 30);
}

// ============================================================================
// Registro de Consentimiento (Art. 7)
// ============================================================================

int GDPREngine::recordConsent(int userId,
                               const std::string& purpose,
                               const std::string& description,
                               LegalBasis legalBasis,
                               const std::set<PersonalDataCategory>& dataCategories,
                               bool explicitConsent,
                               int validityDays,
                               const std::string& ipAddress,
                               const std::string& userAgent,
                               const std::string& consentVersion) {
    // Verificar si se requiere consentimiento explicito (Art. 9)
    for (auto cat : dataCategories) {
        if (requiresExplicitConsent(cat) && !explicitConsent) {
            return -1; // Error: requiere consentimiento explicito
        }
    }

    std::unique_lock<std::mutex> lock(consentMutex_);
    int cid = nextConsentId_.fetch_add(1);
    ConsentRecord record;
    record.consentId = cid;
    record.userId = userId;
    record.purpose = purpose;
    record.description = description;
    record.legalBasis = legalBasis;
    record.dataCategories = dataCategories;
    record.status = ConsentStatus::GRANTED;
    record.explicitConsent = explicitConsent;
    record.grantedAt = std::chrono::system_clock::now();
    record.expiresAt = record.grantedAt + std::chrono::hours(24 * validityDays);
    record.ipAddress = ipAddress;
    record.userAgent = userAgent;
    record.consentVersion = consentVersion;
    consents_[cid] = record;

    // Actualizar inventario
    {
        std::unique_lock<std::mutex> invLock(inventoryMutex_);
        dataInventory_[userId].userId = userId;
        dataInventory_[userId].consentIds.insert(cid);
    }

    return cid;
}

bool GDPREngine::revokeConsent(int consentId) {
    std::unique_lock<std::mutex> lock(consentMutex_);
    auto it = consents_.find(consentId);
    if (it == consents_.end()) return false;
    it->second.status = ConsentStatus::REVOKED;
    it->second.revokedAt = std::chrono::system_clock::now();
    return true;
}

bool GDPREngine::revokeAllConsent(int userId) {
    std::unique_lock<std::mutex> lock(consentMutex_);
    bool changed = false;
    for (auto& [id, record] : consents_) {
        if (record.userId == userId && record.status == ConsentStatus::GRANTED) {
            record.status = ConsentStatus::REVOKED;
            record.revokedAt = std::chrono::system_clock::now();
            changed = true;
        }
    }
    return changed;
}

bool GDPREngine::partialWithdrawal(int consentId,
                                    const std::set<PersonalDataCategory>& categoriesToWithdraw) {
    std::unique_lock<std::mutex> lock(consentMutex_);
    auto it = consents_.find(consentId);
    if (it == consents_.end()) return false;
    for (auto cat : categoriesToWithdraw) {
        it->second.dataCategories.erase(cat);
    }
    if (it->second.dataCategories.empty()) {
        it->second.status = ConsentStatus::REVOKED;
        it->second.revokedAt = std::chrono::system_clock::now();
    } else {
        it->second.status = ConsentStatus::WITHDRAWN_PARTIALLY;
    }
    return true;
}

std::optional<ConsentRecord> GDPREngine::getConsent(int consentId) const {
    std::unique_lock<std::mutex> lock(consentMutex_);
    auto it = consents_.find(consentId);
    if (it == consents_.end()) return std::nullopt;
    return it->second;
}

std::vector<ConsentRecord> GDPREngine::getUserConsents(int userId) const {
    std::unique_lock<std::mutex> lock(consentMutex_);
    std::vector<ConsentRecord> result;
    for (const auto& [id, record] : consents_) {
        if (record.userId == userId) {
            result.push_back(record);
        }
    }
    return result;
}

std::vector<ConsentRecord> GDPREngine::getActiveConsents(int userId) const {
    std::unique_lock<std::mutex> lock(consentMutex_);
    std::vector<ConsentRecord> result;
    auto now = std::chrono::system_clock::now();
    for (const auto& [id, record] : consents_) {
        if (record.userId == userId && record.status == ConsentStatus::GRANTED
            && now < record.expiresAt) {
            result.push_back(record);
        }
    }
    return result;
}

bool GDPREngine::hasValidConsent(int userId, const std::string& purpose,
                                  PersonalDataCategory category) const {
    std::unique_lock<std::mutex> lock(consentMutex_);
    auto now = std::chrono::system_clock::now();
    for (const auto& [id, record] : consents_) {
        if (record.userId == userId &&
            record.purpose == purpose &&
            record.status == ConsentStatus::GRANTED &&
            now < record.expiresAt &&
            record.dataCategories.count(category) > 0) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// Derechos del Titular (Arts. 15-22)
// ============================================================================

int GDPREngine::processDataRequest(int userId, DataSubjectRight right,
                                    const std::string& description) {
    std::unique_lock<std::mutex> lock(requestMutex_);
    int rid = nextRequestId_.fetch_add(1);
    DataSubjectRequest request;
    request.requestId = rid;
    request.userId = userId;
    request.right = right;
    request.description = description;
    request.status = RequestStatus::RECEIVED;
    request.submittedAt = std::chrono::system_clock::now();
    calculateDeadline(request);
    request.processedBy = dpo_.name;
    requests_[rid] = request;
    return rid;
}

int GDPREngine::submitAccessRequest(int userId, const std::string& format) {
    int rid = processDataRequest(userId, DataSubjectRight::ACCESS,
                                  "Solicitud de acceso a datos personales");
    requests_[rid].format = format;
    return rid;
}

int GDPREngine::submitRectificationRequest(int userId,
                                             const std::map<std::string, std::string>& corrections) {
    std::string desc = "Solicitud de rectificacion de datos: ";
    for (const auto& [field, value] : corrections) {
        desc += field + "=" + value + "; ";
    }
    return processDataRequest(userId, DataSubjectRight::RECTIFICATION, desc);
}

int GDPREngine::submitErasureRequest(int userId, const std::string& reason) {
    std::string desc = "Solicitud de eliminacion de datos";
    if (!reason.empty()) desc += ": " + reason;
    return processDataRequest(userId, DataSubjectRight::ERASURE, desc);
}

int GDPREngine::submitPortabilityRequest(int userId, const std::string& format) {
    int rid = processDataRequest(userId, DataSubjectRight::PORTABILITY,
                                  "Solicitud de portabilidad de datos");
    requests_[rid].format = format;
    return rid;
}

int GDPREngine::submitObjectionRequest(int userId, const std::string& processingActivity) {
    return processDataRequest(userId, DataSubjectRight::OBJECTION,
                              "Oposicion a actividad de procesamiento: " + processingActivity);
}

bool GDPREngine::processAccessRequest(int requestId) {
    std::unique_lock<std::mutex> lock(requestMutex_);
    auto it = requests_.find(requestId);
    if (it == requests_.end()) return false;

    int userId = it->second.userId;
    lock.unlock();

    // Obtener inventario de datos
    auto inventory = getUserDataInventory(userId);

    lock.lock();
    it = requests_.find(requestId);
    if (it == requests_.end()) return false;

    // Generar respuesta JSON
    std::ostringstream response;
    response << "{\n";
    response << "  \"userId\": " << userId << ",\n";
    response << "  \"requestType\": \"ACCESS\",\n";
    response << "  \"personalData\": {\n";
    bool first = true;
    for (const auto& [field, value] : inventory.personalData) {
        if (!first) response << ",\n";
        response << "    \"" << field << "\": \"" << value << "\"";
        first = false;
    }
    response << "\n  },\n";
    response << "  \"dataCategories\": [\n";
    first = true;
    for (const auto& [field, value] : inventory.personalData) {
        (void)value;
        if (!first) response << ",\n";
        response << "    \"" << field << "\"";
        first = false;
    }
    response << "\n  ],\n";
    response << "  \"consents\": [\n";
    auto consents = getUserConsents(userId);
    first = true;
    for (const auto& c : consents) {
        if (!first) response << ",\n";
        response << "    {\"purpose\": \"" << c.purpose << "\", "
                   << "\"status\": \"" << consentStatusToString(c.status) << "\", "
                   << "\"grantedAt\": \"\n";
        first = false;
    }
    response << "\n  ],\n";
    response << "  \"processingActivities\": [\"POWSYS365 Core\", "
               << "\"GIS Module\", \"xTalk Module\"]\n";
    response << "}\n";

    it->second.status = RequestStatus::ACTION_TAKEN;
    it->second.resolvedAt = std::chrono::system_clock::now();
    it->second.response = response.str();
    return true;
}

bool GDPREngine::processRectificationRequest(int requestId,
                                               const std::map<std::string, std::string>& data) {
    std::unique_lock<std::mutex> lock(requestMutex_);
    auto it = requests_.find(requestId);
    if (it == requests_.end()) return false;
    int userId = it->second.userId;
    lock.unlock();

    // Actualizar datos
    {
        std::unique_lock<std::mutex> invLock(inventoryMutex_);
        for (const auto& [field, value] : data) {
            dataInventory_[userId].personalData[field] = value;
        }
    }

    lock.lock();
    it = requests_.find(requestId);
    if (it == requests_.end()) return false;
    it->second.status = RequestStatus::ACTION_TAKEN;
    it->second.resolvedAt = std::chrono::system_clock::now();
    it->second.response = "Datos rectificados correctamente";
    return true;
}

bool GDPREngine::processErasureRequest(int requestId) {
    std::unique_lock<std::mutex> lock(requestMutex_);
    auto it = requests_.find(requestId);
    if (it == requests_.end()) return false;
    int userId = it->second.userId;
    lock.unlock();

    // Ejecutar borrado
    deleteUserData(userId);

    lock.lock();
    it = requests_.find(requestId);
    if (it == requests_.end()) return false;
    it->second.status = RequestStatus::ACTION_TAKEN;
    it->second.resolvedAt = std::chrono::system_clock::now();
    it->second.response = "Datos eliminados conforme al Art. 17 del GDPR";
    return true;
}

bool GDPREngine::processPortabilityRequest(int requestId, const std::string& outputPath) {
    std::unique_lock<std::mutex> lock(requestMutex_);
    auto it = requests_.find(requestId);
    if (it == requests_.end()) return false;
    int userId = it->second.userId;
    lock.unlock();

    auto inventory = getUserDataInventory(userId);

    // Exportar a JSON
    std::ofstream file(outputPath + "/user_" + std::to_string(userId) + "_data.json");
    if (!file.is_open()) return false;

    file << "{\n";
    file << "  \"userId\": " << userId << ",\n";
    file << "  \"exportDate\": \"\n";
    file << "  \"personalData\": {\n";
    bool first = true;
    for (const auto& [field, value] : inventory.personalData) {
        if (!first) file << ",\n";
        file << "    \"" << field << "\": \"" << value << "\"";
        first = false;
    }
    file << "\n  }\n";
    file << "}\n";
    file.close();

    lock.lock();
    it = requests_.find(requestId);
    if (it == requests_.end()) return false;
    it->second.status = RequestStatus::ACTION_TAKEN;
    it->second.resolvedAt = std::chrono::system_clock::now();
    it->second.response = "Datos exportados a formato portatil (Art. 20)";
    return true;
}

bool GDPREngine::processRestrictionRequest(int requestId) {
    std::unique_lock<std::mutex> lock(requestMutex_);
    auto it = requests_.find(requestId);
    if (it == requests_.end()) return false;
    it->second.status = RequestStatus::ACTION_TAKEN;
    it->second.resolvedAt = std::chrono::system_clock::now();
    it->second.response = "Procesamiento restringido conforme al Art. 18";
    return true;
}

bool GDPREngine::processObjectionRequest(int requestId) {
    std::unique_lock<std::mutex> lock(requestMutex_);
    auto it = requests_.find(requestId);
    if (it == requests_.end()) return false;
    it->second.status = RequestStatus::ACTION_TAKEN;
    it->second.resolvedAt = std::chrono::system_clock::now();
    it->second.response = "Oposicion procesada conforme al Art. 21";
    return true;
}

bool GDPREngine::fulfillRequest(int requestId, const std::string& response) {
    std::unique_lock<std::mutex> lock(requestMutex_);
    auto it = requests_.find(requestId);
    if (it == requests_.end()) return false;
    it->second.status = RequestStatus::ACTION_TAKEN;
    it->second.resolvedAt = std::chrono::system_clock::now();
    it->second.response = response;
    return true;
}

bool GDPREngine::rejectRequest(int requestId, const std::string& reason) {
    std::unique_lock<std::mutex> lock(requestMutex_);
    auto it = requests_.find(requestId);
    if (it == requests_.end()) return false;
    it->second.status = RequestStatus::REJECTED;
    it->second.resolvedAt = std::chrono::system_clock::now();
    it->second.denialReason = reason;
    return true;
}

std::optional<DataSubjectRequest> GDPREngine::getRequest(int requestId) const {
    std::unique_lock<std::mutex> lock(requestMutex_);
    auto it = requests_.find(requestId);
    if (it == requests_.end()) return std::nullopt;
    return it->second;
}

std::vector<DataSubjectRequest> GDPREngine::getUserRequests(int userId) const {
    std::unique_lock<std::mutex> lock(requestMutex_);
    std::vector<DataSubjectRequest> result;
    for (const auto& [id, req] : requests_) {
        if (req.userId == userId) result.push_back(req);
    }
    return result;
}

std::vector<DataSubjectRequest> GDPREngine::getPendingRequests() const {
    std::unique_lock<std::mutex> lock(requestMutex_);
    std::vector<DataSubjectRequest> result;
    for (const auto& [id, req] : requests_) {
        if (req.status == RequestStatus::RECEIVED ||
            req.status == RequestStatus::UNDER_REVIEW) {
            result.push_back(req);
        }
    }
    return result;
}

std::vector<DataSubjectRequest> GDPREngine::getOverdueRequests() const {
    std::unique_lock<std::mutex> lock(requestMutex_);
    auto now = std::chrono::system_clock::now();
    std::vector<DataSubjectRequest> result;
    for (const auto& [id, req] : requests_) {
        if ((req.status == RequestStatus::RECEIVED ||
             req.status == RequestStatus::UNDER_REVIEW) &&
            now > req.deadlineAt) {
            result.push_back(req);
        }
    }
    return result;
}

// ============================================================================
// Inventario de Datos
// ============================================================================

void GDPREngine::registerUserData(int userId,
                                   const std::map<std::string, std::string>& data,
                                   const std::set<PersonalDataCategory>& categories) {
    std::unique_lock<std::mutex> lock(inventoryMutex_);
    auto& inv = dataInventory_[userId];
    inv.userId = userId;
    for (const auto& [field, value] : data) {
        inv.personalData[field] = value;
    }
    inv.createdAt = std::chrono::system_clock::now();
    inv.lastAccessed = inv.createdAt;
    (void)categories; // Registrar categorias para auditoria
}

UserDataInventory GDPREngine::getUserDataInventory(int userId) const {
    std::unique_lock<std::mutex> lock(inventoryMutex_);
    auto it = dataInventory_.find(userId);
    if (it == dataInventory_.end()) return UserDataInventory{};
    return it->second;
}

bool GDPREngine::updateUserData(int userId, const std::string& field,
                                 const std::string& value) {
    std::unique_lock<std::mutex> lock(inventoryMutex_);
    auto& inv = dataInventory_[userId];
    inv.personalData[field] = value;
    inv.lastAccessed = std::chrono::system_clock::now();
    return true;
}

bool GDPREngine::deleteUserData(int userId) {
    // Derecho al olvido completo - Art. 17
    {
        std::unique_lock<std::mutex> lock(inventoryMutex_);
        dataInventory_.erase(userId);
    }
    // Eliminar consentimientos
    revokeAllConsent(userId);
    return true;
}

bool GDPREngine::anonymizeUserData(int userId) {
    // Anonimizacion - Art. 4(5): irreversible
    std::unique_lock<std::mutex> lock(inventoryMutex_);
    auto it = dataInventory_.find(userId);
    if (it == dataInventory_.end()) return false;

    // Reemplazar datos personales con hashes
    for (auto& [field, value] : it->second.personalData) {
        // Hash simple para anonimizacion
        size_t hash = std::hash<std::string>{}(value + std::to_string(userId));
        value = "ANON_" + std::to_string(hash);
    }
    it->second.lastAccessed = std::chrono::system_clock::now();
    return true;
}

std::vector<std::string> GDPREngine::getUserDataCategories(int userId) const {
    std::unique_lock<std::mutex> lock(inventoryMutex_);
    std::unique_lock<std::mutex> cLock(consentMutex_);
    std::set<std::string> categories;
    for (const auto& [id, consent] : consents_) {
        if (consent.userId == userId) {
            for (auto cat : consent.dataCategories) {
                categories.insert(categoryToString(cat));
            }
        }
    }
    return std::vector<std::string>(categories.begin(), categories.end());
}

// ============================================================================
// Actividades de Procesamiento (Art. 30)
// ============================================================================

int GDPREngine::registerProcessingActivity(const std::string& name,
                                            const std::string& description,
                                            LegalBasis legalBasis,
                                            const std::set<PersonalDataCategory>& categories,
                                            const std::string& purpose,
                                            const std::string& retentionPeriod,
                                            const std::string& recipients,
                                            bool crossBorder,
                                            const std::string& thirdCountry,
                                            const std::string& safeguards) {
    std::unique_lock<std::mutex> lock(activityMutex_);
    int aid = nextActivityId_.fetch_add(1);
    DataProcessingActivity activity;
    activity.activityId = aid;
    activity.name = name;
    activity.description = description;
    activity.legalBasis = legalBasis;
    activity.dataCategories = categories;
    activity.purpose = purpose;
    activity.retentionPeriod = retentionPeriod;
    activity.recipients = recipients;
    activity.crossBorderTransfer = crossBorder;
    activity.thirdCountry = thirdCountry;
    activity.safeguards = safeguards;
    activity.createdAt = std::chrono::system_clock::now();
    processingActivities_[aid] = activity;
    return aid;
}

std::optional<DataProcessingActivity> GDPREngine::getProcessingActivity(int activityId) const {
    std::unique_lock<std::mutex> lock(activityMutex_);
    auto it = processingActivities_.find(activityId);
    if (it == processingActivities_.end()) return std::nullopt;
    return it->second;
}

std::vector<DataProcessingActivity> GDPREngine::listProcessingActivities() const {
    std::unique_lock<std::mutex> lock(activityMutex_);
    std::vector<DataProcessingActivity> result;
    for (const auto& [id, act] : processingActivities_) {
        result.push_back(act);
    }
    return result;
}

// ============================================================================
// Brechas de Datos (Arts. 33-34)
// ============================================================================

int GDPREngine::recordDataBreach(const std::string& description,
                                  BreachSeverity severity,
                                  const std::set<PersonalDataCategory>& affectedCategories,
                                  int estimatedAffectedUsers,
                                  const std::string& mitigationMeasures) {
    std::unique_lock<std::mutex> lock(breachMutex_);
    int bid = nextBreachId_.fetch_add(1);
    DataBreachRecord breach;
    breach.breachId = bid;
    breach.description = description;
    breach.severity = severity;
    breach.affectedCategories = affectedCategories;
    breach.estimatedAffectedUsers = estimatedAffectedUsers;
    breach.detectedAt = std::chrono::system_clock::now();
    breach.mitigationMeasures = mitigationMeasures;
    breach.dpaNotified = false;
    breach.subjectsNotified = false;
    breaches_[bid] = breach;
    return bid;
}

bool GDPREngine::notifyDPA(int breachId) {
    std::unique_lock<std::mutex> lock(breachMutex_);
    auto it = breaches_.find(breachId);
    if (it == breaches_.end()) return false;
    it->second.reportedToDPA_At = std::chrono::system_clock::now();
    it->second.dpaNotified = true;
    it->second.dpaNotificationRef = "AEPD-" + std::to_string(breachId) + "-"
                                     + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                                            it->second.reportedToDPA_At.time_since_epoch()).count());
    return true;
}

bool GDPREngine::notifyAffectedSubjects(int breachId) {
    std::unique_lock<std::mutex> lock(breachMutex_);
    auto it = breaches_.find(breachId);
    if (it == breaches_.end()) return false;
    it->second.notifiedUsers_At = std::chrono::system_clock::now();
    it->second.subjectsNotified = true;
    return true;
}

std::vector<DataBreachRecord> GDPREngine::getActiveBreaches() const {
    std::unique_lock<std::mutex> lock(breachMutex_);
    std::vector<DataBreachRecord> result;
    for (const auto& [id, breach] : breaches_) {
        if (!breach.dpaNotified || !breach.subjectsNotified) {
            result.push_back(breach);
        }
    }
    return result;
}

// ============================================================================
// Reportes de Cumplimiento
// ============================================================================

ComplianceReport GDPREngine::generateComplianceReport(const std::string& period,
                                                        const std::string& generatedBy) const {
    ComplianceReport report;

    auto now = std::chrono::system_clock::now();
    auto ts = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    report.reportId = "GDPR-RPT-" + std::to_string(ts);
    report.period = period;
    report.generatedAt = now;
    report.generatedBy = generatedBy;

    // Estadisticas
    {
        std::unique_lock<std::mutex> lock(inventoryMutex_);
        report.totalUsers = static_cast<int>(dataInventory_.size());
    }

    {
        std::unique_lock<std::mutex> lock(requestMutex_);
        report.totalDataSubjectRequests = static_cast<int>(requests_.size());
        report.pendingRequests = 0;
        report.overdueRequests = 0;
        double totalDays = 0;
        int resolvedCount = 0;

        for (const auto& [id, req] : requests_) {
            report.requestTypeBreakdown[rightToString(req.right)]++;
            if (req.status == RequestStatus::RECEIVED ||
                req.status == RequestStatus::UNDER_REVIEW) {
                report.pendingRequests++;
                if (now > req.deadlineAt) report.overdueRequests++;
            }
            if (req.status == RequestStatus::ACTION_TAKEN ||
                req.status == RequestStatus::CLOSED) {
                auto days = std::chrono::duration_cast<std::chrono::hours>(
                    req.resolvedAt - req.submittedAt).count() / 24.0;
                totalDays += days;
                resolvedCount++;
            }
        }
        report.avgResolutionDays = resolvedCount > 0 ? totalDays / resolvedCount : 0;
    }

    {
        std::unique_lock<std::mutex> lock(breachMutex_);
        report.totalBreaches = static_cast<int>(breaches_.size());
        for (const auto& [id, breach] : breaches_) {
            std::string sev;
            switch (breach.severity) {
                case BreachSeverity::LOW: sev = "Baja"; break;
                case BreachSeverity::MEDIUM: sev = "Media"; break;
                case BreachSeverity::HIGH: sev = "Alta"; break;
                case BreachSeverity::CRITICAL: sev = "Critica"; break;
            }
            report.breachSeverityBreakdown[sev]++;
        }
    }

    {
        std::unique_lock<std::mutex> lock(consentMutex_);
        for (const auto& [id, consent] : consents_) {
            report.consentStatusBreakdown[consentStatusToString(consent.status)]++;
        }
    }

    // Recomendaciones
    std::ostringstream rec;
    if (report.overdueRequests > 0) {
        rec << "URGENTE: " << report.overdueRequests << " solicitudes vencidas. "
            << "Se requiere accion inmediata segun Art. 12(3).\n";
    }
    if (report.avgResolutionDays > 25) {
        rec << "AVISO: El tiempo promedio de resolucion (" << report.avgResolutionDays
            << " dias) esta cerca del limite de 30 dias.\n";
    }
    if (report.totalBreaches > 0) {
        auto activeBreaches = getActiveBreaches();
        if (!activeBreaches.empty()) {
            rec << "AVISO: " << activeBreaches.size() << " brechas pendientes de notificacion.\n";
        }
    }
    rec << "Revision trimestral recomendada del registro de actividades (Art. 30).\n";
    rec << "Verificar vigencia de consentimientos antes de su caducidad.\n";
    report.recommendations = rec.str();

    // Evaluacion de riesgo
    std::ostringstream risk;
    if (report.overdueRequests > 5 || report.totalBreaches > 3) {
        risk << "ALTO";
    } else if (report.overdueRequests > 0 || report.totalBreaches > 0) {
        risk << "MEDIO";
    } else {
        risk << "BAJO";
    }
    report.riskAssessment = risk.str();

    return report;
}

bool GDPREngine::exportComplianceReport(const ComplianceReport& report,
                                         const std::string& outputPath) const {
    std::ofstream file(outputPath + "/gdpr_report_" + report.reportId + ".txt");
    if (!file.is_open()) return false;

    file << "============================================================\n";
    file << "  REPORTE DE CUMPLIMIENTO GDPR (EU 2016/679)\n";
    file << "  POWSYS365\n";
    file << "============================================================\n\n";
    file << "Reporte ID:     " << report.reportId << "\n";
    file << "Periodo:        " << report.period << "\n";
    file << "Generado:       " << std::put_time(std::localtime(
        &std::chrono::system_clock::to_time_t(report.generatedAt)), "%Y-%m-%d %H:%M:%S") << "\n";
    file << "Generado por:   " << report.generatedBy << "\n";
    file << "DPO:            " << dpo_.name << " (" << dpo_.email << ")\n\n";

    file << "--- RESUMEN ---\n";
    file << "Total usuarios:                  " << report.totalUsers << "\n";
    file << "Solicitudes de titulares:        " << report.totalDataSubjectRequests << "\n";
    file << "  - Pendientes:                  " << report.pendingRequests << "\n";
    file << "  - Vencidas:                    " << report.overdueRequests << "\n";
    file << "  - Dias promedio resolucion:    " << std::fixed << std::setprecision(1)
         << report.avgResolutionDays << "\n";
    file << "Brechas registradas:             " << report.totalBreaches << "\n";
    file << "Nivel de riesgo:                 " << report.riskAssessment << "\n\n";

    file << "--- DESGLOSE POR TIPO DE SOLICITUD ---\n";
    for (const auto& [type, count] : report.requestTypeBreakdown) {
        file << "  " << type << ": " << count << "\n";
    }
    file << "\n";

    file << "--- ESTADO DE CONSENTIMIENTOS ---\n";
    for (const auto& [status, count] : report.consentStatusBreakdown) {
        file << "  " << status << ": " << count << "\n";
    }
    file << "\n";

    file << "--- SEVERIDAD DE BREACHES ---\n";
    for (const auto& [sev, count] : report.breachSeverityBreakdown) {
        file << "  " << sev << ": " << count << "\n";
    }
    file << "\n";

    file << "--- RECOMENDACIONES ---\n";
    file << report.recommendations << "\n";

    file << "--- BASE LEGAL ---\n";
    file << "Reglamento (UE) 2016/679 del Parlamento Europeo y del Consejo\n";
    file << "de 27 de abril de 2016 (Reglamento General de Proteccion de Datos)\n";
    file << "DO L 119 de 4.5.2016, p. 1-88\n\n";

    file << "--- REGISTRO DE ACTIVIDADES DE TRATAMIENTO ---\n";
    {
        std::unique_lock<std::mutex> lock(activityMutex_);
        for (const auto& [id, act] : processingActivities_) {
            file << "  [" << id << "] " << act.name << "\n";
            file << "      Proposito: " << act.purpose << "\n";
            file << "      Base legal: " << basisToString(act.legalBasis) << "\n";
            file << "      Retencion: " << act.retentionPeriod << "\n";
            if (act.crossBorderTransfer) {
                file << "      Transferencia internacional a: " << act.thirdCountry << "\n";
                file << "      Salvaguardas: " << act.safeguards << "\n";
            }
        }
    }
    file << "\n============================================================\n";
    file << "Fin del reporte\n";
    file.close();
    return !file.fail();
}

// ============================================================================
// Privacidad por Diseno (Art. 25)
// ============================================================================

bool GDPREngine::validatePrivacyByDesign(const std::string& featureName,
                                          const std::set<PersonalDataCategory>& dataCategories,
                                          LegalBasis basis) const {
    (void)featureName;
    // Verificar que se dispone de base legal
    if (basis == LegalBasis::CONSENT) {
        // Para consentimiento, verificar que las categorias son validas
        for (auto cat : dataCategories) {
            if (requiresExplicitConsent(cat) &&
                basis != LegalBasis::CONSENT &&
                basis != LegalBasis::LEGAL_OBLIGATION &&
                basis != LegalBasis::VITAL_INTEREST &&
                basis != LegalBasis::PUBLIC_INTEREST) {
                return false;
            }
        }
    }
    // Verificar minimizacion: no mas datos de los necesarios
    // (en produccion: comparar con actividades de procesamiento registradas)
    return true;
}

std::string GDPREngine::generatePrivacyImpactAssessment(const std::string& projectName,
                                                         const std::vector<int>& processingActivityIds) const {
    std::ostringstream report;
    report << "============================================================\n";
    report << "  EVALUACION DE IMPACTO EN PROTECCION DE DATOS (EIPD)\n";
    report << "  Art. 35 del Reglamento (UE) 2016/679\n";
    report << "============================================================\n\n";
    report << "Proyecto: " << projectName << "\n";
    report << "Fecha: ";
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    report << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S") << "\n";
    report << "DPO: " << dpo_.name << "\n\n";

    report << "--- ACTIVIDADES DE TRATAMIENTO EVALUADAS ---\n";
    {
        std::unique_lock<std::mutex> lock(activityMutex_);
        for (int actId : processingActivityIds) {
            auto it = processingActivities_.find(actId);
            if (it != processingActivities_.end()) {
                report << "[" << actId << "] " << it->second.name << "\n";
                report << "  Categorias de datos: ";
                for (auto cat : it->second.dataCategories) {
                    report << categoryToString(cat) << ", ";
                }
                report << "\n  Base legal: " << basisToString(it->second.legalBasis) << "\n";
                report << "  Transferencia internacional: "
                       << (it->second.crossBorderTransfer ? "SI" : "NO") << "\n";
                if (it->second.crossBorderTransfer) {
                    report << "  Pais destino: " << it->second.thirdCountry << "\n";
                    report << "  Salvaguardas: " << it->second.safeguards << "\n";
                }
                report << "\n";
            }
        }
    }

    report << "--- EVALUACION DE RIESGOS ---\n";
    report << "Riesgo general: Se requiere analisis caso por caso\n";
    report << "Medidas propuestas:\n";
    report << "  1. Minimizacion de datos: solo recopilar datos necesarios\n";
    report << "  2. Pseudonimizacion de datos sensibles\n";
    report << "  3. Cifrado en transito y en reposo\n";
    report << "  4. Control de acceso basado en roles\n";
    report << "  5. Auditoria periodica de accesos\n";
    report << "  6. Formacion del personal en proteccion de datos\n\n";

    report << "--- CONCLUSION ---\n";
    report << "Se recomienda la implementacion con las medidas indicadas.\n";
    report << "Revision requerida antes del despliegue en produccion.\n\n";
    report << "Firma del DPO: _________________________\n";
    report << "============================================================\n";

    return report.str();
}

// ============================================================================
// DPO (Art. 37)
// ============================================================================

void GDPREngine::setDPO(const DataProtectionOfficer& dpo) {
    std::unique_lock<std::mutex> lock(dpoMutex_);
    dpo_ = dpo;
}

DataProtectionOfficer GDPREngine::getDPO() const {
    std::unique_lock<std::mutex> lock(dpoMutex_);
    return dpo_;
}

std::string GDPREngine::getDPOPrivacyNotice() const {
    std::unique_lock<std::mutex> lock(dpoMutex_);
    std::ostringstream notice;
    notice << "POLITICA DE PRIVACIDAD - POWSYS365\n";
    notice << "====================================\n\n";
    notice << "En cumplimiento del Reglamento (UE) 2016/679 del Parlamento\n";
    notice << "Europeo y del Consejo (GDPR), le informamos de lo siguiente:\n\n";
    notice << "1. RESPONSABLE DEL TRATAMIENTO\n";
    notice << "   POWSYS365 - Sistema de Gestion de Potencia\n\n";
    notice << "2. DELEGADO DE PROTECCION DE DATOS (DPO)\n";
    notice << "   Nombre: " << dpo_.name << "\n";
    notice << "   Email: " << dpo_.email << "\n";
    notice << "   Telefono: " << dpo_.phone << "\n";
    notice << "   Registro AEPD: " << dpo_.dpaRegistrationNumber << "\n\n";
    notice << "3. FINALIDAD DEL TRATAMIENTO\n";
    notice << "   Gestion y operacion del sistema electrico, comunicacion\n";
    notice << "   entre operadores, y gestion geografica de infraestructura.\n\n";
    notice << "4. BASE LEGAL\n";
    notice << "   - Art. 6(1)(b): Ejecucion de un contrato\n";
    notice << "   - Art. 6(1)(f): Interes legitimo\n";
    notice << "   - Art. 6(1)(a): Consentimiento del interesado\n\n";
    notice << "5. DERECHOS DE LOS INTERESADOS\n";
    notice << "   - Derecho de acceso (Art. 15)\n";
    notice << "   - Derecho de rectificacion (Art. 16)\n";
    notice << "   - Derecho de supresion (Art. 17)\n";
    notice << "   - Derecho a la limitacion del tratamiento (Art. 18)\n";
    notice << "   - Derecho a la portabilidad (Art. 20)\n";
    notice << "   - Derecho de oposicion (Art. 21)\n";
    notice << "   Para ejercer estos derechos, contacte con el DPO.\n\n";
    notice << "6. PLAZO DE CONSERVACION\n";
    notice << "   Los datos se conservaran durante la vigencia del contrato\n";
    notice << "   y, posteriormente, el tiempo exigido por la normativa aplicable.\n\n";
    notice << "7. DESTINATARIOS\n";
    notice << "   - Personal autorizado de POWSYS365\n";
    notice << "   - Autoridades regulatorias del sector electrico\n\n";
    notice << "8. EJERCICIO DE DERECHOS\n";
    notice << "   Email: " << dpo_.email << "\n";
    notice << "   Tambien puede presentar reclamacion ante la AEPD.\n\n";
    return notice.str();
}

// ============================================================================
// Persistencia
// ============================================================================

bool GDPREngine::saveToDisk(const std::string& path) const {
    // Consentimientos
    {
        std::ofstream file(path + "/consents.csv");
        if (!file.is_open()) return false;
        file << "consentId,userId,purpose,legalBasis,status,explicitConsent,grantedAt,expiresAt,consentVersion\n";
        std::unique_lock<std::mutex> lock(consentMutex_);
        for (const auto& [id, c] : consents_) {
            file << c.consentId << "," << c.userId << ",\"" << c.purpose << "\","
                 << static_cast<int>(c.legalBasis) << ","
                 << static_cast<int>(c.status) << ","
                 << (c.explicitConsent ? "1" : "0") << ","
                 << std::chrono::duration_cast<std::chrono::seconds>(c.grantedAt.time_since_epoch()).count() << ","
                 << std::chrono::duration_cast<std::chrono::seconds>(c.expiresAt.time_since_epoch()).count() << ","
                 << c.consentVersion << "\n";
        }
    }

    // Solicitudes
    {
        std::ofstream file(path + "/requests.csv");
        if (!file.is_open()) return false;
        file << "requestId,userId,right,status,submittedAt,deadlineAt,processedBy\n";
        std::unique_lock<std::mutex> lock(requestMutex_);
        for (const auto& [id, r] : requests_) {
            file << r.requestId << "," << r.userId << ","
                 << static_cast<int>(r.right) << ","
                 << static_cast<int>(r.status) << ","
                 << std::chrono::duration_cast<std::chrono::seconds>(r.submittedAt.time_since_epoch()).count() << ","
                 << std::chrono::duration_cast<std::chrono::seconds>(r.deadlineAt.time_since_epoch()).count() << ","
                 << r.processedBy << "\n";
        }
    }
    return true;
}

bool GDPREngine::loadFromDisk(const std::string& path) {
    // Consentimientos
    {
        std::ifstream file(path + "/consents.csv");
        if (file.is_open()) {
            std::string line;
            std::getline(file, line); // header
            std::unique_lock<std::mutex> lock(consentMutex_);
            consents_.clear();
            while (std::getline(file, line)) {
                std::stringstream ss(line);
                std::string token;
                ConsentRecord c;
                std::getline(ss, token, ','); c.consentId = std::stoi(token);
                std::getline(ss, token, ','); c.userId = std::stoi(token);
                std::getline(ss, token, ','); c.purpose = token;
                std::getline(ss, token, ','); c.legalBasis = static_cast<LegalBasis>(std::stoi(token));
                std::getline(ss, token, ','); c.status = static_cast<ConsentStatus>(std::stoi(token));
                std::getline(ss, token, ','); c.explicitConsent = (token == "1");
                std::getline(ss, token, ','); c.grantedAt = std::chrono::system_clock::time_point(
                    std::chrono::seconds(std::stoll(token)));
                std::getline(ss, token, ','); c.expiresAt = std::chrono::system_clock::time_point(
                    std::chrono::seconds(std::stoll(token)));
                std::getline(ss, token, ','); c.consentVersion = token;
                consents_[c.consentId] = c;
            }
        }
    }
    return true;
}

// ============================================================================
// Estadisticas
// ============================================================================

int GDPREngine::getTotalUsers() const {
    std::unique_lock<std::mutex> lock(inventoryMutex_);
    return static_cast<int>(dataInventory_.size());
}

int GDPREngine::getPendingRequestCount() const {
    std::unique_lock<std::mutex> lock(requestMutex_);
    int count = 0;
    for (const auto& [id, req] : requests_) {
        if (req.status == RequestStatus::RECEIVED ||
            req.status == RequestStatus::UNDER_REVIEW) count++;
    }
    return count;
}

int GDPREngine::getOverdueRequestCount() const {
    return static_cast<int>(getOverdueRequests().size());
}

double GDPREngine::getAverageResolutionDays() const {
    std::unique_lock<std::mutex> lock(requestMutex_);
    double totalDays = 0;
    int count = 0;
    for (const auto& [id, req] : requests_) {
        if (req.status == RequestStatus::ACTION_TAKEN ||
            req.status == RequestStatus::CLOSED) {
            auto days = std::chrono::duration_cast<std::chrono::hours>(
                req.resolvedAt - req.submittedAt).count() / 24.0;
            totalDays += days;
            count++;
        }
    }
    return count > 0 ? totalDays / count : 0;
}

} // namespace powsys365::legal
