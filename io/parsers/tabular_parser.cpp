#include "tabular_parser.h"
#include <chrono>
#include <cstring>
#include <regex>

namespace powsys365::io {

// ============================================================================
// CSV Parser
// ============================================================================

FileInfo CsvParser::getInfo() const {
    FileInfo info;
    info.formatName = "CSV (Comma-Separated Values)";
    info.extensions = supportedExtensions();
    info.properties = {{"delimiter", std::string(1, delimiter_)},
                        {"has_header", hasHeader_ ? "true" : "false"},
                        {"description", "Generic CSV power system data"}};
    return info;
}

std::vector<ImportError> CsvParser::validate(const std::string& path) {
    std::vector<ImportError> errs;
    std::ifstream ifs(path);
    if (!ifs) {
        errs.push_back({Severity::Fatal, "FILE_OPEN", "Cannot open: " + path, path, 0, 0});
        return errs;
    }
    std::string line;
    if (std::getline(ifs, line)) {
        if (line.find(delimiter_) == std::string::npos) {
            errs.push_back({Severity::Warning, "NO_DELIMITER",
                "First line does not contain delimiter '" + std::string(1, delimiter_) + "'",
                path, 1, 0});
        }
    }
    ifs.seekg(0, std::ios::end);
    if (ifs.tellg() == 0) {
        errs.push_back({Severity::Fatal, "EMPTY_FILE", "File is empty", path, 0, 0});
    }
    return errs;
}

std::vector<std::string> CsvParser::parseCsvLine(const std::string& line) {
    std::vector<std::string> out;
    std::string field;
    bool inQuotes = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '"') {
            if (inQuotes && i + 1 < line.size() && line[i + 1] == '"') {
                field.push_back('"');
                ++i;
            } else {
                inQuotes = !inQuotes;
            }
        } else if (c == delimiter_ && !inQuotes) {
            out.push_back(trim(field));
            field.clear();
        } else {
            field.push_back(c);
        }
    }
    out.push_back(trim(field));
    return out;
}

void CsvParser::parseHeader(ParseContext& ctx, const std::vector<std::string>& fields) {
    ctx.headers = fields;
}

std::string CsvParser::getField(const ParseContext& ctx,
                                 const std::vector<std::string>& fields,
                                 const std::string& colName) {
    // Look up by header name first
    for (std::size_t i = 0; i < ctx.headers.size() && i < fields.size(); ++i) {
        if (toLower(ctx.headers[i]) == toLower(colName)) {
            return fields[i];
        }
    }
    // Try column mapping
    for (const auto& [mapped, original] : columnMapping_) {
        if (toLower(mapped) == toLower(colName)) {
            for (std::size_t i = 0; i < ctx.headers.size() && i < fields.size(); ++i) {
                if (toLower(ctx.headers[i]) == toLower(original)) {
                    return fields[i];
                }
            }
        }
    }
    return {};
}

