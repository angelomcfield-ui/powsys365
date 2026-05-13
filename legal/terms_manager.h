#pragma once

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <chrono>
#include <optional>
#include <functional>
#include <sstream>
#include <iomanip>
#include <set>

namespace powsys365::legal {

// ============================================================================
// Tipos de Terminos y Condiciones
// ============================================================================

enum class TermsDocumentType {
    TERMS_OF_SERVICE,    // Condiciones de servicio
    PRIVACY_POLICY,      // Politica de privacidad
    COOKIE_POLICY,       // Politica de cookies
    DATA_PROCESSING_AGREEMENT, // Acuerdo de tratamiento de datos (Art. 28 GDPR)
    ACCEPTABLE_USE,      // Uso aceptable
    SERVICE_LEVEL,       // Acuerdo de nivel de servicio (SLA)
    LIABILITY_LIMITATION // Limitacion de responsabilidad
};

enum class AcceptanceStatus {
    NOT_ACCEPTED,
    ACCEPTED_CURRENT,     // Acepto la version actual
    ACCEPTED_OUTDATED,    // Acepto una version anterior
    DECLINED,
    PENDING_REVIEW        // Requiere re-aceptacion por cambios significativos
};

enum class ChangeType {
    MINOR,    // Cambio menor (ortografia, formato)
    STANDARD, // Cambio estandar (clarificacion)
    MAJOR     // Cambio mayor (nuevos terminos, cambio de alcance)
};

// ============================================================================
// Estructuras
// ============================================================================

struct TermsVersion {
    std::string versionId;           // ej: "2.3.1"
    std::string content;             // Texto completo del documento
    std::string changelog;           // Resumen de cambios respecto a la version anterior
    TermsDocumentType documentType;
    ChangeType changeType;
    std::chrono::system_clock::time_point publishedAt;
    std::chrono::system_clock::time_point effectiveAt;
    bool requiresReAcceptance;       // true = usuarios deben re-aceptar
    std::string author;
    std::string approvedBy;          // DPO/Legal que aprobo
    std::string language;            // ISO 639-1
    std::string hash;                // SHA-256 del contenido para integridad
};

struct TermsAcceptance {
    int    acceptanceId;
    int    userId;
    std::string versionId;
    TermsDocumentType documentType;
    std::chrono::system_clock::time_point acceptedAt;
    std::string ipAddress;
    std::string userAgent;
    std::string digitalSignature;    // Hash de version + userId + timestamp
    bool   isActive;
    std::chrono::system_clock::time_point expiresAt; // si aplica
};

struct TermsDocument {
    int    documentId;
    std::string name;
    std::string description;
    TermsDocumentType type;
    std::vector<TermsVersion> versions;
    TermsVersion currentVersion;
    bool isActive;
    std::chrono::system_clock::time_point createdAt;
};

struct UserTermsStatus {
    int    userId;
    std::map<TermsDocumentType, AcceptanceStatus> documentStatus;
    std::map<TermsDocumentType, std::string> acceptedVersionId;
    std::map<TermsDocumentType, std::chrono::system_clock::time_point> acceptedAt;
    bool   allDocumentsAccepted;
    bool   hasOutdatedAcceptance;    // Algun documento requiere re-aceptacion
};

struct ComplianceCheck {
    bool   allTermsAccepted;
    std::vector<std::string> missingDocuments;
    std::vector<std::string> outdatedDocuments;
    std::string recommendation;
};

// ============================================================================
// TermsManager
// ============================================================================

class TermsManager {
public:
    TermsManager();

    // --- Gestion de documentos ---
    int  createDocument(const std::string& name,
                        const std::string& description,
                        TermsDocumentType type,
                        const std::string& initialContent,
                        const std::string& author,
                        const std::string& approvedBy);
    bool updateDocument(int documentId,
                        const std::string& newContent,
                        const std::string& changelog,
                        ChangeType changeType,
                        const std::string& author,
                        const std::string& approvedBy);
    bool deactivateDocument(int documentId);
    std::optional<TermsDocument> getDocument(int documentId) const;
    std::vector<TermsDocument> listActiveDocuments() const;
    std::vector<TermsDocument> listDocumentsByType(TermsDocumentType type) const;

    // --- Versionado ---
    std::optional<TermsVersion> getCurrentVersion(int documentId) const;
    std::optional<TermsVersion> getVersion(int documentId, const std::string& versionId) const;
    std::vector<TermsVersion> getVersionHistory(int documentId) const;
    std::string compareVersions(int documentId,
                                 const std::string& versionA,
                                 const std::string& versionB) const;

    // --- Aceptacion ---
    bool recordAcceptance(int userId,
                          int documentId,
                          const std::string& versionId,
                          const std::string& ipAddress = "",
                          const std::string& userAgent = "");
    bool checkTermsAcceptance(int userId) const;
    bool checkTermsAcceptance(int userId, int documentId) const;
    bool checkTermsAcceptance(int userId, TermsDocumentType type) const;
    AcceptanceStatus getAcceptanceStatus(int userId, int documentId) const;
    UserTermsStatus getUserTermsStatus(int userId) const;
    ComplianceCheck getComplianceCheck(int userId) const;

    // --- Revocacion y rechazo ---
    bool revokeAcceptance(int userId, int documentId);
    bool recordDecline(int userId, int documentId, const std::string& reason = "");

    // --- Consultas ---
    std::vector<TermsAcceptance> getUserAcceptances(int userId) const;
    std::vector<TermsAcceptance> getDocumentAcceptances(int documentId) const;
    int  getTotalAcceptanceCount(int documentId) const;
    int  getPendingAcceptanceCount(int documentId) const; // Usuarios que no han aceptado

    // --- Generacion de documentos ---
    std::string generateDocumentHtml(int documentId) const;
    std::string generateAcceptanceCertificate(int userId, int documentId) const;
    std::string generateVersionDiffHtml(int documentId,
                                        const std::string& oldVersion,
                                        const std::string& newVersion) const;

    // --- Reportes ---
    std::string generateAcceptanceReport(int documentId) const;
    std::string generateGlobalComplianceReport() const;

    // --- Utilidades ---
    static std::string documentTypeToString(TermsDocumentType type);
    static std::string changeTypeToString(ChangeType type);
    static std::string acceptanceStatusToString(AcceptanceStatus status);
    static std::string generateHash(const std::string& content);

private:
    mutable std::mutex documentsMutex_;
    mutable std::mutex acceptancesMutex_;

    std::map<int, TermsDocument> documents_;
    std::map<int, std::vector<TermsAcceptance>> acceptances_; // userId -> list
    std::map<int, std::map<int, TermsAcceptance>> latestAcceptance_; // userId -> (docId -> acceptance)

    std::atomic<int> nextDocumentId_{1};
    std::atomic<int> nextAcceptanceId_{1};

    // --- Utilidades ---
    std::string generateVersionId(const std::string& previousVersion, ChangeType change) const;
    std::string generateDigitalSignature(int userId, const std::string& versionId,
                                          const std::chrono::system_clock::time_point& timestamp) const;
    std::string computeContentHash(const std::string& content) const;
    std::string generateDiff(const std::string& oldText, const std::string& newText) const;
    bool requiresReAcceptance(ChangeType change) const;
};

} // namespace powsys365::legal
