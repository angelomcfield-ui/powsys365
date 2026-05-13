#include "psse_raw_parser.h"
#include <chrono>
#include <filesystem>

namespace powsys365::io {

// ============================================================================
// Public interface
// ============================================================================

FileInfo PsseRawParser::getInfo() const {
    FileInfo info;
    info.formatName     = "PSS/E RAW";
    info.extensions     = supportedExtensions();
    info.encoding       = encoding_;
    info.properties     = {
        {"revision",     std::to_string(revision_)},
        {"csv_mode",     csvMode_ ? "true" : "false"},
        {"description",  "PSS/E power flow raw data format (v29-v33)"}
    };
    info.detectedAt = std::chrono::system_clock::now();
    return info;
}

std::vector<ImportError> PsseRawParser::validate(const std::string& path) {
    std::vector<ImportError> errs;
    std::ifstream ifs(path);
    if (!ifs) {
        errs.push_back({Severity::Fatal, "FILE_OPEN", "Cannot open file: " + path, path, 0, 0});
        return errs;
    }
    // Read first non-empty line to check header
    std::string line;
    std::size_t lineNum = 0;
    while (std::getline(ifs, line)) {
        ++lineNum;
        auto tl = trim(line);
        if (tl.empty() || tl.front() == '@') continue;
        // First non-comment line must contain case data (revision number)
        auto tokens = tokenize(tl, csvMode_);
        if (tokens.size() < 3) {
            errs.push_back({Severity::Error, "BAD_HEADER",
                "Case identification line has insufficient fields", path, lineNum, 0});
        } else {
            try {
                int rev = std::stoi(tokens[2]);
                if (rev < 29 || rev > 35) {
                    errs.push_back({Severity::Warning, "UNKNOWN_REV",
                        "Revision " + std::to_string(rev) + " may not be fully supported",
                        path, lineNum, 0});
                }
            } catch (...) {
                errs.push_back({Severity::Warning, "BAD_REV",
                    "Cannot parse revision number", path, lineNum, 0});
            }
        }
        break;
    }
    // Check file size reasonable
    ifs.seekg(0, std::ios::end);
    auto sz = ifs.tellg();
    if (sz == 0) {
        errs.push_back({Severity::Fatal, "EMPTY_FILE", "File is empty", path, 0, 0});
    } else if (sz > 1024 * 1024 * 500) { // 500 MB
        errs.push_back({Severity::Warning, "LARGE_FILE",
            "File exceeds 500 MB, import may be slow", path, 0, 0});
    }
    return errs;
}

ImportResult PsseRawParser::load(const std::string& path, CancellationToken& token) {
    auto t0 = std::chrono::steady_clock::now();
    ImportResult result;
    result.fileInfo.path = path;
    result.fileInfo = getInfo();

    ParseContext ctx;
    ctx.data = std::make_shared<PowerSystemData>();
    ctx.token = &token;
    ctx.csvMode = csvMode_;
    ctx.revision = revision_;

    std::ifstream ifs(path);
    if (!ifs) {
        ctx.addError(Severity::Fatal, "FILE_OPEN", "Cannot open: " + path);
        result.status = ImportStatus::Error;
        result.errors = std::move(ctx.errors);
        return result;
    }

    // Pre-scan for line count estimation
    ifs.seekg(0, std::ios::end);
    auto fileSize = ifs.tellg();
    ifs.seekg(0, std::ios::beg);

    std::string line;
    bool headerParsed = false;
    std::string sectionStack;

    reportProgress("parsing", 0, static_cast<std::size_t>(fileSize), path);

    while (std::getline(ifs, line)) {
        ++ctx.lineNum;
        if (ctx.isCancelled()) {
            result.status = ImportStatus::Cancelled;
            break;
        }
        // Progress every 1000 lines
        if (ctx.lineNum % 1000 == 0) {
            reportProgress("parsing",
                static_cast<std::size_t>(ifs.tellg()),
                static_cast<std::size_t>(fileSize), path);
        }

        auto tl = trim(line);
        if (tl.empty()) continue;
        if (tl.front() == '@') continue; // comment

        // Detect section headers (lines starting with '0')
        if (!headerParsed) {
            parseCaseIdentification(ctx, tl);
            headerParsed = true;
            continue;
        }

        // PSS/E RAW sections begin with a count record of "0" or specific keywords
        auto tokens = tokenize(tl, csvMode_);

        // Check for section change indicator (0 count record)
        if (tokens.size() == 1 && tokens[0] == "0") {
            // Read next line to see which section
            std::size_t savedLine = ctx.lineNum;
            if (!std::getline(ifs, line)) break;
            ++ctx.lineNum;
            tl = trim(line);
            if (tl.empty() || tl.front() == '@') continue;
            tokens = tokenize(tl, csvMode_);
            // Section headers after "0" indicate what follows
            // We'll handle based on what follows the 0
        }

        // Route to appropriate section parser based on current context
        // PSS/E sections come in a well-defined order:
        // 1. Case Identification (already done)
        // 2. Bus Data
        // 3. Load Data
        // 4. Fixed Shunt
        // 5. Generator Data
        // 6. Branch Data
        // 7. Transformer Data
        // 8. Area Interchange
        // 9. Two-Terminal DC
        // 10. VSC DC
        // 11. Switched Shunt
        // etc.

        if (sectionStack.empty()) {
            // First data section is always bus data
            sectionStack = "BUS";
            ctx.currentSection = "BUS";
        }

        // Simple section routing based on token count and pattern
        if (ctx.currentSection == "BUS") {
            if (tokens.size() >= 8) {
                parseBusLine(ctx, tokens);
            } else {
                ctx.currentSection = "LOAD";
                sectionStack = "LOAD";
                // Re-process this line if it looks like load data
                if (tokens.size() >= 5 && tokens.size() < 8) {
                    parseLoadLine(ctx, tokens);
                }
            }
        } else if (ctx.currentSection == "LOAD") {
            if (tokens.size() >= 5) {
                parseLoadLine(ctx, tokens);
            } else {
                ctx.currentSection = "SHUNT";
                if (tokens.size() >= 4) {
                    parseShuntLine(ctx, tokens);
                }
            }
        } else if (ctx.currentSection == "SHUNT") {
            if (tokens.size() >= 4) {
                parseShuntLine(ctx, tokens);
            } else {
                ctx.currentSection = "GEN";
                if (tokens.size() >= 10) {
                    parseGenLine(ctx, tokens);
                }
            }
        } else if (ctx.currentSection == "GEN") {
            if (tokens.size() >= 10) {
                parseGenLine(ctx, tokens);
            } else {
                ctx.currentSection = "BRANCH";
                if (tokens.size() >= 8) {
                    parseBranchLine(ctx, tokens);
                }
            }
        } else if (ctx.currentSection == "BRANCH") {
            if (tokens.size() >= 8) {
                parseBranchLine(ctx, tokens);
            } else {
                ctx.currentSection = "TRANSFORMER";
                // Transformer records span multiple lines
                if (tokens.size() >= 3) {
                    parseTransformerMultiLine(ctx, tokens, ifs);
                }
            }
        } else if (ctx.currentSection == "TRANSFORMER") {
            if (tokens.size() >= 3) {
                parseTransformerMultiLine(ctx, tokens, ifs);
            } else {
                ctx.currentSection = "DONE";
            }
        }
    }

    result.data = ctx.data;
    result.errors = std::move(ctx.errors);
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);

