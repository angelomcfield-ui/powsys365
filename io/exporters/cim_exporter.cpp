#include "cim_exporter.h"
#include <chrono>
#include <cctype>
#include <set>

namespace {
    std::string toLowerCim(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        return s;
    }
}

namespace powsys365::io {

// ============================================================================
// Info & Validation
// ============================================================================

FileInfo CimExporter::getInfo() const {
    FileInfo info;
    info.formatName = "CIM/CGMES";
    info.extensions = supportedExtensions();
    info.properties = {
        {"standard",    "IEC 61970/61968"},
        {"profile",     profile_},
        {"description", "Common Information Model export"}
    };
    return info;
}

std::vector<ImportError> CimExporter::validate(const PowerSystemData& data) {
    std::vector<ImportError> errs;
    if (data.buses.empty()) {
        errs.push_back({Severity::Fatal, "NO_BUSES",
            "CIM export requires at least one bus"});
    }
    for (const auto& b : data.buses) {
        if (b.baseVoltage_kV <= 0) {
            errs.push_back({Severity::Warning, "ZERO_BASEKV",
                "Bus " + b.name + " has non-positive base voltage"});
        }
    }
    return errs;
}

// ============================================================================
// XML utilities
// ============================================================================

std::string CimExporter::generateRdfId(const std::string& type, int localId) {
    return "urn:uuid:powsys365-" + toLowerCim(type) + "-" + std::to_string(localId);
}

std::string CimExporter::escapeXml(const std::string& s) {
    std::string out;
    out.reserve(s.size() * 2);
    for (char c : s) {
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:   out += c;        break;
        }
    }
    return out;
}

std::string CimExporter::exportHeader() {
    std::ostringstream oss;
    oss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\"\n"
        << "         xmlns:cim=\"" << cimNs_ << "\"\n"
        << "         xmlns:md=\"http://iec.ch/TC57/61970-552/ModelDescription/1#\">\n"
        << "  <md:FullModel rdf:about=\"urn:uuid:powsys365-model-1\">\n";

    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    gmtime_r(&t, &tm);
    oss << "    <md:Model.created>";
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    oss << "</md:Model.created>\n";

    oss << "    <md:Model.scenarioTime>2024-01-01T00:00:00Z</md:Model.scenarioTime>\n"
        << "    <md:Model.description>POWSYS365 CIM Export</md:Model.description>\n"
        << "    <md:Model.modelingAuthoritySet>urn:powsys365</md:Model.modelingAuthoritySet>\n"
        << "    <md:Model.profile>http://iec.ch/TC57/61970-401/EquipmentCore/4</md:Model.profile>\n"
        << "    <md:Model.version>1</md:Model.version>\n"
        << "  </md:FullModel>\n";
    return oss.str();
}

std::string CimExporter::exportFooter() {
    return "</rdf:RDF>\n";
}

// ============================================================================
// Save
// ============================================================================

ExportResult CimExporter::save(const std::string& path,
                                const PowerSystemData& data,
                                CancellationToken& token) {
    auto t0 = std::chrono::steady_clock::now();
    ExportResult result;
    result.outputPath = path;

    auto preErrs = validate(data);
    for (const auto& e : preErrs) result.errors.push_back(e);
    bool hasFatal = false;
    for (const auto& e : preErrs) if (e.severity == Severity::Fatal) hasFatal = true;
    if (hasFatal) {
        result.status = ImportStatus::Error;
        result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0);
        return result;
    }

    idCounter_ = 1;
    std::ostringstream oss;

    oss << exportHeader();

    oss << exportBaseVoltages(data);
    if (token.isCancelled()) { result.status = ImportStatus::Cancelled; return result; }

    oss << exportSubstations(data);
    if (token.isCancelled()) { result.status = ImportStatus::Cancelled; return result; }

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

    oss << exportTransformers(data, token);
    if (token.isCancelled()) { result.status = ImportStatus::Cancelled; return result; }

    oss << exportFooter();

    std::string content = oss.str();
    bool ok = writeStringToFile(path, content);
    if (!ok) {
        result.status = ImportStatus::Error;
        result.errors.push_back({Severity::Fatal, "WRITE_ERROR",
            "Failed to write: " + path});
        return result;
    }

    result.bytesWritten = content.size();
    result.status = result.errors.empty() ? ImportStatus::Success : ImportStatus::Warning;
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);
    return result;
}

