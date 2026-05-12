#include "LicenseManager.h"
#include "LicenseValidator.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <regex>
#include <sstream>

#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#ifdef _WIN32
    #include <iphlpapi.h>
    #include <intrin.h>
    #include <winsock2.h>
    #pragma comment(lib, "iphlpapi.lib")
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <arpa/inet.h>
    #include <ifaddrs.h>
    #include <net/if.h>
    #include <sys/ioctl.h>
    #include <sys/socket.h>
    #include <unistd.h>
#endif

namespace powsys365 {
namespace licensing {

// ============================================================
// Helpers
// ============================================================
static std::vector<uint8_t> sha256(const std::string& data) {
    std::vector<uint8_t> hash(SHA256_DIGEST_LENGTH);
    SHA256(reinterpret_cast<const unsigned char*>(data.data()), data.size(), hash.data());
    return hash;
}

static std::string toHex(const std::vector<uint8_t>& data) {
    std::ostringstream oss;
    for (auto b : data) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
    }
    return oss.str();
}

static std::vector<uint8_t> fromHex(const std::string& hex) {
    std::vector<uint8_t> out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        std::string byteStr = hex.substr(i, 2);
        out.push_back(static_cast<uint8_t>(std::stoul(byteStr, nullptr, 16)));
    }
    return out;
}

// ============================================================
// Tier Strings
// ============================================================
std::string tierToString(LicenseTier tier) {
    switch (tier) {
        case LicenseTier::TRIAL:      return "trial";
        case LicenseTier::BASIC:      return "basic";
        case LicenseTier::PRO:        return "pro";
        case LicenseTier::ENTERPRISE: return "enterprise";
        case LicenseTier::LIFETIME:   return "lifetime";
    }
    return "trial";
}

LicenseTier tierFromString(const std::string& s) {
    std::string lower;
    for (char c : s) lower += static_cast<char>(std::tolower(c));
    if (lower == "basic")      return LicenseTier::BASIC;
    if (lower == "pro")        return LicenseTier::PRO;
    if (lower == "enterprise") return LicenseTier::ENTERPRISE;
    if (lower == "lifetime")   return LicenseTier::LIFETIME;
    return LicenseTier::TRIAL;
}

std::string statusToString(LicenseStatus s) {
    switch (s) {
        case LicenseStatus::INACTIVE:     return "inactive";
        case LicenseStatus::ACTIVE:       return "active";
        case LicenseStatus::EXPIRED:      return "expired";
        case LicenseStatus::GRACE_PERIOD: return "grace_period";
        case LicenseStatus::REVOKED:      return "revoked";
        case LicenseStatus::BLOCKED:      return "blocked";
    }
    return "unknown";
}

