#include "cim_parser.h"
#include <chrono>
#include <filesystem>

namespace powsys365::io {

// ============================================================================
// Public interface
// ============================================================================

FileInfo CimParser::getInfo() const {
    FileInfo info;
    info.formatName = "CIM/CGMES";
    info.extensions = supportedExtensions();
    info.encoding   = "UTF-8";
    info.properties = {
        {"standard",     "IEC 61970/61968 (CIM/CGMES)"},
        {"profiles",     "EQ, TP, SV, SSH, GL, DL"},
        {"description",  "Common Information Model for power systems"}
    };
    info.detectedAt = std::chrono::system_clock::now();
    return info;
}

std::string CimParser::profileName(CgmesProfile p) const {
    switch (p) {
        case CgmesProfile::Equipment:       return "EQ (Equipment)";
        case CgmesProfile::Topology:        return "TP (Topology)";
        case CgmesProfile::StateVariables:  return "SV (State Variables)";
        case CgmesProfile::SteadyStateHypothesis: return "SSH (Steady State Hypothesis)";
        case CgmesProfile::GeographicalLocation:  return "GL (Geographical Location)";
        case CgmesProfile::DiagramLayout:   return "DL (Diagram Layout)";
        case CgmesProfile::Dynamics:        return "DY (Dynamics)";
        case CgmesProfile::Full:            return "Full";
        default:                            return "Unknown";
    }
}

CimParser::CgmesProfile CimParser::detectProfile(const std::string& path) const {
    std::ifstream ifs(path);
    if (!ifs) return CgmesProfile::Unknown;

    std::string line;
    std::string content;
    for (int i = 0; i < 50 && std::getline(ifs, line); ++i) {
        content += line + "\n";
    }

    // Check for profile indicators
    auto lower = content;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower.find("equipment") != std::string::npos &&
        lower.find("statevariables") == std::string::npos) {
        if (lower.find("topology") != std::string::npos) return CgmesProfile::Full;
        return CgmesProfile::Equipment;
    }
    if (lower.find("statevariables") != std::string::npos) return CgmesProfile::StateVariables;
    if (lower.find("steadystatehypothesis") != std::string::npos) return CgmesProfile::SteadyStateHypothesis;
    if (lower.find("topology") != std::string::npos) return CgmesProfile::Topology;
    if (lower.find("geographical") != std::string::npos || lower.find("location") != std::string::npos)
        return CgmesProfile::GeographicalLocation;
    if (lower.find("diagram") != std::string::npos) return CgmesProfile::DiagramLayout;
    if (lower.find("dynamics") != std::string::npos) return CgmesProfile::Dynamics;

    return CgmesProfile::Unknown;
}

std::vector<ImportError> CimParser::validate(const std::string& path) {
    std::vector<ImportError> errs;
    std::ifstream ifs(path);
    if (!ifs) {
        errs.push_back({Severity::Fatal, "FILE_OPEN", "Cannot open: " + path, path, 0, 0});
        return errs;
    }

    // Check for XML declaration
    std::string line;
    if (std::getline(ifs, line)) {
        auto tl = trim(line);
        if (tl.find("<?xml") == std::string::npos &&
            tl.find("<rdf:RDF") == std::string::npos) {
            errs.push_back({Severity::Warning, "NO_XML_DECL",
                "File does not start with XML declaration", path, 1, 0});
        }
    }

    // File size check
    ifs.seekg(0, std::ios::end);
    auto sz = ifs.tellg();
    if (sz == 0) {
        errs.push_back({Severity::Fatal, "EMPTY_FILE", "File is empty", path, 0, 0});
    } else if (sz > 1024 * 1024 * 500) {
        errs.push_back({Severity::Warning, "LARGE_FILE",
            "File exceeds 500 MB", path, 0, 0});
    }

    auto profile = detectProfile(path);
    if (profile == CgmesProfile::Unknown) {
        errs.push_back({Severity::Warning, "UNKNOWN_PROFILE",
            "Could not detect CGMES profile", path, 0, 0});
    }

    return errs;
}