// ============================================================================
// Base Voltages
// ============================================================================

std::string CimExporter::exportBaseVoltages(const PowerSystemData& data) {
    std::ostringstream oss;
    std::set<double> voltages;
    for (const auto& b : data.buses) {
        if (b.baseVoltage_kV > 0) voltages.insert(b.baseVoltage_kV);
    }

    for (double v : voltages) {
        std::string id = generateRdfId("BaseVoltage", idCounter_++);
        oss << "  <cim:BaseVoltage rdf:about=\"" << id << "\">\n"
            << "    <cim:IdentifiedObject.name>BV_" << v << "kV</cim:IdentifiedObject.name>\n"
            << "    <cim:BaseVoltage.nominalVoltage>" << v << "</cim:BaseVoltage.nominalVoltage>\n"
            << "  </cim:BaseVoltage>\n";
    }
    return oss.str();
}

// ============================================================================
// Substations
// ============================================================================

std::string CimExporter::exportSubstations(const PowerSystemData& data) {
    std::ostringstream oss;
    std::set<int> areas;
    for (const auto& b : data.buses) areas.insert(b.area);

    for (int area : areas) {
        std::string id = generateRdfId("Substation", idCounter_++);
        oss << "  <cim:Substation rdf:about=\"" << id << "\">\n"
            << "    <cim:IdentifiedObject.name>Sub_" << area << "</cim:IdentifiedObject.name>\n"
            << "  </cim:Substation>\n";
    }
    return oss.str();
}

// ============================================================================
// Buses (TopologicalNode / BusbarSection)
// ============================================================================

std::string CimExporter::exportBuses(const PowerSystemData& data, CancellationToken& token) {
    std::ostringstream oss;
    reportProgress("exporting_buses", 0, data.buses.size(), "");
    for (std::size_t i = 0; i < data.buses.size(); ++i) {
        if (token.isCancelled()) break;
        if (i % 100 == 0) reportProgress("exporting_buses", i, data.buses.size(), "");

        const auto& b = data.buses[i];
        std::string busId = generateRdfId("BusbarSection", static_cast<int>(b.id));
        std::string topoId = generateRdfId("TopologicalNode", static_cast<int>(b.id) + 100000);
        std::string bvId = generateRdfId("BaseVoltage", 1); // simplified

        // TopologicalNode
        oss << "  <cim:TopologicalNode rdf:about=\"" << topoId << "\">\n"
            << "    <cim:IdentifiedObject.name>" << escapeXml(b.name) << "</cim:IdentifiedObject.name>\n"
            << "    <cim:TopologicalNode.BaseVoltage rdf:resource=\"" << bvId << "\"/>\n"
            << "  </cim:TopologicalNode>\n";

        // BusbarSection
        oss << "  <cim:BusbarSection rdf:about=\"" << busId << "\">\n"
            << "    <cim:IdentifiedObject.name>BB_" << escapeXml(b.name) << "</cim:IdentifiedObject.name>\n"
            << "    <cim:Equipment.EquipmentContainer rdf:resource=\"" << topoId << "\"/>\n"
            << "  </cim:BusbarSection>\n";
    }
    return oss.str();
}

// ============================================================================
// Generators (SynchronousMachine)
// ============================================================================

std::string CimExporter::exportGenerators(const PowerSystemData& data, CancellationToken& token) {
    std::ostringstream oss;
    reportProgress("exporting_gens", 0, data.generators.size(), "");
    for (std::size_t i = 0; i < data.generators.size(); ++i) {
        if (token.isCancelled()) break;
        if (i % 100 == 0) reportProgress("exporting_gens", i, data.generators.size(), "");

        const auto& g = data.generators[i];
        std::string genId = generateRdfId("SynchronousMachine", idCounter_++);

        oss << "  <cim:SynchronousMachine rdf:about=\"" << genId << "\">\n"
            << "    <cim:IdentifiedObject.name>GEN_" << g.busId << "_" << escapeXml(g.id) << "</cim:IdentifiedObject.name>\n"
            << "    <cim:RotatingMachine.ratedS>" << g.mBase_MVA << "</cim:RotatingMachine.ratedS>\n"
            << "    <cim:RegulatingControl.targetValue>" << g.vSet_pu << "</cim:RegulatingControl.targetValue>\n"
            << "    <cim:SynchronousMachine.p>" << -g.pGen_MW << "</cim:SynchronousMachine.p>\n"
            << "    <cim:SynchronousMachine.q>" << -g.qGen_Mvar << "</cim:SynchronousMachine.q>\n"
            << "    <cim:SynchronousMachine.type>generator</cim:SynchronousMachine.type>\n"
            << "  </cim:SynchronousMachine>\n";
    }
    return oss.str();
}

