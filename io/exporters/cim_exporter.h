#pragma once

#include "../base_exporter.h"
#include "../import_types.h"
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <map>
#include <chrono>

namespace powsys365::io {

// ============================================================================
// CIM/CGMES Exporter
// ============================================================================

class CimExporter : public BaseFileExporter {
public:
    CimExporter() = default;
    ~CimExporter() override = default;

    ExportResult save(const std::string& path,
                      const PowerSystemData& data,
                      CancellationToken& token) override;
    std::vector<ImportError> validate(const PowerSystemData& data) override;
    FileInfo getInfo() const override;
    std::vector<std::string> supportedExtensions() const override {
        return {".xml", ".rdf", ".eq", ".tp", ".sv", ".ssh"};
    }

    void setProfile(const std::string& p) { profile_ = p; }
    std::string profile() const { return profile_; }

private:
    std::string profile_ = "EQ";
    std::string baseNs_ = "http://iec.ch/TC57/CIM100#";
    std::string rdfNs_ = "http://www.w3.org/1999/02/22-rdf-syntax-ns#";
    std::string cimNs_ = "http://iec.ch/TC57/2013/CIM-schema-cim16#";
    int idCounter_ = 1;

    std::string generateRdfId(const std::string& type, int localId);
    std::string escapeXml(const std::string& s);

    std::string exportHeader();
    std::string exportFooter();

    std::string exportBuses(const PowerSystemData& data, CancellationToken& token);
    std::string exportBranches(const PowerSystemData& data, CancellationToken& token);
    std::string exportTransformers(const PowerSystemData& data, CancellationToken& token);
    std::string exportGenerators(const PowerSystemData& data, CancellationToken& token);
    std::string exportLoads(const PowerSystemData& data, CancellationToken& token);
    std::string exportShunts(const PowerSystemData& data, CancellationToken& token);
    std::string exportBaseVoltages(const PowerSystemData& data);
    std::string exportSubstations(const PowerSystemData& data);
};

} // namespace powsys365::io