ImportResult CimParser::load(const std::string& path, CancellationToken& token) {
    auto t0 = std::chrono::steady_clock::now();
    ImportResult result;
    result.fileInfo.path = path;
    result.fileInfo = getInfo();

    ParseContext ctx;
    ctx.data = std::make_shared<PowerSystemData>();
    ctx.token = &token;
    ctx.currentFile = path;
    ctx.profile = detectProfile(path);

    std::string content = readFileToString(path);
    if (content.empty()) {
        ctx.addError(Severity::Fatal, "EMPTY_FILE", "Cannot read file: " + path);
        result.status = ImportStatus::Error;
        result.errors = std::move(ctx.errors);
        return result;
    }

    ctx.data->metadata["profile"] = profileName(ctx.profile);

    try {
        XmlStream stream(content);
        parseRdfDocument(ctx, stream);
    } catch (const std::exception& ex) {
        ctx.addError(Severity::Error, "PARSE_EXCEPTION", ex.what());
    }

    // Post-processing
    resolveTopology(ctx);
    resolveTerminals(ctx);

    result.data = ctx.data;
    result.errors = std::move(ctx.errors);
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);

    if (token.isCancelled()) {
        result.status = ImportStatus::Cancelled;
    } else if (result.data->buses.empty() && result.data->branches.empty() &&
               result.data->transformers.empty()) {
        if (result.errors.empty()) {
            ctx.addError(Severity::Warning, "NO_ELEMENTS",
                "No power system elements found");
        }
        result.status = ImportStatus::Warning;
    } else {
        result.status = result.errors.empty() ? ImportStatus::Success : ImportStatus::Warning;
    }

    return result;
}

// ============================================================================
// XML Stream (simplified – production code should use libxml2 / xerces)
// ============================================================================

CimParser::XmlStream::XmlStream(const std::string& content)
    : content_(content) {}

void CimParser::XmlStream::skipWhitespace() {
    while (pos_ < content_.size() && std::isspace(static_cast<unsigned char>(content_[pos_]))) {
        if (content_[pos_] == '\n') ++lineNum_;
        ++pos_;
    }
}

bool CimParser::XmlStream::match(const std::string& s) {
    skipWhitespace();
    if (content_.compare(pos_, s.size(), s) == 0) {
        pos_ += s.size();
        return true;
    }
    return false;
}

std::string CimParser::XmlStream::readName() {
    skipWhitespace();
    std::size_t start = pos_;
    while (pos_ < content_.size() &&
           (std::isalnum(static_cast<unsigned char>(content_[pos_])) ||
            content_[pos_] == '_' || content_[pos_] == ':' || content_[pos_] == '-')) {
        ++pos_;
    }
    return content_.substr(start, pos_ - start);
}

std::string CimParser::XmlStream::readQuotedString() {
    skipWhitespace();
    char quote = content_[pos_];
    if (quote != '"' && quote != '\'') return {};
    ++pos_; // skip quote
    std::size_t start = pos_;
    while (pos_ < content_.size() && content_[pos_] != quote) {
        if (content_[pos_] == '\\') pos_ += 2;
        else ++pos_;
    }
    std::string val = content_.substr(start, pos_ - start);
    if (pos_ < content_.size() && content_[pos_] == quote) ++pos_;
    return val;
}

std::string CimParser::XmlStream::readText() {
    std::size_t start = pos_;
    while (pos_ < content_.size() && content_[pos_] != '<') {
        if (content_[pos_] == '\n') ++lineNum_;
        ++pos_;
    }
    auto text = content_.substr(start, pos_ - start);
    // trim
    auto a = text.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    auto b = text.find_last_not_of(" \t\r\n");
    return text.substr(a, b - a + 1);
}

std::string CimParser::XmlStream::readUntil(char c) {
    std::size_t start = pos_;
    while (pos_ < content_.size() && content_[pos_] != c) ++pos_;
    return content_.substr(start, pos_ - start);
}

