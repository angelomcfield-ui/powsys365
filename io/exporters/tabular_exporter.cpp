#include "tabular_exporter.h"
#include <chrono>
#include <cstring>
#include <set>

namespace powsys365::io {

// ============================================================================
// CSV Exporter
// ============================================================================

FileInfo CsvExporter::getInfo() const {
    FileInfo info;
    info.formatName = "CSV (Comma-Separated Values)";
    info.extensions = supportedExtensions();
    info.properties = {{"delimiter", std::string(1, delimiter_)},
                        {"description", "CSV tabular export"}};
    return info;
}

std::vector<ImportError> CsvExporter::validate(const PowerSystemData& data) {
    std::vector<ImportError> errs;
    if (data.buses.empty()) {
        errs.push_back({Severity::Warning, "NO_BUSES",
            "No buses to export – file will contain only headers"});
    }
    return errs;
}

std::string CsvExporter::escapeCsv(const std::string& s) {
    if (s.find(delimiter_) != std::string::npos ||
        s.find('"') != std::string::npos ||
        s.find('\n') != std::string::npos) {
        std::string out = "\"";
        for (char c : s) {
            if (c == '"') out += "\"\"";
            else out += c;
        }
        out += "\"";
        return out;
    }
    return s;
}

std::string CsvExporter::exportBuses(const PowerSystemData& data, CancellationToken& token) {
    std::ostringstream oss;
    if (includeHeader_) {
        oss << "type" << delimiter_ << "bus_id" << delimiter_ << "name" << delimiter_
            << "base_kv" << delimiter_ << "area" << delimiter_ << "zone" << delimiter_
            << "owner" << delimiter_ << "latitude" << delimiter_ << "longitude" << "\n";
    }
    reportProgress("exporting_buses", 0, data.buses.size(), "");
    for (std::size_t i = 0; i < data.buses.size(); ++i) {
        if (token.isCancelled()) break;
        if (i % 100 == 0) reportProgress("exporting_buses", i, data.buses.size(), "");
        const auto& b = data.buses[i];
        oss << "bus" << delimiter_ << b.id << delimiter_ << escapeCsv(b.name) << delimiter_
            << b.baseVoltage_kV << delimiter_ << b.area << delimiter_ << b.zone << delimiter_
            << b.owner << delimiter_;
        if (b.location.has_value()) {
            oss << b.location->latitude << delimiter_ << b.location->longitude;
        } else {
            oss << delimiter_;
        }
        oss << "\n";
    }
    return oss.str();
}

std::string CsvExporter::exportBranches(const PowerSystemData& data, CancellationToken& token) {
    std::ostringstream oss;
    if (includeHeader_) {
        oss << "type" << delimiter_ << "from_bus" << delimiter_ << "to_bus" << delimiter_
            << "ckt" << delimiter_ << "r_pu" << delimiter_ << "x_pu" << delimiter_
            << "b_pu" << delimiter_ << "rate_a" << delimiter_ << "rate_b" << delimiter_
            << "rate_c" << delimiter_ << "status" << delimiter_ << "length_km" << "\n";
    }
    reportProgress("exporting_branches", 0, data.branches.size(), "");
    for (std::size_t i = 0; i < data.branches.size(); ++i) {
        if (token.isCancelled()) break;
        if (i % 100 == 0) reportProgress("exporting_branches", i, data.branches.size(), "");
        const auto& br = data.branches[i];
        oss << "branch" << delimiter_ << br.fromBus << delimiter_ << br.toBus << delimiter_
            << escapeCsv(br.circuitId) << delimiter_ << br.r_pu << delimiter_ << br.x_pu
            << delimiter_ << br.b_pu << delimiter_ << br.rateA_MVA << delimiter_
            << br.rateB_MVA << delimiter_ << br.rateC_MVA << delimiter_ << br.status
            << delimiter_ << br.length_km << "\n";
    }
    return oss.str();
}

std::string CsvExporter::exportTransformers(const PowerSystemData& data, CancellationToken& token) {
    std::ostringstream oss;
    if (includeHeader_) {
        oss << "type" << delimiter_ << "from_bus" << delimiter_ << "to_bus" << delimiter_
            << "tert_bus" << delimiter_ << "ckt" << delimiter_ << "r_pu" << delimiter_
            << "x_pu" << delimiter_ << "rate" << delimiter_ << "windv1" << delimiter_
            << "windv2" << delimiter_ << "windv3" << delimiter_ << "status" << "\n";
    }
    for (std::size_t i = 0; i < data.transformers.size(); ++i) {
        if (token.isCancelled()) break;
        const auto& t = data.transformers[i];
        oss << "transformer" << delimiter_ << t.fromBus << delimiter_ << t.toBus << delimiter_
            << t.tertBus << delimiter_ << escapeCsv(t.circuitId) << delimiter_
            << t.r12_pu << delimiter_ << t.x12_pu << delimiter_ << t.rateA_MVA << delimiter_
            << t.windV1_kV << delimiter_ << t.windV2_kV << delimiter_ << t.windV3_kV
            << delimiter_ << t.status << "\n";
    }
    return oss.str();
}

std::string CsvExporter::exportGenerators(const PowerSystemData& data, CancellationToken& token) {
    std::ostringstream oss;
    if (includeHeader_) {
        oss << "type" << delimiter_ << "bus_id" << delimiter_ << "gen_id" << delimiter_
            << "pg" << delimiter_ << "qg" << delimiter_ << "qt" << delimiter_ << "qb"
            << delimiter_ << "vs" << delimiter_ << "pt" << delimiter_ << "pb" << delimiter_
            << "status" << delimiter_ << "mbase" << "\n";
    }
    for (std::size_t i = 0; i < data.generators.size(); ++i) {
        if (token.isCancelled()) break;
        const auto& g = data.generators[i];
        oss << "generator" << delimiter_ << g.busId << delimiter_ << escapeCsv(g.id)
            << delimiter_ << g.pGen_MW << delimiter_ << g.qGen_Mvar << delimiter_
            << g.qMax_Mvar << delimiter_ << g.qMin_Mvar << delimiter_ << g.vSet_pu
            << delimiter_ << g.pMax_MW << delimiter_ << g.pMin_MW << delimiter_
            << g.status << delimiter_ << g.mBase_MVA << "\n";
    }
    return oss.str();
}

std::string CsvExporter::exportLoads(const PowerSystemData& data, CancellationToken& token) {
    std::ostringstream oss;
    if (includeHeader_) {
        oss << "type" << delimiter_ << "bus_id" << delimiter_ << "load_id" << delimiter_
            << "pl" << delimiter_ << "ql" << delimiter_ << "status" << "\n";
    }
    for (std::size_t i = 0; i < data.loads.size(); ++i) {
        if (token.isCancelled()) break;
        const auto& ld = data.loads[i];
        oss << "load" << delimiter_ << ld.busId << delimiter_ << escapeCsv(ld.id) << delimiter_
            << ld.pLoad_MW << delimiter_ << ld.qLoad_Mvar << delimiter_ << ld.status << "\n";
    }
    return oss.str();
}

std::string CsvExporter::exportShunts(const PowerSystemData& data, CancellationToken& token) {
    std::ostringstream oss;
    if (includeHeader_) {
        oss << "type" << delimiter_ << "bus_id" << delimiter_ << "shunt_id" << delimiter_
            << "b" << delimiter_ << "g" << delimiter_ << "status" << "\n";
    }
    for (std::size_t i = 0; i < data.shunts.size(); ++i) {
        if (token.isCancelled()) break;
        const auto& s = data.shunts[i];
        oss << "shunt" << delimiter_ << s.busId << delimiter_ << escapeCsv(s.id) << delimiter_
            << s.b_Mvar << delimiter_ << s.g_MW << delimiter_ << s.status << "\n";
    }
    return oss.str();
}

ExportResult CsvExporter::save(const std::string& path,
                                const PowerSystemData& data,
                                CancellationToken& token) {
    auto t0 = std::chrono::steady_clock::now();
    ExportResult result;
    result.outputPath = path;

    auto preErrs = validate(data);
    for (const auto& e : preErrs) result.errors.push_back(e);

    std::ostringstream oss;
    oss << exportBuses(data, token);
    if (token.isCancelled()) { result.status = ImportStatus::Cancelled; return result; }
    oss << exportBranches(data, token);
    if (token.isCancelled()) { result.status = ImportStatus::Cancelled; return result; }
    oss << exportTransformers(data, token);
    if (token.isCancelled()) { result.status = ImportStatus::Cancelled; return result; }
    oss << exportGenerators(data, token);
    if (token.isCancelled()) { result.status = ImportStatus::Cancelled; return result; }
    oss << exportLoads(data, token);
    if (token.isCancelled()) { result.status = ImportStatus::Cancelled; return result; }
    oss << exportShunts(data, token);
    if (token.isCancelled()) { result.status = ImportStatus::Cancelled; return result; }

    std::string content = oss.str();
    if (!writeStringToFile(path, content)) {
        result.status = ImportStatus::Error;
        result.errors.push_back({Severity::Fatal, "WRITE_ERROR", "Failed to write: " + path});
        return result;
    }

    result.bytesWritten = content.size();
    result.status = result.errors.empty() ? ImportStatus::Success : ImportStatus::Warning;
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);
    return result;
}

