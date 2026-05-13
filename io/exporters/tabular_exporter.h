#pragma once

#include "../base_exporter.h"
#include "../import_types.h"
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <map>

namespace powsys365::io {

// ============================================================================
// CSV Exporter
// ============================================================================

class CsvExporter : public BaseFileExporter {
public:
    CsvExporter() = default;
    ~CsvExporter() override = default;

    ExportResult save(const std::string& path,
                      const PowerSystemData& data,
                      CancellationToken& token) override;
    std::vector<ImportError> validate(const PowerSystemData& data) override;
    FileInfo getInfo() const override;
    std::vector<std::string> supportedExtensions() const override { return {".csv"}; }

    void setDelimiter(char d) { delimiter_ = d; }
    void setIncludeHeader(bool h) { includeHeader_ = h; }

private:
    char delimiter_ = ',';
    bool includeHeader_ = true;

    std::string escapeCsv(const std::string& s);
    std::string exportBuses(const PowerSystemData& data, CancellationToken& token);
    std::string exportBranches(const PowerSystemData& data, CancellationToken& token);
    std::string exportTransformers(const PowerSystemData& data, CancellationToken& token);
    std::string exportGenerators(const PowerSystemData& data, CancellationToken& token);
    std::string exportLoads(const PowerSystemData& data, CancellationToken& token);
    std::string exportShunts(const PowerSystemData& data, CancellationToken& token);
};

// ============================================================================
// JSON Exporter
// ============================================================================

class JsonExporter : public BaseFileExporter {
public:
    JsonExporter() = default;
    ~JsonExporter() override = default;

    ExportResult save(const std::string& path,
                      const PowerSystemData& data,
                      CancellationToken& token) override;
    std::vector<ImportError> validate(const PowerSystemData& data) override;
    FileInfo getInfo() const override;
    std::vector<std::string> supportedExtensions() const override { return {".json"}; }

    void setPrettyPrint(bool p) { prettyPrint_ = p; }

private:
    bool prettyPrint_ = true;

    std::string escapeJson(const std::string& s);
    std::string indent(int level);
    std::string exportObject(const PowerSystemData& data, CancellationToken& token);
};

// ============================================================================
// XML Exporter
// ============================================================================

class XmlExporter : public BaseFileExporter {
public:
    XmlExporter() = default;
    ~XmlExporter() override = default;

    ExportResult save(const std::string& path,
                      const PowerSystemData& data,
                      CancellationToken& token) override;
    std::vector<ImportError> validate(const PowerSystemData& data) override;
    FileInfo getInfo() const override;
    std::vector<std::string> supportedExtensions() const override { return {".xml"}; }

private:
    std::string escapeXml(const std::string& s);
    std::string exportDocument(const PowerSystemData& data, CancellationToken& token);
};

// ============================================================================
// Excel Exporter (CSV with .xlsx wrapper)
// ============================================================================

class XlsxExporter : public BaseFileExporter {
public:
    XlsxExporter() = default;
    ~XlsxExporter() override = default;

    ExportResult save(const std::string& path,
                      const PowerSystemData& data,
                      CancellationToken& token) override;
    std::vector<ImportError> validate(const PowerSystemData& data) override;
    FileInfo getInfo() const override;
    std::vector<std::string> supportedExtensions() const override { return {".xlsx"}; }

private:
    // Produces a simple XLSX by delegating to CsvExporter and wrapping in minimal ZIP structure
    std::string buildMinimalXlsx(const std::string& csvContent);
};

// ============================================================================
// Combined TabularExporter
// ============================================================================

class TabularExporter : public BaseFileExporter {
public:
    TabularExporter() {
        csv_   = std::make_unique<CsvExporter>();
        json_  = std::make_unique<JsonExporter>();
        xml_   = std::make_unique<XmlExporter>();
        xlsx_  = std::make_unique<XlsxExporter>();
    }
    ~TabularExporter() override = default;

    ExportResult save(const std::string& path,
                      const PowerSystemData& data,
                      CancellationToken& token) override {
        auto ext = FileInfo::lowerExtension(path);
        if (ext == ".csv")   return csv_->save(path, data, token);
        if (ext == ".json")  return json_->save(path, data, token);
        if (ext == ".xml")   return xml_->save(path, data, token);
        if (ext == ".xlsx")  return xlsx_->save(path, data, token);
        ExportResult r;
        r.status = ImportStatus::Error;
        r.errors.push_back({Severity::Fatal, "BAD_EXT",
            "Unknown tabular extension: " + ext, path, 0, 0});
        return r;
    }

    std::vector<ImportError> validate(const PowerSystemData& data) override {
        return csv_->validate(data);
    }

    FileInfo getInfo() const override {
        FileInfo info;
        info.formatName = "Tabular Data (CSV/XLSX/JSON/XML)";
        info.extensions = supportedExtensions();
        info.properties = {{"description", "Combined tabular format exporter"}};
        return info;
    }

    std::vector<std::string> supportedExtensions() const override {
        return {".csv", ".xlsx", ".json", ".xml"};
    }

private:
    std::unique_ptr<CsvExporter>   csv_;
    std::unique_ptr<JsonExporter>  json_;
    std::unique_ptr<XmlExporter>   xml_;
    std::unique_ptr<XlsxExporter>  xlsx_;
};

} // namespace powsys365::io
