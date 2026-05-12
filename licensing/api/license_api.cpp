#include "license_api.h"

#include <curl/curl.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>
#include <openssl/sha.h>

#include <sstream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <regex>

// Platform-specific headers
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <iphlpapi.h>
    #include <windows.h>
    #pragma comment(lib, "iphlpapi.lib")
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/ioctl.h>
    #include <sys/socket.h>
    #include <sys/utsname.h>
    #include <net/if.h>
    #include <unistd.h>
    #include <ifaddrs.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
#endif

namespace powsys365 {

// ============================================================================
// String conversions
// ============================================================================

std::string licenseStatusToString(LicenseStatus status) {
    switch (status) {
        case LicenseStatus::Valid:     return "valid";
        case LicenseStatus::Expired:   return "expired";
        case LicenseStatus::Suspended: return "suspended";
        case LicenseStatus::Invalid:   return "invalid";
        case LicenseStatus::Trial:     return "trial";
        default:                       return "unknown";
    }
}

LicenseStatus stringToLicenseStatus(const std::string& str) {
    std::string lower;
    for (char c : str) lower += static_cast<char>(std::tolower(c));
    if (lower == "valid")     return LicenseStatus::Valid;
    if (lower == "expired")   return LicenseStatus::Expired;
    if (lower == "suspended") return LicenseStatus::Suspended;
    if (lower == "invalid")   return LicenseStatus::Invalid;
    if (lower == "trial")     return LicenseStatus::Trial;
    return LicenseStatus::Invalid;
}

std::string licenseTierToString(LicenseTier tier) {
    switch (tier) {
        case LicenseTier::LIFE_TIME:  return "LIFE_TIME";
        case LicenseTier::ENTERPRISE: return "ENTERPRISE";
        case LicenseTier::PRO:        return "PRO";
        case LicenseTier::BASIC:      return "BASIC";
        case LicenseTier::TRIAL:      return "TRIAL";
        case LicenseTier::STUDENT:    return "STUDENT";
        default:                      return "UNKNOWN";
    }
}

LicenseTier stringToLicenseTier(const std::string& str) {
    std::string upper;
    for (char c : str) upper += static_cast<char>(std::toupper(c));
    if (upper == "LIFE_TIME")  return LicenseTier::LIFE_TIME;
    if (upper == "ENTERPRISE") return LicenseTier::ENTERPRISE;
    if (upper == "PRO")        return LicenseTier::PRO;
    if (upper == "BASIC")      return LicenseTier::BASIC;
    if (upper == "TRIAL")      return LicenseTier::TRIAL;
    if (upper == "STUDENT")    return LicenseTier::STUDENT;
    return LicenseTier::BASIC;
}

// ============================================================================
// JSON helpers (inline minimal implementation)
// ============================================================================

static std::string jsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;       break;
        }
    }
    return out;
}

static std::string jsonStringField(const std::string& name, const std::string& value, bool last = false) {
    return "\"" + name + "\":\"" + jsonEscape(value) + "\"" + (last ? "" : ",");
}

static std::string jsonIntField(const std::string& name, int value, bool last = false) {
    return "\"" + name + "\":" + std::to_string(value) + (last ? "" : ",");
}

static std::string jsonBoolField(const std::string& name, bool value, bool last = false) {
    return "\"" + name + "\":" + std::string(value ? "true" : "false") + (last ? "" : ",");
}

static std::string jsonStringArray(const std::vector<std::string>& arr) {
    std::string json = "[";
    for (size_t i = 0; i < arr.size(); ++i) {
        json += "\"" + jsonEscape(arr[i]) + "\"";
        if (i + 1 < arr.size()) json += ",";
    }
    json += "]";
    return json;
}