void CsvParser::parseDataRow(ParseContext& ctx, const std::vector<std::string>& fields) {
    if (fields.empty()) return;
    if (ctx.headers.empty()) return;

    // Detect row type from headers or content
    auto typeField = getField(ctx, fields, "type");
    auto typeLower = toLower(typeField);

    if (typeLower == "bus" || (!getField(ctx, fields, "bus_id").empty() &&
                                getField(ctx, fields, "base_kv").empty() == false)) {
        Bus b;
        b.id = parseInt64(getField(ctx, fields, "bus_id"));
        b.name = getField(ctx, fields, "name");
        if (b.name.empty()) b.name = getField(ctx, fields, "bus_name");
        b.baseVoltage_kV = parseDouble(getField(ctx, fields, "base_kv"));
        b.area  = parseInt(getField(ctx, fields, "area"));
        b.zone  = parseInt(getField(ctx, fields, "zone"));
        b.owner = parseInt(getField(ctx, fields, "owner"));
        auto lat = getField(ctx, fields, "lat");
        auto lon = getField(ctx, fields, "lon");
        if (!lat.empty() && !lon.empty()) {
            b.location = GeoPoint{parseDouble(lat), parseDouble(lon), 0.0};
        }
        for (std::size_t i = 0; i < ctx.headers.size() && i < fields.size(); ++i) {
            b.attributes[toLower(ctx.headers[i])] = fields[i];
        }
        ctx.data->buses.push_back(std::move(b));
    } else if (typeLower == "branch" || typeLower == "line" ||
               (!getField(ctx, fields, "from_bus").empty())) {
        Branch br;
        br.fromBus = parseInt64(getField(ctx, fields, "from_bus"));
        br.toBus   = parseInt64(getField(ctx, fields, "to_bus"));
        br.circuitId = getField(ctx, fields, "ckt");
        if (br.circuitId.empty()) br.circuitId = "1";
        br.r_pu    = parseDouble(getField(ctx, fields, "r"));
        br.x_pu    = parseDouble(getField(ctx, fields, "x"));
        br.b_pu    = parseDouble(getField(ctx, fields, "b"));
        br.rateA_MVA = parseDouble(getField(ctx, fields, "rate_a"));
        br.rateB_MVA = parseDouble(getField(ctx, fields, "rate_b"));
        br.rateC_MVA = parseDouble(getField(ctx, fields, "rate_c"));
        br.status  = parseInt(getField(ctx, fields, "status"), 1);
        br.length_km = parseDouble(getField(ctx, fields, "length"));
        for (std::size_t i = 0; i < ctx.headers.size() && i < fields.size(); ++i) {
            br.attributes[toLower(ctx.headers[i])] = fields[i];
        }
        ctx.data->branches.push_back(std::move(br));
    } else if (typeLower == "transformer" || typeLower == "xfmr") {
        Transformer t;
        t.fromBus = parseInt64(getField(ctx, fields, "from_bus"));
        t.toBus   = parseInt64(getField(ctx, fields, "to_bus"));
        t.tertBus = parseInt64(getField(ctx, fields, "tert_bus"));
        t.circuitId = getField(ctx, fields, "ckt");
        if (t.circuitId.empty()) t.circuitId = "1";
        t.r12_pu = parseDouble(getField(ctx, fields, "r"));
        t.x12_pu = parseDouble(getField(ctx, fields, "x"));
        t.rateA_MVA = parseDouble(getField(ctx, fields, "rate"));
        t.windV1_kV = parseDouble(getField(ctx, fields, "windv1"));
        t.windV2_kV = parseDouble(getField(ctx, fields, "windv2"));
        t.status = parseInt(getField(ctx, fields, "status"), 1);
        for (std::size_t i = 0; i < ctx.headers.size() && i < fields.size(); ++i) {
            t.attributes[toLower(ctx.headers[i])] = fields[i];
        }
        ctx.data->transformers.push_back(std::move(t));
    } else if (typeLower == "generator" || typeLower == "gen") {
        Generator g;
        g.busId   = parseInt64(getField(ctx, fields, "bus_id"));
        g.id      = getField(ctx, fields, "gen_id");
        if (g.id.empty()) g.id = "1";
        g.pGen_MW  = parseDouble(getField(ctx, fields, "pg"));
        g.qGen_Mvar = parseDouble(getField(ctx, fields, "qg"));
        g.qMax_Mvar = parseDouble(getField(ctx, fields, "qt"), 9999.0);
        g.qMin_Mvar = parseDouble(getField(ctx, fields, "qb"), -9999.0);
        g.vSet_pu  = parseDouble(getField(ctx, fields, "vs"), 1.0);
        g.pMax_MW  = parseDouble(getField(ctx, fields, "pt"));
        g.pMin_MW  = parseDouble(getField(ctx, fields, "pb"));
        g.status   = parseInt(getField(ctx, fields, "status"), 1);
        g.mBase_MVA = parseDouble(getField(ctx, fields, "mbase"));
        for (std::size_t i = 0; i < ctx.headers.size() && i < fields.size(); ++i) {
            g.attributes[toLower(ctx.headers[i])] = fields[i];
        }
        ctx.data->generators.push_back(std::move(g));
    } else if (typeLower == "load") {
        Load ld;
        ld.busId  = parseInt64(getField(ctx, fields, "bus_id"));
        ld.id     = getField(ctx, fields, "load_id");
        if (ld.id.empty()) ld.id = "1";
        ld.pLoad_MW  = parseDouble(getField(ctx, fields, "pl"));
        ld.qLoad_Mvar = parseDouble(getField(ctx, fields, "ql"));
        ld.status    = parseInt(getField(ctx, fields, "status"), 1);
        for (std::size_t i = 0; i < ctx.headers.size() && i < fields.size(); ++i) {
            ld.attributes[toLower(ctx.headers[i])] = fields[i];
        }
        ctx.data->loads.push_back(std::move(ld));
    } else if (typeLower == "shunt") {
        Shunt s;
        s.busId  = parseInt64(getField(ctx, fields, "bus_id"));
        s.id     = getField(ctx, fields, "shunt_id");
        if (s.id.empty()) s.id = "1";
        s.b_Mvar = parseDouble(getField(ctx, fields, "b"));
        s.g_MW   = parseDouble(getField(ctx, fields, "g"));
        s.status = parseInt(getField(ctx, fields, "status"), 1);
        for (std::size_t i = 0; i < ctx.headers.size() && i < fields.size(); ++i) {
            s.attributes[toLower(ctx.headers[i])] = fields[i];
        }
        ctx.data->shunts.push_back(std::move(s));
    }
}

ImportResult CsvParser::load(const std::string& path, CancellationToken& token) {
    auto t0 = std::chrono::steady_clock::now();
    ImportResult result;
    result.fileInfo = getInfo();
    result.fileInfo.path = path;

    ParseContext ctx;
    ctx.data = std::make_shared<PowerSystemData>();
    ctx.token = &token;
    ctx.currentFile = path;

    std::ifstream ifs(path);
    if (!ifs) {
        ctx.addError(Severity::Fatal, "FILE_OPEN", "Cannot open: " + path);
        result.status = ImportStatus::Error;
        result.errors = std::move(ctx.errors);
        return result;
    }

    std::string line;
    bool headerRead = false;
    while (std::getline(ifs, line) && !token.isCancelled()) {
        ++ctx.lineNum;
        auto tl = trim(line);
        if (tl.empty()) continue;

        auto fields = parseCsvLine(tl);
        if (fields.empty()) continue;

        if (!headerRead && hasHeader_) {
            parseHeader(ctx, fields);
            headerRead = true;
            continue;
        }

        parseDataRow(ctx, fields);

        if (ctx.lineNum % 1000 == 0) {
            reportProgress("parsing", ctx.lineNum, 0, path);
        }
    }

    result.data = ctx.data;
    result.errors = std::move(ctx.errors);
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);
    result.status = token.isCancelled() ? ImportStatus::Cancelled :
                    (result.errors.empty() ? ImportStatus::Success : ImportStatus::Warning);
    return result;
}

// ============================================================================
// XLSX Parser (simplified – extracts from ZIP, parses XML)
// ============================================================================

FileInfo XlsxParser::getInfo() const {
    FileInfo info;
    info.formatName = "XLSX (Excel Open XML)";
    info.extensions = supportedExtensions();
    info.properties = {{"description", "Excel spreadsheet ( Office Open XML )"},
                        {"note", "Extracts sheet1.xml from the XLSX ZIP package"}};
    return info;
}

std::vector<ImportError> XlsxParser::validate(const std::string& path) {
    std::vector<ImportError> errs;
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        errs.push_back({Severity::Fatal, "FILE_OPEN", "Cannot open: " + path, path, 0, 0});
        return errs;
    }
    char magic[4];
    ifs.read(magic, 4);
    if (magic[0] != 'P' || magic[1] != 'K') {
        errs.push_back({Severity::Error, "NOT_ZIP",
            "XLSX must be a ZIP archive", path, 0, 0});
    }
    return errs;
}

