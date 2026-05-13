#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <chrono>
#include <functional>
#include <optional>
#include <variant>
#include <atomic>
#include <mutex>
#include <sstream>
#include <iostream>

namespace powsys365::io {

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
struct FileInfo;
struct ImportError;
struct ImportResult;
struct ExportResult;
class  BaseFileImporter;
class  BaseFileExporter;
class  FormatRegistry;
class  ImportExportEngine;

// ---------------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------------

enum class ImportStatus {
    Success   = 0,
    Warning   = 1,
    Error     = 2,
    Cancelled = 3
};

enum class Severity {
    Info    = 0,
    Warning = 1,
    Error   = 2,
    Fatal   = 3
};

// ---------------------------------------------------------------------------
// Power system data model (lightweight in-memory representation)
// ---------------------------------------------------------------------------

struct GeoPoint {
    double latitude  = 0.0;
    double longitude = 0.0;
    double altitude  = 0.0;
    bool operator==(const GeoPoint& o) const noexcept {
        return latitude == o.latitude && longitude == o.longitude && altitude == o.altitude;
    }
};

struct Bus {
    int64_t              id = 0;
    std::string          name;
    double               baseVoltage_kV = 0.0;
    int                  area = 0;
    int                  zone = 0;
    int                  owner = 0;
    std::optional<GeoPoint> location;
    std::map<std::string, std::string> attributes;
};

struct Branch {
    int64_t              fromBus = 0;
    int64_t              toBus   = 0;
    std::string          circuitId = "1";
    double               r_pu   = 0.0;
    double               x_pu   = 0.0;
    double               b_pu   = 0.0;
    double               rateA_MVA = 0.0;
    double               rateB_MVA = 0.0;
    double               rateC_MVA = 0.0;
    int                  status   = 1;          // 1 = in-service, 0 = out
    double               length_km = 0.0;
    std::optional<std::string> lineType;
    std::map<std::string, std::string> attributes;
};

struct Transformer {
    int64_t              fromBus = 0;
    int64_t              toBus   = 0;
    int64_t              tertBus = 0;           // 0 = two-winding
    std::string          circuitId = "1";
    double               rateA_MVA = 0.0;
    int                  status   = 1;
    double               r12_pu   = 0.0;
    double               x12_pu   = 0.0;
    double               r23_pu   = 0.0;
    double               x23_pu   = 0.0;
    double               r31_pu   = 0.0;
    double               x31_pu   = 0.0;
    double               windV1_kV = 0.0;
    double               windV2_kV = 0.0;
    double               windV3_kV = 0.0;
    int                  controlMode = 0;
    std::map<std::string, std::string> attributes;
};

struct Generator {
    int64_t              busId = 0;
    std::string          id = "1";
    double               pGen_MW  = 0.0;
    double               qGen_Mvar = 0.0;
    double               qMax_Mvar = 9999.0;
    double               qMin_Mvar = -9999.0;
    double               vSet_pu  = 1.0;
    double               pMax_MW  = 0.0;
    double               pMin_MW  = 0.0;
    int                  status   = 1;
    double               mBase_MVA = 0.0;
    std::map<std::string, std::string> attributes;
};

struct Load {
    int64_t              busId = 0;
    std::string          id = "1";
    double               pLoad_MW  = 0.0;
    double               qLoad_Mvar = 0.0;
    int                  status    = 1;
    std::map<std::string, std::string> attributes;
};

struct Shunt {
    int64_t              busId = 0;
    std::string          id = "1";
    double               b_Mvar = 0.0;
    double               g_MW   = 0.0;
    int                  status = 1;
    std::map<std::string, std::string> attributes;
};

// Unified power-system dataset used by all importers / exporters
struct PowerSystemData {
    std::vector<Bus>           buses;
    std::vector<Branch>        branches;
    std::vector<Transformer>   transformers;
    std::vector<Generator>     generators;
    std::vector<Load>          loads;
    std::vector<Shunt>         shunts;
    std::map<std::string, std::string> metadata;
    std::map<std::string, std::vector<std::map<std::string, std::string>>> rawSections;

    void clear() noexcept {
        buses.clear(); branches.clear(); transformers.clear();
        generators.clear(); loads.clear(); shunts.clear();
        metadata.clear(); rawSections.clear();
    }
    bool empty() const noexcept {
        return buses.empty() && branches.empty() && transformers.empty()
            && generators.empty() && loads.empty() && shunts.empty();
    }
    size_t totalElements() const noexcept {
        return buses.size() + branches.size() + transformers.size()
             + generators.size() + loads.size() + shunts.size();
    }
};

// ---------------------------------------------------------------------------
// Progress callback
// ---------------------------------------------------------------------------

struct ProgressInfo {
    std::string stage;          // e.g. "parsing", "validating", "exporting"
    std::size_t currentStep = 0;
    std::size_t totalSteps  = 1;
    std::string currentFile;
};

using ProgressCallback = std::function<void(const ProgressInfo&)>;

// ---------------------------------------------------------------------------
// File information
// ---------------------------------------------------------------------------

struct FileInfo {
    std::string                     path;
    std::string                     formatName;
    std::vector<std::string>        extensions;
    std::size_t                     fileSizeBytes = 0;
    std::string                     encoding = "UTF-8";
    std::chrono::system_clock::time_point detectedAt;
    std::map<std::string, std::string> properties;

