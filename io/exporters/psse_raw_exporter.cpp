#include "psse_raw_exporter.h"
#include <chrono>
#include <cmath>
#include <set>

namespace powsys365::io {

// ============================================================================
// Info & validation
// ============================================================================

FileInfo PsseRawExporter::getInfo() const {
    FileInfo info;
    info.formatName = "PSS/E RAW";
    info.extensions = supportedExtensions();
    info.properties = {
        {"revision",     std::to_string(revision_)},
        {"csv_mode",     csvMode_ ? "true" : "false"},
        {"description",  "PSS/E power flow raw data export (v29-v33)"}
    };
    return info;
}

std::vector<ImportError> PsseRawExporter::validate(const PowerSystemData& data) {
    std::vector<ImportError> errs;

    if (data.buses.empty()) {
        errs.push_back({Severity::Fatal, "NO_BUSES",
            "PSS/E RAW requires at least one bus"});
    }

    // Check for duplicate bus IDs
    std::set<int64_t> busIds;
    for (const auto& b : data.buses) {
        if (!busIds.insert(b.id).second) {
            errs.push_back({Severity::Error, "DUP_BUS_ID",
                "Duplicate bus ID: " + std::to_string(b.id)});
        }
        if (b.baseVoltage_kV <= 0) {
            errs.push_back({Severity::Warning, "ZERO_BASEKV",
                "Bus " + b.name + " (" + std::to_string(b.id) +
                ") has non-positive base voltage"});
        }
    }

    // Validate branch connectivity
    for (const auto& br : data.branches) {
        if (busIds.find(br.fromBus) == busIds.end()) {
            errs.push_back({Severity::Error, "INVALID_FROM_BUS",
                "Branch references non-existent from-bus: " + std::to_string(br.fromBus)});
        }
        if (busIds.find(br.toBus) == busIds.end()) {
            errs.push_back({Severity::Error, "INVALID_TO_BUS",
                "Branch references non-existent to-bus: " + std::to_string(br.toBus)});
        }
    }

    // Validate transformer connectivity
    for (const auto& t : data.transformers) {
        if (busIds.find(t.fromBus) == busIds.end()) {
            errs.push_back({Severity::Error, "INVALID_XFMR_BUS",
                "Transformer references non-existent from-bus: " + std::to_string(t.fromBus)});
        }
        if (busIds.find(t.toBus) == busIds.end()) {
            errs.push_back({Severity::Error, "INVALID_XFMR_BUS",
                "Transformer references non-existent to-bus: " + std::to_string(t.toBus)});
        }
    }

    return errs;
}

// ============================================================================
// Save
// ============================================================================

ExportResult PsseRawExporter::save(const std::string& path,
                                    const PowerSystemData& data,
                                    CancellationToken& token) {
    auto t0 = std::chrono::steady_clock::now();
    ExportResult result;
    result.outputPath = path;

    // Validate first
    auto preErrs = validate(data);
    for (const auto& e : preErrs) result.errors.push_back(e);
    bool hasFatal = false;
    for (const auto& e : preErrs) {
        if (e.severity == Severity::Fatal) hasFatal = true;
    }
    if (hasFatal) {
        result.status = ImportStatus::Error;
        result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0);
        return result;
    }

    std::ostringstream oss;

    // 0. Case Identification
    oss << exportCaseIdentification(data);
    if (token.isCancelled()) { result.status = ImportStatus::Cancelled; return result; }

    // 1. Bus Data
    oss << exportBusData(data, token);
    if (token.isCancelled()) { result.status = ImportStatus::Cancelled; return result; }

    // 2. Load Data
    oss << exportLoadData(data, token);
    if (token.isCancelled()) { result.status = ImportStatus::Cancelled; return result; }

    // 3. Fixed Shunt Data
    oss << exportFixedShuntData(data, token);
       if (token.isCancelled()) { result.status = ImportStatus::Cancelled; return result; }

    // 4. Generator Data
    oss << exportGeneratorData(data, token);
    if (token.isCancelled()) { result.status = ImportStatus::Cancelled; return result; }

    // 5. Branch Data
    oss << exportBranchData(data, token);
    if (token.isCancelled()) { result.status = ImportStatus::Cancelled; return result; }

    // 6. Transformer Data
    oss << exportTransformerData(data, token);
    if (token.isCancelled()) { result.status = ImportStatus::Cancelled; return result; }

    // 7. Area Interchange
    oss << exportAreaInterchangeData(data);

    // 8. Two-Terminal DC
    oss << exportTwoTerminalDCLine(data);