    if (!result.data->buses.empty() || !result.data->branches.empty() ||
        !result.data->transformers.empty() || !result.data->generators.empty()) {
        if (result.status != ImportStatus::Cancelled) {
            result.status = result.errors.empty() ? ImportStatus::Success : ImportStatus::Warning;
        }
    } else if (result.status != ImportStatus::Cancelled) {
        result.status = ImportStatus::Error;
        ctx.addError(Severity::Fatal, "NO_DATA", "No power system elements parsed");
    }

    result.errors = std::move(ctx.errors);
    return result;
}

// ============================================================================
// Case Identification (first record)
// ============================================================================

void PsseRawParser::parseCaseIdentification(ParseContext& ctx, const std::string& line) {
    auto tokens = tokenize(line, ctx.csvMode);
    ctx.currentSection = "CASE_IDENTIFICATION";

    if (tokens.size() < 3) {
        ctx.addError(Severity::Error, "CASE_HDR_SHORT",
            "Case identification record has " + std::to_string(tokens.size()) +
            " fields, expected >= 3");
        return;
    }

    // Fields: IC, SBASE, REV, XFRRAT, NXFRAT, BASFRQ, <label...>
    try { ctx.revision = std::stoi(tokens[2]); } catch (...) {}
    ctx.data->metadata["ic"]       = tokens.size() > 0 ? tokens[0] : "0";
    ctx.data->metadata["sbase"]    = tokens.size() > 1 ? tokens[1] : "100.0";
    ctx.data->metadata["rev"]      = tokens.size() > 2 ? tokens[2] : "33";
    ctx.data->metadata["xfrrat"]   = tokens.size() > 3 ? tokens[3] : "0";
    ctx.data->metadata["nxfrat"]   = tokens.size() > 4 ? tokens[4] : "0";
    ctx.data->metadata["basfrq"]   = tokens.size() > 5 ? tokens[5] : "60.0";

    // Remaining tokens = case label
    if (tokens.size() > 6) {
        std::string label;
        for (std::size_t i = 6; i < tokens.size(); ++i) {
            if (i > 6) label += " ";
            label += tokens[i];
        }
        ctx.data->metadata["label"] = label;
    }
}