// ============================================================
// TierCapabilities
// ============================================================
TierCapabilities getTierCapabilities(LicenseTier tier) {
    TierCapabilities caps;
    switch (tier) {
        case LicenseTier::TRIAL:
            caps.maxBuses = 50;
            caps.maxProjects = 1;
            caps.enabledModules = {
                Module::SINGLE_LINE_DIAGRAM,
                Module::PROTECTION_STUDY,
                Module::REPORT_GENERATOR
            };
            caps.hasCloudSync = false;
            caps.hasApiAccess = false;
            caps.hasTeamCollaboration = false;
            break;
        case LicenseTier::BASIC:
            caps.maxBuses = 500;
            caps.maxProjects = 5;
            caps.enabledModules = {
                Module::SINGLE_LINE_DIAGRAM,
                Module::PROTECTION_STUDY,
                Module::ARC_FLASH,
                Module::HARMONIC_ANALYSIS,
                Module::REPORT_GENERATOR
            };
            caps.hasCloudSync = true;
            caps.hasApiAccess = false;
            caps.hasTeamCollaboration = false;
            break;
        case LicenseTier::PRO:
            caps.maxBuses = 5000;
            caps.maxProjects = 25;
            caps.enabledModules = {
                Module::SINGLE_LINE_DIAGRAM,
                Module::PROTECTION_STUDY,
                Module::ARC_FLASH,
                Module::HARMONIC_ANALYSIS,
                Module::MOTOR_STARTING,
                Module::TRANSIENT_STABILITY,
                Module::RELIABILITY,
                Module::OPTIMAL_POWER_FLOW,
                Module::REPORT_GENERATOR,
                Module::CLOUD_SYNC,
                Module::TEAM_COLLABORATION
            };
            caps.hasCloudSync = true;
            caps.hasApiAccess = true;
            caps.hasTeamCollaboration = true;
            break;
        case LicenseTier::ENTERPRISE:
            caps.maxBuses = 50000;
            caps.maxProjects = 999;
            caps.enabledModules = {
                Module::SINGLE_LINE_DIAGRAM,
                Module::PROTECTION_STUDY,
                Module::ARC_FLASH,
                Module::HARMONIC_ANALYSIS,
                Module::MOTOR_STARTING,
                Module::TRANSIENT_STABILITY,
                Module::RELIABILITY,
                Module::OPTIMAL_POWER_FLOW,
                Module::VOLTAGE_STABILITY,
                Module::REPORT_GENERATOR,
                Module::CLOUD_SYNC,
                Module::TEAM_COLLABORATION,
                Module::API_ACCESS
            };
            caps.hasCloudSync = true;
            caps.hasApiAccess = true;
            caps.hasTeamCollaboration = true;
            break;
        case LicenseTier::LIFETIME:
            caps.maxBuses = 999999;
            caps.maxProjects = 999;
            caps.enabledModules = {
                Module::SINGLE_LINE_DIAGRAM,
                Module::PROTECTION_STUDY,
                Module::ARC_FLASH,
                Module::HARMONIC_ANALYSIS,
                Module::MOTOR_STARTING,
                Module::TRANSIENT_STABILITY,
                Module::RELIABILITY,
                Module::OPTIMAL_POWER_FLOW,
                Module::VOLTAGE_STABILITY,
                Module::REPORT_GENERATOR,
                Module::CLOUD_SYNC,
                Module::TEAM_COLLABORATION,
                Module::API_ACCESS
            };
            caps.hasCloudSync = true;
            caps.hasApiAccess = true;
            caps.hasTeamCollaboration = true;
            break;
    }
    return caps;
}

// ============================================================
// LicenseInfo
// ============================================================
bool LicenseInfo::isValid() const noexcept {
    return status == LicenseStatus::ACTIVE || status == LicenseStatus::GRACE_PERIOD;
}

int LicenseInfo::daysRemaining() const {
    auto now = std::chrono::system_clock::now();
    if (now >= expiresAt) {
        if (now < gracePeriodEnds) {
            auto diff = gracePeriodEnds - now;
            return static_cast<int>(std::chrono::duration_cast<std::chrono::hours>(diff).count() / 24);
        }
        return 0;
    }
    auto diff = expiresAt - now;
    return static_cast<int>(std::chrono::duration_cast<std::chrono::hours>(diff).count() / 24);
}

bool LicenseInfo::isExpired() const noexcept {
    auto now = std::chrono::system_clock::now();
    return now > expiresAt && now > gracePeriodEnds;
}

bool LicenseInfo::isInGracePeriod() const noexcept {
    auto now = std::chrono::system_clock::now();
    return now > expiresAt && now <= gracePeriodEnds;
}

// ============================================================
// LicenseManager Implementation
// ============================================================

LicenseManager& LicenseManager::instance() {
    static LicenseManager inst;
    return inst;
}

LicenseManager::LicenseManager() {
    deriveMasterKey();
}

LicenseManager::~LicenseManager() {
    stopPeriodicValidation();
    if (m_rsaPublicKey) {
        RSA_free(m_rsaPublicKey);
        m_rsaPublicKey = nullptr;
    }
}

// --- Master Key Derivation (from device fingerprint) ---
void LicenseManager::deriveMasterKey() {
    std::string fp = getDeviceFingerprint();
    std::vector<uint8_t> hash = sha256(fp);
    m_masterKey.resize(32);
    std::memcpy(m_masterKey.data(), hash.data(), 32);
}

// --- Device Fingerprint ---
std::string LicenseManager::getDeviceFingerprint() const {
    DeviceFingerprint fp;
    return fp.collect().toString();
}

// --- Format Validation ---
bool LicenseManager::validateFormat(const std::string& key) {
    static const std::regex re(R"(^[A-Z0-9]{4}-[A-Z0-9]{4}-[A-Z0-9]{4}-[A-Z0-9]{4}$)");
    return std::regex_match(key, re);
}