    // 9. VSC DC
    oss << exportVSCDCLine(data);

    // 10. Switched Shunt
    oss << exportSwitchedShuntData(data, token);

    // 11. Impedance Correction
    oss << exportImpedanceCorrection(data);

    // 12. Multi-Terminal DC
    oss << exportMultiTerminalDCLine(data);

    // 13. Multi-Section Line
    oss << exportMultiSectionLine(data);

    // 14. Zone
    oss << exportZoneData(data);

    // 15. Interarea Transfer
    oss << exportInterareaTransfer(data);

    // 16. Owner
    oss << exportOwnerData(data);

    // 17. FACTS Device
    oss << exportFACTSDevice(data);

    // End-of-file marker
    oss << "Q\n";

    std::string content = oss.str();
    bool ok = writeStringToFile(path, content);
    if (!ok) {
        result.status = ImportStatus::Error;
        result.errors.push_back({Severity::Fatal, "WRITE_ERROR",
            "Failed to write file: " + path});
        return result;
    }

    result.bytesWritten = content.size();
    result.status = result.errors.empty() ? ImportStatus::Success : ImportStatus::Warning;
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);
    return result;
}

// ============================================================================
// Formatting helpers
// ============================================================================

std::string PsseRawExporter::fmtField(const std::string& val, std::size_t width) {
    if (csvMode_) return val;
    if (width == 0) return val;
    if (val.size() >= width) return val.substr(0, width);
    return val + std::string(width - val.size(), ' ');
}

std::string PsseRawExporter::fmtDouble(double v, int prec, std::size_t width) {
    std::ostringstream oss;
    if (csvMode_) {
        oss << std::fixed << std::setprecision(prec) << v;
        return oss.str();
    }
    if (width > 0) {
        oss << std::setw(static_cast<int>(width)) << std::fixed << std::setprecision(prec) << v;
    } else {
        oss << std::fixed << std::setprecision(prec) << v;
    }
    return oss.str();
}

std::string PsseRawExporter::fmtInt(int64_t v, std::size_t width) {
    std::string s = std::to_string(v);
    if (csvMode_) return s;
    if (width > 0 && s.size() < width) {
        s = s + std::string(width - s.size(), ' ');
    }
    return s;
}

std::string PsseRawExporter::getAttrOrDefault(
    const std::map<std::string, std::string>& attrs,
    const std::string& key, const std::string& def) {
    auto it = attrs.find(key);
    return (it != attrs.end()) ? it->second : def;
}

// ============================================================================
// Case Identification
// ============================================================================

std::string PsseRawExporter::exportCaseIdentification(const PowerSystemData& data) {
    std::ostringstream oss;
    // IC, SBASE, REV, XFRRAT, NXFRAT, BASFRQ
    int ic = 0;
    try { ic = std::stoi(data.metadata.count("ic") ? data.metadata.at("ic") : "0"); } catch (...) {}
    double sbase = 100.0;
    try { sbase = std::stod(data.metadata.count("sbase") ? data.metadata.at("sbase") : "100.0"); } catch (...) {}
    double basfrq = 60.0;
    try { basfrq = std::stod(data.metadata.count("basfrq") ? data.metadata.at("basfrq") : "60.0"); } catch (...) {}

    std::string label = data.metadata.count("label") ? data.metadata.at("label") : "POWSYS365 Export";

    oss << fmtInt(ic, 2) << sep()
        << fmtDouble(sbase, 4) << sep()
        << fmtInt(revision_, 2) << sep()
        << fmtInt(0, 1) << sep()
        << fmtInt(0, 1) << sep()
        << fmtDouble(basfrq, 4) << sep()
        << label << "\n";
    return oss.str();
}

// ============================================================================
// Bus Data
// ============================================================================

std::string PsseRawExporter::exportBusData(const PowerSystemData& data,
                                            CancellationToken& token) {
    std::ostringstream oss;
    reportProgress("exporting_buses", 0, data.buses.size(), "");
    for (std::size_t i = 0; i < data.buses.size(); ++i) {
        if (token.isCancelled()) break;
        if (i % 100 == 0) reportProgress("exporting_buses", i, data.buses.size(), "");

        const auto& b = data.buses[i];
        int ide = 1; // PQ by default
        try {
            if (b.attributes.count("type")) ide = std::stoi(b.attributes.at("type"));
        } catch (...) {}

        double vm = 1.0;
        try {
            if (b.attributes.count("vm_pu")) vm = std::stod(b.attributes.at("vm_pu"));
        } catch (...) {}

        double va = 0.0;
        try {
            if (b.attributes.count("va_deg")) va = std::stod(b.attributes.at("va_deg"));
        } catch (...) {}

        oss << fmtInt(b.id, 6) << sep()
            << "'" << fmtField(b.name.empty() ? "BUS_" + std::to_string(b.id) : b.name, 12) << "'" << sep()
            << fmtDouble(b.baseVoltage_kV, 4) << sep()
            << fmtInt(ide, 1) << sep()
            << fmtInt(b.area, 4) << sep()
            << fmtInt(b.zone, 3) << sep()
            << fmtInt(b.owner, 3) << sep()
            << fmtDouble(vm, 6) << sep()
            << fmtDouble(va, 6) << "\n";
    }
    oss << "0 / END OF BUS DATA, BEGIN LOAD DATA\n";
    return oss.str();
}