std::string XlsxParser::extractSheetXml(const std::string& path,
                                         std::vector<std::string>& sharedStrings,
                                         std::vector<ImportError>& errs) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        errs.push_back({Severity::Fatal, "FILE_OPEN", "Cannot open: " + path, path, 0, 0});
        return {};
    }

    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());

    // Find sharedStrings.xml in the ZIP
    auto ssPos = content.find("sharedStrings.xml");
    if (ssPos != std::string::npos) {
        auto headerPos = content.rfind("PK\x03\x04", ssPos);
        if (headerPos != std::string::npos && headerPos + 30 < content.size()) {
            uint16_t nameLen = *reinterpret_cast<const uint16_t*>(
                content.data() + headerPos + 26);
            uint16_t extraLen = *reinterpret_cast<const uint16_t*>(
                content.data() + headerPos + 28);
            uint32_t compSize = *reinterpret_cast<const uint32_t*>(
                content.data() + headerPos + 18);
            std::size_t dataOff = headerPos + 30 + nameLen + extraLen;
            uint16_t method = *reinterpret_cast<const uint16_t*>(
                content.data() + headerPos + 8);
            if (method == 0 && dataOff + compSize <= content.size()) {
                std::string ssXml = content.substr(dataOff, compSize);
                sharedStrings = parseSharedStrings(ssXml);
            }
        }
    }

    // Find sheet1.xml in the ZIP
    auto sheetPos = content.find("sheet1.xml");
    if (sheetPos == std::string::npos) {
        // Try worksheets/sheet1.xml
        sheetPos = content.find("worksheets/sheet1.xml");
    }
    if (sheetPos == std::string::npos) {
        errs.push_back({Severity::Error, "NO_SHEET",
            "sheet1.xml not found in XLSX", path, 0, 0});
        return {};
    }

    auto headerPos = content.rfind("PK\x03\x04", sheetPos);
    if (headerPos == std::string::npos) {
        errs.push_back({Severity::Error, "CORRUPT_XLSX",
            "Cannot find ZIP header for sheet1.xml", path, 0, 0});
        return {};
    }

    uint16_t nameLen = *reinterpret_cast<const uint16_t*>(
        content.data() + headerPos + 26);
    uint16_t extraLen = *reinterpret_cast<const uint16_t*>(
        content.data() + headerPos + 28);
    uint32_t compSize = *reinterpret_cast<const uint32_t*>(
        content.data() + headerPos + 18);
    uint16_t method = *reinterpret_cast<const uint16_t*>(
        content.data() + headerPos + 8);
    std::size_t dataOff = headerPos + 30 + nameLen + extraLen;

    if (method == 0 && dataOff + compSize <= content.size()) {
        return content.substr(dataOff, compSize);
    }

    errs.push_back({Severity::Error, "COMPRESSED_SHEET",
        "Sheet compression not supported in this build", path, 0, 0});
    return {};
}

std::vector<std::string> XlsxParser::parseSharedStrings(const std::string& xml) {
    std::vector<std::string> out;
    std::regex siRe("<si>(.*?)</si>");
    std::sregex_iterator it(xml.begin(), xml.end(), siRe);
    std::sregex_iterator end;
    while (it != end) {
        std::string si = (*it)[1];
        // Extract text from <t> tags
        std::regex tRe("<t>([^<]*)</t>");
        std::sregex_iterator tit(si.begin(), si.end(), tRe);
        std::sregex_iterator tend;
        std::string text;
        while (tit != tend) {
            text += (*tit)[1];
            ++tit;
        }
        out.push_back(text);
        ++it;
    }
    return out;
}

std::vector<std::vector<std::string>> XlsxParser::parseSheetCells(
    const std::string& xml, const std::vector<std::string>& ss) {
    std::vector<std::vector<std::string>> rows;

    // Extract all <row> elements
    std::regex rowRe("<row[^>]*>(.*?)</row>");
    std::sregex_iterator rowIt(xml.begin(), xml.end(), rowRe);
    std::sregex_iterator rowEnd;

    while (rowIt != rowEnd) {
        std::string rowXml = (*rowIt)[1];
        std::vector<std::string> cells;

        // Extract <c> elements
        std::regex cRe("<c[^>]*>(.*?)</c>");
        std::sregex_iterator cIt(rowXml.begin(), rowXml.end(), cRe);
        std::sregex_iterator cEnd;

        while (cIt != cEnd) {
            std::string cXml = (*cIt)[0];
            std::string cellText;

            // Check for shared string reference
            auto tAttr = cXml.find("t=\"s\"");
            if (tAttr != std::string::npos) {
                // Shared string: extract <v> index
                std::regex vRe("<v>(\\d+)</v>");
                std::smatch vMatch;
                if (std::regex_search(cXml, vMatch, vRe)) {
                    int idx = std::stoi(vMatch[1]);
                    if (idx >= 0 && static_cast<std::size_t>(idx) < ss.size()) {
                        cellText = ss[idx];
                    }
                }
            } else {
                // Inline value
                std::regex vRe("<v>([^<]*)</v>");
                std::smatch vMatch;
                if (std::regex_search(cXml, vMatch, vRe)) {
                    cellText = vMatch[1];
                } else {
                    // Check for inline string <is><t>
                    std::regex tRe("<is>.*?<t>([^<]*)</t>.*?</is>");
                    std::smatch tMatch;
                    if (std::regex_search(cXml, tMatch, tRe)) {
                        cellText = tMatch[1];
                    }
                }
            }
            cells.push_back(cellText);
            ++cIt;
        }
        if (!cells.empty()) rows.push_back(std::move(cells));
        ++rowIt;
    }
    return rows;
}