// ============================================================================
// Bus Data (section 2)
// ============================================================================

void PsseRawParser::parseBusLine(ParseContext& ctx,
                                  const std::vector<std::string>& tok) {
    // PSS/E bus: I, 'NAME', BASKV, IDE, AREA, ZONE, OWNER, VM, VA
    if (tok.size() < 8) {
        ctx.addError(Severity::Warning, "BUS_SHORT",
            "Bus record has " + std::to_string(tok.size()) + " fields");
        return;
    }
    Bus b;
    b.id           = parseInt64(tok[0]);
    b.name         = tok.size() > 1 ? trim(tok[1]) : "";
    b.baseVoltage_kV = parseDouble(tok.size() > 2 ? tok[2] : "0");
    int ide        = parseInt(tok.size() > 3 ? tok[3] : "1");
    b.area         = parseInt(tok.size() > 4 ? tok[4] : "1");
    b.zone         = parseInt(tok.size() > 5 ? tok[5] : "1");
    b.owner        = parseInt(tok.size() > 6 ? tok[6] : "1");
    double vm      = parseDouble(tok.size() > 7 ? tok[7] : "1.0");
    double va      = parseDouble(tok.size() > 8 ? tok[8] : "0.0");

    // IDE: 1=PQ, 2=PV, 3=slack(ref), 4=isolated
    b.attributes["type"] = std::to_string(ide);
    b.attributes["vm_pu"] = std::to_string(vm);
    b.attributes["va_deg"] = std::to_string(va);

    ctx.data->buses.push_back(std::move(b));
}

// ============================================================================
// Load Data (section 3)
// ============================================================================

void PsseRawParser::parseLoadLine(ParseContext& ctx,
                                   const std::vector<std::string>& tok) {
    // I, ID, STATUS, AREA, ZONE, PL, QL, IP, IQ, YP, YQ, OWNER
    if (tok.size() < 6) {
        ctx.addError(Severity::Warning, "LOAD_SHORT",
            "Load record has " + std::to_string(tok.size()) + " fields");
        return;
    }
    Load ld;
    ld.busId  = parseInt64(tok[0]);
    ld.id     = tok.size() > 1 ? trim(tok[1]) : "1";
    ld.status = parseInt(tok.size() > 2 ? tok[2] : "1");
    // AREA, ZONE skipped (idx 3,4)
    ld.pLoad_MW  = parseDouble(tok.size() > 5 ? tok[5] : "0");
    ld.qLoad_Mvar = parseDouble(tok.size() > 6 ? tok[6] : "0");

    if (tok.size() > 7) ld.attributes["ip"] = tok[7];
    if (tok.size() > 8) ld.attributes["iq"] = tok[8];
    if (tok.size() > 9) ld.attributes["yp"] = tok[9];
    if (tok.size() > 10) ld.attributes["yq"] = tok[10];

    ctx.data->loads.push_back(std::move(ld));
}

// ============================================================================
// Fixed Shunt (section 4)
// ============================================================================

void PsseRawParser::parseShuntLine(ParseContext& ctx,
                                    const std::vector<std::string>& tok) {
    // I, ID, STATUS, G, B
    if (tok.size() < 4) {
        ctx.addError(Severity::Warning, "SHUNT_SHORT",
            "Shunt record has " + std::to_string(tok.size()) + " fields");
        return;
    }
    Shunt s;
    s.busId  = parseInt64(tok[0]);
    s.id     = tok.size() > 1 ? trim(tok[1]) : "1";
    s.status = parseInt(tok.size() > 2 ? tok[2] : "1");
    s.g_MW   = parseDouble(tok.size() > 3 ? tok[3] : "0");
    s.b_Mvar = parseDouble(tok.size() > 4 ? tok[4] : "0");
    ctx.data->shunts.push_back(std::move(s));
}

// ============================================================================
// Generator Data (section 5)
// ============================================================================