static std::string extractJsonString(const std::string& json, const std::string& key) {
    std::regex re("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch match;
    if (std::regex_search(json, match, re) && match.size() > 1) {
        return match[1].str();
    }
    return "";
}

static int extractJsonInt(const std::string& json, const std::string& key) {
    std::regex re("\"" + key + "\"\\s*:\\s*(\\d+)");
    std::smatch match;
    if (std::regex_search(json, match, re) && match.size() > 1) {
        return std::stoi(match[1].str());
    }
    return 0;
}

static bool extractJsonBool(const std::string& json, const std::string& key) {
    std::regex re("\"" + key + "\"\\s*:\\s*(true|false)");
    std::smatch match;
    if (std::regex_search(json, match, re) && match.size() > 1) {
        return match[1].str() == "true";
    }
    return false;
}

// ============================================================================
// DeviceFingerprint
// ============================================================================

std::string DeviceFingerprint::toJson() const {
    return "{" +
        jsonStringField("hwid", hwid) +
        jsonStringField("ip", ip) +
        jsonStringField("geo_location", geo_location) +
        jsonStringField("disk_serial", disk_serial) +
        jsonStringField("mac_address", mac_address) +
        jsonStringField("os_version", os_version) +
        jsonStringField("cpu_id", cpu_id) +
        jsonStringField("model", model) +
        jsonStringField("motherboard_serial", motherboard_serial, true) +
        "}";
}

DeviceFingerprint DeviceFingerprint::fromJson(const std::string& json) {
    DeviceFingerprint fp;
    fp.hwid               = extractJsonString(json, "hwid");
    fp.ip                 = extractJsonString(json, "ip");
    fp.geo_location       = extractJsonString(json, "geo_location");
    fp.disk_serial        = extractJsonString(json, "disk_serial");
    fp.mac_address        = extractJsonString(json, "mac_address");
    fp.os_version         = extractJsonString(json, "os_version");
    fp.cpu_id             = extractJsonString(json, "cpu_id");
    fp.model              = extractJsonString(json, "model");
    fp.motherboard_serial = extractJsonString(json, "motherboard_serial");
    return fp;
}

// ============================================================================
// DeviceData
// ============================================================================

std::string DeviceData::toJson() const {
    return "{" +
        jsonStringField("fingerprint", fingerprint.toJson()) +
        jsonStringField("user_agent", user_agent) +
        jsonStringField("screen_resolution", screen_resolution) +
        jsonIntField("timestamp", timestamp, true) +
        "}";
}

DeviceData DeviceData::fromJson(const std::string& json) {
    DeviceData dd;
    dd.user_agent        = extractJsonString(json, "user_agent");
    dd.screen_resolution = extractJsonString(json, "screen_resolution");
    dd.timestamp         = extractJsonInt(json, "timestamp");
    // fingerprint extraction simplified
    return dd;
}

// ============================================================================
// ActivationResult
// ============================================================================

std::string ActivationResult::toJson() const {
    return "{" +
        jsonBoolField("success", success) +
        jsonStringField("message", message) +
        jsonStringField("expiration", expiration) +
        jsonStringField("activation_token", activation_token) +
        "\"features\":" + jsonStringArray(features) + "}";
}

ActivationResult ActivationResult::fromJson(const std::string& json) {
    ActivationResult ar;
    ar.success          = extractJsonBool(json, "success");
    ar.message          = extractJsonString(json, "message");
    ar.expiration       = extractJsonString(json, "expiration");
    ar.activation_token = extractJsonString(json, "activation_token");
    return ar;
}

// ============================================================================
// LicenseInfo
// ============================================================================

std::string LicenseInfo::toJson() const {
    return "{" +
        jsonStringField("key", key) +
        jsonStringField("tier", tier) +
        jsonStringField("status", status) +
        jsonStringField("created_at", created_at) +
        jsonStringField("expires_at", expires_at) +
        jsonIntField("max_devices", max_devices) +
        jsonIntField("active_devices", active_devices) +
        jsonStringField("user_id", user_id) +
        "\"features\":" + jsonStringArray(features) + "}";
}

LicenseInfo LicenseInfo::fromJson(const std::string& json) {
    LicenseInfo li;
    li.key            = extractJsonString(json, "key");
    li.tier           = extractJsonString(json, "tier");
    li.status         = extractJsonString(json, "status");
    li.created_at     = extractJsonString(json, "created_at");
    li.expires_at     = extractJsonString(json, "expires_at");
    li.max_devices    = extractJsonInt(json, "max_devices");
    li.active_devices = extractJsonInt(json, "active_devices");
    li.user_id        = extractJsonString(json, "user_id");
    return li;
}

// ============================================================================
// UsageStats
// ============================================================================

std::string UsageStats::toJson() const {
    return "{" +
        jsonIntField("calls_per_hour", calls_per_hour) +
        jsonStringField("last_check", last_check) +
        jsonIntField("devices_active", devices_active) +
        jsonStringField("license_key", license_key) +
        jsonIntField("total_requests_24h", total_requests_24h, true) +
        "}";
}

UsageStats UsageStats::fromJson(const std::string& json) {
    UsageStats us;
    us.calls_per_hour      = extractJsonInt(json, "calls_per_hour");
    us.last_check          = extractJsonString(json, "last_check");
    us.devices_active      = extractJsonInt(json, "devices_active");
    us.license_key         = extractJsonString(json, "license_key");
    us.total_requests_24h  = extractJsonInt(json, "total_requests_24h");
    return us;
}

// ============================================================================
// Excepciones
// ============================================================================

LicenseException::LicenseException(const std::string& msg) : std::runtime_error(msg) {}
LicenseException::LicenseException(const char* msg) : std::runtime_error(msg) {}
NetworkException::NetworkException(const std::string& msg) : LicenseException(msg) {}
AuthenticationException::AuthenticationException(const std::string& msg) : LicenseException(msg) {}
RateLimitException::RateLimitException(const std::string& msg) : LicenseException(msg), retry_after_seconds(0) {}
RateLimitException::RateLimitException(const std::string& msg, int retry_after)
    : LicenseException(msg), retry_after_seconds(retry_after) {}

// ============================================================================
// CURL write callback
// ============================================================================

static size_t curlWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    auto* response = static_cast<std::string*>(userp);
    response->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

// ============================================================================
// LicenseAPI Implementation
// ============================================================================

LicenseAPI::LicenseAPI(const std::string& base_url)
    : base_url_(base_url), timeout_ms_(10000), verify_ssl_(true) {}

void LicenseAPI::setApiKey(const std::string& api_key) { api_key_ = api_key; }
void LicenseAPI::setTimeoutMs(int timeout_ms) { timeout_ms_ = timeout_ms; }
void LicenseAPI::setVerifySsl(bool verify) { verify_ssl_ = verify; }

std::vector<std::string> LicenseAPI::buildHeaders() const {
    std::vector<std::string> headers;
    headers.emplace_back("Content-Type: application/json");
    headers.emplace_back("Accept: application/json");
    headers.emplace_back("User-Agent: POWSYS365-LicenseClient/1.0");
    if (!api_key_.empty()) {
        headers.emplace_back("X-API-Key: " + api_key_);
    }
    return headers;
}

ApiResponse LicenseAPI::httpPost(const std::string& endpoint, const std::string& json_body) {
    CURL* curl = curl_easy_init();
    ApiResponse response{false, 0, "", ""};

    if (!curl) {
        response.error = "Failed to initialize CURL";
        return response;
    }

    std::string url = base_url_ + endpoint;
    std::string response_body;

    struct curl_slist* header_list = nullptr;
    auto headers = buildHeaders();
    for (const auto& h : headers) {
        header_list = curl_slist_append(header_list, h.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms_);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, verify_ssl_ ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, verify_ssl_ ? 2L : 0L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        response.error = curl_easy_strerror(res);
        curl_slist_free_all(header_list);
        curl_easy_cleanup(curl);
        throw NetworkException("HTTP POST failed: " + response.error);
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    response.http_code = static_cast<int>(http_code);
    response.body = response_body;
    response.success = (http_code >= 200 && http_code < 300);

    curl_slist_free_all(header_list);
    curl_easy_cleanup(curl);

    // Handle rate limiting
    if (http_code == 429) {
        int retry_after = 60;
        // Extract Retry-After from headers if possible
        throw RateLimitException("Rate limit exceeded. Retry after " + std::to_string(retry_after) + " seconds.", retry_after);
    }

    // Handle auth errors
    if (http_code == 401 || http_code == 403) {
        throw AuthenticationException("Authentication failed: " + response_body);
    }

    return response;
}

ApiResponse LicenseAPI::httpGet(const std::string& endpoint) {
    CURL* curl = curl_easy_init();
    ApiResponse response{false, 0, "", ""};

    if (!curl) {
        response.error = "Failed to initialize CURL";
        return response;
    }

    std::string url = base_url_ + endpoint;
    std::string response_body;

    struct curl_slist* header_list = nullptr;
    auto headers = buildHeaders();
    for (const auto& h : headers) {
        header_list = curl_slist_append(header_list, h.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms_);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, verify_ssl_ ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, verify_ssl_ ? 2L : 0L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        response.error = curl_easy_strerror(res);
        curl_slist_free_all(header_list);
        curl_easy_cleanup(curl);
        throw NetworkException("HTTP GET failed: " + response.error);
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    response.http_code = static_cast<int>(http_code);
    response.body = response_body;
    response.success = (http_code >= 200 && http_code < 300);

    curl_slist_free_all(header_list);
    curl_easy_cleanup(curl);

    if (http_code == 429) {
        int retry_after = 60;
        throw RateLimitException("Rate limit exceeded. Retry after " + std::to_string(retry_after) + " seconds.", retry_after);
    }
    if (http_code == 401 || http_code == 403) {
        throw AuthenticationException("Authentication failed: " + response_body);
    }

    return response;
}

// ============================================================================
// API Methods
// ============================================================================

bool LicenseAPI::authenticateLicense(const std::string& key, const DeviceFingerprint& fingerprint) {
    std::string json_body = "{" +
        jsonStringField("license_key", key) +
        jsonStringField("fingerprint", fingerprint.toJson(), true) +
        "}";

    try {
        ApiResponse resp = httpPost("/api/v1/auth", json_body);
        if (resp.success) {
            return extractJsonBool(resp.body, "authenticated");
        }
        return false;
    } catch (const AuthenticationException&) {
        return false;
    } catch (const NetworkException& e) {
        throw LicenseException(std::string("Authentication request failed: ") + e.what());
    }
}

LicenseStatus LicenseAPI::validateLicense(const std::string& key) {
    std::string json_body = "{" +
        jsonStringField("license_key", key, true) +
        "}";

    try {
        ApiResponse resp = httpPost("/api/v1/validate", json_body);
        if (resp.success) {
            std::string status_str = extractJsonString(resp.body, "status");
            return stringToLicenseStatus(status_str);
        }
        return LicenseStatus::Invalid;
    } catch (const NetworkException& e) {
        throw LicenseException(std::string("Validation request failed: ") + e.what());
    }
}

ActivationResult LicenseAPI::activateLicense(const std::string& key, const DeviceData& device_data) {
    std::string json_body = "{" +
        jsonStringField("license_key", key) +
        jsonStringField("device_data", device_data.toJson(), true) +
        "}";

    try {
        ApiResponse resp = httpPost("/api/v1/activate", json_body);
        if (resp.success) {
            ActivationResult result = ActivationResult::fromJson(resp.body);
            if (!result.success) {
                throw LicenseException("Activation failed: " + result.message);
            }
            return result;
        }
        ActivationResult fail;
        fail.success = false;
        fail.message = "Server returned HTTP " + std::to_string(resp.http_code);
        return fail;
    } catch (const NetworkException& e) {
        throw LicenseException(std::string("Activation request failed: ") + e.what());
    }
}

bool LicenseAPI::deactivateLicense(const std::string& key, const DeviceData& device_data) {
    std::string json_body = "{" +
        jsonStringField("license_key", key) +
        jsonStringField("device_data", device_data.toJson(), true) +
        "}";

    try {
        ApiResponse resp = httpPost("/api/v1/deactivate", json_body);
        if (resp.success) {
            return extractJsonBool(resp.body, "deactivated");
        }
        return false;
    } catch (const NetworkException& e) {
        throw LicenseException(std::string("Deactivation request failed: ") + e.what());
    }
}

bool LicenseAPI::suspendLicense(const std::string& key, const std::string& reason) {
    std::string json_body = "{" +
        jsonStringField("license_key", key) +
        jsonStringField("reason", reason, true) +
        "}";

    try {
        ApiResponse resp = httpPost("/api/v1/suspend", json_body);
        if (resp.success) {
            return extractJsonBool(resp.body, "suspended");
        }
        return false;
    } catch (const NetworkException& e) {
        throw LicenseException(std::string("Suspend request failed: ") + e.what());
    }
}

bool LicenseAPI::reactivateLicense(const std::string& key) {
    std::string json_body = "{" +
        jsonStringField("license_key", key, true) +
        "}";

    try {
        ApiResponse resp = httpPost("/api/v1/reactivate", json_body);
        if (resp.success) {
            return extractJsonBool(resp.body, "reactivated");
        }
        return false;
    } catch (const NetworkException& e) {
        throw LicenseException(std::string("Reactivation request failed: ") + e.what());
    }
}

LicenseInfo LicenseAPI::getLicenseInfo(const std::string& key) {
    try {
        ApiResponse resp = httpGet("/api/v1/license/" + urlEncode(key));
        if (resp.success) {
            return LicenseInfo::fromJson(resp.body);
        }
        throw LicenseException("Failed to get license info: HTTP " + std::to_string(resp.http_code));
    } catch (const NetworkException& e) {
        throw LicenseException(std::string("Get license info failed: ") + e.what());
    }
}

// ============================================================================
// Device Fingerprinting
// ============================================================================

std::string LicenseAPI::hashSha256(const std::string& input) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return "";
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx, input.c_str(), input.size());
    EVP_DigestFinal_ex(ctx, hash, nullptr);
    EVP_MD_CTX_free(ctx);

    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return ss.str();
}

std::string LicenseAPI::base64Encode(const unsigned char* data, size_t len) {
    BIO* bio = BIO_new(BIO_s_mem());
    BIO* b64 = BIO_new(BIO_f_base64());
    bio = BIO_push(b64, bio);
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(bio, data, static_cast<int>(len));
    BIO_flush(bio);

    BUF_MEM* bufferPtr;
    BIO_get_mem_ptr(bio, &bufferPtr);
    std::string result(bufferPtr->data, bufferPtr->length);
    BIO_free_all(bio);
    return result;
}

std::string LicenseAPI::getLocalIpAddress() {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return "127.0.0.1";

    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        WSACleanup();
        return "127.0.0.1";
    }

    struct addrinfo hints{}, *res;
    hints.ai_family = AF_INET;
    if (getaddrinfo(hostname, nullptr, &hints, &res) != 0) {
        WSACleanup();
        return "127.0.0.1";
    }

    char ipStr[INET_ADDRSTRLEN];
    struct sockaddr_in* addr = reinterpret_cast<struct sockaddr_in*>(res->ai_addr);
    inet_ntop(AF_INET, &addr->sin_addr, ipStr, sizeof(ipStr));
    std::string result(ipStr);
    freeaddrinfo(res);
    WSACleanup();
    return result;
#else
    struct ifaddrs* ifaddr;
    if (getifaddrs(&ifaddr) == -1) return "127.0.0.1";

    std::string result = "127.0.0.1";
    for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr) continue;
        if (ifa->ifa_addr->sa_family == AF_INET) {
            std::string name(ifa->ifa_name);
            if (name != "lo") {
                char addr_buf[INET_ADDRSTRLEN];
                struct sockaddr_in* sin = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr);
                inet_ntop(AF_INET, &sin->sin_addr, addr_buf, sizeof(addr_buf));
                result = addr_buf;
                break;
            }
        }
    }
    freeifaddrs(ifaddr);
    return result;