bool LicenseManager::validateChecksum(const std::string& key) {
    // Checksum: SHA-256 of key prefix (first 2 groups) must match encoded in last 2 groups
    std::string prefix = key.substr(0, 9); // "XXXX-XXXX"
    auto hash = sha256(prefix);
    std::string hashHex = toHex(hash);

    // Encode hashHex into groups 3 and 4 (simplified: first 8 hex chars -> group3, next 8 -> group4)
    // This means a valid key encodes part of the hash
    std::string group3 = key.substr(10, 4);
    std::string group4 = key.substr(15, 4);

    // Validate: the 8 chars of groups 3+4 should appear somewhere in the hash
    std::string combined = group3 + group4;
    std::string hashUpper = hashHex;
    for (auto& c : hashUpper) c = static_cast<char>(std::toupper(c));

    // Check that the combined 8-char string is a substring of the first 32 hex chars
    return hashUpper.find(combined) != std::string::npos;
}

bool LicenseManager::verifySignature(const LicenseInfo& info) {
    if (!m_rsaPublicKey || info.signature.empty()) {
        // No public key loaded or no signature: accept for default/dev licenses
        return (info.licenseKey == DEFAULT_LICENSE);
    }
    // Verify RSA signature over rawPayload
    std::vector<uint8_t> sig = fromHex(info.signature);
    std::vector<uint8_t> digest = sha256(info.rawPayload);

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return false;

    EVP_PKEY* pkey = EVP_PKEY_new();
    EVP_PKEY_assign_RSA(pkey, m_rsaPublicKey);

    bool ok = (EVP_DigestVerifyInit(ctx, nullptr, EVP_sha256(), nullptr, pkey) == 1);
    if (ok) {
        ok = (EVP_DigestVerifyUpdate(ctx, info.rawPayload.data(), info.rawPayload.size()) == 1);
    }
    if (ok) {
        ok = (EVP_DigestVerifyFinal(ctx, sig.data(), sig.size()) == 1);
    }

    EVP_PKEY_free(pkey);
    EVP_MD_CTX_free(ctx);
    return ok;
}

// --- Activation ---
bool LicenseManager::activateLicense(const std::string& licenseKey,
                                      const std::string& issuer) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // 1. Validate format
    if (!validateFormat(licenseKey)) {
        std::cerr << "[LicenseManager] Invalid license key format" << std::endl;
        return false;
    }

    // 2. Validate checksum
    if (!validateChecksum(licenseKey)) {
        std::cerr << "[LicenseManager] Checksum validation failed" << std::endl;
        return false;
    }

    // 3. Build LicenseInfo
    LicenseInfo info;
    info.licenseKey = licenseKey;
    info.issuer = issuer;

    // Determine tier from key prefix patterns
    if (licenseKey == DEFAULT_LICENSE) {
        info.tier = LicenseTier::LIFETIME;
        info.status = LicenseStatus::ACTIVE;
        info.maxBuses = 999999;
        info.maxProjects = 999;
    } else if (licenseKey.substr(0, 2) == "LT") {
        info.tier = LicenseTier::LIFETIME;
        info.status = LicenseStatus::ACTIVE;
    } else if (licenseKey.substr(0, 2) == "EN") {
        info.tier = LicenseTier::ENTERPRISE;
        info.status = LicenseStatus::ACTIVE;
    } else if (licenseKey.substr(0, 2) == "PR") {
        info.tier = LicenseTier::PRO;
        info.status = LicenseStatus::ACTIVE;
    } else if (licenseKey.substr(0, 2) == "BS") {
        info.tier = LicenseTier::BASIC;
        info.status = LicenseStatus::ACTIVE;
    } else {
        info.tier = LicenseTier::TRIAL;
        info.status = LicenseStatus::ACTIVE;
    }

    // Set expiration
    auto now = std::chrono::system_clock::now();
    info.issuedAt = now;

    if (info.tier == LicenseTier::LIFETIME) {
        info.expiresAt = std::chrono::system_clock::time_point::max();
        info.gracePeriodEnds = std::chrono::system_clock::time_point::max();
    } else {
        // Default: 1 year from now
        info.expiresAt = now + std::chrono::hours(24 * 365);
        info.gracePeriodEnds = info.expiresAt + std::chrono::hours(24 * GRACE_PERIOD_DAYS);
    }

    // Device fingerprint
    info.deviceFingerprint = getDeviceFingerprint();

    // Tier capabilities
    auto caps = getTierCapabilities(info.tier);
    info.maxBuses = caps.maxBuses;
    info.maxProjects = caps.maxProjects;

    // Build raw payload for signature verification
    std::ostringstream oss;
    oss << info.licenseKey << "|" << info.issuer << "|" << tierToString(info.tier)
        << "|" << info.maxBuses << "|" << info.deviceFingerprint;
    info.rawPayload = oss.str();

    // 4. Verify signature (if applicable)
    if (!verifySignature(info)) {
        // Allow default license to pass
        if (licenseKey != DEFAULT_LICENSE) {
            std::cerr << "[LicenseManager] Signature verification failed" << std::endl;
            return false;
        }
    }

    // 5. Apply license
    m_license = info;
    m_blocked = false;

    std::cout << "[LicenseManager] License activated: " << licenseKey
              << " (Tier: " << tierToString(info.tier) << ")" << std::endl;

    notifyLicenseChanged();
    return true;
}

