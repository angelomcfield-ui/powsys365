#pragma once

#include <memory>
#include <string>
#include <chrono>
#include <atomic>
#include <mutex>
#include <thread>
#include <functional>
#include <vector>
#include <map>
#include <openssl/evp.h>
#include <openssl/aes.h>
#include <openssl/rsa.h>
#include "DeviceFingerprint.h"

namespace powsys365 {
namespace licensing {

// ============================================================
// License Tier Enumeration
// ============================================================
enum class LicenseTier {
    TRIAL       = 0,
    BASIC       = 1,
    PRO         = 2,
    ENTERPRISE  = 3,
    LIFETIME    = 4
};

std::string tierToString(LicenseTier tier);
LicenseTier tierFromString(const std::string& s);

// ============================================================
// License Status
// ============================================================
enum class LicenseStatus {
    INACTIVE,
    ACTIVE,
    EXPIRED,
    GRACE_PERIOD,
    REVOKED,
    BLOCKED
};

std::string statusToString(LicenseStatus s);

// ============================================================
// Module Enumeration (for tier-based access control)
// ============================================================
enum class Module {
    SINGLE_LINE_DIAGRAM = 0,
    PROTECTION_STUDY    = 1,
    ARC_FLASH           = 2,
    HARMONIC_ANALYSIS   = 3,
    MOTOR_STARTING      = 4,
    TRANSIENT_STABILITY = 5,
    RELIABILITY         = 6,
    OPTIMAL_POWER_FLOW  = 7,
    VOLTAGE_STABILITY   = 8,
    REPORT_GENERATOR    = 9,
    CLOUD_SYNC          = 10,
    TEAM_COLLABORATION  = 11,
    API_ACCESS          = 12,
    COUNT
};

// ============================================================
// LicenseInfo - Datos desencriptados de la licencia
// ============================================================
struct LicenseInfo {
    std::string licenseKey;      // XXXX-XXXX-XXXX-XXXX
    std::string issuer;          // Emisor (e.g., "XNOX L.L.C")
    LicenseTier tier{LicenseTier::TRIAL};
    LicenseStatus status{LicenseStatus::INACTIVE};
    std::string deviceFingerprint;
    std::chrono::system_clock::time_point issuedAt;
    std::chrono::system_clock::time_point expiresAt;
    std::chrono::system_clock::time_point gracePeriodEnds;
    int maxBuses{50};
    int maxProjects{1};
    bool autoRenew{false};
    std::string paymentProvider;
    std::string subscriptionId;
    std::string signature;       // RSA signature
    std::string rawPayload;      // Para verificacion de firma

    bool isValid() const noexcept;
    int daysRemaining() const;
    bool isExpired() const noexcept;
    bool isInGracePeriod() const noexcept;
};

// ============================================================
// LicenseManager - Singleton
// ============================================================
class LicenseManager {
public:
    // --- Singleton ---
    static LicenseManager& instance();
    ~LicenseManager();

    // No copy/move
    LicenseManager(const LicenseManager&) = delete;
    LicenseManager& operator=(const LicenseManager&) = delete;
    LicenseManager(LicenseManager&&) = delete;
    LicenseManager& operator=(LicenseManager&&) = delete;

    // --- Constants ---
    static constexpr const char* DEFAULT_LICENSE = "1A2B-3C4D-5E6F-7G8H";
    static constexpr const char* DEFAULT_ISSUER  = "XNOX L.L.C";
    static constexpr int GRACE_PERIOD_DAYS = 30;
    static constexpr int VALIDATION_INTERVAL_HOURS = 24;

    // --- Core License Operations ---
    bool activateLicense(const std::string& licenseKey,
                         const std::string& issuer = DEFAULT_ISSUER);
    bool validateLicense();
    bool isLicenseValid() const noexcept;
    bool isExpired() const noexcept;
    int  daysRemaining() const;
    bool isModuleEnabled(Module module) const;

    // --- Blocking ---
    void blockAllModules();
    bool isBlocked() const noexcept;

    // --- Renewal ---
    bool renewLicense(const std::string& newLicenseKey);

    // --- Query ---
    LicenseInfo getLicenseInfo() const;
    LicenseTier currentTier() const noexcept;
    LicenseStatus currentStatus() const noexcept;
    std::string currentLicenseKey() const;
    int maxBuses() const noexcept;
    int maxProjects() const noexcept;

    // --- Device Fingerprint ---
    std::string getDeviceFingerprint() const;

    // --- Encryption (AES-256-GCM) ---
    std::vector<uint8_t> encryptData(const std::vector<uint8_t>& plaintext,
                                     const std::vector<uint8_t>& key,
                                     const std::vector<uint8_t>& iv);
    std::vector<uint8_t> decryptData(const std::vector<uint8_t>& ciphertext,
                                     const std::vector<uint8_t>& key,
                                     const std::vector<uint8_t>& iv,
                                     const std::vector<uint8_t>& tag);

    // --- Callbacks / Signals ---
    using LicenseChangedCallback = std::function<void(const LicenseInfo&)>;
    using LicenseExpiredCallback = std::function<void()>;
    void onLicenseChanged(LicenseChangedCallback cb);
    void onLicenseExpired(LicenseExpiredCallback cb);

    // --- Periodic Validation ---
    void startPeriodicValidation();
    void stopPeriodicValidation();

    // --- Persistence ---
    bool saveLicenseToDisk(const std::string& filepath) const;
    bool loadLicenseFromDisk(const std::string& filepath);

    // --- Server Validation ---
    bool validateWithServer(const std::string& serverUrl);

    // --- Utility ---
    static std::string generateLicenseKey();
    static bool validateFormat(const std::string& key);

private:
    LicenseManager(); // private constructor

    // Internal state
    mutable std::mutex m_mutex;
    LicenseInfo m_license;
    std::atomic<bool> m_blocked{false};
    std::atomic<bool> m_validationRunning{false};
    std::thread m_validationThread;

    // AES-256 master key (derived from hardware fingerprint)
    std::vector<uint8_t> m_masterKey;

    // Callbacks
    std::vector<LicenseChangedCallback> m_changeCallbacks;
    std::vector<LicenseExpiredCallback> m_expiredCallbacks;

    // RSA public key for signature verification (PEM format loaded at init)
    RSA* m_rsaPublicKey{nullptr};

    // Internal helpers
    void deriveMasterKey();
    bool validateChecksum(const std::string& key);
    bool verifySignature(const LicenseInfo& info);
    void notifyLicenseChanged();
    void notifyLicenseExpired();
    void validationLoop();
    std::vector<uint8_t> serializeLicense(const LicenseInfo& info) const;
    bool deserializeLicense(const std::vector<uint8_t>& data, LicenseInfo& info);
};

// ============================================================
// Tier / Module Matrix
// ============================================================
struct TierCapabilities {
    int maxBuses;
    int maxProjects;
    std::vector<Module> enabledModules;
    bool hasCloudSync;
    bool hasApiAccess;
    bool hasTeamCollaboration;
};

TierCapabilities getTierCapabilities(LicenseTier tier);

} // namespace licensing
} // namespace powsys365