// ============================================================================
// Load Data
// ============================================================================

std::string PsseRawExporter::exportLoadData(const PowerSystemData& data,
                                             CancellationToken& token) {
    std::ostringstream oss;
    reportProgress("exporting_loads", 0, data.loads.size(), "");
    for (std::size_t i = 0; i < data.loads.size(); ++i) {
        if (token.isCancelled()) break;
        if (i % 100 == 0) reportProgress("exporting_loads", i, data.loads.size(), "");

        const auto& ld = data.loads[i];
        // I, ID, STATUS, AREA, ZONE, PL, QL, IP, IQ, YP, YQ, OWNER
        oss << fmtInt(ld.busId, 6) << sep()
            << "'" << fmtField(ld.id, 2) << "'" << sep()
            << fmtInt(ld.status, 1) << sep()
            << fmtInt(1, 4) << sep()  // area
            << fmtInt(1, 3) << sep()  // zone
            << fmtDouble(ld.pLoad_MW, 6) << sep()
            << fmtDouble(ld.qLoad_Mvar, 6) << sep()
            << fmtDouble(0.0, 6) << sep()  // IP
            << fmtDouble(0.0, 6) << sep()  // IQ
            << fmtDouble(0.0, 6) << sep()  // YP
            << fmtDouble(0.0, 6) << sep()  // YQ
            << fmtInt(1, 4) << "\n";         // owner
    }
    oss << "0 / END OF LOAD DATA, BEGIN FIXED SHUNT DATA\n";
    return oss.str();
}

// ============================================================================
// Fixed Shunt Data
// ============================================================================

std::string PsseRawExporter::exportFixedShuntData(const PowerSystemData& data,
                                                   CancellationToken& token) {
    std::ostringstream oss;
    reportProgress("exporting_shunts", 0, data.shunts.size(), "");
    for (std::size_t i = 0; i < data.shunts.size(); ++i) {
        if (token.isCancelled()) break;
        if (i % 100 == 0) reportProgress("exporting_shunts", i, data.shunts.size(), "");

        const auto& s = data.shunts[i];
        // I, ID, STATUS, G, B
        oss << fmtInt(s.busId, 6) << sep()
            << "'" << fmtField(s.id, 2) << "'" << sep()
            << fmtInt(s.status, 1) << sep()
            << fmtDouble(s.g_MW, 6) << sep()
            << fmtDouble(s.b_Mvar, 6) << "\n";
    }
    oss << "0 / END OF FIXED SHUNT DATA, BEGIN GENERATOR DATA\n";
    return oss.str();
}

// ============================================================================
// Generator Data
// ============================================================================

std::string PsseRawExporter::exportGeneratorData(const PowerSystemData& data,
                                                  CancellationToken& token) {
    std::ostringstream oss;
    reportProgress("exporting_gens", 0, data.generators.size(), "");
    for (std::size_t i = 0; i < data.generators.size(); ++i) {
        if (token.isCancelled()) break;
        if (i % 100 == 0) reportProgress("exporting_gens", i, data.generators.size(), "");

        const auto& g = data.generators[i];
        // I, ID, PG, QG, QT, QB, VS, IREG, MBASE, ZR, ZX, RT, XT, GTAP, STAT, RMPCT, PT, PB
        oss << fmtInt(g.busId, 6) << sep()
            << "'" << fmtField(g.id, 2) << "'" << sep()
            << fmtDouble(g.pGen_MW, 6) << sep()
            << fmtDouble(g.qGen_Mvar, 6) << sep()
            << fmtDouble(g.qMax_Mvar, 6) << sep()
            << fmtDouble(g.qMin_Mvar, 6) << sep()
            << fmtDouble(g.vSet_pu, 6) << sep()
            << fmtInt(0, 6) << sep()   // IREG
            << fmtDouble(g.mBase_MVA, 6) << sep()
            << fmtDouble(0.0, 6) << sep()  // ZR
            << fmtDouble(0.0, 6) << sep()  // ZX
            << fmtDouble(0.0, 6) << sep()  // RT
            << fmtDouble(0.0, 6) << sep()  // XT
            << fmtDouble(1.0, 6) << sep()  // GTAP
            << fmtInt(g.status, 1) << sep()
            << fmtDouble(100.0, 4) << sep()  // RMPCT
            << fmtDouble(g.pMax_MW, 6) << sep()
            << fmtDouble(g.pMin_MW, 6) << "\n";
    }
    oss << "0 / END OF GENERATOR DATA, BEGIN BRANCH DATA\n";
    return oss.str();
}