void XlsxParser::processRows(ParseContext& ctx,
                              const std::vector<std::vector<std::string>>& rows) {
    if (rows.empty()) return;

    // First row as headers
    std::vector<std::string> headers = rows[0];
    for (auto& h : headers) h = toLower(trim(h));

    CsvParser csvParser;
    csvParser.setHasHeader(false);

    for (std::size_t i = 1; i < rows.size() && !ctx.isCancelled(); ++i) {
        ++ctx.rowNum;
        // Map to column positions
        std::map<std::string, std::string> colMap;
        for (std::size_t j = 0; j < headers.size() && j < rows[i].size(); ++j) {
            colMap[headers[j]] = rows[i][j];
        }

        // Detect type
        auto itType = colMap.find("type");
        std::string type = (itType != colMap.end()) ? toLower(itType->second) : "";

        if (type == "bus" || colMap.count("bus_id")) {
            Bus b;
            b.id = parseInt64(colMap.count("bus_id") ? colMap["bus_id"] : "0");
            b.name = colMap.count("name") ? colMap["name"] :
                     (colMap.count("bus_name") ? colMap["bus_name"] : "");
            b.baseVoltage_kV = parseDouble(colMap.count("base_kv") ? colMap["base_kv"] : "0");
            b.area  = parseInt(colMap.count("area") ? colMap["area"] : "1");
            b.zone  = parseInt(colMap.count("zone") ? colMap["zone"] : "1");
            b.owner = parseInt(colMap.count("owner") ? colMap["owner"] : "1");
            if (colMap.count("lat") && colMap.count("lon")) {
                b.location = GeoPoint{parseDouble(colMap["lat"]), parseDouble(colMap["lon"]), 0.0};
            }
            for (const auto& [k, v] : colMap) b.attributes[k] = v;
            ctx.data->buses.push_back(std::move(b));
        } else if (type == "branch" || type == "line" || colMap.count("from_bus")) {
            Branch br;
            br.fromBus = parseInt64(colMap.count("from_bus") ? colMap["from_bus"] : "0");
            br.toBus   = parseInt64(colMap.count("to_bus") ? colMap["to_bus"] : "0");
            br.circuitId = colMap.count("ckt") ? colMap["ckt"] : "1";
            br.r_pu    = parseDouble(colMap.count("r") ? colMap["r"] : "0");
            br.x_pu    = parseDouble(colMap.count("x") ? colMap["x"] : "0");
            br.b_pu    = parseDouble(colMap.count("b") ? colMap["b"] : "0");
            br.rateA_MVA = parseDouble(colMap.count("rate_a") ? colMap["rate_a"] : "0");
            br.rateB_MVA = parseDouble(colMap.count("rate_b") ? colMap["rate_b"] : "0");
            br.rateC_MVA = parseDouble(colMap.count("rate_c") ? colMap["rate_c"] : "0");
            br.status  = parseInt(colMap.count("status") ? colMap["status"] : "1", 1);
            for (const auto& [k, v] : colMap) br.attributes[k] = v;
            ctx.data->branches.push_back(std::move(br));
        } else if (type == "generator" || type == "gen" || colMap.count("pg")) {
            Generator g;
            g.busId  = parseInt64(colMap.count("bus_id") ? colMap["bus_id"] : "0");
            g.id     = colMap.count("gen_id") ? colMap["gen_id"] : "1";
            g.pGen_MW  = parseDouble(colMap.count("pg") ? colMap["pg"] : "0");
            g.qGen_Mvar = parseDouble(colMap.count("qg") ? colMap["qg"] : "0");
            g.qMax_Mvar = parseDouble(colMap.count("qt") ? colMap["qt"] : "9999");
            g.qMin_Mvar = parseDouble(colMap.count("qb") ? colMap["qb"] : "-9999");
            g.vSet_pu  = parseDouble(colMap.count("vs") ? colMap["vs"] : "1.0");
            g.pMax_MW  = parseDouble(colMap.count("pt") ? colMap["pt"] : "0");
            g.pMin_MW  = parseDouble(colMap.count("pb") ? colMap["pb"] : "0");
            g.status   = parseInt(colMap.count("status") ? colMap["status"] : "1", 1);
            g.mBase_MVA = parseDouble(colMap.count("mbase") ? colMap["mbase"] : "0");
            for (const auto& [k, v] : colMap) g.attributes[k] = v;
            ctx.data->generators.push_back(std::move(g));
        } else if (type == "load" || colMap.count("pl")) {
            Load ld;
            ld.busId  = parseInt64(colMap.count("bus_id") ? colMap["bus_id"] : "0");
            ld.id     = colMap.count("load_id") ? colMap["load_id"] : "1";
            ld.pLoad_MW  = parseDouble(colMap.count("pl") ? colMap["pl"] : "0");
            ld.qLoad_Mvar = parseDouble(colMap.count("ql") ? colMap["ql"] : "0");
            ld.status    = parseInt(colMap.count("status") ? colMap["status"] : "1", 1);
            for (const auto& [k, v] : colMap) ld.attributes[k] = v;
            ctx.data->loads.push_back(std::move(ld));
        } else if (type == "transformer" || type == "xfmr" || colMap.count("windv1")) {
            Transformer t;
            t.fromBus = parseInt64(colMap.count("from_bus") ? colMap["from_bus"] : "0");
            t.toBus   = parseInt64(colMap.count("to_bus") ? colMap["to_bus"] : "0");
            t.tertBus = parseInt64(colMap.count("tert_bus") ? colMap["tert_bus"] : "0");
            t.circuitId = colMap.count("ckt") ? colMap["ckt"] : "1";
            t.r12_pu = parseDouble(colMap.count("r") ? colMap["r"] : "0");
            t.x12_pu = parseDouble(colMap.count("x") ? colMap["x"] : "0");
            t.rateA_MVA = parseDouble(colMap.count("rate") ? colMap["rate"] : "0");
            t.windV1_kV = parseDouble(colMap.count("windv1") ? colMap["windv1"] : "0");
            t.windV2_kV = parseDouble(colMap.count("windv2") ? colMap["windv2"] : "0");
            t.status = parseInt(colMap.count("status") ? colMap["status"] : "1", 1);
            for (const auto& [k, v] : colMap) t.attributes[k] = v;
            ctx.data->transformers.push_back(std::move(t));
        } else if (type == "shunt" || colMap.count("b")) {
            Shunt s;
            s.busId  = parseInt64(colMap.count("bus_id") ? colMap["bus_id"] : "0");
            s.id     = colMap.count("shunt_id") ? colMap["shunt_id"] : "1";
            s.b_Mvar = parseDouble(colMap.count("b") ? colMap["b"] : "0");
            s.g_MW   = parseDouble(colMap.count("g") ? colMap["g"] : "0");
            s.status = parseInt(colMap.count("status") ? colMap["status"] : "1", 1);
            for (const auto& [k, v] : colMap) s.attributes[k] = v;
            ctx.data->shunts.push_back(std::move(s));
        }

        if (ctx.rowNum % 1000 == 0) {
            reportProgress("parsing", ctx.rowNum, rows.size(), ctx.currentFile);
        }
    }
}

