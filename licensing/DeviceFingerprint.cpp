#include "DeviceFingerprint.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>

#include <openssl/sha.h>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <iphlpapi.h>
    #include <intrin.h>
    #pragma comment(lib, "iphlpapi.lib")
    #pragma comment(lib, "ws2_32.lib")
    #include <wbemidl.h>
    #pragma comment(lib, "wbemuuid.lib")
#else
    #include <sys/ioctl.h>
    #include <sys/socket.h>
    #include <sys/statvfs.h>
    #include <sys/sysinfo.h>
    #include <sys/utsname.h>
    #include <unistd.h>
    #include <net/if.h>
    #include <ifaddrs.h>
    #include <arpa/inet.h>
    #include <fstream>
#endif

#ifdef __APPLE__
    #include <sys/sysctl.h>
    #include <net/if_dl.h>
    #include <IOKit/IOKitLib.h>
    #include <CoreFoundation/CoreFoundation.h>
#endif

namespace powsys365 {
namespace licensing {

// ============================================================
// DeviceFingerprintData Comparison
// ============================================================
bool DeviceFingerprintData::operator==(const DeviceFingerprintData& other) const {
    return hwid == other.hwid &&
           ipAddress == other.ipAddress &&
           diskSerial == other.diskSerial &&
           macAddress == other.macAddress &&
           osVersion == other.osVersion &&
           cpuId == other.cpuId &&
           motherboardSerial == other.motherboardSerial &&
           hostname == other.hostname &&
           biosVersion == other.biosVersion &&
           totalRam == other.totalRam &&
           totalDisk == other.totalDisk &&
           numCores == other.numCores;
}

bool DeviceFingerprintData::operator!=(const DeviceFingerprintData& other) const {
    return !(*this == other);
}

// ============================================================
// DeviceFingerprint Implementation
// ============================================================

DeviceFingerprintData DeviceFingerprint::collect() {
    m_data = collectAll();
    return m_data;
}

DeviceFingerprintData DeviceFingerprint::collectAll() {
    DeviceFingerprintData d;
    d.hwid = collectHwid();
    d.ipAddress = collectIpAddress();
    d.diskSerial = collectDiskSerial();
    d.macAddress = collectMacAddress();
    d.osVersion = collectOsVersion();
    d.cpuId = collectCpuId();
    d.motherboardSerial = collectMotherboardSerial();
    d.hostname = collectHostname();
    d.biosVersion = collectBiosVersion();
    d.totalRam = collectTotalRam();
    d.totalDisk = collectTotalDisk();
    d.numCores = collectNumCores();
    return d;
}

// ============================================================
// Serialization: pipe-delimited with hex-escaped values
// ============================================================
std::string DeviceFingerprint::toString() const {
    std::ostringstream oss;
    oss << "HWID=" << m_data.hwid << "|"
        << "IP=" << m_data.ipAddress << "|"
        << "DISK=" << m_data.diskSerial << "|"
        << "MAC=" << m_data.macAddress << "|"
        << "OS=" << m_data.osVersion << "|"
        << "CPU=" << m_data.cpuId << "|"
        << "MB=" << m_data.motherboardSerial << "|"
        << "HOST=" << m_data.hostname << "|"
        << "BIOS=" << m_data.biosVersion << "|"
        << "RAM=" << m_data.totalRam << "|"
        << "DISKSIZE=" << m_data.totalDisk << "|"
        << "CORES=" << m_data.numCores;
    return oss.str();
}

bool DeviceFingerprint::fromString(const std::string& serialized) {
    DeviceFingerprintData d;
    size_t pos = 0;
    while (pos < serialized.length()) {
        size_t eq = serialized.find('=', pos);
        if (eq == std::string::npos) break;
        std::string key = serialized.substr(pos, eq - pos);
        size_t pipe = serialized.find('|', eq);
        if (pipe == std::string::npos) pipe = serialized.length();
        std::string value = serialized.substr(eq + 1, pipe - eq - 1);

        if (key == "HWID") d.hwid = value;
        else if (key == "IP") d.ipAddress = value;
        else if (key == "DISK") d.diskSerial = value;
        else if (key == "MAC") d.macAddress = value;
        else if (key == "OS") d.osVersion = value;
        else if (key == "CPU") d.cpuId = value;
        else if (key == "MB") d.motherboardSerial = value;
        else if (key == "HOST") d.hostname = value;
        else if (key == "BIOS") d.biosVersion = value;
        else if (key == "RAM") d.totalRam = std::stoull(value);
        else if (key == "DISKSIZE") d.totalDisk = std::stoull(value);
        else if (key == "CORES") d.numCores = std::stoi(value);

        pos = pipe + 1;
    }
    m_data = d;
    return true;
}

bool DeviceFingerprint::operator==(const DeviceFingerprint& other) const {
    return m_data == other.m_data;
}

bool DeviceFingerprint::operator!=(const DeviceFingerprint& other) const {
    return !(*this == other);
}

std::string DeviceFingerprint::getPlatformName() {
#ifdef _WIN32
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#elif defined(__linux__)
    return "Linux";
#else
    return "Unknown";
#endif
}

// ============================================================
// Platform-Specific Collectors
// ============================================================

// --- HWID ---
std::string DeviceFingerprint::collectHwid() {
#if defined(__APPLE__)
    // Get IOPlatformUUID from IOKit
    io_service_t platformExpert = IOServiceGetMatchingService(kIOMasterPortDefault,
        IOServiceMatching("IOPlatformExpertDevice"));
    if (platformExpert) {
        CFStringRef uuidRef = (CFStringRef)IORegistryEntryCreateCFProperty(platformExpert,
            CFSTR(kIOPlatformUUIDKey), kCFAllocatorDefault, 0);
        if (uuidRef) {
            char buffer[256];
            if (CFStringGetCString(uuidRef, buffer, sizeof(buffer), kCFStringEncodingUTF8)) {
                CFRelease(uuidRef);
                IOObjectRelease(platformExpert);
                return std::string(buffer);
            }
            CFRelease(uuidRef);
        }
        IOObjectRelease(platformExpert);
    }
    // Fallback to system profiler
    std::string result = execCommand("system_profiler SPHardwareDataType | awk '/Hardware UUID/ {print $3}'");
    if (!result.empty()) return result;
#elif defined(__linux__)
    // Try /etc/machine-id first
    std::ifstream fsMachineId("/etc/machine-id");
    if (fsMachineId) {
        std::string id;
        std::getline(fsMachineId, id);
        if (!id.empty()) return id;
    }
    // Fallback to dmidecode
    std::string result = execCommand("cat /sys/class/dmi/id/product_uuid 2>/dev/null || dmidecode -s system-uuid 2>/dev/null");
    if (!result.empty()) return result;
    // Final fallback: hostname + MAC hash
    std::string hostname = execCommand("hostname");
    std::string mac = collectMacAddress();
    return hashComponents({hostname + mac, "", "", mac, "", "", "", hostname, "", 0, 0, 0});
#elif defined(_WIN32)
    // Windows: use WMI to get UUID
    std::string result = execCommand("wmic csproduct get UUID /value 2>nul");
    if (!result.empty() && result.find('=') != std::string::npos) {
        size_t eq = result.find('=');
        if (eq != std::string::npos) {
            std::string uuid = result.substr(eq + 1);
            // Trim whitespace
            uuid.erase(0, uuid.find_first_not_of(" \t\r\n"));
            uuid.erase(uuid.find_last_not_of(" \t\r\n") + 1);
            if (uuid != "FFFFFFFF-FFFF-FFFF-FFFF-FFFFFFFFFFFF" && !uuid.empty()) {
                return uuid;
            }
        }
    }
    // Fallback: CPUID + disk serial hash
#endif
    // Ultimate fallback
    std::string fallback = collectHostname() + "-" + collectMacAddress() + "-" + collectCpuId();
    return hashComponents({fallback, "", "", collectMacAddress(), "", collectCpuId(), "", collectHostname(), "", 0, 0, 0});
}

// --- IP Address ---
std::string DeviceFingerprint::collectIpAddress() {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return "127.0.0.1";

    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        WSACleanup();
        return "127.0.0.1";
    }