// ============================================================================
// Branch Data
// ============================================================================

std::string PsseRawExporter::exportBranchData(const PowerSystemData& data,
                                               CancellationToken& token) {
    std::ostringstream oss;
    reportProgress("exporting_branches", 0, data.branches.size(), "");
    for (std::size_t i = 0; i < data.branches.size(); ++i) {
        if (token.isCancelled()) break;
        if (i % 100 == 0) reportProgress("exporting_branches", i, data.branches.size(), "");

        const auto& br = data.branches[i];
        // I, J, CKT, R, X, B, RATEA, RATEB, RATEC, GI, BI, GJ, BJ, ST, MET, LEN
        oss << fmtInt(br.fromBus, 6) << sep()
            << fmtInt(br.toBus, 6) << sep()
            << "'" << fmtField(br.circuitId, 2) << "'" << sep()
            << fmtDouble(br.r_pu, 6) << sep()
            << fmtDouble(br.x_pu, 6) << sep()
            << fmtDouble(br.b_pu, 6) << sep()
            << fmtDouble(br.rateA_MVA, 6) << sep()
            << fmtDouble(br.rateB_MVA, 6) << sep()
            << fmtDouble(br.rateC_MVA, 6) << sep()
            << fmtDouble(0.0, 6) << sep()  // GI
            << fmtDouble(0.0, 6) << sep()  // BI
            << fmtDouble(0.0, 6) << sep()  // GJ
            << fmtDouble(0.0, 6) << sep()  // BJ
            << fmtInt(br.status, 1) << sep()
            << fmtInt(1, 1) << sep()       // MET
            << fmtDouble(br.length_km, 4) << "\n";
    }
    oss << "0 / END OF BRANCH DATA, BEGIN TRANSFORMER DATA\n";
    return oss.str();
}

// ============================================================================
// Transformer Data
// ============================================================================