#endif
}

std::string LicenseAPI::getPrimaryMacAddress() {
#ifdef _WIN32
    ULONG bufLen = 0;
    if (GetAdaptersInfo(nullptr, &bufLen) != ERROR_BUFFER_OVERFLOW) return "00:00:00:00:00:00";

    std::vector<BYTE> buffer(bufLen);
    PIP_ADAPTER_INFO pAdapterInfo = reinterpret_cast<PIP_ADAPTER_INFO>(buffer.data());
    if (GetAdaptersInfo(pAdapterInfo, &bufLen) != NO_ERROR) return "00:00:00:00:00:00";

    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             pAdapterInfo->Address[0], pAdapterInfo->Address[1],
             pAdapterInfo->Address[2], pAdapterInfo->Address[3],
             pAdapterInfo->Address[4], pAdapterInfo->Address[5]);
    return std::string(macStr);
#else
    struct ifaddrs* ifaddr;
    if (getifaddrs(&ifaddr) == -1) return "00:00:00:00:00:00";

    std::string result = "00:00:00:00:00:00";
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        freeifaddrs(ifaddr);
        return result;
    }

    for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr) continue;
        std::string name(ifa->ifa_name ? ifa->ifa_name : "");
        if (name != "lo" && ifa->ifa_addr->sa_family == AF_INET) {
            struct ifreq ifr;
            std::memset(&ifr, 0, sizeof(ifr));
            std::strncpy(ifr.ifr_name, name.c_str(), IFNAMSIZ - 1);
            if (ioctl(fd, SIOCGIFHWADDR, &ifr) == 0) {
                unsigned char* mac = reinterpret_cast<unsigned char*>(ifr.ifr_hwaddr.sa_data);
                char macStr[18];
                snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                         mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                result = macStr;
                break;
            }
        }
    }
    close(fd);
    freeifaddrs(ifaddr);
    return result;