ImportResult XlsxParser::load(const std::string& path, CancellationToken& token) {
    auto t0 = std::chrono::steady_clock::now();
    ImportResult result;
    result.fileInfo = getInfo();
    result.fileInfo.path = path;

    ParseContext ctx;
    ctx.data = std::make_shared<PowerSystemData>();
    ctx.token = &token;
    ctx.currentFile = path;

    std::vector<std::string> sharedStrings;
    std::vector<ImportError> extractErrs;
    std::string sheetXml = extractSheetXml(path, sharedStrings, extractErrs);

    if (sheetXml.empty()) {
        result.status = ImportStatus::Error;
        result.errors = std::move(extractErrs);
        return result;
    }

    auto rows = parseSheetCells(sheetXml, sharedStrings);
    processRows(ctx, rows);

    for (auto& e : extractErrs) ctx.errors.push_back(e);

    result.data = ctx.data;
    result.errors = std::move(ctx.errors);
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);
    result.status = token.isCancelled() ? ImportStatus::Cancelled :
                    (result.errors.empty() ? ImportStatus::Success : ImportStatus::Warning);
    return result;
}

// ============================================================================
// JSON Parser (generic)
// ============================================================================

FileInfo JsonParser::getInfo() const {
    FileInfo info;
    info.formatName = "JSON (JavaScript Object Notation)";
    info.extensions = supportedExtensions();
    info.properties = {{"standard", "RFC 8259"},
                        {"description", "Generic JSON power system data"}};
    return info;
}

std::vector<ImportError> JsonParser::validate(const std::string& path) {
    std::vector<ImportError> errs;
    std::ifstream ifs(path);
    if (!ifs) {
        errs.push_back({Severity::Fatal, "FILE_OPEN", "Cannot open: " + path, path, 0, 0});
        return errs;
    }
    std::string line;
    if (std::getline(ifs, line)) {
        auto tl = trim(line);
        if (tl.front() != '{' && tl.front() != '[') {
            errs.push_back({Severity::Warning, "NOT_JSON",
                "File does not appear to be JSON", path, 1, 0});
        }
    }
    return errs;
}

std::vector<std::string> JsonParser::extractJsonArrayItems(const std::string& arrJson) {
    std::vector<std::string> items;
    if (arrJson.empty() || arrJson.front() != '[') return items;

    std::size_t pos = 1;
    while (pos < arrJson.size()) {
        while (pos < arrJson.size() && std::isspace(arrJson[pos])) ++pos;
        if (arrJson[pos] == ']') break;

        std::size_t start = pos;
        int depth = 0;
        bool inStr = false;
        while (pos < arrJson.size()) {
            if (!inStr) {
                if (arrJson[pos] == '{') ++depth;
                else if (arrJson[pos] == '}') --depth;
                else if (arrJson[pos] == '"') inStr = true;
                else if (arrJson[pos] == ']' && depth == 0) break;
                else if (arrJson[pos] == ',' && depth == 0) break;
            } else {
                if (arrJson[pos] == '"' && arrJson[pos - 1] != '\\') inStr = false;
                else if (arrJson[pos] == '\\') ++pos;
            }
            ++pos;
        }
        items.push_back(arrJson.substr(start, pos - start));
        while (pos < arrJson.size() && (std::isspace(arrJson[pos]) || arrJson[pos] == ',')) ++pos;
    }
    return items;
}

std::string JsonParser::extractJsonValue(const std::string& json, const std::string& key) {
    return extractJsonString(json, key);
}

std::string JsonParser::extractJsonString(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return {};
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return {};
    ++pos;
    while (pos < json.size() && std::isspace(json[pos])) ++pos;
    if (pos >= json.size()) return {};
    if (json[pos] == '"') {
        ++pos;
        std::size_t end = pos;
        while (end < json.size() && json[end] != '"') {
            if (json[end] == '\\') end += 2;
            else ++end;
        }
        return json.substr(pos, end - pos);
    }
    // Numeric or literal
    std::size_t end = pos;
    while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != ']')
        ++end;
    return trim(json.substr(pos, end - pos));
}

double JsonParser::extractJsonDouble(const std::string& json, const std::string& key, double def) {
    auto s = extractJsonString(json, key);
    return s.empty() ? def : parseDouble(s, def);
}

int64_t JsonParser::extractJsonInt64(const std::string& json, const std::string& key, int64_t def) {
    auto s = extractJsonString(json, key);
    return s.empty() ? def : parseInt64(s, def);
}

