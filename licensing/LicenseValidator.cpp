#include "LicenseValidator.h"
#include "DeviceFingerprint.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <regex>
#include <sstream>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>

// For HTTP POST in validateWithServer
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <netdb.h>
    #include <sys/socket.h>
    #include <unistd.h>
    #define SOCKET int
    #define closesocket close
#endif

namespace powsys365 {
namespace licensing {

// ============================================================
// Helpers
// ============================================================
static std::vector<uint8_t> sha256Bytes(const std::string& data) {
    std::vector<uint8_t> hash(SHA256_DIGEST_LENGTH);
    SHA256(reinterpret_cast<const unsigned char*>(data.data()), data.size(), hash.data());
    return hash;
}

static std::string toHexString(const std::vector<uint8_t>& data) {
    std::ostringstream oss;
    for (auto b : data) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
    }
    return oss.str();
}

static std::string toUpper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// ============================================================
// Constructor / Destructor
// ============================================================
LicenseValidator::LicenseValidator() = default;

LicenseValidator::~LicenseValidator() {
    unloadPublicKey();
}

// ============================================================
// Format Validation: XXXX-XXXX-XXXX-XXXX
// ============================================================
ValidationResult LicenseValidator::validateFormat(const std::string& licenseKey) {
    if (licenseKey.empty()) {
        return ValidationResult::fail("FORMAT_EMPTY", "License key is empty");
    }

    // Check length: 4+1+4+1+4+1+4 = 19 chars
    if (licenseKey.length() != 19) {
        return ValidationResult::fail("FORMAT_LENGTH",
            "Invalid license key length: expected 19, got " + std::to_string(licenseKey.length()));
    }

    // Check dashes at positions 4, 9, 14
    if (licenseKey[4] != '-' || licenseKey[9] != '-' || licenseKey[14] != '-') {
        return ValidationResult::fail("FORMAT_SEPARATORS",
            "Invalid separator positions in license key");
    }

    // Check each group: 4 uppercase alphanumeric chars
    static const std::regex groupRe("[A-Z0-9]{4}");
    std::string groups[4] = {
        licenseKey.substr(0, 4),
        licenseKey.substr(5, 4),
        licenseKey.substr(10, 4),
        licenseKey.substr(15, 4)
    };

    for (int i = 0; i < 4; ++i) {
        if (!std::regex_match(groups[i], groupRe)) {
            return ValidationResult::fail("FORMAT_GROUP_" + std::to_string(i),
                "Invalid characters in group " + std::to_string(i + 1) +
                ": expected 4 uppercase alphanumeric chars");
        }
    }

    // Check prohibited characters (I, O to avoid confusion)
    static const std::string forbidden = "IO";
    for (char c : licenseKey) {
        if (forbidden.find(c) != std::string::npos) {
            return ValidationResult::fail("FORMAT_FORBIDDEN_CHAR",
                "License key contains forbidden character: '" + std::string(1, c) + "'");
        }
    }

    return ValidationResult::ok();
}

// ============================================================
// Checksum Validation (SHA-256 based)
// ============================================================
ValidationResult LicenseValidator::validateChecksum(const std::string& licenseKey) {
    auto formatResult = validateFormat(licenseKey);
    if (!formatResult.success) return formatResult;

    // Extract prefix: first 2 groups (positions 0-8)
    std::string prefix = licenseKey.substr(0, 4) + licenseKey.substr(5, 4); // 8 chars without dash

    // Compute SHA-256 hash of prefix
    auto hash = sha256Bytes(prefix);
    std::string hashHex = toUpper(toHexString(hash));

    // Groups 3 and 4 should encode a 8-char fragment from the hash
    std::string group3 = licenseKey.substr(10, 4);
    std::string group4 = licenseKey.substr(15, 4);
    std::string combined = group3 + group4;

    // Check that the 8-char combined string appears in first 32 chars of hash
    bool found = (hashHex.find(combined) != std::string::npos);

    // Also verify: the combined value when decoded should match hash segment
    if (!found) {
        // Alternative: direct checksum verification
        // Compute checksum as XOR of all prefix bytes, then hex-encode
        uint8_t checksum = 0;
        for (auto c : prefix) {
            checksum ^= static_cast<uint8_t>(c);
        }
        uint16_t checksum16 = (static_cast<uint16_t>(checksum) << 8) | checksum;
        std::ostringstream ckOss;
        ckOss << std::hex << std::setw(4) << std::setfill('0') << std::uppercase << checksum16;
        std::string expectedChecksum = toUpper(ckOss.str());

        if (combined != expectedChecksum) {
            return ValidationResult::fail("CHECKSUM_MISMATCH",
                "License key checksum validation failed");
        }
    }

    return ValidationResult::ok();
}

// ============================================================
// Expiration Validation (with 30-day grace period)
// ============================================================
ValidationResult LicenseValidator::validateExpiration(const LicenseInfo& info) {
    auto now = std::chrono::system_clock::now();

    // Lifetime licenses never expire
    if (info.tier == LicenseTier::LIFETIME) {
        return ValidationResult::ok();
    }

    // Check if still within main validity period
    if (now <= info.expiresAt) {
        auto remaining = info.expiresAt - now;
        auto days = std::chrono::duration_cast<std::chrono::hours>(remaining).count() / 24;
        auto result = ValidationResult::ok();
        result.errorMessage = "License valid, " + std::to_string(days) + " days remaining";
        return result;
    }

    // Check grace period (30 days after expiration)
    if (now <= info.gracePeriodEnds) {
        auto graceRemaining = info.gracePeriodEnds - now;
        auto graceDays = std::chrono::duration_cast<std::chrono::hours>(graceRemaining).count() / 24;
        auto result = ValidationResult::ok();
        result.errorMessage = "License in grace period, " + std::to_string(graceDays) +
                              " grace days remaining";
        return result;
    }

    // Fully expired
    auto expiredSince = now - info.expiresAt;
    auto expiredDays = std::chrono::duration_cast<std::chrono::hours>(expiredSince).count() / 24;
    return ValidationResult::fail("LICENSE_EXPIRED",
        "License expired " + std::to_string(expiredDays) + " days ago");
}

// ============================================================
// Device Fingerprint Validation
// ============================================================
ValidationResult LicenseValidator::validateDevice(const LicenseInfo& info) {
    if (info.deviceFingerprint.empty()) {
        // No device binding: allow
        return ValidationResult::ok();
    }

    DeviceFingerprint currentFp;
    std::string current = currentFp.collect().toString();

    // Normalize: compare core components (HWID, MAC, CPU, Disk)
    // Allow minor changes (IP, OS version updates)
    DeviceFingerprint storedFp;
    storedFp.fromString(info.deviceFingerprint);
    auto currentComponents = currentFp.collect();
    auto storedComponents = storedFp.collect();

    // Check core fingerprint match (HWID + MAC + CPU + Disk = strong identity)
    int matchScore = 0;
    int totalScore = 4;

    if (currentComponents.hwid == storedComponents.hwid) matchScore++;
    if (currentComponents.macAddress == storedComponents.macAddress) matchScore++;
    if (currentComponents.cpuId == storedComponents.cpuId) matchScore++;
    if (currentComponents.diskSerial == storedComponents.diskSerial) matchScore++;

    // Require at least 2 of 4 core components to match (tolerates hardware changes)
    if (matchScore >= 2) {
        return ValidationResult::ok();
    }

    return ValidationResult::fail("DEVICE_MISMATCH",
        "Device fingerprint mismatch: " + std::to_string(matchScore) + "/" +
        std::to_string(totalScore) + " core components match");
}

// ============================================================
// RSA Signature Validation
// ============================================================
ValidationResult LicenseValidator::validateSignature(const LicenseInfo& info) {
    if (!m_rsaPublicKey) {
        // No public key configured: skip signature check for default license
        if (info.licenseKey == LicenseManager::DEFAULT_LICENSE) {
            return ValidationResult::ok();
        }
        return ValidationResult::fail("SIGNATURE_NO_KEY",
            "No RSA public key loaded for signature verification");
    }

    if (info.signature.empty()) {
        return ValidationResult::fail("SIGNATURE_MISSING",
            "License signature is missing");
    }

    if (info.rawPayload.empty()) {
        return ValidationResult::fail("SIGNATURE_NO_PAYLOAD",
            "License raw payload is empty");
    }

    // Decode hex signature
    std::string hexSig = info.signature;
    if (hexSig.length() % 2 != 0) {
        return ValidationResult::fail("SIGNATURE_FORMAT",
            "Invalid signature hex format");
    }

    std::vector<uint8_t> sigBytes;
    sigBytes.reserve(hexSig.length() / 2);
    for (size_t i = 0; i + 1 < hexSig.length(); i += 2) {
        std::string byteStr = hexSig.substr(i, 2);
        try {
            sigBytes.push_back(static_cast<uint8_t>(std::stoul(byteStr, nullptr, 16)));
        } catch (...) {
            return ValidationResult::fail("SIGNATURE_DECODE",
                "Failed to decode signature from hex");
        }
    }

    if (!performRsaVerify(info.rawPayload, sigBytes)) {
        return ValidationResult::fail("SIGNATURE_INVALID",
            "RSA signature verification failed");
    }

    return ValidationResult::ok();
}

bool LicenseValidator::performRsaVerify(const std::string& data,
                                         const std::vector<uint8_t>& signature) {
    if (!m_rsaPublicKey) return false;

    // Compute SHA-256 digest of data
    std::vector<uint8_t> digest(SHA256_DIGEST_LENGTH);
    SHA256(reinterpret_cast<const unsigned char*>(data.data()), data.size(), digest.data());

    // Verify with EVP API
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return false;

    EVP_PKEY* pkey = EVP_PKEY_new();
    if (!pkey) {
        EVP_MD_CTX_free(ctx);
        return false;
    }
    EVP_PKEY_assign_RSA(pkey, m_rsaPublicKey);

    bool result = false;
    if (EVP_DigestVerifyInit(ctx, nullptr, EVP_sha256(), nullptr, pkey) == 1) {
        if (EVP_DigestVerifyUpdate(ctx, data.data(), data.size()) == 1) {
            result = (EVP_DigestVerifyFinal(ctx, signature.data(), signature.size()) == 1);
        }
    }

    EVP_PKEY_free(pkey);
    EVP_MD_CTX_free(ctx);
    return result;
}

// ============================================================
// Full Validation (runs all checks)
// ============================================================
ValidationResult LicenseValidator::validateFull(const LicenseInfo& info) {
    // 1. Format
    auto r = validateFormat(info.licenseKey);
    if (!r.success) return r;

    // 2. Checksum
    r = validateChecksum(info.licenseKey);
    if (!r.success) return r;

    // 3. Expiration
    r = validateExpiration(info);
    if (!r.success) return r;

    // 4. Device
    r = validateDevice(info);
    if (!r.success) return r;

    // 5. Signature
    r = validateSignature(info);
    if (!r.success) return r;

    // 6. Custom validation steps
    r = runValidationChain(info);
    if (!r.success) return r;

    return ValidationResult::ok();
}

// ============================================================
// Server Validation (HTTP POST)
// ============================================================
ServerValidationResponse LicenseValidator::validateWithServer(
    const std::string& licenseKey,
    const std::string& deviceFingerprint,
    const std::string& serverUrl) {

    ServerValidationResponse response;
    response.licenseKey = licenseKey;

    // Parse server URL (simple parsing for host:port/path)
    // Expected format: "https://api.example.com:443/v1/validate"
    size_t protoEnd = serverUrl.find("://");
    if (protoEnd == std::string::npos) {
        response.message = "Invalid server URL: missing protocol";
        return response;
    }

    std::string protocol = serverUrl.substr(0, protoEnd);
    std::string rest = serverUrl.substr(protoEnd + 3);

    size_t pathStart = rest.find('/');
    std::string hostPort = (pathStart != std::string::npos) ? rest.substr(0, pathStart) : rest;
    std::string path = (pathStart != std::string::npos) ? rest.substr(pathStart) : "/";

    size_t colonPos = hostPort.find(':');
    std::string host = (colonPos != std::string::npos) ? hostPort.substr(0, colonPos) : hostPort;
    int port = 443;
    if (colonPos != std::string::npos) {
        try {
            port = std::stoi(hostPort.substr(colonPos + 1));
        } catch (...) {
            port = (protocol == "https") ? 443 : 80;
        }
    }

    // Build JSON payload
    std::ostringstream jsonPayload;
    jsonPayload << "{"
                << "\"license_key\":\"" << licenseKey << "\","
                << "\"device_fingerprint\":\"" << deviceFingerprint << "\","
                << "\"timestamp\":\"" << std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch()).count() << "\","
                << "\"version\":\"1.0.0\""
                << "}";
    std::string payload = jsonPayload.str();