CimParser::XmlToken CimParser::XmlStream::next() {
    skipWhitespace();
    if (pos_ >= content_.size()) return {XmlToken::EOF_, {}, {}, {}, {}};

    if (content_[pos_] != '<') {
        // Text content
        auto text = readText();
        return {XmlToken::Text, {}, text, {}, {}};
    }

    ++pos_; // skip '<'

    // Comment
    if (match("!--")) {
        while (pos_ + 2 < content_.size() && content_.compare(pos_, 3, "-->") != 0) {
            if (content_[pos_] == '\n') ++lineNum_;
            ++pos_;
        }
        pos_ += 3;
        return next(); // skip comment
    }

    // CDATA
    if (match("![CDATA[")) {
        auto text = readUntil(']');
        if (pos_ + 2 < content_.size()) pos_ += 3; // skip "]]>"
        return {XmlToken::Text, {}, text, {}, {}};
    }

    // End element
    if (match("/")) {
        auto name = readName();
        match(">");
        return {XmlToken::EndElement, name, {}, {}, {}};
    }

    // Start element
    auto name = readName();
    std::map<std::string, std::string> attrs;

    while (pos_ < content_.size() && content_[pos_] != '>' && content_[pos_] != '/') {
        auto attrName = readName();
        if (attrName.empty()) { ++pos_; continue; }
        match("=");
        auto attrVal = readQuotedString();
        attrs[attrName] = attrVal;
    }

    bool selfClosing = match("/");
    match(">");

    if (selfClosing) {
        return {XmlToken::StartElement, name, {}, {}, std::move(attrs)};
    }
    return {XmlToken::StartElement, name, {}, {}, std::move(attrs)};
}

// ============================================================================
// RDF document parsing
// ============================================================================

void CimParser::parseRdfDocument(ParseContext& ctx, XmlStream& stream) {
    std::string currentRdfId;
    std::string currentType;
    std::map<std::string, std::string> currentProps;
    std::map<std::string, std::string> currentRefs;
    std::stack<std::string> elementStack;

    while (true) {
        if (ctx.isCancelled()) break;

        auto tok = stream.next();
        ctx.lineNum = stream.lineNum();

        if (tok.type == XmlToken::EOF_) break;
        if (tok.type == XmlToken::Text) continue;
        if (tok.type == XmlToken::EndElement) {
            if (!elementStack.empty()) elementStack.pop();
            continue;
        }
        if (tok.type != XmlToken::StartElement) continue;

        elementStack.push(tok.name);

        // Detect rdf:Description or CIM class elements
        auto localName = extractLocalName(tok.name);

        // Check for rdf:about / rdf:ID to start a new object
        auto itAbout = tok.attrs.find("rdf:about");
        auto itID    = tok.attrs.find("rdf:ID");
        if (itAbout != tok.attrs.end() || itID != tok.attrs.end()) {
            // Process previous object if any
            if (!currentRdfId.empty()) {
                processObject(ctx, currentRdfId, currentType, currentProps, currentRefs);
            }
            // Start new object
            currentRdfId = stripHash(itAbout != tok.attrs.end() ? itAbout->second : itID->second);
            currentType = localName;
            currentProps.clear();
            currentRefs.clear();
            ctx.idToType[currentRdfId] = currentType;
            continue;
        }

        // Skip rdf:RDF root
        if (localName == "RDF") continue;

        // Process properties of current object
        if (!currentRdfId.empty()) {
            // Read next token – could be text or a rdf:resource reference
            auto nextTok = stream.next();
            ctx.lineNum = stream.lineNum();

            if (nextTok.type == XmlToken::Text) {
                currentProps[localName] = nextTok.value;
                // Read closing tag
                auto closeTok = stream.next();
                if (!elementStack.empty()) elementStack.pop();
            } else if (nextTok.type == XmlToken::StartElement) {
                // Check for rdf:resource attribute
                auto itRes = nextTok.attrs.find("rdf:resource");
                if (itRes != nextTok.attrs.end()) {
                    currentRefs[localName] = stripHash(itRes->second);
                }
                // Read closing tag of inner element
                auto closeTok = stream.next();
                // Read closing tag of outer element
                auto closeOuter = stream.next();
                if (!elementStack.empty()) elementStack.pop();
            }
        }
    }

    // Process final object
    if (!currentRdfId.empty()) {
        processObject(ctx, currentRdfId, currentType, currentProps, currentRefs);
    }
}

// ============================================================================
// Object processing
// ============================================================================