// --- Validation ---
bool LicenseManager::validateLicense() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_blocked.load()) {
        m_license.status = LicenseStatus::BLOCKED;
        return false;
    }

    if (m_license.status == LicenseStatus::INACTIVE) {
        return false;
    }

    if (m_license.tier == LicenseTier::LIFETIME) {
        m_license.status = LicenseStatus::ACTIVE;
        return true;
    }

    auto now = std::chrono::system_clock::now();

    if (now > m_license.gracePeriodEnds) {
        m_license.status = LicenseStatus::EXPIRED;
        notifyLicenseExpired();
        return false;
    }

    if (now > m_license.expiresAt) {
        m_license.status = LicenseStatus::GRACE_PERIOD;
        return true; // Still usable in grace period
    }

    m_license.status = LicenseStatus::ACTIVE;
    return true;
}

bool LicenseManager::isLicenseValid() const noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_blocked.load()) return false;
    return m_license.isValid();
}

bool LicenseManager::isExpired() const noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_license.isExpired();
}

int LicenseManager::daysRemaining() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_license.daysRemaining();
}

// --- Module Access Control ---
bool LicenseManager::isModuleEnabled(Module module) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_blocked.load()) return false;
    if (!m_license.isValid()) return false;

    auto caps = getTierCapabilities(m_license.tier);
    for (auto m : caps.enabledModules) {
        if (m == module) return true;
    }
    return false;
}

// --- Blocking ---
void LicenseManager::blockAllModules() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_blocked = true;
    m_license.status = LicenseStatus::BLOCKED;
    std::cerr << "[LicenseManager] ALL MODULES BLOCKED" << std::endl;
    notifyLicenseChanged();
}

bool LicenseManager::isBlocked() const noexcept {
    return m_blocked.load();
}

// --- Renewal ---
bool LicenseManager::renewLicense(const std::string& newLicenseKey) {
    if (!validateFormat(newLicenseKey)) {
        return false;
    }
    return activateLicense(newLicenseKey, m_license.issuer);
}

// --- Queries ---
LicenseInfo LicenseManager::getLicenseInfo() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_license;
}

LicenseTier LicenseManager::currentTier() const noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_license.tier;
}

LicenseStatus LicenseManager::currentStatus() const noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_license.status;
}

std::string LicenseManager::currentLicenseKey() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_license.licenseKey;
}

int LicenseManager::maxBuses() const noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_license.maxBuses;
}

int LicenseManager::maxProjects() const noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_license.maxProjects;
}