    // Build HTTP POST request
    std::ostringstream request;
    request << "POST " << path << " HTTP/1.1\r\n"
            << "Host: " << host << "\r\n"
            << "Content-Type: application/json\r\n"
            << "Content-Length: " << payload.length() << "\r\n"
            << "Connection: close\r\n"
            << "\r\n"
            << payload;

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        response.message = "WSAStartup failed";
        return response;
    }
#endif

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        response.message = "Socket creation failed";
#ifdef _WIN32
        WSACleanup();
#endif
        return response;
    }

    struct hostent* server = gethostbyname(host.c_str());
    if (!server) {
        response.message = "DNS resolution failed for: " + host;
        closesocket(sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return response;
    }

    struct sockaddr_in serverAddr;
    std::memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    std::memcpy(&serverAddr.sin_addr.s_addr, server->h_addr, server->h_length);

    if (connect(sock, reinterpret_cast<struct sockaddr*>(&serverAddr), sizeof(serverAddr)) < 0) {
        response.message = "Connection failed to " + host + ":" + std::to_string(port);
        closesocket(sock);
#ifdef _WIN32
        WSACleanup();
#endif
        // Offline mode: accept license
        response.valid = true;
        response.isActive = true;
        response.message = "Offline mode: server unreachable, accepting license";
        return response;
    }

    std::string httpRequest = request.str();
    if (send(sock, httpRequest.c_str(), static_cast<int>(httpRequest.length()), 0) < 0) {
        response.message = "HTTP send failed";
        closesocket(sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return response;
    }

    // Receive response
    std::string responseData;
    char buffer[4096];
    int bytesRead;
    while ((bytesRead = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytesRead] = '\0';
        responseData += buffer;
    }

    closesocket(sock);
#ifdef _WIN32
    WSACleanup();
#endif

    // Parse HTTP response (simple)
    if (responseData.find("HTTP/1.1 200") != std::string::npos ||
        responseData.find("HTTP/1.0 200") != std::string::npos) {
        response.httpStatusCode = 200;

        // Parse JSON body (after \r\n\r\n)
        size_t bodyStart = responseData.find("\r\n\r\n");
        if (bodyStart != std::string::npos) {
            std::string body = responseData.substr(bodyStart + 4);

            // Simple JSON parsing
            if (body.find("\"valid\":true") != std::string::npos ||
                body.find("\"valid\": true") != std::string::npos) {
                response.valid = true;
            }
            if (body.find("\"active\":true") != std::string::npos ||
                body.find("\"active\": true") != std::string::npos) {
                response.isActive = true;
            }
            if (body.find("\"revoked\":true") != std::string::npos ||
                body.find("\"revoked\": true") != std::string::npos) {
                response.isRevoked = true;
                response.isActive = false;
                response.valid = false;
            }

            // Extract tier
            size_t tierPos = body.find("\"tier\"");
            if (tierPos != std::string::npos) {
                size_t tierValStart = body.find('"', tierPos + 6);
                if (tierValStart != std::string::npos) {
                    size_t tierValEnd = body.find('"', tierValStart + 1);
                    if (tierValEnd != std::string::npos) {
                        response.tier = body.substr(tierValStart + 1, tierValEnd - tierValStart - 1);
                    }
                }
            }
        }

        response.message = "Server validation successful";
    } else {
        response.httpStatusCode = 0;
        // Try to extract status code
        size_t statusPos = responseData.find("HTTP/1.");
        if (statusPos != std::string::npos) {
            size_t codeStart = responseData.find(' ', statusPos);
            if (codeStart != std::string::npos) {
                try {
                    response.httpStatusCode = std::stoi(responseData.substr(codeStart + 1, 3));
                } catch (...) {}
            }
        }
        response.message = "Server returned non-200 status: " +
                           std::to_string(response.httpStatusCode);
        // Offline fallback: accept if we can't reach server
        response.valid = true;
        response.isActive = true;
    }

    return response;
}

// ============================================================
// RSA Key Management
// ============================================================
bool LicenseValidator::loadPublicKeyPEM(const std::string& pemData) {
    unloadPublicKey();

    BIO* bio = BIO_new_mem_buf(pemData.data(), static_cast<int>(pemData.length()));
    if (!bio) return false;

    m_rsaPublicKey = PEM_read_bio_RSA_PUBKEY(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);

    return (m_rsaPublicKey != nullptr);
}

bool LicenseValidator::loadPublicKeyFromFile(const std::string& filepath) {
    std::ifstream ifs(filepath);
    if (!ifs) return false;

    std::string pemData((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());
    return loadPublicKeyPEM(pemData);
}

void LicenseValidator::unloadPublicKey() {
    if (m_rsaPublicKey) {
        RSA_free(m_rsaPublicKey);
        m_rsaPublicKey = nullptr;
    }
}

// ============================================================
// Checksum Generation
// ============================================================
std::string LicenseValidator::generateChecksum(const std::string& prefix) {
    // Compute XOR checksum of prefix bytes
    uint8_t checksum = 0;
    for (auto c : prefix) {
        checksum ^= static_cast<uint8_t>(c);
    }

    // Expand to 4 hex chars
    uint16_t checksum16 = (static_cast<uint16_t>(checksum) << 8) | checksum;
    std::ostringstream oss;
    oss << std::hex << std::setw(4) << std::setfill('0') << std::uppercase << checksum16;
    return oss.str();
}

std::string LicenseValidator::generateLicenseKeyWithChecksum() {
    static const char* chars = "ABCDEFGHJKLMNPQRSTUVWXYZ0123456789"; // No I, O
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, static_cast<int>(std::strlen(chars)) - 1);

    auto genGroup = [&]() -> std::string {
        std::string group;
        for (int i = 0; i < 4; ++i) {
            group += chars[dis(gen)];
        }
        return group;
    };

    std::string g1 = genGroup();
    std::string g2 = genGroup();
    std::string prefix = g1 + g2;
    std::string checksum = generateChecksum(prefix);

    // Split checksum into two groups of 4 (pad if needed)
    std::string g3 = checksum.substr(0, 4);
    std::string g4 = genGroup(); // Random 4th group

    return g1 + "-" + g2 + "-" + g3 + "-" + g4;
}

// ============================================================
// Validation Chain
// ============================================================
void LicenseValidator::addValidationStep(ValidationStep step) {
    m_validationSteps.push_back(std::move(step));
}

ValidationResult LicenseValidator::runValidationChain(const LicenseInfo& info) {
    for (auto& step : m_validationSteps) {
        auto result = step(info);
        if (!result.success) return result;
    }
    return ValidationResult::ok();
}

} // namespace licensing
} // namespace powsys365
