#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace powsys365 {
namespace licensing {

// ============================================================
// DeviceFingerprintData - Raw collected data
// ============================================================
struct DeviceFingerprintData {
    std::string hwid;           // Hardware UUID / serial
    std::string ipAddress;      // Primary IP address
    std::string geoLocation;    // Geo coordinates (if available)
    std::string diskSerial;     // Boot disk serial number
    std::string macAddress;     // Primary MAC address
    std::string osVersion;      // OS name and version
    std::string cpuId;          // CPU identifier
    std::string motherboardSerial; // Motherboard serial
    std::string hostname;       // Machine hostname
    std::string biosVersion;    // BIOS/UEFI version
    uint64_t totalRam = 0;      // Total RAM in bytes
    uint64_t totalDisk = 0;     // Total disk in bytes
    int numCores = 0;           // CPU cores

    bool operator==(const DeviceFingerprintData& other) const;
    bool operator!=(const DeviceFingerprintData& other) const;
};

// ============================================================
// DeviceFingerprint - Collection and serialization
// ============================================================
class DeviceFingerprint {
public:
    DeviceFingerprint() = default;

    // --- Collection ---
    DeviceFingerprintData collect();

    // --- Serialization ---
    std::string toString() const;
    bool fromString(const std::string& serialized);

    // --- Access ---
    const DeviceFingerprintData& data() const { return m_data; }

    // --- Comparison ---
    bool operator==(const DeviceFingerprint& other) const;
    bool operator!=(const DeviceFingerprint& other) const;

    // --- Platform Detection ---
    static std::string getPlatformName();

private:
    DeviceFingerprintData m_data;

    // Platform-specific collectors
    DeviceFingerprintData collectAll();
    std::string collectHwid();
    std::string collectIpAddress();
    std::string collectDiskSerial();
    std::string collectMacAddress();
    std::string collectOsVersion();
    std::string collectCpuId();
    std::string collectMotherboardSerial();
    std::string collectHostname();
    std::string collectBiosVersion();
    uint64_t collectTotalRam();
    uint64_t collectTotalDisk();
    int collectNumCores();

    // Helpers
    static std::string execCommand(const char* cmd);
    static std::string normalizeMac(const std::string& mac);
    static std::string hashComponents(const DeviceFingerprintData& d);
};

} // namespace licensing
} // namespace powsys365