// --- AES-256-GCM Encryption ---
std::vector<uint8_t> LicenseManager::encryptData(const std::vector<uint8_t>& plaintext,
                                                  const std::vector<uint8_t>& key,
                                                  const std::vector<uint8_t>& iv) {
    if (key.size() != 32 || iv.size() != 12) {
        std::cerr << "[LicenseManager] encryptData: invalid key/iv size" << std::endl;
        return {};
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return {};

    std::vector<uint8_t> ciphertext(plaintext.size() + EVP_MAX_BLOCK_LENGTH);
    int len = 0;
    int ciphertextLen = 0;

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, key.data(), iv.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    if (EVP_EncryptUpdate(ctx, ciphertext.data(), &len, plaintext.data(), static_cast<int>(plaintext.size())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }
    ciphertextLen = len;

    if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }
    ciphertextLen += len;

    // Get tag
    std::vector<uint8_t> tag(16);
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    EVP_CIPHER_CTX_free(ctx);

    // Append tag to ciphertext
    ciphertext.resize(ciphertextLen);
    ciphertext.insert(ciphertext.end(), tag.begin(), tag.end());
    return ciphertext;
}

std::vector<uint8_t> LicenseManager::decryptData(const std::vector<uint8_t>& ciphertext,
                                                  const std::vector<uint8_t>& key,
                                                  const std::vector<uint8_t>& iv,
                                                  const std::vector<uint8_t>& tag) {
    if (key.size() != 32 || iv.size() != 12 || tag.size() != 16) {
        std::cerr << "[LicenseManager] decryptData: invalid key/iv/tag size" << std::endl;
        return {};
    }

    if (ciphertext.size() < 16) return {};

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return {};

    std::vector<uint8_t> plaintext(ciphertext.size() + EVP_MAX_BLOCK_LENGTH);
    int len = 0;
    int plaintextLen = 0;

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, key.data(), iv.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, const_cast<uint8_t*>(tag.data())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    if (EVP_DecryptUpdate(ctx, plaintext.data(), &len, ciphertext.data(), static_cast<int>(ciphertext.size())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }
    plaintextLen = len;

    if (EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }
    plaintextLen += len;

    EVP_CIPHER_CTX_free(ctx);
    plaintext.resize(plaintextLen);
    return plaintext;
}

// --- Callbacks ---
void LicenseManager::onLicenseChanged(LicenseChangedCallback cb) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_changeCallbacks.push_back(std::move(cb));
}

void LicenseManager::onLicenseExpired(LicenseExpiredCallback cb) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_expiredCallbacks.push_back(std::move(cb));
}

void LicenseManager::notifyLicenseChanged() {
    for (auto& cb : m_changeCallbacks) {
        cb(m_license);
    }
}

void LicenseManager::notifyLicenseExpired() {
    for (auto& cb : m_expiredCallbacks) {
        cb();
    }
}

// --- Periodic Validation ---
void LicenseManager::startPeriodicValidation() {
    stopPeriodicValidation();
    m_validationRunning = true;
    m_validationThread = std::thread(&LicenseManager::validationLoop, this);
}

void LicenseManager::stopPeriodicValidation() {
    m_validationRunning = false;
    if (m_validationThread.joinable()) {
        m_validationThread.join();
    }
}

void LicenseManager::validationLoop() {
    while (m_validationRunning) {
        std::this_thread::sleep_for(std::chrono::hours(VALIDATION_INTERVAL_HOURS));
        if (!m_validationRunning) break;
        validateLicense();
    }
}

// --- Persistence ---
bool LicenseManager::saveLicenseToDisk(const std::string& filepath) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto data = serializeLicense(m_license);
    if (data.empty()) return false;

    // Generate random IV
    std::vector<uint8_t> iv(12);
    if (RAND_bytes(iv.data(), 12) != 1) return false;

    // Encrypt with master key
    auto encrypted = const_cast<LicenseManager*>(this)->encryptData(data, m_masterKey, iv);
    if (encrypted.empty()) return false;

    // Write: [IV (12 bytes)] [encrypted data + tag]
    std::ofstream ofs(filepath, std::ios::binary);
    if (!ofs) return false;

    ofs.write(reinterpret_cast<const char*>(iv.data()), iv.size());
    ofs.write(reinterpret_cast<const char*>(encrypted.data()), encrypted.size());
    return ofs.good();
}