#endif
}

std::string LicenseAPI::getDiskSerial() {
#ifdef _WIN32
    DWORD serialNumber = 0;
    if (GetVolumeInformationA("C:\\", nullptr, 0, &serialNumber, nullptr, nullptr, nullptr, 0)) {
        return std::to_string(serialNumber);
    }
    return "UNKNOWN_DISK";
#else
    std::ifstream f("/sys/class/block/sda/serial");
    if (f.is_open()) {
        std::serial;
        std::getline(f, serial);
        if (!serial.empty()) return serial;
    }
    std::ifstream m("/etc/machine-id");
    if (m.is_open()) {
        std::string id;
        std::getline(m, id);
        return id.empty() ? "UNKNOWN_DISK" : id;
    }
    return "UNKNOWN_DISK";
#endif
}

std::string LicenseAPI::getCpuId() {
#ifdef _WIN32
    int cpuInfo[4] = { -1 };
    __cpuid(cpuInfo, 0);
    char buf[17];
    snprintf(buf, sizeof(buf), "%08X%08X%08X", cpuInfo[1], cpuInfo[3], cpuInfo[2]);
    return std::string(buf);
#else
    std::ifstream f("/proc/cpuinfo");
    if (f.is_open()) {
        std::string line;
        while (std::getline(f, line)) {
            if (line.find("serial") != std::string::npos || line.find("Serial") != std::string::npos) {
                size_t pos = line.find(':');
                if (pos != std::string::npos) {
                    std::string serial = line.substr(pos + 1);
                    // trim
                    size_t start = serial.find_first_not_of(" \t");
                    size_t end = serial.find_last_not_of(" \t");
                    if (start != std::string::npos) {
                        return serial.substr(start, end - start + 1);
                    }
                }
            }
        }
    }
    std::ifstream msr("/var/lib/dbus/machine-id");
    if (msr.is_open()) {
        std::string id;
        std::getline(msr, id);
        return id.empty() ? "UNKNOWN_CPU" : id;
    }
    return "UNKNOWN_CPU";
#endif
}