void CimParser::processObject(ParseContext& ctx,
                               const std::string& rdfId,
                               const std::string& type,
                               const std::map<std::string, std::string>& props,
                               const std::map<std::string, std::string>& refs) {
    ctx.objects[rdfId] = props;

    auto ln = extractLocalName(type);
    // Normalize to lower
    std::string lnLower = ln;
    std::transform(lnLower.begin(), lnLower.end(), lnLower.begin(), ::tolower);

    // Dispatch to appropriate builder
    if (lnLower.find("busbarsection") != std::string::npos ||
        lnLower.find("topologicalnode") != std::string::npos ||
        lnLower.find("connectivitynode") != std::string::npos ||
        lnLower.find("substation") != std::string::npos ||
        lnLower == "busbarsection" || lnLower == "topologicalnode" ||
        lnLower == "connectivitynode") {
        buildBus(ctx, rdfId, props);
    } else if (lnLower.find("line") != std::string::npos ||
               lnLower.find("aclinesegment") != std::string::npos) {
        buildBranch(ctx, rdfId, props);
    } else if (lnLower.find("transformer") != std::string::npos ||
               lnLower.find("powertransformer") != std::string::npos) {
        buildTransformer(ctx, rdfId, props);
    } else if (lnLower.find("generatingunit") != std::string::npos ||
               lnLower.find("synchronousmachine") != std::string::npos ||
               lnLower.find("energyconsumer") != std::string::npos) {
        // EnergyConsumer is a load, SynchronousMachine could be gen or load
        auto catIt = props.find("SynchronousMachine.type");
        if (catIt != props.end() &&
            (catIt->second == "generator" || catIt->second == "Generator")) {
            buildGenerator(ctx, rdfId, props);
        } else if (lnLower.find("energyconsumer") != std::string::npos ||
                   lnLower.find("conformload") != std::string::npos ||
                   lnLower.find("nonconformload") != std::string::npos) {
            buildLoad(ctx, rdfId, props);
        } else {
            buildGenerator(ctx, rdfId, props);
        }
    } else if (lnLower.find("energysource") != std::string::npos ||
               lnLower.find("synchronousmachine") != std::string::npos) {
        // Try to determine if gen or motor based on properties
        auto pIt = props.find("SynchronousMachine.p");
        if (pIt != props.end() && parseDouble(pIt->second) < 0) {
            buildGenerator(ctx, rdfId, props); // negative p = generation
        }
    } else if (lnLower.find("shuntcompensator") != std::string::npos ||
               lnLower.find("linearshuntcompensator") != std::string::npos ||
               lnLower.find("nonlinearshuntcompensator") != std::string::npos) {
        buildShunt(ctx, rdfId, props);
    } else if (lnLower.find("energyconsumer") != std::string::npos ||
               lnLower.find("conformload") != std::string::npos ||
               lnLower.find("nonconformload") != std::string::npos) {
        buildLoad(ctx, rdfId, props);
    }
}

// ============================================================================
// CIM object builders
// ============================================================================

void CimParser::buildBus(ParseContext& ctx, const std::string& id,
                          const std::map<std::string, std::string>& props) {
    Bus b;
    b.id = static_cast<int64_t>(std::hash<std::string>{}(id) % 100000000);
    if (b.id == 0) b.id = 1;

    auto itName = props.find("IdentifiedObject.name");
    if (itName != props.end()) b.name = itName->second;
    else b.name = extractLocalName(id);

    auto itVkV = props.find("BaseVoltage.nominalVoltage");
    if (itVkV != props.end()) b.baseVoltage_kV = parseDouble(itVkV->second);

    auto itArea = props.find("Substation.Region");
    if (itArea != props.end()) b.area = 1; // Would need SubGeographicalRegion lookup

    // Store original CIM attributes
    for (const auto& [k, v] : props) b.attributes[k] = v;

    // Check for duplicate bus id
    bool dup = false;
    for (const auto& existing : ctx.data->buses) {
        if (existing.id == b.id) { dup = true; break; }
    }
    if (!dup) ctx.data->buses.push_back(std::move(b));
}

