#include "terms_manager.h"
#include <algorithm>
#include <numeric>
#include <sstream>
#include <iomanip>

namespace powsys365::legal {

// ============================================================================
// Utilidades de conversion
// ============================================================================

std::string TermsManager::documentTypeToString(TermsDocumentType type) {
    switch (type) {
        case TermsDocumentType::TERMS_OF_SERVICE: return "Condiciones de Servicio";
        case TermsDocumentType::PRIVACY_POLICY: return "Politica de Privacidad";
        case TermsDocumentType::COOKIE_POLICY: return "Politica de Cookies";
        case TermsDocumentType::DATA_PROCESSING_AGREEMENT: return "Acuerdo de Tratamiento de Datos";
        case TermsDocumentType::ACCEPTABLE_USE: return "Uso Aceptable";
        case TermsDocumentType::SERVICE_LEVEL: return "Acuerdo de Nivel de Servicio";
        case TermsDocumentType::LIABILITY_LIMITATION: return "Limitacion de Responsabilidad";
        default: return "Desconocido";
    }
}

std::string TermsManager::changeTypeToString(ChangeType type) {
    switch (type) {
        case ChangeType::MINOR: return "Menor";
        case ChangeType::STANDARD: return "Estandar";
        case ChangeType::MAJOR: return "Mayor";
        default: return "Desconocido";
    }
}

std::string TermsManager::acceptanceStatusToString(AcceptanceStatus status) {
    switch (status) {
        case AcceptanceStatus::NOT_ACCEPTED: return "No aceptado";
        case AcceptanceStatus::ACCEPTED_CURRENT: return "Aceptado (version actual)";
        case AcceptanceStatus::ACCEPTED_OUTDATED: return "Aceptado (version desactualizada)";
        case AcceptanceStatus::DECLINED: return "Rechazado";
        case AcceptanceStatus::PENDING_REVIEW: return "Pendiente de revision";
        default: return "Desconocido";
    }
}

// ============================================================================
// Utilidades internas
// ============================================================================

std::string TermsManager::generateVersionId(const std::string& previousVersion, ChangeType change) const {
    int major = 1, minor = 0, patch = 0;
    if (!previousVersion.empty()) {
        sscanf(previousVersion.c_str(), "%d.%d.%d", &major, &minor, &patch);
    }
    switch (change) {
        case ChangeType::MAJOR:
            major++;
            minor = 0;
            patch = 0;
            break;
        case ChangeType::STANDARD:
            minor++;
            patch = 0;
            break;
        case ChangeType::MINOR:
            patch++;
            break;
    }
    return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}

std::string TermsManager::computeContentHash(const std::string& content) const {
    // Hash simple FNV-1a para identificar contenido
    const uint64_t FNV_OFFSET = 14695981039346656037ULL;
    const uint64_t FNV_PRIME = 1099511628211ULL;
    uint64_t hash = FNV_OFFSET;
    for (unsigned char c : content) {
        hash ^= c;
        hash *= FNV_PRIME;
    }
    std::ostringstream oss;
    oss << std::hex << std::setw(16) << std::setfill('0') << hash;
    return oss.str();
}

std::string TermsManager::generateHash(const std::string& content) {
    // SHA-256 simulado - en produccion usar openssl/sha.h
    return computeContentHash(content);
}

std::string TermsManager::generateDigitalSignature(int userId, const std::string& versionId,
                                                     const std::chrono::system_clock::time_point& timestamp) const {
    auto ts = std::chrono::duration_cast<std::chrono::seconds>(timestamp.time_since_epoch()).count();
    std::string data = std::to_string(userId) + "|" + versionId + "|" + std::to_string(ts);
    return computeContentHash(data);
}

bool TermsManager::requiresReAcceptance(ChangeType change) const {
    return change == ChangeType::MAJOR;
}

// Generador de diff simple (line-based)
std::string TermsManager::generateDiff(const std::string& oldText, const std::string& newText) const {
    std::vector<std::string> oldLines, newLines;
    std::istringstream oldStream(oldText);
    std::istringstream newStream(newText);
    std::string line;

    while (std::getline(oldStream, line)) oldLines.push_back(line);
    while (std::getline(newStream, line)) newLines.push_back(line);

    std::ostringstream diff;
    diff << "--- Version anterior\n";
    diff << "+++ Version nueva\n\n";

    size_t maxLines = std::max(oldLines.size(), newLines.size());
    for (size_t i = 0; i < maxLines; ++i) {
        if (i < oldLines.size() && i < newLines.size()) {
            if (oldLines[i] != newLines[i]) {
                diff << "- " << oldLines[i] << "\n";
                diff << "+ " << newLines[i] << "\n\n";
            }
        } else if (i < oldLines.size()) {
            diff << "- " << oldLines[i] << "\n";
        } else {
            diff << "+ " << newLines[i] << "\n";
        }
    }
    return diff.str();
}

// ============================================================================
// Constructor
// ============================================================================

TermsManager::TermsManager() {}

// ============================================================================
// Gestion de documentos
// ============================================================================

int TermsManager::createDocument(const std::string& name,
                                  const std::string& description,
                                  TermsDocumentType type,
                                  const std::string& initialContent,
                                  const std::string& author,
                                  const std::string& approvedBy) {
    std::unique_lock<std::mutex> lock(documentsMutex_);
    int docId = nextDocumentId_.fetch_add(1);

    TermsVersion version;
    version.versionId = "1.0.0";
    version.content = initialContent;
    version.changelog = "Version inicial";
    version.documentType = type;
    version.changeType = ChangeType::STANDARD;
    version.publishedAt = std::chrono::system_clock::now();
    version.effectiveAt = version.publishedAt;
    version.requiresReAcceptance = false;
    version.author = author;
    version.approvedBy = approvedBy;
    version.language = "es";
    version.hash = computeContentHash(initialContent);

    TermsDocument doc;
    doc.documentId = docId;
    doc.name = name;
    doc.description = description;
    doc.type = type;
    doc.versions.push_back(version);
    doc.currentVersion = version;
    doc.isActive = true;
    doc.createdAt = std::chrono::system_clock::now();

    documents_[docId] = doc;
    return docId;
}

bool TermsManager::updateDocument(int documentId,
                                   const std::string& newContent,
                                   const std::string& changelog,
                                   ChangeType changeType,
                                   const std::string& author,
                                   const std::string& approvedBy) {
    std::unique_lock<std::mutex> lock(documentsMutex_);
    auto it = documents_.find(documentId);
    if (it == documents_.end() || !it->second.isActive) return false;

    TermsVersion version;
    version.versionId = generateVersionId(it->second.currentVersion.versionId, changeType);
    version.content = newContent;
    version.changelog = changelog;
    version.documentType = it->second.type;
    version.changeType = changeType;
    version.publishedAt = std::chrono::system_clock::now();
    version.effectiveAt = version.publishedAt;
    version.requiresReAcceptance = requiresReAcceptance(changeType);
    version.author = author;
    version.approvedBy = approvedBy;
    version.language = "es";
    version.hash = computeContentHash(newContent);

    it->second.versions.push_back(version);
    it->second.currentVersion = version;

    return true;
}

bool TermsManager::deactivateDocument(int documentId) {
    std::unique_lock<std::mutex> lock(documentsMutex_);
    auto it = documents_.find(documentId);
    if (it == documents_.end()) return false;
    it->second.isActive = false;
    return true;
}

std::optional<TermsDocument> TermsManager::getDocument(int documentId) const {
    std::unique_lock<std::mutex> lock(documentsMutex_);
    auto it = documents_.find(documentId);
    if (it == documents_.end()) return std::nullopt;
    return it->second;
}

std::vector<TermsDocument> TermsManager::listActiveDocuments() const {
    std::unique_lock<std::mutex> lock(documentsMutex_);
    std::vector<TermsDocument> result;
    for (const auto& [id, doc] : documents_) {
        if (doc.isActive) result.push_back(doc);
    }
    return result;
}

std::vector<TermsDocument> TermsManager::listDocumentsByType(TermsDocumentType type) const {
    std::unique_lock<std::mutex> lock(documentsMutex_);
    std::vector<TermsDocument> result;
    for (const auto& [id, doc] : documents_) {
        if (doc.type == type && doc.isActive) result.push_back(doc);
    }
    return result;
}

// ============================================================================
// Versionado
// ============================================================================

std::optional<TermsVersion> TermsManager::getCurrentVersion(int documentId) const {
    std::unique_lock<std::mutex> lock(documentsMutex_);
    auto it = documents_.find(documentId);
    if (it == documents_.end()) return std::nullopt;
    return it->second.currentVersion;
}

std::optional<TermsVersion> TermsManager::getVersion(int documentId,
                                                      const std::string& versionId) const {
    std::unique_lock<std::mutex> lock(documentsMutex_);
    auto it = documents_.find(documentId);
    if (it == documents_.end()) return std::nullopt;
    for (const auto& v : it->second.versions) {
        if (v.versionId == versionId) return v;
    }
    return std::nullopt;
}

std::vector<TermsVersion> TermsManager::getVersionHistory(int documentId) const {
    std::unique_lock<std::mutex> lock(documentsMutex_);
    auto it = documents_.find(documentId);
    if (it == documents_.end()) return {};
    return it->second.versions;
}

std::string TermsManager::compareVersions(int documentId,
                                           const std::string& versionA,
                                           const std::string& versionB) const {
    auto vA = getVersion(documentId, versionA);
    auto vB = getVersion(documentId, versionB);
    if (!vA || !vB) return "Error: Version no encontrada";
    return generateDiff(vA->content, vB->content);
}

// ============================================================================
// Aceptacion
// ============================================================================

bool TermsManager::recordAcceptance(int userId,
                                     int documentId,
                                     const std::string& versionId,
                                     const std::string& ipAddress,
                                     const std::string& userAgent) {
    std::unique_lock<std::mutex> docLock(documentsMutex_);
    auto docIt = documents_.find(documentId);
    if (docIt == documents_.end() || !docIt->second.isActive) return false;

    // Verificar que la version existe
    bool versionExists = false;
    for (const auto& v : docIt->second.versions) {
        if (v.versionId == versionId) {
            versionExists = true;
            break;
        }
    }
    if (!versionExists) return false;
    docLock.unlock();

    std::unique_lock<std::mutex> lock(acceptancesMutex_);
    int aid = nextAcceptanceId_.fetch_add(1);
    auto now = std::chrono::system_clock::now();

    TermsAcceptance acceptance;
    acceptance.acceptanceId = aid;
    acceptance.userId = userId;
    acceptance.versionId = versionId;
    acceptance.documentType = docIt->second.type;
    acceptance.acceptedAt = now;
    acceptance.ipAddress = ipAddress;
    acceptance.userAgent = userAgent;
    acceptance.digitalSignature = generateDigitalSignature(userId, versionId, now);
    acceptance.isActive = true;

    acceptances_[userId].push_back(acceptance);
    latestAcceptance_[userId][documentId] = acceptance;

    return true;
}

bool TermsManager::checkTermsAcceptance(int userId) const {
    std::unique_lock<std::mutex> docLock(documentsMutex_);
    std::unique_lock<std::mutex> accLock(acceptancesMutex_);

    // Verificar todos los documentos activos
    for (const auto& [id, doc] : documents_) {
        if (!doc.isActive) continue;

        auto userIt = latestAcceptance_.find(userId);
        if (userIt == latestAcceptance_.end()) return false;

        auto docIt = userIt->second.find(id);
        if (docIt == userIt->second.end()) return false;

        // Verificar que la version aceptada es la actual
        if (doc.currentVersion.requiresReAcceptance &&
            docIt->second.versionId != doc.currentVersion.versionId) {
            return false;
        }
    }
    return true;
}

bool TermsManager::checkTermsAcceptance(int userId, int documentId) const {
    std::unique_lock<std::mutex> lock(acceptancesMutex_);

    auto userIt = latestAcceptance_.find(userId);
    if (userIt == latestAcceptance_.end()) return false;

    auto docIt = userIt->second.find(documentId);
    if (docIt == userIt->second.end()) return false;

    // Verificar version actual
    {
        std::unique_lock<std::mutex> docLock(documentsMutex_);
        auto dIt = documents_.find(documentId);
        if (dIt != documents_.end()) {
            if (dIt->second.currentVersion.requiresReAcceptance &&
                docIt->second.versionId != dIt->second.currentVersion.versionId) {
                return false;
            }
        }
    }

    return true;
}

bool TermsManager::checkTermsAcceptance(int userId, TermsDocumentType type) const {
    std::unique_lock<std::mutex> docLock(documentsMutex_);
    std::unique_lock<std::mutex> accLock(acceptancesMutex_);

    for (const auto& [id, doc] : documents_) {
        if (!doc.isActive || doc.type != type) continue;

        auto userIt = latestAcceptance_.find(userId);
        if (userIt == latestAcceptance_.end()) return false;

        auto docIt = userIt->second.find(id);
        if (docIt == userIt->second.end()) return false;

        if (doc.currentVersion.requiresReAcceptance &&
            docIt->second.versionId != doc.currentVersion.versionId) {
            return false;
        }
    }
    return true;
}

AcceptanceStatus TermsManager::getAcceptanceStatus(int userId, int documentId) const {
    std::unique_lock<std::mutex> accLock(acceptancesMutex_);
    std::unique_lock<std::mutex> docLock(documentsMutex_);

    auto userIt = latestAcceptance_.find(userId);
    if (userIt == latestAcceptance_.end()) return AcceptanceStatus::NOT_ACCEPTED;

    auto docIt = userIt->second.find(documentId);
    if (docIt == userIt->second.end()) return AcceptanceStatus::NOT_ACCEPTED;

    auto dIt = documents_.find(documentId);
    if (dIt == documents_.end()) return AcceptanceStatus::NOT_ACCEPTED;

    if (dIt->second.currentVersion.requiresReAcceptance &&
        docIt->second.versionId != dIt->second.currentVersion.versionId) {
        return AcceptanceStatus::PENDING_REVIEW;
    }

    if (docIt->second.versionId == dIt->second.currentVersion.versionId) {
        return AcceptanceStatus::ACCEPTED_CURRENT;
    }

    return AcceptanceStatus::ACCEPTED_OUTDATED;
}

UserTermsStatus TermsManager::getUserTermsStatus(int userId) const {
    UserTermsStatus status;
    status.userId = userId;
    status.allDocumentsAccepted = true;
    status.hasOutdatedAcceptance = false;

    std::unique_lock<std::mutex> docLock(documentsMutex_);
    std::unique_lock<std::mutex> accLock(acceptancesMutex_);

    for (const auto& [id, doc] : documents_) {
        if (!doc.isActive) continue;

        AcceptanceStatus accStatus = AcceptanceStatus::NOT_ACCEPTED;
        std::string acceptedVer = "";
        std::chrono::system_clock::time_point accTime;

        auto userIt = latestAcceptance_.find(userId);
        if (userIt != latestAcceptance_.end()) {
            auto docIt = userIt->second.find(id);
            if (docIt != userIt->second.end()) {
                acceptedVer = docIt->second.versionId;
                accTime = docIt->second.acceptedAt;

                if (doc.currentVersion.requiresReAcceptance &&
                    docIt->second.versionId != doc.currentVersion.versionId) {
                    accStatus = AcceptanceStatus::PENDING_REVIEW;
                    status.hasOutdatedAcceptance = true;
                    status.allDocumentsAccepted = false;
                } else if (docIt->second.versionId == doc.currentVersion.versionId) {
                    accStatus = AcceptanceStatus::ACCEPTED_CURRENT;
                } else {
                    accStatus = AcceptanceStatus::ACCEPTED_OUTDATED;
                }
            } else {
                status.allDocumentsAccepted = false;
            }
        } else {
            status.allDocumentsAccepted = false;
        }

        status.documentStatus[doc.type] = accStatus;
        status.acceptedVersionId[doc.type] = acceptedVer;
        status.acceptedAt[doc.type] = accTime;
    }

    return status;
}

ComplianceCheck TermsManager::getComplianceCheck(int userId) const {
    ComplianceCheck check;
    check.allTermsAccepted = true;

    auto status = getUserTermsStatus(userId);

    for (const auto& [type, accStatus] : status.documentStatus) {
        if (accStatus != AcceptanceStatus::ACCEPTED_CURRENT) {
            check.allTermsAccepted = false;
            if (accStatus == AcceptanceStatus::NOT_ACCEPTED ||
                accStatus == AcceptanceStatus::DECLINED) {
                check.missingDocuments.push_back(documentTypeToString(type));
            } else if (accStatus == AcceptanceStatus::ACCEPTED_OUTDATED ||
                       accStatus == AcceptanceStatus::PENDING_REVIEW) {
                check.outdatedDocuments.push_back(documentTypeToString(type));
            }
        }
    }

    if (!check.allTermsAccepted) {
        check.recommendation = "El usuario debe aceptar los siguientes documentos: ";
        for (const auto& doc : check.missingDocuments) {
            check.recommendation += doc + ", ";
        }
        for (const auto& doc : check.outdatedDocuments) {
            check.recommendation += doc + " (actualizar), ";
        }
    } else {
        check.recommendation = "Cumplimiento completo.";
    }

    return check;
}

// ============================================================================
// Revocacion
// ============================================================================

bool TermsManager::revokeAcceptance(int userId, int documentId) {
    std::unique_lock<std::mutex> lock(acceptancesMutex_);
    auto userIt = latestAcceptance_.find(userId);
    if (userIt == latestAcceptance_.end()) return false;

    auto docIt = userIt->second.find(documentId);
    if (docIt == userIt->second.end()) return false;

    docIt->second.isActive = false;
    userIt->second.erase(docIt);
    return true;
}

bool TermsManager::recordDecline(int userId, int documentId, const std::string& reason) {
    (void)reason;
    std::unique_lock<std::mutex> lock(acceptancesMutex_);
    // Registrar como declinado (marcar el estado)
    auto userIt = latestAcceptance_.find(userId);
    if (userIt != latestAcceptance_.end()) {
        userIt->second.erase(documentId);
    }
    return true;
}

// ============================================================================
// Consultas
// ============================================================================

std::vector<TermsAcceptance> TermsManager::getUserAcceptances(int userId) const {
    std::unique_lock<std::mutex> lock(acceptancesMutex_);
    auto it = acceptances_.find(userId);
    if (it == acceptances_.end()) return {};
    return it->second;
}

std::vector<TermsAcceptance> TermsManager::getDocumentAcceptances(int documentId) const {
    std::unique_lock<std::mutex> lock(acceptancesMutex_);
    std::vector<TermsAcceptance> result;
    for (const auto& [uid, accList] : acceptances_) {
        (void)uid;
        for (const auto& acc : accList) {
            auto docOpt = getDocument(documentId);
            if (docOpt && acc.documentType == docOpt->type) {
                result.push_back(acc);
            }
        }
    }
    return result;
}

int TermsManager::getTotalAcceptanceCount(int documentId) const {
    return static_cast<int>(getDocumentAcceptances(documentId).size());
}

int TermsManager::getPendingAcceptanceCount(int documentId) const {
    // Estimacion: usuarios que no han aceptado la version actual
    auto docOpt = getDocument(documentId);
    if (!docOpt) return 0;

    std::unique_lock<std::mutex> lock(acceptancesMutex_);
    int pending = 0;
    for (const auto& [uid, accMap] : latestAcceptance_) {
        (void)uid;
        auto it = accMap.find(documentId);
        if (it == accMap.end()) {
            pending++;
        } else if (it->second.versionId != docOpt->currentVersion.versionId) {
            pending++;
        }
    }
    return pending;
}

// ============================================================================
// Generacion de documentos HTML
// ============================================================================

std::string TermsManager::generateDocumentHtml(int documentId) const {
    auto docOpt = getDocument(documentId);
    if (!docOpt) return "";

    const auto& doc = *docOpt;
    const auto& ver = doc.currentVersion;

    std::ostringstream html;
    html << "<!DOCTYPE html><html lang='" << ver.language << "'><head>\n";
    html << "<meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>\n";
    html << "<title>" << doc.name << " - " << ver.versionId << "</title>\n";
    html << "<style>\n";
    html << "body{font-family:system-ui,sans-serif;max-width:900px;margin:40px auto;padding:20px; "
         << "line-height:1.6;color:#333;} "
         << "h1{color:#0078D4;border-bottom:2px solid #0078D4;padding-bottom:8px;} "
         << "h2{color:#333;margin-top:30px;border-bottom:1px solid #ddd;padding-bottom:6px;} "
         << "h3{color:#555;margin-top:20px;} "
         << ".meta{background:#f5f5f5;padding:12px;border-radius:6px;margin:16px 0;font-size:13px;} "
         << ".version-badge{background:#0078D4;color:#fff;padding:3px 10px;border-radius:12px;font-size:12px;} "
         << ".changelog{background:#fff8e1;padding:12px;border-radius:6px;margin:16px 0;border-left:4px solid #ffc107;} "
         << ".effective-date{color:#666;font-style:italic;} "
         << "</style></head><body>\n";

    html << "<h1>" << doc.name << "</h1>\n";
    html << "<span class='version-badge'>Version " << ver.versionId << "</span>\n\n";

    html << "<div class='meta'>\n";
    html << "<strong>Tipo:</strong> " << documentTypeToString(doc.type) << "<br>\n";
    html << "<strong>Descripcion:</strong> " << doc.description << "<br>\n";
    html << "<strong>Publicado:</strong> " << formatTime(ver.publishedAt) << "<br>\n";
    html << "<strong class='effective-date'>Entrada en vigor:</strong> " << formatTime(ver.effectiveAt) << "<br>\n";
    html << "<strong>Autor:</strong> " << ver.author << "<br>\n";
    html << "<strong>Aprobado por:</strong> " << ver.approvedBy << "<br>\n";
    html << "<strong>Hash de integridad:</strong> <code>" << ver.hash << "</code>\n";
    html << "</div>\n";

    if (!ver.changelog.empty()) {
        html << "<div class='changelog'>\n";
        html << "<strong>Cambios en esta version:</strong><br>\n";
        html << ver.changelog << "\n";
        html << "</div>\n";
    }

    // Contenido
    html << "<div class='content'>\n";
    // Convertir saltos de linea a parrafos HTML
    std::istringstream contentStream(ver.content);
    std::string paragraph;
    bool inList = false;
    while (std::getline(contentStream, paragraph)) {
        if (paragraph.empty()) continue;

        // Detectar headers
        if (paragraph.substr(0, 3) == "###") {
            html << "<h3>" << paragraph.substr(3) << "</h3>\n";
        } else if (paragraph.substr(0, 2) == "##") {
            html << "<h2>" << paragraph.substr(2) << "</h2>\n";
        } else if (paragraph.substr(0, 1) == "#") {
            // Skip - ya tenemos el h1
        } else if (paragraph.substr(0, 2) == "- " || paragraph.substr(0, 2) == "* ") {
            if (!inList) {
                html << "<ul>\n";
                inList = true;
            }
            html << "<li>" << paragraph.substr(2) << "</li>\n";
        } else {
            if (inList) {
                html << "</ul>\n";
                inList = false;
            }
            html << "<p>" << paragraph << "</p>\n";
        }
    }
    if (inList) html << "</ul>\n";
    html << "</div>\n";

    // Pie de pagina legal
    html << "<div class='meta' style='margin-top:40px;'>\n";
    html << "<strong>AVISO LEGAL:</strong> Este documento ha sido aprobado conforme al Reglamento "
         << "(UE) 2016/679 (GDPR). El hash de integridad garantiza que el contenido no ha sido "
         << "modificado desde su aprobacion.\n";
    html << "</div>\n";

    html << "</body></html>\n";
    return html.str();
}

std::string TermsManager::generateAcceptanceCertificate(int userId, int documentId) const {
    auto docOpt = getDocument(documentId);
    if (!docOpt) return "";

    std::unique_lock<std::mutex> lock(acceptancesMutex_);
    auto userIt = latestAcceptance_.find(userId);
    if (userIt == latestAcceptance_.end()) return "";

    auto docIt = userIt->second.find(documentId);
    if (docIt == userIt->second.end()) return "";

    const auto& acc = docIt->second;
    const auto& doc = *docOpt;

    std::ostringstream cert;
    cert << "============================================================\n";
    cert << "         CERTIFICADO DE ACEPTACION DIGITAL\n";
    cert << "============================================================\n\n";
    cert << "Documento:    " << doc.name << "\n";
    cert << "Tipo:         " << documentTypeToString(doc.type) << "\n";
    cert << "Version:      " << acc.versionId << "\n";
    cert << "Usuario ID:   " << userId << "\n";
    cert << "Aceptado el:  " << formatTimeHelper(acc.acceptedAt) << "\n";
    cert << "IP:           " << acc.ipAddress << "\n";
    cert << "Navegador:    " << acc.userAgent << "\n";
    cert << "\n";
    cert << "Firma Digital:\n";
    cert << "  " << acc.digitalSignature << "\n";
    cert << "\n";
    cert << "Hash de Version:\n";
    cert << "  " << doc.currentVersion.hash << "\n";
    cert << "\n";
    cert << "Este certificado acredita que el usuario ha leido y aceptado\n";
    cert << "los terminos y condiciones indicados conforme al Reglamento\n";
    cert << "(UE) 2016/679 (GDPR) y la normativa aplicable.\n";
    cert << "\n";
    cert << "La firma digital garantiza la autenticidad e integridad de\n";
    cert << "esta aceptacion.\n";
    cert << "\n";
    cert << "============================================================\n";
    cert << "Fecha de emision: " << formatTimeHelper(std::chrono::system_clock::now()) << "\n";
    cert << "Sistema: POWSYS365 Legal Compliance System\n";
    cert << "============================================================\n";

    return cert.str();
}

std::string TermsManager::generateVersionDiffHtml(int documentId,
                                                    const std::string& oldVersion,
                                                    const std::string& newVersion) const {
    auto docOpt = getDocument(documentId);
    if (!docOpt) return "";

    auto vA = getVersion(documentId, oldVersion);
    auto vB = getVersion(documentId, newVersion);
    if (!vA || !vB) return "Error: Version no encontrada";

    std::string diff = generateDiff(vA->content, vB->content);

    std::ostringstream html;
    html << "<!DOCTYPE html><html><head>\n";
    html << "<title>Diferencias de Version - " << docOpt->name << "</title>\n";
    html << "<style>\n";
    html << "body{font-family:monospace;max-width:1000px;margin:20px auto;padding:20px;background:#1e1e1e;color:#d4d4d4;} "
         << "h1,h2{color:#fff;} "
         << ".diff-container{background:#252526;padding:16px;border-radius:8px;overflow-x:auto;} "
         << ".line{white-space:pre;padding:2px 8px;margin:1px 0;font-size:13px;} "
         << ".removed{background:#4a1a1a;color:#f48771;} "
         << ".added{background:#1a4a1a;color:#7ee787;} "
         << ".context{color:#808080;} "
         << ".header{color:#569cd6;font-weight:bold;} "
         << ".meta{background:#2d2d30;padding:12px;border-radius:6px;margin-bottom:16px;} "
         << "</style></head><body>\n";

    html << "<h1>Diferencias: " << docOpt->name << "</h1>\n";
    html << "<div class='meta'>\n";
    html << "De: <strong>" << oldVersion << "</strong> &rarr; A: <strong>" << newVersion << "</strong><br>\n";
    html << "Cambio: " << changeTypeToString(vB->changeType) << "<br>\n";
    html << "Autor: " << vB->author << " | Aprobado por: " << vB->approvedBy << "\n";
    html << "</div>\n";

    html << "<div class='diff-container'>\n";
    std::istringstream diffStream(diff);
    std::string line;
    while (std::getline(diffStream, line)) {
        std::string cssClass = "context";
        if (line.substr(0, 3) == "---" || line.substr(0, 3) == "+++") {
            cssClass = "header";
        } else if (!line.empty() && line[0] == '-') {
            cssClass = "removed";
        } else if (!line.empty() && line[0] == '+') {
            cssClass = "added";
        }
        html << "<div class='line " << cssClass << "'>" << line << "</div>\n";
    }
    html << "</div>\n";

    if (!vB->changelog.empty()) {
        html << "<h2>Resumen de cambios</h2>\n";
        html << "<div class='meta'>" << vB->changelog << "</div>\n";
    }

    html << "</body></html>\n";
    return html.str();
}

// ============================================================================
// Reportes
// ============================================================================

std::string TermsManager::generateAcceptanceReport(int documentId) const {
    auto docOpt = getDocument(documentId);
    if (!docOpt) return "";

    auto acceptances = getDocumentAcceptances(documentId);
    int totalAcceptances = static_cast<int>(acceptances.size());

    // Contar por version
    std::map<std::string, int> versionCounts;
    for (const auto& acc : acceptances) {
        versionCounts[acc.versionId]++;
    }

    std::ostringstream report;
    report << "============================================================\n";
    report << "         INFORME DE ACEPTACION DE TERMINOS\n";
    report << "============================================================\n\n";
    report << "Documento:     " << docOpt->name << "\n";
    report << "Tipo:          " << documentTypeToString(docOpt->type) << "\n";
    report << "Version actual: " << docOpt->currentVersion.versionId << "\n";
    report << "Fecha:         " << formatTimeHelper(std::chrono::system_clock::now()) << "\n\n";

    report << "--- RESUMEN ---\n";
    report << "Total de aceptaciones registradas: " << totalAcceptances << "\n";
    report << "Versiones en uso:\n";
    for (const auto& [ver, count] : versionCounts) {
        report << "  " << ver << ": " << count << " usuarios";
        if (ver == docOpt->currentVersion.versionId) {
            report << " (actual)";
        }
        report << "\n";
    }

    report << "\n--- HISTORIAL DE VERSIONES ---\n";
    for (const auto& ver : docOpt->versions) {
        report << "  " << ver.versionId << " - " << formatTime(ver.publishedAt);
        report << " [" << changeTypeToString(ver.changeType) << "]";
        report << " - " << ver.author << "\n";
    }

    report << "\n============================================================\n";
    return report.str();
}

std::string TermsManager::generateGlobalComplianceReport() const {
    std::unique_lock<std::mutex> docLock(documentsMutex_);
    std::unique_lock<std::mutex> accLock(acceptancesMutex_);

    std::ostringstream report;
    report << "============================================================\n";
    report << "    INFORME GLOBAL DE CUMPLIMIENTO DE TERMINOS Y CONDICIONES\n";
    report << "============================================================\n\n";
    report << "Fecha: " << formatTimeHelper(std::chrono::system_clock::now()) << "\n\n";

    report << "--- DOCUMENTOS ACTIVOS ---\n";
    int activeDocs = 0;
    for (const auto& [id, doc] : documents_) {
        if (!doc.isActive) continue;
        activeDocs++;
        report << "[" << id << "] " << doc.name << "\n";
        report << "    Tipo: " << documentTypeToString(doc.type) << "\n";
        report << "    Version: " << doc.currentVersion.versionId << "\n";
        report << "    Requiere re-aceptacion: "
               << (doc.currentVersion.requiresReAcceptance ? "SI" : "NO") << "\n";

        // Contar aceptaciones
        int accCount = 0;
        int currentVerCount = 0;
        for (const auto& [uid, accMap] : latestAcceptance_) {
            (void)uid;
            auto it = accMap.find(id);
            if (it != accMap.end()) {
                accCount++;
                if (it->second.versionId == doc.currentVersion.versionId) {
                    currentVerCount++;
                }
            }
        }
        report << "    Usuarios con aceptacion: " << accCount << "\n";
        report << "    Usuarios en version actual: " << currentVerCount << "\n";
    }
    report << "\nTotal documentos activos: " << activeDocs << "\n";

    report << "\n--- ESTADISTICAS DE ACEPTACION ---\n";
    int totalUsers = 0;
    int fullCompliance = 0;
    for (const auto& [uid, accMap] : latestAcceptance_) {
        (void)uid;
        totalUsers++;
        // Verificar si tiene todas las aceptaciones
        bool complete = true;
        for (const auto& [id, doc] : documents_) {
            if (!doc.isActive) continue;
            auto it = accMap.find(id);
            if (it == accMap.end()) {
                complete = false;
                break;
            }
            if (doc.currentVersion.requiresReAcceptance &&
                it->second.versionId != doc.currentVersion.versionId) {
                complete = false;
                break;
            }
        }
        if (complete) fullCompliance++;
    }
    report << "Usuarios totales: " << totalUsers << "\n";
    report << "Cumplimiento total: " << fullCompliance << "\n";
    if (totalUsers > 0) {
        report << "Porcentaje: " << (fullCompliance * 100 / totalUsers) << "%\n";
    }

    report << "\n============================================================\n";
    return report.str();
}

// ============================================================================
// Helper
// ============================================================================

// Helper privado para formatear timestamps
static std::string formatTimeHelper(const std::chrono::system_clock::time_point& tp) {
    auto t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm;
    localtime_r(&t, &tm);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

} // namespace powsys365::legal