ImportResult JsonParser::load(const std::string& path, CancellationToken& token) {
    auto t0 = std::chrono::steady_clock::now();
    ImportResult result;
    result.fileInfo = getInfo();
    result.fileInfo.path = path;

    ParseContext ctx;
    ctx.data = std::make_shared<PowerSystemData>();
    ctx.token = &token;
    ctx.currentFile = path;

    std::string content = readFileToString(path);
    if (content.empty()) {
        ctx.addError(Severity::Fatal, "EMPTY_FILE", "Cannot read: " + path);
        result.status = ImportStatus::Error;
        result.errors = std::move(ctx.errors);
        return result;
    }

    // Look for known arrays
    auto busesArr = extractJsonArrayItems(extractJsonValue(content, "buses"));
    auto branchesArr = extractJsonArrayItems(extractJsonValue(content, "branches"));
    auto transformersArr = extractJsonArrayItems(extractJsonValue(content, "transformers"));
    auto generatorsArr = extractJsonArrayItems(extractJsonValue(content, "generators"));
    auto loadsArr = extractJsonArrayItems(extractJsonValue(content, "loads"));
    auto shuntsArr = extractJsonArrayItems(extractJsonValue(content, "shunts"));

    // Parse buses
    for (const auto& item : busesArr) {
        if (token.isCancelled()) break;
        Bus b;
        b.id = extractJsonInt64(item, "id");
        b.name = extractJsonString(item, "name");
        b.baseVoltage_kV = extractJsonDouble(item, "baseVoltage_kV");
        b.area  = static_cast<int>(extractJsonInt64(item, "area"));
        b.zone  = static_cast<int>(extractJsonInt64(item, "zone"));
        b.owner = static_cast<int>(extractJsonInt64(item, "owner"));
        double lat = extractJsonDouble(item, "latitude");
        double lon = extractJsonDouble(item, "longitude");
        if (lat != 0.0 || lon != 0.0) b.location = GeoPoint{lat, lon, 0.0};
        // Store remaining attributes
        std::regex attrRe(R"(("[^"]+")\s*:\s*("[^"]*"|[^,\}]+))");
        std::sregex_iterator it(item.begin(), item.end(), attrRe);
        std::sregex_iterator end;
        while (it != end) {
            std::string k = (*it)[1];
            std::string v = (*it)[2];
            if (k.size() >= 2 && k.front() == '"') k = k.substr(1, k.size() - 2);
            if (v.size() >= 2 && v.front() == '"') v = v.substr(1, v.size() - 2);
            b.attributes[trim(k)] = trim(v);
            ++it;
        }
        ctx.data->buses.push_back(std::move(b));
    }

    // Parse branches
    for (const auto& item : branchesArr) {
        if (token.isCancelled()) break;
        Branch br;
        br.fromBus = extractJsonInt64(item, "fromBus");
        br.toBus   = extractJsonInt64(item, "toBus");
        br.circuitId = extractJsonString(item, "circuitId");
        if (br.circuitId.empty()) br.circuitId = "1";
        br.r_pu    = extractJsonDouble(item, "r_pu");
        br.x_pu    = extractJsonDouble(item, "x_pu");
        br.b_pu    = extractJsonDouble(item, "b_pu");
        br.rateA_MVA = extractJsonDouble(item, "rateA_MVA");
        br.rateB_MVA = extractJsonDouble(item, "rateB_MVA");
        br.rateC_MVA = extractJsonDouble(item, "rateC_MVA");
        br.status  = static_cast<int>(extractJsonInt64(item, "status", 1));
        br.length_km = extractJsonDouble(item, "length_km");
        ctx.data->branches.push_back(std::move(br));
    }

    // Parse transformers
    for (const auto& item : transformersArr) {
        if (token.isCancelled()) break;
        Transformer t;
        t.fromBus = extractJsonInt64(item, "fromBus");
        t.toBus   = extractJsonInt64(item, "toBus");
        t.tertBus = extractJsonInt64(item, "tertBus");
        t.circuitId = extractJsonString(item, "circuitId");
        if (t.circuitId.empty()) t.circuitId = "1";
        t.r12_pu = extractJsonDouble(item, "r12_pu");
        t.x12_pu = extractJsonDouble(item, "x12_pu");
        t.rateA_MVA = extractJsonDouble(item, "rateA_MVA");
        t.windV1_kV = extractJsonDouble(item, "windV1_kV");
        t.windV2_kV = extractJsonDouble(item, "windV2_kV");
        t.status = static_cast<int>(extractJsonInt64(item, "status", 1));
        ctx.data->transformers.push_back(std::move(t));
    }

    // Parse generators
    for (const auto& item : generatorsArr) {
        if (token.isCancelled()) break;
        Generator g;
        g.busId   = extractJsonInt64(item, "busId");
        g.id      = extractJsonString(item, "id");
        if (g.id.empty()) g.id = "1";
        g.pGen_MW  = extractJsonDouble(item, "pGen_MW");
        g.qGen_Mvar = extractJsonDouble(item, "qGen_Mvar");
        g.qMax_Mvar = extractJsonDouble(item, "qMax_Mvar", 9999.0);
        g.qMin_Mvar = extractJsonDouble(item, "qMin_Mvar", -9999.0);
        g.vSet_pu  = extractJsonDouble(item, "vSet_pu", 1.0);
        g.pMax_MW  = extractJsonDouble(item, "pMax_MW");
        g.pMin_MW  = extractJsonDouble(item, "pMin_MW");
        g.status   = static_cast<int>(extractJsonInt64(item, "status", 1));
        g.mBase_MVA = extractJsonDouble(item, "mBase_MVA");
        ctx.data->generators.push_back(std::move(g));
    }

    // Parse loads
    for (const auto& item : loadsArr) {
        if (token.isCancelled()) break;
        Load ld;
        ld.busId  = extractJsonInt64(item, "busId");
        ld.id     = extractJsonString(item, "id");
        if (ld.id.empty()) ld.id = "1";
        ld.pLoad_MW  = extractJsonDouble(item, "pLoad_MW");
        ld.qLoad_Mvar = extractJsonDouble(item, "qLoad_Mvar");
        ld.status    = static_cast<int>(extractJsonInt64(item, "status", 1));
        ctx.data->loads.push_back(std::move(ld));
    }

    // Parse shunts
    for (const auto& item : shuntsArr) {
        if (token.isCancelled()) break;
        Shunt s;
        s.busId  = extractJsonInt64(item, "busId");
        s.id     = extractJsonString(item, "id");
        if (s.id.empty()) s.id = "1";
        s.b_Mvar = extractJsonDouble(item, "b_Mvar");
        s.g_MW   = extractJsonDouble(item, "g_MW");
        s.status = static_cast<int>(extractJsonInt64(item, "status", 1));
        ctx.data->shunts.push_back(std::move(s));
    }

    result.data = ctx.data;
    result.errors = std::move(ctx.errors);
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);
    result.status = token.isCancelled() ? ImportStatus::Cancelled :
                    (result.errors.empty() ? ImportStatus::Success : ImportStatus::Warning);
    ctx.data->metadata["format"] = "JSON";
    return result;
}