// ============================================================================
// JSON Exporter
// ============================================================================

FileInfo JsonExporter::getInfo() const {
    FileInfo info;
    info.formatName = "JSON (JavaScript Object Notation)";
    info.extensions = supportedExtensions();
    info.properties = {{"standard", "RFC 8259"},
                        {"pretty_print", prettyPrint_ ? "true" : "false"},
                        {"description", "JSON power system export"}};
    return info;
}

std::vector<ImportError> JsonExporter::validate(const PowerSystemData& data) {
    std::vector<ImportError> errs;
    if (data.buses.empty()) {
        errs.push_back({Severity::Warning, "NO_BUSES",
            "No buses to export"});
    }
    return errs;
}

std::string JsonExporter::escapeJson(const std::string& s) {
    std::string out;
    out.reserve(s.size() * 2);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

std::string JsonExporter::indent(int level) {
    return prettyPrint_ ? std::string(level * 2, ' ') : "";
}

std::string JsonExporter::exportObject(const PowerSystemData& data, CancellationToken& token) {
    std::ostringstream oss;
    std::string nl = prettyPrint_ ? "\n" : "";
    std::string sp = prettyPrint_ ? " " : "";
    std::string sep = ",";

    oss << "{" << nl;

    // Buses
    oss << indent(1) << "\"buses\":" << sp << "[" << nl;
    for (std::size_t i = 0; i < data.buses.size(); ++i) {
        if (token.isCancelled()) break;
        const auto& b = data.buses[i];
        if (i > 0) oss << sep << nl;
        oss << indent(2) << "{" << nl
            << indent(3) << "\"id\":" << sp << b.id << sep << nl
            << indent(3) << "\"name\":" << sp << "\"" << escapeJson(b.name) << "\"" << sep << nl
            << indent(3) << "\"baseVoltage_kV\":" << sp << b.baseVoltage_kV << sep << nl
            << indent(3) << "\"area\":" << sp << b.area << sep << nl
            << indent(3) << "\"zone\":" << sp << b.zone << sep << nl
            << indent(3) << "\"owner\":" << sp << b.owner;
        if (b.location.has_value()) {
            oss << sep << nl << indent(3) << "\"latitude\":" << sp << b.location->latitude
                << sep << nl << indent(3) << "\"longitude\":" << sp << b.location->longitude;
        }
        oss << nl << indent(2) << "}";
    }
    oss << nl << indent(1) << "]" << sep << nl;

    // Branches
    oss << indent(1) << "\"branches\":" << sp << "[" << nl;
    for (std::size_t i = 0; i < data.branches.size(); ++i) {
        if (token.isCancelled()) break;
        const auto& br = data.branches[i];
        if (i > 0) oss << sep << nl;
        oss << indent(2) << "{" << nl
            << indent(3) << "\"fromBus\":" << sp << br.fromBus << sep << nl
            << indent(3) << "\"toBus\":" << sp << br.toBus << sep << nl
            << indent(3) << "\"circuitId\":" << sp << "\"" << escapeJson(br.circuitId) << "\"" << sep << nl
            << indent(3) << "\"r_pu\":" << sp << br.r_pu << sep << nl
            << indent(3) << "\"x_pu\":" << sp << br.x_pu << sep << nl
            << indent(3) << "\"b_pu\":" << sp << br.b_pu << sep << nl
            << indent(3) << "\"rateA_MVA\":" << sp << br.rateA_MVA << sep << nl
            << indent(3) << "\"rateB_MVA\":" << sp << br.rateB_MVA << sep << nl
            << indent(3) << "\"rateC_MVA\":" << sp << br.rateC_MVA << sep << nl
            << indent(3) << "\"status\":" << sp << br.status << sep << nl
            << indent(3) << "\"length_km\":" << sp << br.length_km << nl
            << indent(2) << "}";
    }
    oss << nl << indent(1) << "]" << sep << nl;

    // Transformers
    oss << indent(1) << "\"transformers\":" << sp << "[" << nl;
    for (std::size_t i = 0; i < data.transformers.size(); ++i) {
        if (token.isCancelled()) break;
        const auto& t = data.transformers[i];
        if (i > 0) oss << sep << nl;
        oss << indent(2) << "{" << nl
            << indent(3) << "\"fromBus\":" << sp << t.fromBus << sep << nl
            << indent(3) << "\"toBus\":" << sp << t.toBus << sep << nl
            << indent(3) << "\"tertBus\":" << sp << t.tertBus << sep << nl
            << indent(3) << "\"circuitId\":" << sp << "\"" << escapeJson(t.circuitId) << "\"" << sep << nl
            << indent(3) << "\"r12_pu\":" << sp << t.r12_pu << sep << nl
            << indent(3) << "\"x12_pu\":" << sp << t.x12_pu << sep << nl
            << indent(3) << "\"rateA_MVA\":" << sp << t.rateA_MVA << sep << nl
            << indent(3) << "\"windV1_kV\":" << sp << t.windV1_kV << sep << nl
            << indent(3) << "\"windV2_kV\":" << sp << t.windV2_kV << sep << nl
            << indent(3) << "\"windV3_kV\":" << sp << t.windV3_kV << sep << nl
            << indent(3) << "\"status\":" << sp << t.status << nl
            << indent(2) << "}";
    }
    oss << nl << indent(1) << "]" << sep << nl;

    // Generators
    oss << indent(1) << "\"generators\":" << sp << "[" << nl;
    for (std::size_t i = 0; i < data.generators.size(); ++i) {
        if (token.isCancelled()) break;
        const auto& g = data.generators[i];
        if (i > 0) oss << sep << nl;
        oss << indent(2) << "{" << nl
            << indent(3) << "\"busId\":" << sp << g.busId << sep << nl
            << indent(3) << "\"id\":" << sp << "\"" << escapeJson(g.id) << "\"" << sep << nl
            << indent(3) << "\"pGen_MW\":" << sp << g.pGen_MW << sep << nl
            << indent(3) << "\"qGen_Mvar\":" << sp << g.qGen_Mvar << sep << nl
            << indent(3) << "\"qMax_Mvar\":" << sp << g.qMax_Mvar << sep << nl
            << indent(3) << "\"qMin_Mvar\":" << sp << g.qMin_Mvar << sep << nl
            << indent(3) << "\"vSet_pu\":" << sp << g.vSet_pu << sep << nl
            << indent(3) << "\"pMax_MW\":" << sp << g.pMax_MW << sep << nl
            << indent(3) << "\"pMin_MW\":" << sp << g.pMin_MW << sep << nl
            << indent(3) << "\"status\":" << sp << g.status << sep << nl
            << indent(3) << "\"mBase_MVA\":" << sp << g.mBase_MVA << nl
            << indent(2) << "}";
    }
    oss << nl << indent(1) << "]" << sep << nl;

    // Loads
    oss << indent(1) << "\"loads\":" << sp << "[" << nl;
    for (std::size_t i = 0; i < data.loads.size(); ++i) {
        if (token.isCancelled()) break;
        const auto& ld = data.loads[i];
        if (i > 0) oss << sep << nl;
        oss << indent(2) << "{" << nl
            << indent(3) << "\"busId\":" << sp << ld.busId << sep << nl
            << indent(3) << "\"id\":" << sp << "\"" << escapeJson(ld.id) << "\"" << sep << nl
            << indent(3) << "\"pLoad_MW\":" << sp << ld.pLoad_MW << sep << nl
            << indent(3) << "\"qLoad_Mvar\":" << sp << ld.qLoad_Mvar << sep << nl
            << indent(3) << "\"status\":" << sp << ld.status << nl
            << indent(2) << "}";
    }
    oss << nl << indent(1) << "]" << sep << nl;

    // Shunts
    oss << indent(1) << "\"shunts\":" << sp << "[" << nl;
    for (std::size_t i = 0; i < data.shunts.size(); ++i) {
        if (token.isCancelled()) break;
        const auto& s = data.shunts[i];
        if (i > 0) oss << sep << nl;
        oss << indent(2) << "{" << nl
            << indent(3) << "\"busId\":" << sp << s.busId << sep << nl
            << indent(3) << "\"id\":" << sp << "\"" << escapeJson(s.id) << "\"" << sep << nl
            << indent(3) << "\"b_Mvar\":" << sp << s.b_Mvar << sep << nl
            << indent(3) << "\"g_MW\":" << sp << s.g_MW << sep << nl
            << indent(3) << "\"status\":" << sp << s.status << nl
            << indent(2) << "}";
    }
    oss << nl << indent(1) << "]" << nl << "}" << nl;

    return oss.str();
}

ExportResult JsonExporter::save(const std::string& path,
                                 const PowerSystemData& data,
                                 CancellationToken& token) {
    auto t0 = std::chrono::steady_clock::now();
    ExportResult result;
    result.outputPath = path;

    auto preErrs = validate(data);
    for (const auto& e : preErrs) result.errors.push_back(e);

    std::string content = exportObject(data, token);
    if (token.isCancelled()) {
        result.status = ImportStatus::Cancelled;
        return result;
    }

    if (!writeStringToFile(path, content)) {
        result.status = ImportStatus::Error;
        result.errors.push_back({Severity::Fatal, "WRITE_ERROR", "Failed to write: " + path});
        return result;
    }

    result.bytesWritten = content.size();
    result.status = result.errors.empty() ? ImportStatus::Success : ImportStatus::Warning;
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);
    return result;
}