    struct hostent* host = gethostbyname(hostname);
    if (!host || !host->h_addr_list[0]) {
        WSACleanup();
        return "127.0.0.1";
    }

    struct in_addr addr;
    memcpy(&addr, host->h_addr_list[0], sizeof(struct in_addr));
    std::string ip = inet_ntoa(addr);
    WSACleanup();
    return ip;
#else
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) != 0) return "127.0.0.1";

    std::string bestIp = "127.0.0.1";
    for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;
        if (ifa->ifa_addr->sa_family == AF_INET) {
            struct sockaddr_in* sin = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr);
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));
            std::string ipStr(ip);
            // Prefer non-loopback addresses
            if (ipStr != "127.0.0.1" && !ipStr.empty()) {
                bestIp = ipStr;
                break;
            }
        }
    }
    freeifaddrs(ifaddr);
    return bestIp;
#endif
}

// --- Disk Serial ---
std::string DeviceFingerprint::collectDiskSerial() {
#if defined(__APPLE__)
    std::string result = execCommand("diskutil info disk0 | awk '/Device Identifier/ {print $3}'");
    if (result.empty()) {
        result = execCommand("system_profiler SPStorageDataType | awk '/Device Serial/ {print $3}'");
    }
    return result.empty() ? "UNKNOWN" : result;
#elif defined(__linux__)
    std::string result = execCommand("udevadm info --query=all --name=/dev/sda 2>/dev/null | grep ID_SERIAL_SHORT");
    if (result.empty()) {
        result = execCommand("lsblk -o SERIAL -n -d /dev/sda 2>/dev/null | head -1");
    }
    if (result.empty()) {
        result = execCommand("cat /sys/class/block/sda/device/serial 2>/dev/null");
    }
    if (result.find('=') != std::string::npos) {
        size_t eq = result.find('=');
        result = result.substr(eq + 1);
    }
    return result.empty() ? "UNKNOWN" : result;
#elif defined(_WIN32)
    std::string result = execCommand("wmic diskdrive get SerialNumber /value 2>nul");
    if (!result.empty() && result.find('=') != std::string::npos) {
        size_t eq = result.find('=');
        result = result.substr(eq + 1);
        result.erase(0, result.find_first_not_of(" \t\r\n"));
        result.erase(result.find_last_not_of(" \t\r\n") + 1);
    }
    return result.empty() ? "UNKNOWN" : result;
#else
    return "UNKNOWN";
#endif
}