// ============================================================================
// XML Parser (generic)
// ============================================================================

FileInfo XmlParser::getInfo() const {
    FileInfo info;
    info.formatName = "XML (Generic Power System Data)";
    info.extensions = supportedExtensions();
    info.properties = {{"description", "Generic XML power system data format"}};
    return info;
}

std::vector<ImportError> XmlParser::validate(const std::string& path) {
    std::vector<ImportError> errs;
    std::ifstream ifs(path);
    if (!ifs) {
        errs.push_back({Severity::Fatal, "FILE_OPEN", "Cannot open: " + path, path, 0, 0});
        return errs;
    }
    std::string line;
    if (std::getline(ifs, line)) {
        if (line.find("<?xml") == std::string::npos && line.find("<") == std::string::npos) {
            errs.push_back({Severity::Warning, "NOT_XML",
                "File does not appear to be XML", path, 1, 0});
        }
    }
    return errs;
}

std::vector<XmlParser::XmlElement> XmlParser::parseXmlElements(const std::string& content) {
    std::vector<XmlElement> out;
    std::size_t pos = 0;

    while (pos < content.size()) {
        auto lt = content.find('<', pos);
        if (lt == std::string::npos) break;
        auto gt = content.find('>', lt);
        if (gt == std::string::npos) break;

        std::string tag = content.substr(lt + 1, gt - lt - 1);
        bool closing = false;
        if (tag.front() == '/') {
            closing = true;
            tag = tag.substr(1);
        }
        bool selfClose = false;
        if (tag.back() == '/') {
            selfClose = true;
            tag.pop_back();
        }

        auto space = tag.find(' ');
        std::string name = (space != std::string::npos) ? tag.substr(0, space) : tag;

        std::map<std::string, std::string> attrs;
        if (space != std::string::npos) {
            std::string attrStr = tag.substr(space + 1);
            std::regex attrRe(R"((\w+)=["']([^"']*)["'])");
            std::sregex_iterator it(attrStr.begin(), attrStr.end(), attrRe);
            std::sregex_iterator end;
            while (it != end) {
                attrs[(*it)[1]] = (*it)[2];
                ++it;
            }
        }

        if (closing) {
            out.push_back({"/" + name, {}, {}});
        } else if (selfClose) {
            out.push_back({name, {}, attrs});
        } else {
            auto closeTag = "</" + name + ">";
            auto closePos = content.find(closeTag, gt + 1);
            std::string text;
            if (closePos != std::string::npos) {
                text = content.substr(gt + 1, closePos - gt - 1);
                pos = closePos + closeTag.size();
            } else {
                pos = gt + 1;
            }
            out.push_back({name, text, attrs});
        }
        if (!closing) pos = gt + 1;
    }
    return out;
}