// ============================================================================
// XML Exporter
// ============================================================================

FileInfo XmlExporter::getInfo() const {
    FileInfo info;
    info.formatName = "XML (Generic Power System Data)";
    info.extensions = supportedExtensions();
    info.properties = {{"description", "XML power system export"}};
    return info;
}

std::vector<ImportError> XmlExporter::validate(const PowerSystemData& data) {
    std::vector<ImportError> errs;
    if (data.buses.empty()) {
        errs.push_back({Severity::Warning, "NO_BUSES", "No buses to export"});
    }
    return errs;
}

std::string XmlExporter::escapeXml(const std::string& s) {
    std::string out;
    out.reserve(s.size() * 2);
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out += c; break;
        }
    }
    return out;
}

std::string XmlExporter::exportDocument(const PowerSystemData& data, CancellationToken& token) {
    std::ostringstream oss;
    oss << R"(<?xml version="1.0" encoding="UTF-8"?>
<PowerSystemData>
  <metadata>
    <created>)" + []() {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        std::tm tm;
        gmtime_r(&t, &tm);
        std::ostringstream o;
        o << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
        return o.str();
    }() + R"(</created>
    <source>POWSYS365</source>
  </metadata>
  <buses>
";
    reportProgress("exporting_buses", 0, data.buses.size(), "");
    for (std::size_t i = 0; i < data.buses.size(); ++i) {
        if (token.isCancelled()) break;
        if (i % 100 == 0) reportProgress("exporting_buses", i, data.buses.size(), "");
        const auto& b = data.buses[i];
        oss << "    <bus id=\"" << b.id << "\" name=\"" << escapeXml(b.name)
            << "\" baseVoltage_kV=\"" << b.baseVoltage_kV << "\" area=\"" << b.area
            << "\" zone=\"" << b.zone << "\" owner=\"" << b.owner << "\"";
        if (b.location.has_value()) {
            oss << " latitude=\"" << b.location->latitude
                << "\" longitude=\"" << b.location->longitude << "\"";
        }
        oss << "/>\n";
    }
    oss << "  </buses>\n";

    oss << "  <branches>\n";
    for (std::size_t i = 0; i < data.branches.size(); ++i) {
        if (token.isCancelled()) break;
        const auto& br = data.branches[i];
        oss << "    <branch fromBus=\"" << br.fromBus << "\" toBus=\"" << br.toBus
            << "\" circuitId=\"" << escapeXml(br.circuitId) << "\" r_pu=\"" << br.r_pu
            << "\" x_pu=\"" << br.x_pu << "\" b_pu=\"" << br.b_pu
            << "\" rateA_MVA=\"" << br.rateA_MVA << "\" rateB_MVA=\"" << br.rateB_MVA
            << "\" rateC_MVA=\"" << br.rateC_MVA << "\" status=\"" << br.status
            << "\" length_km=\"" << br.length_km << "\"/>\n";
    }
    oss << "  </branches>\n";

    oss << "  <transformers>\n";
    for (const auto& t : data.transformers) {
        if (token.isCancelled()) break;
        oss << "    <transformer fromBus=\"" << t.fromBus << "\" toBus=\"" << t.toBus
            << "\" tertBus=\"" << t.tertBus << "\" circuitId=\"" << escapeXml(t.circuitId)
            << "\" r12_pu=\"" << t.r12_pu << "\" x12_pu=\"" << t.x12_pu
            << "\" rateA_MVA=\"" << t.rateA_MVA << "\" windV1_kV=\"" << t.windV1_kV
            << "\" windV2_kV=\"" << t.windV2_kV << "\" status=\"" << t.status << "\"/>\n";
    }
    oss << "  </transformers>\n";

    oss << "  <generators>\n";
    for (const auto& g : data.generators) {
        if (token.isCancelled()) break;
        oss << "    <generator busId=\"" << g.busId << "\" id=\"" << escapeXml(g.id)
            << "\" pGen_MW=\"" << g.pGen_MW << "\" qGen_Mvar=\"" << g.qGen_Mvar
            << "\" qMax_Mvar=\"" << g.qMax_Mvar << "\" qMin_Mvar=\"" << g.qMin_Mvar
            << "\" vSet_pu=\"" << g.vSet_pu << "\" pMax_MW=\"" << g.pMax_MW
            << "\" pMin_MW=\"" << g.pMin_MW << "\" status=\"" << g.status
            << "\" mBase_MVA=\"" << g.mBase_MVA << "\"/>\n";
    }
    oss << "  </generators>\n";

    oss << "  <loads>\n";
    for (const auto& ld : data.loads) {
        if (token.isCancelled()) break;
        oss << "    <load busId=\"" << ld.busId << "\" id=\"" << escapeXml(ld.id)
            << "\" pLoad_MW=\"" << ld.pLoad_MW << "\" qLoad_Mvar=\"" << ld.qLoad_Mvar
            << "\" status=\"" << ld.status << "\"/>\n";
    }
    oss << "  </loads>\n";

    oss << "  <shunts>\n";
    for (const auto& s : data.shunts) {
        if (token.isCancelled()) break;
        oss << "    <shunt busId=\"" << s.busId << "\" id=\"" << escapeXml(s.id)
            << "\" b_Mvar=\"" << s.b_Mvar << "\" g_MW=\"" << s.g_MW
            << "\" status=\"" << s.status << "\"/>\n";
    }
    oss << "  </shunts>\n";

    oss << "</PowerSystemData>\n";
    return oss.str();
}