// --- MAC Address ---
std::string DeviceFingerprint::collectMacAddress() {
#if defined(_WIN32)
    ULONG outBufLen = 0;
    if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr, nullptr, &outBufLen)
        != ERROR_BUFFER_OVERFLOW) {
        return "00:00:00:00:00:00";
    }

    std::vector<uint8_t> buffer(outBufLen);
    PIP_ADAPTER_ADDRESSES pAddresses = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());

    if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr, pAddresses, &outBufLen)
        != ERROR_SUCCESS) {
        return "00:00:00:00:00:00";
    }

    for (PIP_ADAPTER_ADDRESSES pCurr = pAddresses; pCurr; pCurr = pCurr->Next) {
        if (pCurr->PhysicalAddressLength == 0) continue;
        if (pCurr->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;

        std::ostringstream oss;
        for (DWORD i = 0; i < pCurr->PhysicalAddressLength; ++i) {
            if (i > 0) oss << ":";
            oss << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<int>(pCurr->PhysicalAddress[i]);
        }
        return normalizeMac(oss.str());
    }
    return "00:00:00:00:00:00";
#elif defined(__APPLE__) || defined(__linux__)
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) != 0) return "00:00:00:00:00:00";

    std::string bestMac = "00:00:00:00:00:00";
    for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;
        if (ifa->ifa_addr->sa_family != AF_LINK) continue;

        std::string ifname(ifa->ifa_name);
        // Skip loopback
        if (ifname.find("lo") == 0) continue;

#ifdef __APPLE__
        struct sockaddr_dl* sdl = reinterpret_cast<struct sockaddr_dl*>(ifa->ifa_addr);
        if (sdl->sdl_alen == 6) {
            unsigned char* mac = (unsigned char*)LLADDR(sdl);
            std::ostringstream oss;
            for (int i = 0; i < 6; ++i) {
                if (i > 0) oss << ":";
                oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(mac[i]);
            }
            bestMac = normalizeMac(oss.str());
            if (ifname.find("en") == 0) break; // Prefer Ethernet interfaces
        }
#else // Linux
        struct ifreq ifr;
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) continue;
        strncpy(ifr.ifr_name, ifa->ifa_name, IFNAMSIZ - 1);
        if (ioctl(fd, SIOCGIFHWADDR, &ifr) == 0) {
            unsigned char* mac = reinterpret_cast<unsigned char*>(ifr.ifr_hwaddr.sa_data);
            std::ostringstream oss;
            for (int i = 0; i < 6; ++i) {
                if (i > 0) oss << ":";
                oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(mac[i]);
            }
            bestMac = normalizeMac(oss.str());
        }
        close(fd);
        if (ifname.find("eth") == 0 || ifname.find("en") == 0) break;
#endif
    }
    freeifaddrs(ifaddr);
    return bestMac;
#else
    return "00:00:00:00:00:00";
#endif
}