std::string LicenseAPI::getOsVersion() {
#ifdef _WIN32
    OSVERSIONINFOEXA osvi;
    ZeroMemory(&osvi, sizeof(OSVERSIONINFOEXA));
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEXA);

    typedef NTSTATUS(WINAPI* RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
    HMODULE hMod = GetModuleHandleW(L"ntdll.dll");
    if (hMod) {
        auto fxPtr = reinterpret_cast<RtlGetVersionPtr>(GetProcAddress(hMod, "RtlGetVersion"));
        if (fxPtr) {
            RTL_OSVERSIONINFOW rovi{ 0 };
            rovi.dwOSVersionInfoSize = sizeof(rovi);
            if (fxPtr(&rovi) == 0) {
                return "Windows " + std::to_string(rovi.dwMajorVersion) + "." +
                       std::to_string(rovi.dwMinorVersion) + " (Build " +
                       std::to_string(rovi.dwBuildNumber) + ")";
            }
        }
    }
    return "Windows (version unknown)";
#else
    struct utsname buf;
    if (uname(&buf) == 0) {
        return std::string(buf.sysname) + " " + buf.release + " (" + buf.machine + ")";
    }
    return "Linux (version unknown)";
#endif
}

std::string LicenseAPI::getMachineModel() {
#ifdef _WIN32
    char model[128] = { 0 };
    DWORD bufSize = sizeof(model);
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "HARDWARE\\DESCRIPTION\\System\\BIOS",
                      0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExA(hKey, "SystemProductName", nullptr, nullptr,
                         reinterpret_cast<LPBYTE>(model), &bufSize);
        RegCloseKey(hKey);
    }
    if (strlen(model) > 0) return std::string(model);
    return "Windows PC";