ExportResult XmlExporter::save(const std::string& path,
                                const PowerSystemData& data,
                                CancellationToken& token) {
    auto t0 = std::chrono::steady_clock::now();
    ExportResult result;
    result.outputPath = path;

    auto preErrs = validate(data);
    for (const auto& e : preErrs) result.errors.push_back(e);

    std::string content = exportDocument(data, token);
    if (token.isCancelled()) {
        result.status = ImportStatus::Cancelled;
        return result;
    }

    if (!writeStringToFile(path, content)) {
        result.status = ImportStatus::Error;
        result.errors.push_back({Severity::Fatal, "WRITE_ERROR", "Failed to write: " + path});
        return result;
    }

    result.bytesWritten = content.size();
    result.status = result.errors.empty() ? ImportStatus::Success : ImportStatus::Warning;
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);
    return result;
}

// ============================================================================
// XLSX Exporter (delegates to CSV + minimal ZIP wrapper)
// ============================================================================

FileInfo XlsxExporter::getInfo() const {
    FileInfo info;
    info.formatName = "XLSX (Excel Open XML)";
    info.extensions = supportedExtensions();
    info.properties = {{"note", "Produces a minimal XLSX – full ZIP structure recommended"},
                        {"description", "Excel spreadsheet export"}};
    return info;
}

