#pragma once

#include "../base_exporter.h"
#include "../import_types.h"
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <fstream>

namespace powsys365::io {

// ============================================================================
// PSS/E RAW Exporter
// ============================================================================

class PsseRawExporter : public BaseFileExporter {
public:
    PsseRawExporter() = default;
    ~PsseRawExporter() override = default;

    ExportResult save(const std::string& path,
                      const PowerSystemData& data,
                      CancellationToken& token) override;
    std::vector<ImportError> validate(const PowerSystemData& data) override;
    FileInfo getInfo() const override;
    std::vector<std::string> supportedExtensions() const override {
        return {".raw", ".rawx", ".psse"};
    }

    void setRevision(int rev) { revision_ = rev; }
    int revision() const noexcept { return revision_; }

    void setCsvMode(bool csv) { csvMode_ = csv; }
    bool csvMode() const noexcept { return csvMode_; }

private:
    int revision_ = 33;
    bool csvMode_ = false;

    std::string exportCaseIdentification(const PowerSystemData& data);
    std::string exportBusData(const PowerSystemData& data, CancellationToken& token);
    std::string exportLoadData(const PowerSystemData& data, CancellationToken& token);
    std::string exportFixedShuntData(const PowerSystemData& data, CancellationToken& token);
    std::string exportGeneratorData(const PowerSystemData& data, CancellationToken& token);
    std::string exportBranchData(const PowerSystemData& data, CancellationToken& token);
    std::string exportTransformerData(const PowerSystemData& data, CancellationToken& token);
    std::string exportAreaInterchangeData(const PowerSystemData& data);
    std::string exportTwoTerminalDCLine(const PowerSystemData& data);
    std::string exportVSCDCLine(const PowerSystemData& data);
    std::string exportSwitchedShuntData(const PowerSystemData& data, CancellationToken& token);
    std::string exportImpedanceCorrection(const PowerSystemData& data);
    std::string exportMultiTerminalDCLine(const PowerSystemData& data);
    std::string exportMultiSectionLine(const PowerSystemData& data);
    std::string exportZoneData(const PowerSystemData& data);
    std::string exportInterareaTransfer(const PowerSystemData& data);
    std::string exportOwnerData(const PowerSystemData& data);
    std::string exportFACTSDevice(const PowerSystemData& data);

    // CSV / fixed-width formatting
    std::string fmtField(const std::string& val, std::size_t width);
    std::string fmtDouble(double v, int prec = 6, std::size_t width = 0);
    std::string fmtInt(int64_t v, std::size_t width = 0);
    std::string sep() const { return csvMode_ ? "," : " "; }

    std::string getAttrOrDefault(const std::map<std::string, std::string>& attrs,
                                  const std::string& key,
                                  const std::string& def = "");
};

using PSSEExporter = PsseRawExporter;

} // namespace powsys365::io