void PsseRawParser::parseGenLine(ParseContext& ctx,
                                  const std::vector<std::string>& tok) {
    // I, ID, PG, QG, QT, QB, VS, IREG, MBASE, ZR, ZX, RT, XT, GTAP, STAT, RMPCT, PT, PB, O1...O4, F1...F4
    if (tok.size() < 10) {
        ctx.addError(Severity::Warning, "GEN_SHORT",
            "Generator record has " + std::to_string(tok.size()) + " fields");
        return;
    }
    Generator g;
    g.busId   = parseInt64(tok[0]);
    g.id      = tok.size() > 1 ? trim(tok[1]) : "1";
    g.pGen_MW  = parseDouble(tok.size() > 2 ? tok[2] : "0");
    g.qGen_Mvar = parseDouble(tok.size() > 3 ? tok[3] : "0");
    g.qMax_Mvar = parseDouble(tok.size() > 4 ? tok[4] : "9999");
    g.qMin_Mvar = parseDouble(tok.size() > 5 ? tok[5] : "-9999");
    g.vSet_pu  = parseDouble(tok.size() > 6 ? tok[6] : "1.0");
    // IREG idx 7
    g.mBase_MVA = parseDouble(tok.size() > 8 ? tok[8] : "0");
    // ZR, ZX idx 9,10
    // RT, XT idx 11,12
    // GTAP idx 13
    g.status   = parseInt(tok.size() > 14 ? tok[14] : "1");
    // RMPCT idx 15
    g.pMax_MW  = parseDouble(tok.size() > 16 ? tok[16] : "0");
    g.pMin_MW  = parseDouble(tok.size() > 17 ? tok[17] : "0");

    ctx.data->generators.push_back(std::move(g));
}

// ============================================================================
// Branch Data (section 6)
// ============================================================================

void PsseRawParser::parseBranchLine(ParseContext& ctx,
                                     const std::vector<std::string>& tok) {
    // I, J, CKT, R, X, B, RATEA, RATEB, RATEC, GI, BI, GJ, BJ, ST, MET, LEN, O1...O4, F1...F4
    if (tok.size() < 8) {
        ctx.addError(Severity::Warning, "BRANCH_SHORT",
            "Branch record has " + std::to_string(tok.size()) + " fields");
        return;
    }
    Branch br;
    br.fromBus = parseInt64(tok[0]);
    br.toBus   = parseInt64(tok[1]);
    br.circuitId = tok.size() > 2 ? trim(tok[2]) : "1";
    br.r_pu    = parseDouble(tok.size() > 3 ? tok[3] : "0");
    br.x_pu    = parseDouble(tok.size() > 4 ? tok[4] : "0");
    br.b_pu    = parseDouble(tok.size() > 5 ? tok[5] : "0");
    br.rateA_MVA = parseDouble(tok.size() > 6 ? tok[6] : "0");
    br.rateB_MVA = parseDouble(tok.size() > 7 ? tok[7] : "0");
    br.rateC_MVA = parseDouble(tok.size() > 8 ? tok[8] : "0");
    // GI, BI, GJ, BJ idx 9-12
    br.status  = parseInt(tok.size() > 13 ? tok[13] : "1");
    // MET idx 14
    br.length_km = parseDouble(tok.size() > 15 ? tok[15] : "0");

    ctx.data->branches.push_back(std::move(br));
}

// ============================================================================
// Transformer Data (section 7) – multi-line records
// ============================================================================

