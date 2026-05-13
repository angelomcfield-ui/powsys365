#pragma once

#include "../base_importer.h"
#include "../import_types.h"
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_set>

namespace powsys365::io {

// ---------------------------------------------------------------------------
// PSS/E RAW parser – supports fixed-width (v29-v33) and CSV variants
// ---------------------------------------------------------------------------

class PsseRawParser : public BaseFileImporter {
public:
    PsseRawParser() = default;
    ~PsseRawParser() override = default;

    // ---- BaseFileImporter interface ---------------------------------------

    ImportResult load(const std::string& path, CancellationToken& token) override;
    std::vector<ImportError> validate(const std::string& path) override;
    FileInfo getInfo() const override;
    std::vector<std::string> supportedExtensions() const override {
        return {".raw", ".rawx", ".psse", ".siemens"};
    }

    // ---- PSS/E specific configuration -------------------------------------

    void setRevision(int rev) { revision_ = rev; }   // 29, 30, 31, 32, 33
    int  revision() const noexcept { return revision_; }

    void setCsvMode(bool csv) { csvMode_ = csv; }
    bool csvMode() const noexcept { return csvMode_; }

    void setEncoding(const std::string& enc) { encoding_ = enc; }

private:
    int revision_ = 33;          // default to latest known
    bool csvMode_ = false;
    std::string encoding_ = "UTF-8";

    // ---- parsing state ----------------------------------------------------
    struct ParseContext {
        std::vector<ImportError> errors;
        std::shared_ptr<PowerSystemData> data;
        std::size_t lineNum = 0;
        std::string currentSection;
        int revision = 33;
        bool csvMode = false;
        CancellationToken* token = nullptr;

        void addError(Severity sev, const std::string& code,
                      const std::string& msg, std::size_t col = 0) {
            errors.push_back({sev, code, msg, currentSection, lineNum, col});
        }
        bool isCancelled() const { return token && token->isCancelled(); }
    };

    // ---- section parsers --------------------------------------------------
    void parseCaseIdentification(ParseContext& ctx, const std::string& line);
    void parseBusData(ParseContext& ctx, std::ifstream& ifs);
    void parseLoadData(ParseContext& ctx, std::ifstream& ifs);
    void parseFixedShuntData(ParseContext& ctx, std::ifstream& ifs);
    void parseGeneratorData(ParseContext& ctx, std::ifstream& ifs);
    void parseBranchData(ParseContext& ctx, std::ifstream& ifs);
    void parseTransformerData(ParseContext& ctx, std::ifstream& ifs);
    void parseAreaInterchangeData(ParseContext& ctx, std::ifstream& ifs);
    void parseTwoTerminalDCLine(ParseContext& ctx, std::ifstream& ifs);
    void parseVSCDCLine(ParseContext& ctx, std::ifstream& ifs);
    void parseSwitchedShuntData(ParseContext& ctx, std::ifstream& ifs);
    void parseImpedanceCorrection(ParseContext& ctx, std::ifstream& ifs);
    void parseMultiTerminalDCLine(ParseContext& ctx, std::ifstream& ifs);
    void parseMultiSectionLine(ParseContext& ctx, std::ifstream& ifs);
    void parseZoneData(ParseContext& ctx, std::ifstream& ifs);
    void parseInterareaTransfer(ParseContext& ctx, std::ifstream& ifs);
    void parseOwnerData(ParseContext& ctx, std::ifstream& ifs);
    void parseFACTSDevice(ParseContext& ctx, std::ifstream& ifs);

    // ---- inline line parsers ----------------------------------------------
    void parseBusLine(ParseContext& ctx, const std::vector<std::string>& tok);
    void parseLoadLine(ParseContext& ctx, const std::vector<std::string>& tok);
    void parseShuntLine(ParseContext& ctx, const std::vector<std::string>& tok);
    void parseGenLine(ParseContext& ctx, const std::vector<std::string>& tok);
    void parseBranchLine(ParseContext& ctx, const std::vector<std::string>& tok);
    void parseTransformerMultiLine(ParseContext& ctx,
                                   const std::vector<std::string>& firstTok,
                                   std::ifstream& ifs);

    // ---- utility ----------------------------------------------------------
    std::vector<std::string> tokenize(const std::string& line, bool csv);
    bool isEndOfSection(const std::string& line) const;
    std::string trimComment(const std::string& line) const;
};

// ---------------------------------------------------------------------------
// Convenience alias
// ---------------------------------------------------------------------------
using PSSEParser = PsseRawParser;

} // namespace powsys365::io