void CimParser::buildBranch(ParseContext& ctx, const std::string& id,
                             const std::map<std::string, std::string>& props) {
    Branch br;
    br.circuitId = extractLocalName(id);

    auto itR = props.find("ACLineSegment.r");
    if (itR != props.end()) br.r_pu = parseDouble(itR->second);

    auto itX = props.find("ACLineSegment.x");
    if (itX != props.end()) br.x_pu = parseDouble(itX->second);

    auto itB = props.find("ACLineSegment.bch");
    if (itB != props.end()) br.b_pu = parseDouble(itB->second);

    auto itRate = props.find("ACLineSegment.ratedCurrent");
    if (itRate != props.end()) br.rateA_MVA = parseDouble(itRate->second);

    auto itLen = props.find("ACLineSegment.length");
    if (itLen != props.end()) br.length_km = parseDouble(itLen->second);

    // Store terminals reference for later resolution
    for (const auto& [k, v] : props) {
        if (k.find("Terminal") != std::string::npos) {
            br.attributes[k] = v;
        }
    }
    ctx.data->branches.push_back(std::move(br));
}

void CimParser::buildTransformer(ParseContext& ctx, const std::string& id,
                                  const std::map<std::string, std::string>& props) {
    Transformer t;

    auto itRate = props.find("PowerTransformerEnd.ratedS");
    if (itRate != props.end()) t.rateA_MVA = parseDouble(itRate->second);

    auto itR = props.find("PowerTransformerEnd.r");
    if (itR != props.end()) t.r12_pu = parseDouble(itR->second);

    auto itX = props.find("PowerTransformerEnd.x");
    if (itX != props.end()) t.x12_pu = parseDouble(itX->second);

    auto itVkV = props.find("PowerTransformerEnd.ratedU");
    if (itVkV != props.end()) t.windV1_kV = parseDouble(itVkV->second);

    for (const auto& [k, v] : props) t.attributes[k] = v;
    ctx.data->transformers.push_back(std::move(t));
}

void CimParser::buildGenerator(ParseContext& ctx, const std::string& id,
                                const std::map<std::string, std::string>& props) {
    Generator g;

    auto itP = props.find("SynchronousMachine.p");
    if (itP != props.end()) g.pGen_MW = -parseDouble(itP->second); // CIM: load positive

    auto itQ = props.find("SynchronousMachine.q");
    if (itQ != props.end()) g.qGen_Mvar = -parseDouble(itQ->second);

    auto itQmax = props.find("ReactiveCapabilityCurve.maxQ");
    if (itQmax != props.end()) g.qMax_Mvar = parseDouble(itQmax->second);

    auto itQmin = props.find("ReactiveCapabilityCurve.minQ");
    if (itQmin != props.end()) g.qMin_Mvar = parseDouble(itQmin->second);

    auto itRatedS = props.find("SynchronousMachine.ratedS");
    if (itRatedS != props.end()) g.mBase_MVA = parseDouble(itRatedS->second);

    auto itV = props.find("SynchronousMachine.targetVoltage");
    if (itV != props.end()) g.vSet_pu = parseDouble(itV->second);

    auto itMinP = props.find("GeneratingUnit.minOperatingP");
    if (itMinP != props.end()) g.pMin_MW = parseDouble(itMinP->second);

    auto itMaxP = props.find("GeneratingUnit.maxOperatingP");
    if (itMaxP != props.end()) g.pMax_MW = parseDouble(itMaxP->second);

    for (const auto& [k, v] : props) g.attributes[k] = v;
    ctx.data->generators.push_back(std::move(g));
}

void CimParser::buildLoad(ParseContext& ctx, const std::string& id,
                           const std::map<std::string, std::string>& props) {
    Load ld;

    auto itP = props.find("EnergyConsumer.p");
    if (itP != props.end()) ld.pLoad_MW = parseDouble(itP->second);

    auto itQ = props.find("EnergyConsumer.q");
    if (itQ != props.end()) ld.qLoad_Mvar = parseDouble(itQ->second);

    // Try ConformLoad / NonConformLoad specific properties
    auto itPfixed = props.find("ConformLoad.pfixed");
    if (itPfixed != props.end()) ld.pLoad_MW = parseDouble(itPfixed->second);

    for (const auto& [k, v] : props) ld.attributes[k] = v;
    ctx.data->loads.push_back(std::move(ld));
}