// ============================================================================
// Loads (EnergyConsumer)
// ============================================================================

std::string CimExporter::exportLoads(const PowerSystemData& data, CancellationToken& token) {
    std::ostringstream oss;
    reportProgress("exporting_loads", 0, data.loads.size(), "");
    for (std::size_t i = 0; i < data.loads.size(); ++i) {
        if (token.isCancelled()) break;
        if (i % 100 == 0) reportProgress("exporting_loads", i, data.loads.size(), "");

        const auto& ld = data.loads[i];
        std::string loadId = generateRdfId("EnergyConsumer", idCounter_++);

        oss << "  <cim:EnergyConsumer rdf:about=\"" << loadId << "\">\n"
            << "    <cim:IdentifiedObject.name>LOAD_" << ld.busId << "_" << escapeXml(ld.id) << "</cim:IdentifiedObject.name>\n"
            << "    <cim:EnergyConsumer.p>" << ld.pLoad_MW << "</cim:EnergyConsumer.p>\n"
            << "    <cim:EnergyConsumer.q>" << ld.qLoad_Mvar << "</cim:EnergyConsumer.q>\n"
            << "  </cim:EnergyConsumer>\n";
    }
    return oss.str();
}

// ============================================================================
// Shunts (LinearShuntCompensator)
// ============================================================================

std::string CimExporter::exportShunts(const PowerSystemData& data, CancellationToken& token) {
    std::ostringstream oss;
    reportProgress("exporting_shunts", 0, data.shunts.size(), "");
    for (std::size_t i = 0; i < data.shunts.size(); ++i) {
        if (token.isCancelled()) break;
        if (i % 100 == 0) reportProgress("exporting_shunts", i, data.shunts.size(), "");

        const auto& s = data.shunts[i];
        std::string shuntId = generateRdfId("LinearShuntCompensator", idCounter_++);

        oss << "  <cim:LinearShuntCompensator rdf:about=\"" << shuntId << "\">\n"
            << "    <cim:IdentifiedObject.name>SH_" << s.busId << "_" << escapeXml(s.id) << "</cim:IdentifiedObject.name>\n"
            << "    <cim:LinearShuntCompensator.bPerSection>" << s.b_Mvar << "</cim:LinearShuntCompensator.bPerSection>\n"
            << "    <cim:LinearShuntCompensator.gPerSection>" << s.g_MW << "</cim:LinearShuntCompensator.gPerSection>\n"
            << "    <cim:ShuntCompensator.sections>1</cim:ShuntCompensator.sections>\n"
            << "  </cim:LinearShuntCompensator>\n";
    }
    return oss.str();
}

// ============================================================================
// Branches (ACLineSegment)
// ============================================================================

std::string CimExporter::exportBranches(const PowerSystemData& data, CancellationToken& token) {
    std::ostringstream oss;
    reportProgress("exporting_branches", 0, data.branches.size(), "");
    for (std::size_t i = 0; i < data.branches.size(); ++i) {
        if (token.isCancelled()) break;
        if (i % 100 == 0) reportProgress("exporting_branches", i, data.branches.size(), "");

        const auto& br = data.branches[i];
        std::string lineId = generateRdfId("ACLineSegment", idCounter_++);
        std::string bvId = generateRdfId("BaseVoltage", 1);

        oss << "  <cim:ACLineSegment rdf:about=\"" << lineId << "\">\n"
            << "    <cim:IdentifiedObject.name>LINE_" << br.fromBus << "_" << br.toBus << "_" << escapeXml(br.circuitId) << "</cim:IdentifiedObject.name>\n"
            << "    <cim:ACLineSegment.r>" << br.r_pu << "</cim:ACLineSegment.r>\n"
            << "    <cim:ACLineSegment.x>" << br.x_pu << "</cim:ACLineSegment.x>\n"
            << "    <cim:ACLineSegment.bch>" << br.b_pu << "</cim:ACLineSegment.bch>\n"
            << "    <cim:ACLineSegment.length>" << br.length_km << "</cim:ACLineSegment.length>\n"
            << "    <cim:ConductingEquipment.BaseVoltage rdf:resource=\"" << bvId << "\"/>\n"
            << "  </cim:ACLineSegment>\n";
    }
    return oss.str();
}