void XmlParser::processElement(ParseContext& ctx, const XmlElement& el) {
    auto ln = toLower(el.name);

    if (ln == "bus" || ln == "node" || ln == "substation") {
        Bus b;
        b.id = extractXmlInt64(el.attrs, "id");
        b.name = extractXmlAttr(el.attrs, "name");
        b.baseVoltage_kV = extractXmlDouble(el.attrs, "baseVoltage_kV",
                                             extractXmlDouble(el.attrs, "base_kv"));
        b.area  = static_cast<int>(extractXmlInt64(el.attrs, "area"));
        b.zone  = static_cast<int>(extractXmlInt64(el.attrs, "zone"));
        b.owner = static_cast<int>(extractXmlInt64(el.attrs, "owner"));
        double lat = extractXmlDouble(el.attrs, "latitude");
        double lon = extractXmlDouble(el.attrs, "longitude");
        if (lat != 0.0 || lon != 0.0) b.location = GeoPoint{lat, lon, 0.0};
        for (const auto& [k, v] : el.attrs) b.attributes[k] = v;
        b.attributes["text_content"] = el.text;
        ctx.data->buses.push_back(std::move(b));
    } else if (ln == "branch" || ln == "line" || ln == "aclinesegment") {
        Branch br;
        br.fromBus = extractXmlInt64(el.attrs, "fromBus",
                                      extractXmlInt64(el.attrs, "from_bus"));
        br.toBus   = extractXmlInt64(el.attrs, "toBus",
                                      extractXmlInt64(el.attrs, "to_bus"));
        br.circuitId = extractXmlAttr(el.attrs, "circuitId",
                                       extractXmlAttr(el.attrs, "ckt", "1"));
        br.r_pu    = extractXmlDouble(el.attrs, "r_pu", extractXmlDouble(el.attrs, "r"));
        br.x_pu    = extractXmlDouble(el.attrs, "x_pu", extractXmlDouble(el.attrs, "x"));
        br.b_pu    = extractXmlDouble(el.attrs, "b_pu", extractXmlDouble(el.attrs, "b"));
        br.rateA_MVA = extractXmlDouble(el.attrs, "rateA_MVA", extractXmlDouble(el.attrs, "rate_a"));
        br.status  = static_cast<int>(extractXmlInt64(el.attrs, "status", 1));
        for (const auto& [k, v] : el.attrs) br.attributes[k] = v;
        ctx.data->branches.push_back(std::move(br));
    } else if (ln == "transformer" || ln == "powertransformer") {
        Transformer t;
        t.fromBus = extractXmlInt64(el.attrs, "fromBus", extractXmlInt64(el.attrs, "from_bus"));
        t.toBus   = extractXmlInt64(el.attrs, "toBus", extractXmlInt64(el.attrs, "to_bus"));
        t.tertBus = extractXmlInt64(el.attrs, "tertBus", extractXmlInt64(el.attrs, "tert_bus"));
        t.circuitId = extractXmlAttr(el.attrs, "circuitId", extractXmlAttr(el.attrs, "ckt", "1"));
        t.r12_pu = extractXmlDouble(el.attrs, "r_pu", extractXmlDouble(el.attrs, "r"));
        t.x12_pu = extractXmlDouble(el.attrs, "x_pu", extractXmlDouble(el.attrs, "x"));
        t.rateA_MVA = extractXmlDouble(el.attrs, "rateA_MVA", extractXmlDouble(el.attrs, "rate"));
        t.windV1_kV = extractXmlDouble(el.attrs, "windV1_kV", extractXmlDouble(el.attrs, "windv1"));
        t.windV2_kV = extractXmlDouble(el.attrs, "windV2_kV", extractXmlDouble(el.attrs, "windv2"));
        t.status = static_cast<int>(extractXmlInt64(el.attrs, "status", 1));
        for (const auto& [k, v] : el.attrs) t.attributes[k] = v;
        ctx.data->transformers.push_back(std::move(t));
    } else if (ln == "generator" || ln == "synchronousmachine") {
        Generator g;
        g.busId   = extractXmlInt64(el.attrs, "busId", extractXmlInt64(el.attrs, "bus_id"));
        g.id      = extractXmlAttr(el.attrs, "id", "1");
        g.pGen_MW  = extractXmlDouble(el.attrs, "pGen_MW", extractXmlDouble(el.attrs, "pg"));
        g.qGen_Mvar = extractXmlDouble(el.attrs, "qGen_Mvar", extractXmlDouble(el.attrs, "qg"));
        g.qMax_Mvar = extractXmlDouble(el.attrs, "qMax_Mvar", extractXmlDouble(el.attrs, "qt"));
        g.qMin_Mvar = extractXmlDouble(el.attrs, "qMin_Mvar", extractXmlDouble(el.attrs, "qb"));
        g.vSet_pu  = extractXmlDouble(el.attrs, "vSet_pu", extractXmlDouble(el.attrs, "vs", 1.0));
        g.pMax_MW  = extractXmlDouble(el.attrs, "pMax_MW", extractXmlDouble(el.attrs, "pt"));
        g.pMin_MW  = extractXmlDouble(el.attrs, "pMin_MW", extractXmlDouble(el.attrs, "pb"));
        g.status   = static_cast<int>(extractXmlInt64(el.attrs, "status", 1));
        g.mBase_MVA = extractXmlDouble(el.attrs, "mBase_MVA", extractXmlDouble(el.attrs, "mbase"));
        for (const auto& [k, v] : el.attrs) g.attributes[k] = v;
        ctx.data->generators.push_back(std::move(g));
    } else if (ln == "load" || ln == "energyconsumer") {
        Load ld;
        ld.busId  = extractXmlInt64(el.attrs, "busId", extractXmlInt64(el.attrs, "bus_id"));
        ld.id     = extractXmlAttr(el.attrs, "id", "1");
        ld.pLoad_MW  = extractXmlDouble(el.attrs, "pLoad_MW", extractXmlDouble(el.attrs, "pl"));
        ld.qLoad_Mvar = extractXmlDouble(el.attrs, "qLoad_Mvar", extractXmlDouble(el.attrs, "ql"));
        ld.status    = static_cast<int>(extractXmlInt64(el.attrs, "status", 1));
        for (const auto& [k, v] : el.attrs) ld.attributes[k] = v;
        ctx.data->loads.push_back(std::move(ld));
    } else if (ln == "shunt" || ln == "shuntcompensator") {
        Shunt s;
        s.busId  = extractXmlInt64(el.attrs, "busId", extractXmlInt64(el.attrs, "bus_id"));
        s.id     = extractXmlAttr(el.attrs, "id", "1");
        s.b_Mvar = extractXmlDouble(el.attrs, "b_Mvar", extractXmlDouble(el.attrs, "b"));
        s.g_MW   = extractXmlDouble(el.attrs, "g_MW", extractXmlDouble(el.attrs, "g"));
        s.status = static_cast<int>(extractXmlInt64(el.attrs, "status", 1));
        for (const auto& [k, v] : el.attrs) s.attributes[k] = v;
        ctx.data->shunts.push_back(std::move(s));
    }
}

ImportResult XmlParser::load(const std::string& path, CancellationToken& token) {
    auto t0 = std::chrono::steady_clock::now();
    ImportResult result;
    result.fileInfo = getInfo();
    result.fileInfo.path = path;

    ParseContext ctx;
    ctx.data = std::make_shared<PowerSystemData>();
    ctx.token = &token;
    ctx.currentFile = path;

    std::string content = readFileToString(path);
    if (content.empty()) {
        ctx.addError(Severity::Fatal, "EMPTY_FILE", "Cannot read: " + path);
        result.status = ImportStatus::Error;
        result.errors = std::move(ctx.errors);
        return result;
    }

    auto elements = parseXmlElements(content);
    std::stack<std::string> tagStack;

    for (const auto& el : elements) {
        if (token.isCancelled()) break;
        if (el.name.front() == '/') {
            if (!tagStack.empty()) tagStack.pop();
            continue;
        }

        processElement(ctx, el);
        tagStack.push(el.name);

        ++ctx.lineNum;
        if (ctx.lineNum % 1000 == 0) {
            reportProgress("parsing", ctx.lineNum, 0, path);
        }
    }

    result.data = ctx.data;
    result.errors = std::move(ctx.errors);
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);
    result.status = token.isCancelled() ? ImportStatus::Cancelled :
                    (result.errors.empty() ? ImportStatus::Success : ImportStatus::Warning);
    ctx.data->metadata["format"] = "XML";
    return result;
}

std::string XmlParser::extractXmlAttr(const std::map<std::string, std::string>& attrs,
                                        const std::string& key,
                                        const std::string& def) {
    auto it = attrs.find(key);
    return (it != attrs.end()) ? it->second : def;
}

double XmlParser::extractXmlDouble(const std::map<std::string, std::string>& attrs,
                                     const std::string& key, double def) {
    auto it = attrs.find(key);
    return (it != attrs.end()) ? parseDouble(it->second, def) : def;
}

int64_t XmlParser::extractXmlInt64(const std::map<std::string, std::string>& attrs,
                                      const std::string& key, int64_t def) {
    auto it = attrs.find(key);
    return (it != attrs.end()) ? parseInt64(it->second, def) : def;
}

} // namespace powsys365::io