void CimParser::buildShunt(ParseContext& ctx, const std::string& id,
                            const std::map<std::string, std::string>& props) {
    Shunt s;

    auto itB = props.find("LinearShuntCompensator.bPerSection");
    if (itB != props.end()) s.b_Mvar = parseDouble(itB->second);

    auto itG = props.find("LinearShuntCompensator.gPerSection");
    if (itG != props.end()) s.g_MW = parseDouble(itG->second);

    auto itSections = props.find("ShuntCompensator.sections");
    if (itSections != props.end() && s.b_Mvar != 0.0) {
        double sections = parseDouble(itSections->second);
        s.b_Mvar *= sections;
    }

    for (const auto& [k, v] : props) s.attributes[k] = v;
    ctx.data->shunts.push_back(std::move(s));
}

// ============================================================================
// Post-processing
// ============================================================================

void CimParser::resolveTopology(ParseContext& ctx) {
    // Resolve terminal → equipment mappings
    // In a full implementation, we would use the TP (topology) profile
    // to establish connectivity. Here we assign sequential bus IDs
    // to branches that don't have from/to bus set.

    int64_t nextBusId = 1;
    std::map<std::string, int64_t> terminalToBus;

    // Create bus mapping from terminals
    for (const auto& [id, type] : ctx.idToType) {
        auto ln = extractLocalName(type);
        std::string lnLower = ln;
        std::transform(lnLower.begin(), lnLower.end(), lnLower.begin(), ::tolower);
        if (lnLower.find("terminal") != std::string::npos) {
            terminalToBus[id] = nextBusId++;
        }
    }

    // Assign bus IDs to branches with unresolved terminals
    for (auto& br : ctx.data->branches) {
        if (br.fromBus == 0) {
            auto it = br.attributes.find("Terminal.0");
            if (it != br.attributes.end()) {
                auto tit = terminalToBus.find(it->second);
                if (tit != terminalToBus.end()) br.fromBus = tit->second;
            }
            if (br.fromBus == 0 && !ctx.data->buses.empty()) br.fromBus = ctx.data->buses[0].id;
        }
        if (br.toBus == 0) {
            auto it = br.attributes.find("Terminal.1");
            if (it != br.attributes.end()) {
                auto tit = terminalToBus.find(it->second);
                if (tit != terminalToBus.end()) br.toBus = tit->second;
            }
            if (br.toBus == 0 && ctx.data->buses.size() > 1) br.toBus = ctx.data->buses[1].id;
        }
    }

    // Assign bus IDs to generators
    for (auto& g : ctx.data->generators) {
        if (g.busId == 0 && !ctx.data->buses.empty()) {
            g.busId = ctx.data->buses[0].id;
        }
    }

    // Assign bus IDs to loads
    for (auto& ld : ctx.data->loads) {
        if (ld.busId == 0 && !ctx.data->buses.empty()) {
            ld.busId = ctx.data->buses[0].id;
        }
    }

    // Assign bus IDs to shunts
    for (auto& s : ctx.data->shunts) {
        if (s.busId == 0 && !ctx.data->buses.empty()) {
            s.busId = ctx.data->buses[0].id;
        }
    }
}

void CimParser::resolveTerminals(ParseContext& ctx) {
    // Additional terminal resolution if TP profile is available
    // For now, ensure every branch has valid from/to bus
    for (const auto& br : ctx.data->branches) {
        if (br.fromBus == 0 || br.toBus == 0) {
            ctx.addError(Severity::Warning, "UNRESOLVED_TERMINAL",
                "Branch " + br.circuitId + " has unresolved terminals");
        }
    }
}

// ============================================================================
// Utility
// ============================================================================

std::string CimParser::extractLocalName(const std::string& qname) {
    auto colon = qname.find(':');
    if (colon != std::string::npos) return qname.substr(colon + 1);
    return qname;
}

std::string CimParser::stripHash(const std::string& ref) {
    if (!ref.empty() && ref.front() == '#') return ref.substr(1);
    return ref;
}

double CimParser::getDouble(const std::map<std::string, std::string>& m,
                             const std::string& key, double def) {
    auto it = m.find(key);
    return (it != m.end()) ? parseDouble(it->second, def) : def;
}

int64_t CimParser::getInt64(const std::map<std::string, std::string>& m,
                             const std::string& key, int64_t def) {
    auto it = m.find(key);
    return (it != m.end()) ? parseInt64(it->second, def) : def;
}

} // namespace powsys365::io