#else
    std::ifstream f("/sys/class/dmi/id/product_name");
    if (f.is_open()) {
        std::string model;
        std::getline(f, model);
        if (!model.empty()) return model;
    }
    std::ifstream f2("/sys/devices/virtual/dmi/id/product_name");
    if (f2.is_open()) {
        std::string model;
        std::getline(f2, model);
        if (!model.empty()) return model;
    }
    return "Linux Machine";
#endif
}

std::string LicenseAPI::getMotherboardSerial() {
#ifdef _WIN32
    char serial[128] = { 0 };
    DWORD bufSize = sizeof(serial);
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "HARDWARE\\DESCRIPTION\\System\\BIOS",
                      0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExA(hKey, "BaseBoardSerialNumber", nullptr, nullptr,
                         reinterpret_cast<LPBYTE>(serial), &bufSize);
        RegCloseKey(hKey);
    }
    if (strlen(serial) > 0) return std::string(serial);
    return "UNKNOWN_MB";
#else
    std::ifstream f("/sys/class/dmi/id/board_serial");
    if (f.is_open()) {
        std::string serial;
        std::getline(f, serial);
        if (!serial.empty()) return serial;
    }
    return "UNKNOWN_MB";
#endif
}

std::string LicenseAPI::generateHwid(const DeviceFingerprint& fp) {
    std::string combined = fp.disk_serial + "|" + fp.mac_address + "|" +
                           fp.cpu_id + "|" + fp.motherboard_serial + "|" + fp.model;
    return hashSha256(combined);
}