void PsseRawParser::parseTransformerMultiLine(ParseContext& ctx,
                                               const std::vector<std::string>& firstTok,
                                               std::ifstream& ifs) {
    // Transformer records span 3 lines (2-winding) or 4 lines (3-winding)
    // Line 1: I, J, K, CKT, CW, CZ, CM, MAG1, MAG2, NMET1, NAME, STAT, O1-F1, O2-F2, O3-F3, O4-F4, VECGRP
    // Lines 2-4 depend on CW/CZ/CM

    Transformer t;
    t.fromBus = parseInt64(firstTok.size() > 0 ? firstTok[0] : "0");
    t.toBus   = parseInt64(firstTok.size() > 1 ? firstTok[0] : "0");
    t.tertBus = parseInt64(firstTok.size() > 2 ? firstTok[2] : "0");
    t.circuitId = firstTok.size() > 3 ? trim(firstTok[3]) : "1";

    int cw = parseInt(firstTok.size() > 4 ? firstTok[4] : "1");
    int cz = parseInt(firstTok.size() > 5 ? firstTok[5] : "1");
    int cm = parseInt(firstTok.size() > 6 ? firstTok[6] : "1");
    // mag1, mag2, nmet1 skipped
    t.status = parseInt(firstTok.size() > 11 ? firstTok[11] : "1");

    // Read line 2: winding 1 data
    std::string line;
    if (!std::getline(ifs, line)) return;
    ++ctx.lineNum;
    auto tok2 = tokenize(trim(line), ctx.csvMode);

    // Line 2: R1-2, X1-2, SBASE1-2, WINDV1, NOMV1, ANG1, RATA1, RATB1, RATC1, COD1, CONT1, RMA1, RMI1, VMA1, VMI1, NTP1, TAB1, CR1, CX1, CNXA1
    if (tok2.size() >= 2) {
        t.r12_pu = parseDouble(tok2[0]);
        t.x12_pu = parseDouble(tok2[1]);
    }
    // SBASE idx 2
    t.windV1_kV = parseDouble(tok2.size() > 3 ? tok2[3] : "0");
    // NOMV idx 4
    // ANG idx 5
    t.rateA_MVA = parseDouble(tok2.size() > 6 ? tok2[6] : "0");
    // RATB, RATC idx 7,8

    // Read line 3: winding 2 data
    if (!std::getline(ifs, line)) return;
    ++ctx.lineNum;
    auto tok3 = tokenize(trim(line), ctx.csvMode);

    // WINDV2, NOMV2, ANG2, RATA2, RATB2, RATC2, COD2, CONT2, ...
    t.windV2_kV = parseDouble(tok3.size() > 0 ? tok3[0] : "0");

    // If 3-winding, read line 4
    if (t.tertBus != 0) {
        if (!std::getline(ifs, line)) return;
        ++ctx.lineNum;
        auto tok4 = tokenize(trim(line), ctx.csvMode);
        t.windV3_kV = parseDouble(tok4.size() > 0 ? tok4[0] : "0");
    }

    ctx.data->transformers.push_back(std::move(t));
}

// ============================================================================
// Tokenizer
// ============================================================================

std::vector<std::string> PsseRawParser::tokenize(const std::string& line, bool csv) {
    if (csv) {
        return splitLine(line, ',', true);
    }
    // Fixed-width parsing: split by whitespace, respecting quoted strings
    std::vector<std::string> out;
    std::string current;
    bool inQuotes = false;
    for (char c : line) {
        if (c == '\'') {
            inQuotes = !inQuotes;
            current.push_back(c);
        } else if ((std::isspace(static_cast<unsigned char>(c)) || c == ',') && !inQuotes) {
            if (!current.empty()) {
                out.push_back(trim(current));
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) out.push_back(trim(current));
    return out;
}

bool PsseRawParser::isEndOfSection(const std::string& line) const {
    auto tl = trim(line);
    return tl == "0";
}

std::string PsseRawParser::trimComment(const std::string& line) const {
    auto at = line.find('@');
    if (at != std::string::npos) return line.substr(0, at);
    return line;
}

// ---- Section parser stubs (called by routing in load()) ------------------

void PsseRawParser::parseBusData(ParseContext&, std::ifstream&) { }
void PsseRawParser::parseLoadData(ParseContext&, std::ifstream&) { }
void PsseRawParser::parseFixedShuntData(ParseContext&, std::ifstream&) { }
void PsseRawParser::parseGeneratorData(ParseContext&, std::ifstream&) { }
void PsseRawParser::parseBranchData(ParseContext&, std::ifstream&) { }
void PsseRawParser::parseTransformerData(ParseContext&, std::ifstream&) { }
void PsseRawParser::parseAreaInterchangeData(ParseContext&, std::ifstream&) { }
void PsseRawParser::parseTwoTerminalDCLine(ParseContext&, std::ifstream&) { }
void PsseRawParser::parseVSCDCLine(ParseContext&, std::ifstream&) { }
void PsseRawParser::parseSwitchedShuntData(ParseContext&, std::ifstream&) { }
void PsseRawParser::parseImpedanceCorrection(ParseContext&, std::ifstream&) { }
void PsseRawParser::parseMultiTerminalDCLine(ParseContext&, std::ifstream&) { }
void PsseRawParser::parseMultiSectionLine(ParseContext&, std::ifstream&) { }
void PsseRawParser::parseZoneData(ParseContext&, std::ifstream&) { }
void PsseRawParser::parseInterareaTransfer(ParseContext&, std::ifstream&) { }
void PsseRawParser::parseOwnerData(ParseContext&, std::ifstream&) { }
void PsseRawParser::parseFACTSDevice(ParseContext&, std::ifstream&) { }

} // namespace powsys365::io