    static std::string extensionFromPath(const std::string& p) {
        auto pos = p.find_last_of('.');
        if (pos == std::string::npos) return {};
        return p.substr(pos);
    }
    static std::string lowerExtension(const std::string& p) {
        auto ext = extensionFromPath(p);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return ext;
    }
};

// ---------------------------------------------------------------------------
// Error / warning descriptor
// ---------------------------------------------------------------------------

struct ImportError {
    Severity    severity = Severity::Error;
    std::string code;           // e.g. "CONN_ISLAND", "DUP_BUS_ID"
    std::string message;
    std::string source;         // file / line / column
    std::size_t line   = 0;
    std::size_t column = 0;

    std::string toString() const {
        std::ostringstream oss;
        switch (severity) {
            case Severity::Info:    oss << "[INFO] ";    break;
            case Severity::Warning: oss << "[WARN] ";    break;
            case Severity::Error:   oss << "[ERROR] ";   break;
            case Severity::Fatal:   oss << "[FATAL] ";   break;
        }
        oss << code << ": " << message;
        if (line > 0) oss << " (at line " << line;
        if (column > 0) oss << ", col " << column;
        if (line > 0) oss << ")";
        return oss.str();
    }
};

// ---------------------------------------------------------------------------
// Import result
// ---------------------------------------------------------------------------

struct ImportResult {
    ImportStatus                       status = ImportStatus::Success;
    std::shared_ptr<PowerSystemData>   data;
    FileInfo                           fileInfo;
    std::vector<ImportError>           errors;
    std::chrono::milliseconds          duration{0};

    bool ok() const noexcept {
        return status == ImportStatus::Success || status == ImportStatus::Warning;
    }
    std::vector<ImportError> errorsBySeverity(Severity s) const {
        std::vector<ImportError> out;
        for (const auto& e : errors) if (e.severity == s) out.push_back(e);
        return out;
    }
    std::string summary() const {
        std::ostringstream oss;
        oss << "Import ";
        switch (status) {
            case ImportStatus::Success:   oss << "SUCCESS";   break;
            case ImportStatus::Warning:   oss << "WARNING";   break;
            case ImportStatus::Error:     oss << "ERROR";     break;
            case ImportStatus::Cancelled: oss << "CANCELLED"; break;
        }
        if (data) {
            oss << " | Buses=" << data->buses.size()
                << " Branches=" << data->branches.size()
                << " XFMRs=" << data->transformers.size()
                << " Gens=" << data->generators.size()
                << " Loads=" << data->loads.size()
                << " Shunts=" << data->shunts.size();
        }
        oss << " | Errors=" << errors.size()
            << " | " << duration.count() << "ms";
        return oss.str();
    }
};

// ---------------------------------------------------------------------------
// Export result
// ---------------------------------------------------------------------------

struct ExportResult {
    ImportStatus                       status = ImportStatus::Success;
    std::string                        outputPath;
    std::size_t                        bytesWritten = 0;
    std::vector<ImportError>           errors;
    std::chrono::milliseconds          duration{0};

    bool ok() const noexcept {
        return status == ImportStatus::Success || status == ImportStatus::Warning;
    }
    std::string summary() const {
        std::ostringstream oss;
        oss << "Export ";
        switch (status) {
            case ImportStatus::Success:   oss << "SUCCESS";   break;
            case ImportStatus::Warning:   oss << "WARNING";   break;
            case ImportStatus::Error:     oss << "ERROR";     break;
            case ImportStatus::Cancelled: oss << "CANCELLED"; break;
        }
        oss << " | " << outputPath << " | " << bytesWritten << " bytes | "
            << duration.count() << "ms";
        return oss.str();
    }
};

// ---------------------------------------------------------------------------
// Cancellation token (thread-safe)
// ---------------------------------------------------------------------------

class CancellationToken {
public:
    void cancel() noexcept { flag_.store(true, std::memory_order_relaxed); }
    bool isCancelled() const noexcept { return flag_.load(std::memory_order_relaxed); }
    void reset() noexcept { flag_.store(false, std::memory_order_relaxed); }
private:
    std::atomic<bool> flag_{false};
};

} // namespace powsys365::io
