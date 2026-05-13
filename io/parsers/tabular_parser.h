#pragma once

#include "../base_importer.h"
#include "../import_types.h"
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <regex>
#include <stack>

namespace powsys365::io {

// ============================================================================
// CSV Parser
// ============================================================================

class CsvParser : public BaseFileImporter {
public:
    CsvParser() = default;
    ~CsvParser() override = default;

    ImportResult load(const std::string& path, CancellationToken& token) override;
    std::vector<ImportError> validate(const std::string& path) override;
    FileInfo getInfo() const override;
    std::vector<std::string> supportedExtensions() const override { return {".csv"}; }

    void setDelimiter(char d) { delimiter_ = d; }
    void setHasHeader(bool h) { hasHeader_ = h; }
    void setColumnMapping(const std::map<std::string, std::string>& mapping) {
        columnMapping_ = mapping;
    }

private:
    char delimiter_ = ',';
    bool hasHeader_ = true;
    std::map<std::string, std::string> columnMapping_;

    struct ParseContext {
        std::vector<ImportError> errors;
        std::shared_ptr<PowerSystemData> data;
        std::size_t lineNum = 0;
        std::string currentFile;
        CancellationToken* token = nullptr;
        std::vector<std::string> headers;

        void addError(Severity sev, const std::string& code, const std::string& msg) {
            errors.push_back({sev, code, msg, currentFile, lineNum, 0});
        }
        bool isCancelled() const { return token && token->isCancelled(); }
    };

    std::vector<std::string> parseCsvLine(const std::string& line);
    void parseHeader(ParseContext& ctx, const std::vector<std::string>& fields);
    void parseDataRow(ParseContext& ctx, const std::vector<std::string>& fields);
    std::string getField(const ParseContext& ctx,
                         const std::vector<std::string>& fields,
                         const std::string& colName);
};

// ============================================================================
// XLSX Parser (simplified – reads shared strings & sheet XML)
// ============================================================================

class XlsxParser : public BaseFileImporter {
public:
    XlsxParser() = default;
    ~XlsxParser() override = default;

    ImportResult load(const std::string& path, CancellationToken& token) override;
    std::vector<ImportError> validate(const std::string& path) override;
    FileInfo getInfo() const override;
    std::vector<std::string> supportedExtensions() const override { return {".xlsx"}; }

private:
    struct ParseContext {
        std::vector<ImportError> errors;
        std::shared_ptr<PowerSystemData> data;
        std::size_t rowNum = 0;
        std::string currentFile;
        CancellationToken* token = nullptr;

        void addError(Severity sev, const std::string& code, const std::string& msg) {
            errors.push_back({sev, code, msg, currentFile, rowNum, 0});
        }
        bool isCancelled() const { return token && token->isCancelled(); }
    };

    // Extract sheet XML from XLSX (ZIP)
    std::string extractSheetXml(const std::string& path,
                                 std::vector<std::string>& sharedStrings,
                                 std::vector<ImportError>& errs);

    // Parse shared strings
    std::vector<std::string> parseSharedStrings(const std::string& xml);

    // Parse sheet cells
    std::vector<std::vector<std::string>> parseSheetCells(const std::string& xml,
                                                           const std::vector<std::string>& ss);

    void processRows(ParseContext& ctx,
                     const std::vector<std::vector<std::string>>& rows);
};

// ============================================================================
// JSON Parser (generic power system data)
// ============================================================================

class JsonParser : public BaseFileImporter {
public:
    JsonParser() = default;
    ~JsonParser() override = default;

    ImportResult load(const std::string& path, CancellationToken& token) override;
    std::vector<ImportError> validate(const std::string& path) override;
    FileInfo getInfo() const override;
    std::vector<std::string> supportedExtensions() const override { return {".json"}; }

private:
    struct ParseContext {
        std::vector<ImportError> errors;
        std::shared_ptr<PowerSystemData> data;
        std::size_t lineNum = 0;
        std::string currentFile;
        CancellationToken* token = nullptr;

        void addError(Severity sev, const std::string& code, const std::string& msg) {
            errors.push_back({sev, code, msg, currentFile, lineNum, 0});
        }
        bool isCancelled() const { return token && token->isCancelled(); }
    };

