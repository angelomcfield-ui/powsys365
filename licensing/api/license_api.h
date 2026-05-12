#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <stdexcept>

namespace powsys365 {

// ============================================================================
// Enumeraciones
// ============================================================================

enum class LicenseStatus {
    Valid,
    Expired,
    Suspended,
    Invalid,
    Trial
};

enum class LicenseTier {
    LIFE_TIME,
    ENTERPRISE,
    PRO,
    BASIC,
    TRIAL,
    STUDENT
};

std::string licenseStatusToString(LicenseStatus status);
LicenseStatus stringToLicenseStatus(const std::string& str);
std::string licenseTierToString(LicenseTier tier);
LicenseTier stringToLicenseTier(const std::string& str);

// ============================================================================
// Estructuras de datos
// ============================================================================

struct DeviceFingerprint {
    std::string hwid;              // Hardware ID hash
    std::string ip;                // IP address
    std::string geo_location;      // GeoLocation string (lat,lon)
    std::string disk_serial;       // Disk serial number
    std::string mac_address;       // MAC address (primary interface)
    std::string os_version;        // OS version string
    std::string cpu_id;            // CPU identifier
    std::string model;             // Machine model
    std::string motherboard_serial;// Motherboard serial

    std::string toJson() const;
    static DeviceFingerprint fromJson(const std::string& json);
};

struct DeviceData {
    DeviceFingerprint fingerprint;
    std::string user_agent;
    std::string screen_resolution;
    int64_t timestamp;

    std::string toJson() const;
    static DeviceData fromJson(const std::string& json);
};

struct ActivationResult {
    bool success;
    std::string message;
    std::vector<std::string> features;
    std::string expiration;
    std::string activation_token;

    std::string toJson() const;
    static ActivationResult fromJson(const std::string& json);
};

struct LicenseInfo {
    std::string key;
    std::string tier;
    std::string status;
    std::string created_at;
    std::string expires_at;
    int max_devices;
    int active_devices;
    std::string user_id;
    std::vector<std::string> features;

    std::string toJson() const;
    static LicenseInfo fromJson(const std::string& json);
};

struct UsageStats {
    int calls_per_hour;
    std::string last_check;
    int devices_active;
    std::string license_key;
    int total_requests_24h;

    std::string toJson() const;
    static UsageStats fromJson(const std::string& json);
};

struct ApiResponse {
    bool success;
    int http_code;
    std::string body;
    std::string error;
};

// ============================================================================
// Excepciones
// ============================================================================

class LicenseException : public std::runtime_error {
public:
    explicit LicenseException(const std::string& msg);
    explicit LicenseException(const char* msg);
};

class NetworkException : public LicenseException {
public:
    explicit NetworkException(const std::string& msg);
};

class AuthenticationException : public LicenseException {
public:
    explicit AuthenticationException(const std::string& msg);
};

class RateLimitException : public LicenseException {
public:
    explicit RateLimitException(const std::string& msg);
    int retry_after_seconds;
    explicit RateLimitException(const std::string& msg, int retry_after);
};

// ============================================================================
// LicenseAPI Cliente REST
// ============================================================================

class LicenseAPI {
public:
    // Constructor
    explicit LicenseAPI(const std::string& base_url = "https://api.powsys365.com");

    // Configuracion
    void setApiKey(const std::string& api_key);
    void setTimeoutMs(int timeout_ms);
    void setVerifySsl(bool verify);

    // --- Metodos principales ---

    // Autenticar licencia (key + deviceFingerprint)
    bool authenticateLicense(const std::string& key,
                             const DeviceFingerprint& fingerprint);

    // Validar licencia (solo key)
    LicenseStatus validateLicense(const std::string& key);

    // Activar licencia en dispositivo
    ActivationResult activateLicense(const std::string& key,
                                     const DeviceData& device_data);

    // Desactivar licencia de dispositivo
    bool deactivateLicense(const std::string& key,
                           const DeviceData& device_data);

    // Suspender licencia (admin)
    bool suspendLicense(const std::string& key,
                        const std::string& reason);

    // Reactivar licencia (admin)
    bool reactivateLicense(const std::string& key);

    // Obtener info completa de licencia
    LicenseInfo getLicenseInfo(const std::string& key);

    // Obtener fingerprint del dispositivo local
    DeviceFingerprint getDeviceFingerprint();

    // Colectar datos completos del dispositivo
    DeviceData collectDeviceData();

    // Verificar acceso a feature por tier
    bool checkFeatureAccess(const std::string& feature, LicenseTier tier);

    // Monitorear uso de licencia
    UsageStats monitorLicenseUsage(const std::string& key);

    // --- Utilidades ---

    // Health check del servidor
    bool healthCheck();

    // Obtener version del API
    std::string getApiVersion();

private:
    std::string base_url_;
    std::string api_key_;
    int timeout_ms_;
    bool verify_ssl_;

    // HTTP helpers
    ApiResponse httpPost(const std::string& endpoint,
                         const std::string& json_body);
    ApiResponse httpGet(const std::string& endpoint);

    // Construir headers con auth
    std::vector<std::string> buildHeaders() const;

    // Helpers de sistema
    static std::string getLocalIpAddress();
    static std::string getPrimaryMacAddress();
    static std::string getDiskSerial();
    static std::string getCpuId();
    static std::string getOsVersion();
    static std::string getMachineModel();
    static std::string getMotherboardSerial();
    static std::string generateHwid(const DeviceFingerprint& fp);
    static std::string hashSha256(const std::string& input);
    static std::string base64Encode(const unsigned char* data, size_t len);

    // URL encode
    static std::string urlEncode(const std::string& value);
};

} // namespace powsys365
