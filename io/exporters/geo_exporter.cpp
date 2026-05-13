#include "geo_exporter.h"
#include <chrono>

namespace powsys365::io {

// ============================================================================
// KML Exporter
// ============================================================================

FileInfo KmlExporter::getInfo() const {
    FileInfo info;
    info.formatName = "KML (Keyhole Markup Language)";
    info.extensions = supportedExtensions();
    info.properties = {{"standard", "OGC KML 2.2"},
                        {"description", "Google Earth geographic export"}};
    return info;
}

std::vector<ImportError> KmlExporter::validate(const PowerSystemData& data) {
    std::vector<ImportError> errs;
    for (const auto& b : data.buses) {
        if (!b.location.has_value()) {
            errs.push_back({Severity::Warning, "NO_COORDS",
                "Bus " + b.name + " has no coordinates – will be skipped in KML"});
        }
    }
    return errs;
}

std::string KmlExporter::escapeXml(const std::string& s) {
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

std::string KmlExporter::exportHeader() {
    std::ostringstream oss;
    oss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<kml xmlns=\"http://www.opengis.net/kml/2.2\">\n"
        << "  <Document>\n"
        << "    <name>" << escapeXml(networkName_) << "</name>\n"
        << "    <description>Exported by POWSYS365</description>\n"
        << "    <Style id=\"busStyle\">\n"
        << "      <IconStyle>\n"
        << "        <color>ff0000ff</color>\n"
        << "        <scale>1.2</scale>\n"
        << "        <Icon><href>http://maps.google.com/mapfiles/kml/shapes/placemark_circle.png</href></Icon>\n"
        << "      </IconStyle>\n"
        << "    </Style>\n"
        << "    <Style id=\"genStyle\">\n"
        << "      <IconStyle>\n"
        << "        <color>ff00ff00</color>\n"
        << "        <scale>1.0</scale>\n"
        << "        <Icon><href>http://maps.google.com/mapfiles/kml/shapes/target.png</href></Icon>\n"
        << "      </IconStyle>\n"
        << "    </Style>\n"
        << "    <Style id=\"loadStyle\">\n"
        << "      <IconStyle>\n"
        << "        <color>ffff0000</color>\n"
        << "        <scale>0.8</scale>\n"
        << "        <Icon><href>http://maps.google.com/mapfiles/kml/shapes/square.png</href></Icon>\n"
        << "      </IconStyle>\n"
        << "    </Style>\n"
        << "    <Style id=\"lineStyle\">\n"
        << "      <LineStyle>\n"
        << "        <color>ff00aaff</color>\n"
        << "        <width>3</width>\n"
        << "      </LineStyle>\n"
        << "    </Style>\n"
        << "    <Style id=\"shuntStyle\">\n"
        << "      <IconStyle>\n"
        << "        <color>ffffff00</color>\n"
        << "        <scale>0.8</scale>\n"
        << "        <Icon><href>http://maps.google.com/mapfiles/kml/shapes/triangle.png</href></Icon>\n"
        << "      </IconStyle>\n"
        << "    </Style>\n";
    return oss.str();
}

std::string KmlExporter::exportFooter() {
    return "  </Document>\n</kml>\n";
}

std::string KmlExporter::exportBuses(const PowerSystemData& data, CancellationToken& token) {
    std::ostringstream oss;
    oss << "    <Folder><name>Buses</name>\n";
    for (std::size_t i = 0; i < data.buses.size(); ++i) {
        if (token.isCancelled()) break;
        if (i % 100 == 0) reportProgress("exporting_buses", i, data.buses.size(), "");
        const auto& b = data.buses[i];
        if (!b.location.has_value()) continue;

        oss << "      <Placemark>\n"
            << "        <name>" << escapeXml(b.name) << "</name>\n"
            << "        <description>Bus " << b.id << " | " << b.baseVoltage_kV
            << " kV | Area " << b.area << "</description>\n"
            << "        <styleUrl>#busStyle</styleUrl>\n"
            << "        <Point><coordinates>"
            << b.location->longitude << "," << b.location->latitude << ",0</coordinates></Point>\n"
            << "        <ExtendedData>\n"
            << "          <Data name=\"id\"><value>" << b.id << "</value></Data>\n"
            << "          <Data name=\"base_kv\"><value>" << b.baseVoltage_kV << "</value></Data>\n"
            << "          <Data name=\"area\"><value>" << b.area << "</value></Data>\n"
            << "        </ExtendedData>\n"
            << "      </Placemark>\n";
    }
    oss << "    </Folder>\n";
    return oss.str();
}

std::string KmlExporter::exportBranches(const PowerSystemData& data, CancellationToken& token) {
    std::ostringstream oss;
    oss << "    <Folder><name>Lines</name>\n";

    // Build bus coordinate lookup
    std::map<int64_t, GeoPoint> busCoords;
    for (const auto& b : data.buses) {
        if (b.location.has_value()) busCoords[b.id] = *b.location;
    }

    for (std::size_t i = 0; i < data.branches.size(); ++i) {
        if (token.isCancelled()) break;
        if (i % 100 == 0) reportProgress("exporting_branches", i, data.branches.size(), "");
        const auto& br = data.branches[i];
        auto fromIt = busCoords.find(br.fromBus);
        auto toIt = busCoords.find(br.toBus);
        if (fromIt == busCoords.end() || toIt == busCoords.end()) continue;

        oss << "      <Placemark>\n"
            << "        <name>Line " << br.fromBus << "-" << br.toBus << "</name>\n"
            << "        <description>R=" << br.r_pu << " | X=" << br.x_pu
            << " | B=" << br.b_pu << " | RateA=" << br.rateA_MVA << " MVA</description>\n"
            << "        <styleUrl>#lineStyle</styleUrl>\n"
            << "        <LineString><coordinates>"
            << fromIt->second.longitude << "," << fromIt->second.latitude << ",0 "
            << toIt->second.longitude   << "," << toIt->second.latitude   << ",0"
            << "</coordinates></LineString>\n"
            << "      </Placemark>\n";
    }
    oss << "    </Folder>\n";
    return oss.str();
}

std::string KmlExporter::exportGenerators(const PowerSystemData& data, CancellationToken& token) {
    std::ostringstream oss;
    oss << "    <Folder><name>Generators</name>\n";

    std::map<int64_t, GeoPoint> busCoords;
    for (const auto& b : data.buses) {
        if (b.location.has_value()) busCoords[b.id] = *b.location;
    }

    for (std::size_t i = 0; i < data.generators.size(); ++i) {
        if (token.isCancelled()) break;
        const auto& g = data.generators[i];
        auto it = busCoords.find(g.busId);
        if (it == busCoords.end()) continue;

        oss << "      <Placemark>\n"
            << "        <name>Gen " << g.busId << "_" << g.id << "</name>\n"
            << "        <description>P=" << g.pGen_MW << " MW | Q=" << g.qGen_Mvar
            << " Mvar | Status=" << g.status << "</description>\n"
            << "        <styleUrl>#genStyle</styleUrl>\n"
            << "        <Point><coordinates>"
            << it->second.longitude << "," << it->second.latitude << ",0</coordinates></Point>\n"
            << "      </Placemark>\n";
    }
    oss << "    </Folder>\n";
    return oss.str();
}

std::string KmlExporter::exportLoads(const PowerSystemData& data, CancellationToken& token) {
    std::ostringstream oss;
    oss << "    <Folder><name>Loads</name>\n";

    std::map<int64_t, GeoPoint> busCoords;
    for (const auto& b : data.buses) {
        if (b.location.has_value()) busCoords[b.id] = *b.location;
    }

    for (std::size_t i = 0; i < data.loads.size(); ++i) {
        if (token.isCancelled()) break;
        const auto& ld = data.loads[i];
        auto it = busCoords.find(ld.busId);
        if (it == busCoords.end()) continue;

        oss << "      <Placemark>\n"
            << "        <name>Load " << ld.busId << "_" << ld.id << "</name>\n"
            << "        <description>P=" << ld.pLoad_MW << " MW | Q=" << ld.qLoad_Mvar
            << " Mvar | Status=" << ld.status << "</description>\n"
            << "        <styleUrl>#loadStyle</styleUrl>\n"
            << "        <Point><coordinates>"
            << it->second.longitude << "," << it->second.latitude << ",0</coordinates></Point>\n"
            << "      </Placemark>\n";
    }
    oss << "    </Folder>\n";
    return oss.str();
}

std::string KmlExporter::exportShunts(const PowerSystemData& data, CancellationToken& token) {
    std::ostringstream oss;
    oss << "    <Folder><name>Shunts</name>\n";

    std::map<int64_t, GeoPoint> busCoords;
    for (const auto& b : data.buses) {
        if (b.location.has_value()) busCoords[b.id] = *b.location;
    }

    for (std::size_t i = 0; i < data.shunts.size(); ++i) {
        if (token.isCancelled()) break;
        const auto& s = data.shunts[i];
        auto it = busCoords.find(s.busId);
        if (it == busCoords.end()) continue;

        oss << "      <Placemark>\n"
            << "        <name>Shunt " << s.busId << "_" << s.id << "</name>\n"
            << "        <description>B=" << s.b_Mvar << " Mvar | G=" << s.g_MW
            << " MW | Status=" << s.status << "</description>\n"
            << "        <styleUrl>#shuntStyle</styleUrl>\n"
            << "        <Point><coordinates>"
            << it->second.longitude << "," << it->second.latitude << ",0</coordinates></Point>\n"
            << "      </Placemark>\n";
    }
    oss << "    </Folder>\n";
    return oss.str();
}

ExportResult KmlExporter::save(const std::string& path,
                                const PowerSystemData& data,
                                CancellationToken& token) {
    auto t0 = std::chrono::steady_clock::now();
    ExportResult result;
    result.outputPath = path;

    auto preErrs = validate(data);
    for (const auto& e : preErrs) result.errors.push_back(e);

    std::ostringstream oss;
    oss << exportHeader();
    oss << exportBuses(data, token);
    if (token.isCancelled()) { result.status = ImportStatus::Cancelled; return result; }
    oss << exportGenerators(data, token);
    if (token.isCancelled()) { result.status = ImportStatus::Cancelled; return result; }
    oss << exportLoads(data, token);
    if (token.isCancelled()) { result.status = ImportStatus::Cancelled; return result; }
    oss << exportShunts(data, token);
    if (token.isCancelled()) { result.status = ImportStatus::Cancelled; return result; }
    oss << exportBranches(data, token);
    if (token.isCancelled()) { result.status = ImportStatus::Cancelled; return result; }
    oss << exportFooter();

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
// GeoJSON Exporter
// ============================================================================

FileInfo GeoJsonExporter::getInfo() const {
    FileInfo info;
    info.formatName = "GeoJSON";
    info.extensions = supportedExtensions();
    info.properties = {{"standard", "RFC 7946"},
                        {"description", "Geographic JSON export"}};
    return info;
}

std::vector<ImportError> GeoJsonExporter::validate(const PowerSystemData& data) {
    std::vector<ImportError> errs;
    for (const auto& b : data.buses) {
        if (!b.location.has_value()) {
            errs.push_back({Severity::Warning, "NO_COORDS",
                "Bus " + b.name + " has no coordinates – will be skipped"});
        }
    }
    return errs;
}

std::string GeoJsonExporter::escapeJson(const std::string& s) {
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

std::string GeoJsonExporter::exportBusFeatures(const PowerSystemData& data,
                                                CancellationToken& token) {
    std::ostringstream oss;
    for (std::size_t i = 0; i < data.buses.size(); ++i) {
        if (token.isCancelled()) break;
        if (i % 100 == 0) reportProgress("exporting_buses", i, data.buses.size(), "");
        const auto& b = data.buses[i];
        if (!b.location.has_value()) continue;

        if (i > 0) oss << ",\n";
        oss << "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Point\",\"coordinates\":["
            << b.location->longitude << "," << b.location->latitude
            << "]},\"properties\":{\"type\":\"bus\",\"id\":" << b.id
            << ",\"name\":\"" << escapeJson(b.name) << "\",\"baseVoltage_kV\":"
            << b.baseVoltage_kV << ",\"area\":" << b.area << "}}";
    }
    return oss.str();
}

std::string GeoJsonExporter::exportBranchFeatures(const PowerSystemData& data,
                                                   CancellationToken& token) {
    std::ostringstream oss;

    std::map<int64_t, GeoPoint> busCoords;
    for (const auto& b : data.buses) {
        if (b.location.has_value()) busCoords[b.id] = *b.location;
    }

    bool first = data.buses.empty();
    for (std::size_t i = 0; i < data.branches.size(); ++i) {
        if (token.isCancelled()) break;
        const auto& br = data.branches[i];
        auto fromIt = busCoords.find(br.fromBus);
        auto toIt = busCoords.find(br.toBus);
        if (fromIt == busCoords.end() || toIt == busCoords.end()) continue;

        if (!first) oss << ",\n";
        first = false;
        oss << R"({"type":"Feature","geometry":{"type":"LineString","coordinates":[)"
            << "[" << fromIt->second.longitude << "," << fromIt->second.latitude << "],"
            << "[" << toIt->second.longitude   << "," << toIt->second.latitude   << "]"
            << R"(]},"properties":{"type":"line","fromBus":)" << br.fromBus
            << R"(,"toBus":)" << br.toBus
            << R"(,"r_pu":)" << br.r_pu
            << R"(,"x_pu":)" << br.x_pu
            << R"(,"rateA_MVA":)" << br.rateA_MVA
            << "}}";
    }
    return oss.str();
}

std::string GeoJsonExporter::exportFeatureCollection(const PowerSystemData& data,
                                                       CancellationToken& token) {
    std::ostringstream oss;
    oss << R"({"type":"FeatureCollection","name":"POWSYS365","features":[)" << "\n";
    oss << exportBusFeatures(data, token);
    if (token.isCancelled()) return oss.str();
    oss << ",\n" << exportBranchFeatures(data, token);
    oss << "\n]}\n";
    return oss.str();
}

ExportResult GeoJsonExporter::save(const std::string& path,
                                    const PowerSystemData& data,
                                    CancellationToken& token) {
    auto t0 = std::chrono::steady_clock::now();
    ExportResult result;
    result.outputPath = path;

    auto preErrs = validate(data);
    for (const auto& e : preErrs) result.errors.push_back(e);

    std::string content = exportFeatureCollection(data, token);
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

} // namespace powsys365::io