// --- OS Version ---
std::string DeviceFingerprint::collectOsVersion() {
#if defined(__APPLE__)
    std::string result = execCommand("sw_vers -productName && sw_vers -productVersion && sw_vers -buildVersion");
    if (!result.empty()) return result;
    return "macOS Unknown";
#elif defined(__linux__)
    std::ifstream osRelease("/etc/os-release");
    if (osRelease) {
        std::string line;
        std::string name, version;
        while (std::getline(osRelease, line)) {
            if (line.find("PRETTY_NAME=") == 0) {
                size_t start = line.find('"');
                size_t end = line.rfind('"');
                if (start != std::string::npos && end != std::string::npos && end > start) {
                    return line.substr(start + 1, end - start - 1);
                }
            }
        }
    }
    struct utsname buf;
    if (uname(&buf) == 0) {
        return std::string(buf.sysname) + " " + buf.release;
    }
    return "Linux Unknown";
#elif defined(_WIN32)
    OSVERSIONINFOEXW osvi;
    ZeroMemory(&osvi, sizeof(OSVERSIONINFOEXW));
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEXW);
    std::ostringstream oss;
    oss << "Windows " << osvi.dwMajorVersion << "." << osvi.dwMinorVersion
        << " Build " << osvi.dwBuildNumber;
    return oss.str();
#else
    return "Unknown OS";
#endif
}

// --- CPU ID ---
std::string DeviceFingerprint::collectCpuId() {
#if defined(_WIN32)
    int cpuInfo[4] = {0};
    __cpuid(cpuInfo, 0);
    std::ostringstream oss;
    oss << std::hex << std::uppercase;
    for (int i = 0; i < 4; ++i) {
        oss << std::setw(8) << std::setfill('0') << cpuInfo[i];
    }
    // Get processor brand string
    char brand[49] = {0};
    __cpuid(cpuInfo, 0x80000000);
    if (static_cast<unsigned>(cpuInfo[0]) >= 0x80000004) {
        __cpuid(reinterpret_cast<int*>(brand), 0x80000002);
        __cpuid(reinterpret_cast<int*>(brand + 16), 0x80000003);
        __cpuid(reinterpret_cast<int*>(brand + 32), 0x80000004);
    }
    oss << "-" << brand;
    return oss.str();
#elif defined(__APPLE__)
    std::string result = execCommand("sysctl -n machdep.cpu.brand_string 2>/dev/null");
    if (result.empty()) {
        result = execCommand("sysctl -n hw.machine 2>/dev/null");
    }
    std::string family = execCommand("sysctl -n machdep.cpu.family 2>/dev/null");
    if (!family.empty()) result += "-" + family;
    return result.empty() ? "UNKNOWN" : result;
#elif defined(__linux__)
    std::ifstream cpuinfo("/proc/cpuinfo");
    if (cpuinfo) {
        std::string line;
        std::string model, vendor, serial;
        while (std::getline(cpuinfo, line)) {
            if (line.find("model name") == 0) {
                size_t col = line.find(':');
                if (col != std::string::npos) model = line.substr(col + 2);
            } else if (line.find("vendor_id") == 0) {
                size_t col = line.find(':');
                if (col != std::string::npos) vendor = line.substr(col + 2);
            } else if (line.find("serial number") == 0 || line.find("Processor Serial Number") == 0) {
                size_t col = line.find(':');
                if (col != std::string::npos) serial = line.substr(col + 2);
            }
        }
        if (!model.empty()) return vendor + "-" + model + (serial.empty() ? "" : "-" + serial);
    }
    return "UNKNOWN";
#else
    return "UNKNOWN";
#endif
}

// --- Motherboard Serial ---
std::string DeviceFingerprint::collectMotherboardSerial() {
#if defined(__APPLE__)
    std::string result = execCommand("system_profiler SPMemoryDataType | awk '/Serial Number/ {print $3}' | head -1");
    return result.empty() ? "UNKNOWN" : result;
#elif defined(__linux__)
    std::string result = execCommand("cat /sys/class/dmi/id/board_serial 2>/dev/null");
    if (result.empty()) {
        result = execCommand("dmidecode -s baseboard-serial-number 2>/dev/null");
    }
    return result.empty() ? "UNKNOWN" : result;
#elif defined(_WIN32)
    std::string result = execCommand("wmic baseboard get SerialNumber /value 2>nul");
    if (!result.empty() && result.find('=') != std::string::npos) {
        size_t eq = result.find('=');
        result = result.substr(eq + 1);
        result.erase(0, result.find_first_not_of(" \t\r\n"));
        result.erase(result.find_last_not_of(" \t\r\n") + 1);
    }
    return result.empty() ? "UNKNOWN" : result;
#else
    return "UNKNOWN";
#endif
}

// --- Hostname ---
std::string DeviceFingerprint::collectHostname() {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        return std::string(hostname);
    }
    return "UNKNOWN";
}