std::string PsseRawExporter::exportTransformerData(const PowerSystemData& data,
                                                    CancellationToken& token) {
    std::ostringstream oss;
    reportProgress("exporting_xfmrs", 0, data.transformers.size(), "");
    for (std::size_t i = 0; i < data.transformers.size(); ++i) {
        if (token.isCancelled()) break;
        if (i % 100 == 0) reportProgress("exporting_xfmrs", i, data.transformers.size(), "");

        const auto& t = data.transformers[i];
        bool threeWinding = (t.tertBus != 0);

        // Line 1: I, J, K, CKT, CW, CZ, CM, MAG1, MAG2, NMET1, NAME, STAT
        oss << fmtInt(t.fromBus, 6) << sep()
            << fmtInt(t.toBus, 6) << sep()
            << fmtInt(t.tertBus, 6) << sep()
            << "'" << fmtField(t.circuitId, 2) << "'" << sep()
            << fmtInt(1, 1) << sep()  // CW
            << fmtInt(1, 1) << sep()  // CZ
            << fmtInt(1, 1) << sep()  // CM
            << fmtDouble(0.0, 4) << sep()  // MAG1
            << fmtDouble(0.0, 4) << sep()  // MAG2
            << fmtInt(1, 1) << sep()   // NMET1
            << "'" << fmtField("T_" + std::to_string(i + 1), 12) << "'" << sep()
            << fmtInt(t.status, 1) << "\n";

        // Line 2: Winding 1 data
        oss << fmtDouble(t.r12_pu, 6) << sep()
            << fmtDouble(t.x12_pu, 6) << sep()
            << fmtDouble(t.rateA_MVA, 6) << sep()
            << fmtDouble(t.windV1_kV, 6) << sep()
            << fmtDouble(t.windV1_kV, 6) << sep()  // NOMV1
            << fmtDouble(0.0, 4) << sep()  // ANG1
            << fmtDouble(t.rateA_MVA, 6) << sep()  // RATA1
            << fmtDouble(t.rateA_MVA, 6) << sep()  // RATB1
            << fmtDouble(t.rateA_MVA, 6) << sep()  // RATC1
            << fmtInt(0, 2) << sep()   // COD1
            << fmtInt(0, 5) << sep()   // CONT1
            << fmtDouble(1.1, 4) << sep()  // RMA1
            << fmtDouble(0.9, 4) << sep()  // RMI1
            << fmtDouble(1.1, 4) << sep()  // VMA1
            << fmtDouble(0.9, 4) << sep()  // VMI1
            << fmtInt(33, 3) << sep()  // NTP1
            << fmtInt(0, 3) << sep()   // TAB1
            << fmtDouble(0.0, 4) << sep()  // CR1
            << fmtDouble(0.0, 4) << sep()  // CX1
            << fmtDouble(0.0, 4) << "\n";   // CNXA1

        // Line 3: Winding 2 data
        oss << fmtDouble(t.windV2_kV, 6) << sep()
            << fmtDouble(t.windV2_kV, 6) << sep()  // NOMV2
            << fmtDouble(0.0, 4) << sep()  // ANG2
            << fmtDouble(t.rateA_MVA, 6) << sep()
            << fmtDouble(t.rateA_MVA, 6) << sep()
            << fmtDouble(t.rateA_MVA, 6) << sep()
            << fmtInt(0, 2) << sep()   // COD2
            << fmtInt(0, 5) << "\n";

        // Line 4 (3-winding only): Winding 3 data
        if (threeWinding) {
            oss << fmtDouble(t.windV3_kV, 6) << sep()
                << fmtDouble(t.windV3_kV, 6) << sep()
                << fmtDouble(0.0, 4) << sep()
                << fmtDouble(t.rateA_MVA, 6) << sep()
                << fmtDouble(t.rateA_MVA, 6) << sep()
                << fmtDouble(t.rateA_MVA, 6) << sep()
                << fmtInt(0, 2) << sep()
                << fmtInt(0, 5) << "\n";
        }
    }
    oss << "0 / END OF TRANSFORMER DATA, BEGIN AREA DATA\n";
    return oss.str();
}

// ============================================================================
// Remaining sections (placeholders with correct format)
// ============================================================================

std::string PsseRawExporter::exportAreaInterchangeData(const PowerSystemData&) {
    return "0 / END OF AREA DATA, BEGIN TWO-TERMINAL DC DATA\n";
}
std::string PsseRawExporter::exportTwoTerminalDCLine(const PowerSystemData&) {
    return "0 / END OF TWO-TERMINAL DC DATA, BEGIN VSC DC DATA\n";
}
std::string PsseRawExporter::exportVSCDCLine(const PowerSystemData&) {
    return "0 / END OF VSC DC DATA, BEGIN SWITCHED SHUNT DATA\n";
}
std::string PsseRawExporter::exportSwitchedShuntData(const PowerSystemData&, CancellationToken&) {
    return "0 / END OF SWITCHED SHUNT DATA, BEGIN IMPEDANCE CORRECTION DATA\n";
}
std::string PsseRawExporter::exportImpedanceCorrection(const PowerSystemData&) {
    return "0 / END OF IMPEDANCE CORRECTION DATA, BEGIN MULTI-TERMINAL DC DATA\n";
}
std::string PsseRawExporter::exportMultiTerminalDCLine(const PowerSystemData&) {
    return "0 / END OF MULTI-TERMINAL DC DATA, BEGIN MULTI-SECTION LINE DATA\n";
}
std::string PsseRawExporter::exportMultiSectionLine(const PowerSystemData&) {
    return "0 / END OF MULTI-SECTION LINE DATA, BEGIN ZONE DATA\n";
}
std::string PsseRawExporter::exportZoneData(const PowerSystemData&) {
    return "0 / END OF ZONE DATA, BEGIN INTERAREA TRANSFER DATA\n";
}
std::string PsseRawExporter::exportInterareaTransfer(const PowerSystemData&) {
    return "0 / END OF INTERAREA TRANSFER DATA, BEGIN OWNER DATA\n";
}
std::string PsseRawExporter::exportOwnerData(const PowerSystemData&) {
    return "0 / END OF OWNER DATA, BEGIN FACTS DEVICE DATA\n";
}
std::string PsseRawExporter::exportFACTSDevice(const PowerSystemData&) {
    return "0 / END OF FACTS DEVICE DATA\n";
}

} // namespace powsys365::io