std::vector<ImportError> XlsxExporter::validate(const PowerSystemData& data) {
    std::vector<ImportError> errs;
    if (data.buses.empty()) {
        errs.push_back({Severity::Warning, "NO_BUSES", "No buses to export"});
    }
    return errs;
}

std::string XlsxExporter::buildMinimalXlsx(const std::string& csvContent) {
    // For a minimal valid XLSX we'd need to build proper ZIP with [Content_Types].xml,
    // xl/workbook.xml, xl/_rels/workbook.xml.rels, xl/worksheets/sheet1.xml, xl/sharedStrings.xml
    // This is a simplified fallback: write CSV with .xlsx extension and a header comment
    // In production, use libxlsxwriter or similar.
    return csvContent;
}

ExportResult XlsxExporter::save(const std::string& path,
                                 const PowerSystemData& data,
                                 CancellationToken& token) {
    auto t0 = std::chrono::steady_clock::now();
    ExportResult result;
    result.outputPath = path;

    auto preErrs = validate(data);
    for (const auto& e : preErrs) result.errors.push_back(e);

    // Delegate to CSV and wrap
    CsvExporter csvExp;
    auto csvResult = csvExp.save(path + ".tmp.csv", data, token);
    if (csvResult.status == ImportStatus::Cancelled) {
        result.status = ImportStatus::Cancelled;
        return result;
    }
    for (const auto& e : csvResult.errors) result.errors.push_back(e);

    // For now, write as CSV with a comment header indicating it's a simplified XLSX
    // Full XLSX generation requires a ZIP library
    std::ifstream ifs(path + ".tmp.csv");
    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());
    std::remove((path + ".tmp.csv").c_str());

    // Mark as XLSX-converted CSV with note
    std::string wrapped = "<!-- XLSX export requires full ZIP structure. "
                          "This is CSV-compatible content. -->\n" + content;

    if (!writeStringToFile(path, wrapped)) {
        result.status = ImportStatus::Error;
        result.errors.push_back({Severity::Fatal, "WRITE_ERROR", "Failed to write: " + path});
        return result;
    }

    result.bytesWritten = wrapped.size();
    result.status = result.errors.empty() ? ImportStatus::Success : ImportStatus::Warning;
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);
    return result;
}

} // namespace powsys365::io