    void parseBusArray(ParseContext& ctx, const std::string& json);
    void parseBranchArray(ParseContext& ctx, const std::string& json);
    void parseTransformerArray(ParseContext& ctx, const std::string& json);
    void parseGeneratorArray(ParseContext& ctx, const std::string& json);
    void parseLoadArray(ParseContext& ctx, const std::string& json);
    void parseShuntArray(ParseContext& ctx, const std::string& json);

    std::vector<std::string> extractJsonArrayItems(const std::string& arrJson);
    std::string extractJsonValue(const std::string& json, const std::string& key);
    double extractJsonDouble(const std::string& json, const std::string& key, double def = 0.0);
    int64_t extractJsonInt64(const std::string& json, const std::string& key, int64_t def = 0);
    std::string extractJsonString(const std::string& json, const std::string& key);
};

// ============================================================================
// XML Parser (generic power system data)
// ============================================================================

class XmlParser : public BaseFileImporter {
public:
    XmlParser() = default;
    ~XmlParser() override = default;

    ImportResult load(const std::string& path, CancellationToken& token) override;
    std::vector<ImportError> validate(const std::string& path) override;
    FileInfo getInfo() const override;
    std::vector<std::string> supportedExtensions() const override { return {".xml"}; }

private:
    struct ParseContext {
        std::vector<ImportError> errors;
        std::shared_ptr<PowerSystemData> data;
        std::size_t lineNum = 0;
        std::string currentFile;
        CancellationToken* token = nullptr;

        void addError(Severity sev, const std::string& code, const std::string& msg) {
            errors.push_back({sev, code, msg, currentFile, lineNum, 0});
        }
        bool isCancelled() const { return token && token->isCancelled(); }
    };

    struct XmlElement {
        std::string name;
        std::string text;
        std::map<std::string, std::string> attrs;
    };

    std::vector<XmlElement> parseXmlElements(const std::string& content);
    void processElement(ParseContext& ctx, const XmlElement& el);

    std::string extractXmlAttr(const std::map<std::string, std::string>& attrs,
                                const std::string& key,
                                const std::string& def = "");
    double extractXmlDouble(const std::map<std::string, std::string>& attrs,
                             const std::string& key, double def = 0.0);
    int64_t extractXmlInt64(const std::map<std::string, std::string>& attrs,
                              const std::string& key, int64_t def = 0);
};

// ============================================================================
// Combined TabularImporter
// ============================================================================

class TabularImporter : public BaseFileImporter {
public:
    TabularImporter() {
        csv_  = std::make_unique<CsvParser>();
        xlsx_ = std::make_unique<XlsxParser>();
        json_ = std::make_unique<JsonParser>();
        xml_  = std::make_unique<XmlParser>();
    }
    ~TabularImporter() override = default;

    ImportResult load(const std::string& path, CancellationToken& token) override {
        auto ext = FileInfo::lowerExtension(path);
        if (ext == ".csv")      return csv_->load(path, token);
        if (ext == ".xlsx")     return xlsx_->load(path, token);
        if (ext == ".json")     return json_->load(path, token);
        if (ext == ".xml")      return xml_->load(path, token);
        ImportResult r;
        r.status = ImportStatus::Error;
        r.errors.push_back({Severity::Fatal, "BAD_EXT",
            "Unknown tabular extension: " + ext, path, 0, 0});
        return r;
    }

    std::vector<ImportError> validate(const std::string& path) override {
        auto ext = FileInfo::lowerExtension(path);
        if (ext == ".csv")      return csv_->validate(path);
        if (ext == ".xlsx")     return xlsx_->validate(path);
        if (ext == ".json")     return json_->validate(path);
        if (ext == ".xml")      return xml_->validate(path);
        return {{Severity::Fatal, "BAD_EXT",
            "Unknown tabular extension: " + ext, path, 0, 0}};
    }

    FileInfo getInfo() const override {
        FileInfo info;
        info.formatName = "Tabular Data (CSV/XLSX/JSON/XML)";
        info.extensions = supportedExtensions();
        info.properties = {{"description", "Combined tabular format importer"}};
        return info;
    }

    std::vector<std::string> supportedExtensions() const override {
        return {".csv", ".xlsx", ".json", ".xml"};
    }

private:
    std::unique_ptr<CsvParser>   csv_;
    std::unique_ptr<XlsxParser>  xlsx_;
    std::unique_ptr<JsonParser>  json_;
    std::unique_ptr<XmlParser>   xml_;
};

} // namespace powsys365::io