DeviceFingerprint LicenseAPI::getDeviceFingerprint() {
    DeviceFingerprint fp;
    fp.ip                 = getLocalIpAddress();
    fp.disk_serial        = getDiskSerial();
    fp.mac_address        = getPrimaryMacAddress();
    fp.os_version         = getOsVersion();
    fp.cpu_id             = getCpuId();
    fp.model              = getMachineModel();
    fp.motherboard_serial = getMotherboardSerial();
    fp.hwid               = generateHwid(fp);
    return fp;
}

DeviceData LicenseAPI::collectDeviceData() {
    DeviceData dd;
    dd.fingerprint        = getDeviceFingerprint();
    dd.timestamp          = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count();
    dd.user_agent         = "POWSYS365-Client/1.0";
    dd.screen_resolution  = "1920x1080";
    return dd;
}

std::string LicenseAPI::urlEncode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    for (char c : value) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << std::uppercase;
            escaped << '%' << std::setw(2) << static_cast<int>(static_cast<unsigned char>(c));
            escaped << std::nouppercase;
        }
    }
    return escaped.str();
}

bool LicenseAPI::checkFeatureAccess(const std::string& feature, LicenseTier tier) {
    std::string tier_str = licenseTierToString(tier);
    std::string json_body = "{" +
        jsonStringField("feature", feature) +
        jsonStringField("tier", tier_str, true) +
        "}";

    try {
        ApiResponse resp = httpPost("/api/v1/feature/check", json_body);
        if (resp.success) {
            return extractJsonBool(resp.body, "has_access");
        }
        // Fallback: local feature map
        std::vector<std::string> life_time_features = {
            "all_modules", "power_flow", "short_circuit", "arc_flash", "transient",
            "motor_starting", "harmonics", "relay_coordination", "cable_sizing",
            "generator_sizing", "transformer_sizing", "grounding", "lightning",
            "reporting", "api_access", "cloud_sync", "multi_user", "priority_support"
        };
        std::vector<std::string> enterprise_features = {
            "power_flow", "short_circuit", "arc_flash", "transient",
            "motor_starting", "harmonics", "relay_coordination", "cable_sizing",
            "generator_sizing", "transformer_sizing", "grounding", "reporting",
            "api_access", "cloud_sync", "multi_user", "priority_support"
        };
        std::vector<std::string> pro_features = {
            "power_flow", "short_circuit", "arc_flash",
            "harmonics", "cable_sizing", "reporting", "api_access"
        };
        std::vector<std::string> basic_features = {
            "power_flow", "short_circuit", "reporting"
        };
        std::vector<std::string> trial_features = {
            "power_flow", "short_circuit"
        };
        std::vector<std::string> student_features = {
            "power_flow", "short_circuit", "reporting"
        };

        const std::vector<std::string>* features = nullptr;
        switch (tier) {
            case LicenseTier::LIFE_TIME:  features = &life_time_features; break;
            case LicenseTier::ENTERPRISE: features = &enterprise_features; break;
            case LicenseTier::PRO:        features = &pro_features; break;
            case LicenseTier::BASIC:      features = &basic_features; break;
            case LicenseTier::TRIAL:      features = &trial_features; break;
            case LicenseTier::STUDENT:    features = &student_features; break;
            default:                      return false;
        }
        for (const auto& f : *features) {
            if (f == feature) return true;
        }
        return false;
    } catch (const NetworkException&) {
        // Offline: use local map
        return checkFeatureAccess(feature, tier);
    }
}

UsageStats LicenseAPI::monitorLicenseUsage(const std::string& key) {
    try {
        ApiResponse resp = httpGet("/api/v1/usage/" + urlEncode(key));
        if (resp.success) {
            return UsageStats::fromJson(resp.body);
        }
        UsageStats empty{};
        empty.license_key = key;
        empty.calls_per_hour = 0;
        empty.devices_active = 0;
        empty.total_requests_24h = 0;
        return empty;
    } catch (const NetworkException& e) {
        throw LicenseException(std::string("Usage monitoring failed: ") + e.what());
    }
}

bool LicenseAPI::healthCheck() {
    try {
        ApiResponse resp = httpGet("/api/v1/health");
        return resp.success && resp.http_code == 200;
    } catch (const NetworkException&) {
        return false;
    }
}

std::string LicenseAPI::getApiVersion() {
    try {
        ApiResponse resp = httpGet("/api/v1/version");
        if (resp.success) {
            return extractJsonString(resp.body, "version");
        }
        return "unknown";
    } catch (const NetworkException&) {
        return "unknown";
    }
}

} // namespace powsys365