// ============================================================================
// Transformers (PowerTransformer + PowerTransformerEnd)
// ============================================================================

std::string CimExporter::exportTransformers(const PowerSystemData& data, CancellationToken& token) {
    std::ostringstream oss;
    reportProgress("exporting_xfmrs", 0, data.transformers.size(), "");
    for (std::size_t i = 0; i < data.transformers.size(); ++i) {
        if (token.isCancelled()) break;
        if (i % 100 == 0) reportProgress("exporting_xfmrs", i, data.transformers.size(), "");

        const auto& t = data.transformers[i];
        std::string xfmrId = generateRdfId("PowerTransformer", idCounter_++);

        oss << "  <cim:PowerTransformer rdf:about=\"" << xfmrId << "\">\n"
            << "    <cim:IdentifiedObject.name>XFMR_" << t.fromBus << "_" << t.toBus << "_" << escapeXml(t.circuitId) << "</cim:IdentifiedObject.name>\n"
            << "  </cim:PowerTransformer>\n";

        // Winding 1
        std::string end1Id = generateRdfId("PowerTransformerEnd", idCounter_++);
        oss << "  <cim:PowerTransformerEnd rdf:about=\"" << end1Id << "\">\n"
            << "    <cim:IdentifiedObject.name>XFMR_W1</cim:IdentifiedObject.name>\n"
            << "    <cim:PowerTransformerEnd.r>" << t.r12_pu << "</cim:PowerTransformerEnd.r>\n"
            << "    <cim:PowerTransformerEnd.x>" << t.x12_pu << "</cim:PowerTransformerEnd.x>\n"
            << "    <cim:PowerTransformerEnd.ratedS>" << t.rateA_MVA << "</cim:PowerTransformerEnd.ratedS>\n"
            << "    <cim:PowerTransformerEnd.ratedU>" << t.windV1_kV << "</cim:PowerTransformerEnd.ratedU>\n"
            << "    <cim:TransformerEnd.endNumber>1</cim:TransformerEnd.endNumber>\n"
            << "    <cim:PowerTransformerEnd.PowerTransformer rdf:resource=\"" << xfmrId << "\"/>\n"
            << "  </cim:PowerTransformerEnd>\n";

        // Winding 2
        std::string end2Id = generateRdfId("PowerTransformerEnd", idCounter_++);
        oss << "  <cim:PowerTransformerEnd rdf:about=\"" << end2Id << "\">\n"
            << "    <cim:IdentifiedObject.name>XFMR_W2</cim:IdentifiedObject.name>\n"
            << "    <cim:PowerTransformerEnd.ratedU>" << t.windV2_kV << "</cim:PowerTransformerEnd.ratedU>\n"
            << "    <cim:TransformerEnd.endNumber>2</cim:TransformerEnd.endNumber>\n"
            << "    <cim:PowerTransformerEnd.PowerTransformer rdf:resource=\"" << xfmrId << "\"/>\n"
            << "  </cim:PowerTransformerEnd>\n";

        // Winding 3 (if 3-winding)
        if (t.tertBus != 0) {
            std::string end3Id = generateRdfId("PowerTransformerEnd", idCounter_++);
            oss << "  <cim:PowerTransformerEnd rdf:about=\"" << end3Id << "\">\n"
                << "    <cim:IdentifiedObject.name>XFMR_W3</cim:IdentifiedObject.name>\n"
                << "    <cim:PowerTransformerEnd.r>" << t.r23_pu << "</cim:PowerTransformerEnd.r>\n"
                << "    <cim:PowerTransformerEnd.x>" << t.x23_pu << "</cim:PowerTransformerEnd.x>\n"
                << "    <cim:PowerTransformerEnd.ratedU>" << t.windV3_kV << "</cim:PowerTransformerEnd.ratedU>\n"
                << "    <cim:TransformerEnd.endNumber>3</cim:TransformerEnd.endNumber>\n"
                << "    <cim:PowerTransformerEnd.PowerTransformer rdf:resource=\"" << xfmrId << "\"/>\n"
                << "  </cim:PowerTransformerEnd>\n";
        }
    }
    return oss.str();
}

} // namespace powsys365::io