bool LicenseManager::loadLicenseFromDisk(const std::string& filepath) {
    std::ifstream ifs(filepath, std::ios::binary | std::ios::ate);
    if (!ifs) return false;

    auto size = ifs.tellg();
    ifs.seekg(0, std::ios::beg);

    if (size < 28) return false; // IV + minimum ciphertext + tag

    std::vector<uint8_t> iv(12);
    ifs.read(reinterpret_cast<char*>(iv.data()), 12);

    std::streamsize encSize = size - static_cast<std::streamsize>(12);
    std::vector<uint8_t> encrypted(encSize);
    ifs.read(reinterpret_cast<char*>(encrypted.data()), encSize);

    // Split ciphertext and tag (tag is last 16 bytes)
    if (encrypted.size() < 16) return false;
    std::vector<uint8_t> tag(encrypted.end() - 16, encrypted.end());
    std::vector<uint8_t> ciphertext(encrypted.begin(), encrypted.end() - 16);

    auto decrypted = decryptData(ciphertext, m_masterKey, iv, tag);
    if (decrypted.empty()) return false;

    LicenseInfo info;
    if (!deserializeLicense(decrypted, info)) return false;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_license = info;
    return true;
}

std::vector<uint8_t> LicenseManager::serializeLicense(const LicenseInfo& info) const {
    std::ostringstream oss;
    oss << info.licenseKey << "\n"
        << info.issuer << "\n"
        << tierToString(info.tier) << "\n"
        << static_cast<int>(info.status) << "\n"
        << info.deviceFingerprint << "\n"
        << std::chrono::duration_cast<std::chrono::seconds>(info.issuedAt.time_since_epoch()).count() << "\n"
        << std::chrono::duration_cast<std::chrono::seconds>(info.expiresAt.time_since_epoch()).count() << "\n"
        << std::chrono::duration_cast<std::chrono::seconds>(info.gracePeriodEnds.time_since_epoch()).count() << "\n"
        << info.maxBuses << "\n"
        << info.maxProjects << "\n"
        << (info.autoRenew ? 1 : 0) << "\n"
        << info.paymentProvider << "\n"
        << info.subscriptionId << "\n"
        << info.signature << "\n"
        << info.rawPayload;

    std::string s = oss.str();
    return std::vector<uint8_t>(s.begin(), s.end());
}

bool LicenseManager::deserializeLicense(const std::vector<uint8_t>& data, LicenseInfo& info) {
    std::string str(data.begin(), data.end());
    std::istringstream iss(str);
    std::string line;

    auto readLine = [&iss, &line]() -> bool {
        return std::getline(iss, line);
    };

    if (!readLine()) return false; info.licenseKey = line;
    if (!readLine()) return false; info.issuer = line;
    if (!readLine()) return false; info.tier = tierFromString(line);
    if (!readLine()) return false; info.status = static_cast<LicenseStatus>(std::stoi(line));
    if (!readLine()) return false; info.deviceFingerprint = line;

    if (!readLine()) return false;
    auto issuedSec = std::stoll(line);
    info.issuedAt = std::chrono::system_clock::time_point(std::chrono::seconds(issuedSec));

    if (!readLine()) return false;
    auto expiresSec = std::stoll(line);
    info.expiresAt = std::chrono::system_clock::time_point(std::chrono::seconds(expiresSec));

    if (!readLine()) return false;
    auto graceSec = std::stoll(line);
    info.gracePeriodEnds = std::chrono::system_clock::time_point(std::chrono::seconds(graceSec));

    if (!readLine()) return false; info.maxBuses = std::stoi(line);
    if (!readLine()) return false; info.maxProjects = std::stoi(line);
    if (!readLine()) return false; info.autoRenew = (std::stoi(line) != 0);
    if (!readLine()) return false; info.paymentProvider = line;
    if (!readLine()) return false; info.subscriptionId = line;
    if (!readLine()) return false; info.signature = line;

    // rawPayload: rest of stream
    std::ostringstream rawOss;
    rawOss << iss.rdbuf();
    info.rawPayload = rawOss.str();

    return true;
}

// --- Server Validation ---
bool LicenseManager::validateWithServer(const std::string& serverUrl) {
    // Stub for HTTP POST - would use libcurl in production
    // Returns true for offline/development mode
    std::cout << "[LicenseManager] Server validation against: " << serverUrl << std::endl;
    return true;
}

// --- License Key Generation ---
std::string LicenseManager::generateLicenseKey() {
    static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 35);

    auto genGroup = [&]() -> std::string {
        std::string group;
        for (int i = 0; i < 4; ++i) {
            group += chars[dis(gen)];
        }
        return group;
    };

    return genGroup() + "-" + genGroup() + "-" + genGroup() + "-" + genGroup();
}

} // namespace licensing
} // namespace powsys365
