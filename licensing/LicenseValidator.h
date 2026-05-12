#pragma once

#include <string>
#include <chrono>
#include <vector>
#include <functional>
#include <map>
#include "LicenseManager.h"

namespace powsys365 {
namespace licensing {

// ============================================================
// ValidationResult
// ============================================================
struct ValidationResult {
    bool success = false;
    std::string errorCode;
    std::string errorMessage;
    std::chrono::system_clock::time_point validatedAt;

    static ValidationResult ok() {
        ValidationResult r;
        r.success = true;
        r.validatedAt = std::chrono::system_clock::now();
        return r;
    }

    static ValidationResult fail(const std::string& code, const std::string& msg) {
        ValidationResult r;
        r.success = false;
        r.errorCode = code;
        r.errorMessage = msg;
        r.validatedAt = std::chrono::system_clock::now();
        return r;
    }
};

// ============================================================
// ServerValidationResponse
// ============================================================
struct ServerValidationResponse {
    bool valid = false;
    std::string licenseKey;
    std::string tier;
    bool isActive = false;
    bool isRevoked = false;
    std::chrono::system_clock::time_point expiresAt;
    std::string deviceFingerprint;
    std::string message;
    int httpStatusCode = 0;
};

// ============================================================
// LicenseValidator
// ============================================================
class LicenseValidator {
public:
    LicenseValidator();
    ~LicenseValidator();

    // No copy
    LicenseValidator(const LicenseValidator&) = delete;
    LicenseValidator& operator=(const LicenseValidator&) = delete;

    // --- Format Validation ---
    static ValidationResult validateFormat(const std::string& licenseKey);

    // --- Checksum Validation ---
    static ValidationResult validateChecksum(const std::string& licenseKey);

    // --- Expiration Validation ---
    static ValidationResult validateExpiration(const LicenseInfo& info);

    // --- Device Fingerprint Validation ---
    static ValidationResult validateDevice(const LicenseInfo& info);

    // --- RSA Signature Validation ---
    ValidationResult validateSignature(const LicenseInfo& info);

    // --- Full Validation (all checks) ---
    ValidationResult validateFull(const LicenseInfo& info);

    // --- Server Validation ---
    ServerValidationResponse validateWithServer(
        const std::string& licenseKey,
        const std::string& deviceFingerprint,
        const std::string& serverUrl);

    // --- RSA Key Management ---
    bool loadPublicKeyPEM(const std::string& pemData);
    bool loadPublicKeyFromFile(const std::string& filepath);
    void unloadPublicKey();

    // --- Checksum Generation (for license key creation) ---
    static std::string generateChecksum(const std::string& prefix);
    static std::string generateLicenseKeyWithChecksum();

    // --- Validation Chain ---
    using ValidationStep = std::function<ValidationResult(const LicenseInfo&)>;
    void addValidationStep(ValidationStep step);
    ValidationResult runValidationChain(const LicenseInfo& info);

private:
    RSA* m_rsaPublicKey = nullptr;
    std::vector<ValidationStep> m_validationSteps;

    bool performRsaVerify(const std::string& data, const std::vector<uint8_t>& signature);
};

} // namespace licensing
} // namespace powsys365
