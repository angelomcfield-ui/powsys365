#pragma once

#include "../base_importer.h"
#include "../import_types.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <regex>
#include <stack>

namespace powsys365::io {

// ---------------------------------------------------------------------------
// CIM/CGMES RDF/XML parser
// ---------------------------------------------------------------------------

class CimParser : public BaseFileImporter {
public:
    CimParser() = default;
    ~CimParser() override = default;

    ImportResult load(const std::string& path, CancellationToken& token) override;
    std::vector<ImportError> validate(const std::string& path) override;
    FileInfo getInfo() const override;
    std::vector<std::string> supportedExtensions() const override {
        return {".xml", ".rdf", ".eq", ".tp", ".sv", ".ssh", ".cim"};
    }

    // CGMES profile detection
    enum class CgmesProfile {
        Unknown,
        Equipment,       // EQ – network elements
        Topology,        // TP – node-breaker / bus-branch topology
        StateVariables,  // SV – load flow results
        SteadyStateHypothesis, // SSH – setpoints / status
        GeographicalLocation,  // GL – coordinates
        DiagramLayout,   // DL – schematic positions
        Dynamics,        // DY – dynamic models
        Full
    };

    CgmesProfile detectProfile(const std::string& path) const;
    std::string profileName(CgmesProfile p) const;

private:
    // ---- Lightweight XML/RDF token streaming ------------------------------
    struct XmlToken {
        enum Type { StartElement, EndElement, Text, Attribute, EOF_ } type;
        std::string name;
        std::string value;
        std::string ns;   // namespace prefix
        std::map<std::string, std::string> attrs;
    };

    class XmlStream {
    public:
        explicit XmlStream(const std::string& content);
        XmlToken next();
        bool hasNext() const { return pos_ < content_.size(); }
        std::size_t lineNum() const { return lineNum_; }

    private:
        const std::string& content_;
        std::size_t pos_ = 0;
        std::size_t lineNum_ = 1;

        void skipWhitespace();
        bool match(const std::string& s);
        std::string readName();
        std::string readQuotedString();
        std::string readText();
        std::string readUntil(char c);
    };

    // ---- Parsing context --------------------------------------------------
    struct ParseContext {
        std::vector<ImportError> errors;
        std::shared_ptr<PowerSystemData> data;
        std::size_t lineNum = 0;
        std::string currentFile;
        CgmesProfile profile = CgmesProfile::Unknown;
        CancellationToken* token = nullptr;

        // CIM RDF ID → object mapping
        std::map<std::string, std::map<std::string, std::string>> objects;
        std::map<std::string, std::string> idToType;

        void addError(Severity sev, const std::string& code,
                      const std::string& msg, std::size_t col = 0) {
            errors.push_back({sev, code, msg, currentFile, lineNum, col});
        }
        bool isCancelled() const { return token && token->isCancelled(); }
    };

    // ---- Core parsing -----------------------------------------------------
    void parseRdfDocument(ParseContext& ctx, XmlStream& stream);
    void processObject(ParseContext& ctx,
                       const std::string& rdfId,
                       const std::string& type,
                       const std::map<std::string, std::string>& props,
                       const std::map<std::string, std::string>& refs);

    // ---- CIM object builders ----------------------------------------------
    void buildBus(ParseContext& ctx, const std::string& id,
                  const std::map<std::string, std::string>& props);
    void buildBranch(ParseContext& ctx, const std::string& id,
                     const std::map<std::string, std::string>& props);
    void buildTransformer(ParseContext& ctx, const std::string& id,
                          const std::map<std::string, std::string>& props);
    void buildGenerator(ParseContext& ctx, const std::string& id,
                        const std::map<std::string, std::string>& props);
    void buildLoad(ParseContext& ctx, const std::string& id,
                   const std::map<std::string, std::string>& props);
    void buildShunt(ParseContext& ctx, const std::string& id,
                    const std::map<std::string, std::string>& props);

    // ---- Post-processing --------------------------------------------------
    void resolveTopology(ParseContext& ctx);
    void resolveTerminals(ParseContext& ctx);

    // ---- Utility ----------------------------------------------------------
    static std::string extractLocalName(const std::string& qname);
    static std::string stripHash(const std::string& ref);
    static double getDouble(const std::map<std::string, std::string>& m,
                            const std::string& key, double def = 0.0);
    static int64_t getInt64(const std::map<std::string, std::string>& m,
                             const std::string& key, int64_t def = 0);
};

} // namespace powsys365::io