// --- BIOS Version ---
std::string DeviceFingerprint::collectBiosVersion() {
#if defined(__APPLE__)
    std::string result = execCommand("system_profiler SPHardwareDataType | awk '/Boot ROM Version/ {print $4,$5}'");
    return result.empty() ? "UNKNOWN" : result;
#elif defined(__linux__)
    std::string result = execCommand("cat /sys/class/dmi/id/bios_version 2>/dev/null");
    if (result.empty()) {
        result = execCommand("dmidecode -s bios-version 2>/dev/null");
    }
    return result.empty() ? "UNKNOWN" : result;
#elif defined(_WIN32)
    std::string result = execCommand("wmic bios get SMBIOSBIOSVersion /value 2>nul");
    if (!result.empty() && result.find('=') != std::string::npos) {
        size_t eq = result.find('=');
        result = result.substr(eq + 1);
        result.erase(0, result.find_first_not_of(" \t\r\n"));
        result.erase(result.find_last_not_of(" \t\r\n") + 1);
    }
    return result.empty() ? "UNKNOWN" : result;
#else
    return "UNKNOWN";
#endif
}

// --- Total RAM ---
uint64_t DeviceFingerprint::collectTotalRam() {
#if defined(__APPLE__)
    std::string result = execCommand("sysctl -n hw.memsize 2>/dev/null");
    if (!result.empty()) {
        try {
            return std::stoull(result);
        } catch (...) {}
    }
    return 0;
#elif defined(__linux__)
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        return si.totalram;
    }
    return 0;
#elif defined(_WIN32)
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        return ms.ullTotalPhys;
    }
    return 0;
#else
    return 0;
#endif
}

// --- Total Disk ---
uint64_t DeviceFingerprint::collectTotalDisk() {
#if defined(__APPLE__) || defined(__linux__)
    struct statvfs buf;
    if (statvfs("/", &buf) == 0) {
        return buf.f_frsize * buf.f_blocks;
    }
    return 0;
#elif defined(_WIN32)
    ULARGE_INTEGER freeBytes, totalBytes, totalFreeBytes;
    if (GetDiskFreeSpaceExW(L"C:\\", &freeBytes, &totalBytes, &totalFreeBytes)) {
        return totalBytes.QuadPart;
    }
    return 0;
#else
    return 0;
#endif
}

// --- Number of Cores ---
int DeviceFingerprint::collectNumCores() {
#if defined(__APPLE__)
    std::string result = execCommand("sysctl -n hw.ncpu 2>/dev/null");
    if (!result.empty()) {
        try {
            return std::stoi(result);
        } catch (...) {}
    }
    return 1;
#elif defined(__linux__)
    std::ifstream cpuinfo("/proc/cpuinfo");
    if (cpuinfo) {
        std::string line;
        int cores = 0;
        while (std::getline(cpuinfo, line)) {
            if (line.find("processor") == 0) cores++;
        }
        if (cores > 0) return cores;
    }
    return 1;
#elif defined(_WIN32)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return static_cast<int>(si.dwNumberOfProcessors);
#else
    return 1;
#endif
}

// ============================================================
// Helpers
// ============================================================
std::string DeviceFingerprint::execCommand(const char* cmd) {
    std::array<char, 256> buffer;
    std::string result;

#ifdef _WIN32
    FILE* pipe = _popen(cmd, "r");
#else
    FILE* pipe = popen(cmd, "r");
#endif
    if (!pipe) return "";

    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }

#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif

    // Trim trailing whitespace/newlines
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' ' || result.back() == '\t')) {
        result.pop_back();
    }
    return result;
}

std::string DeviceFingerprint::normalizeMac(const std::string& mac) {
    std::string normalized;
    for (char c : mac) {
        if (c == '-' || c == '.' || c == ' ') continue;
        normalized += static_cast<char>(std::tolower(c));
    }
    // Format as XX:XX:XX:XX:XX:XX
    std::string formatted;
    for (size_t i = 0; i < normalized.length() && i < 12; ++i) {
        if (i > 0 && i % 2 == 0) formatted += ':';
        formatted += normalized[i];
    }
    return formatted;
}

std::string DeviceFingerprint::hashComponents(const DeviceFingerprintData& d) {
    std::string input = d.hwid + "|" + d.macAddress + "|" + d.cpuId + "|" + d.diskSerial;
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(input.data()), input.size(), hash);

    std::ostringstream oss;
    for (int i = 0; i < 16; ++i) { // First 128 bits (32 hex chars)
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return oss.str();
}

} // namespace licensing
} // namespace powsys365
