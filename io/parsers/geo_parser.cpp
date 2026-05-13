#include "geo_parser.h"
#include <chrono>
#include <filesystem>
#include <cstring>

namespace powsys365::io {

// ============================================================================
// KML Parser
// ============================================================================

FileInfo KmlParser::getInfo() const {
    FileInfo info;
    info.formatName = "KML (Keyhole Markup Language)";
    info.extensions = supportedExtensions();
    info.encoding   = "UTF-8";
    info.properties = {{"standard", "OGC KML 2.2"},
                        {"description", "Geographic data for Google Earth"}};
    return info;
}

std::vector<ImportError> KmlParser::validate(const std::string& path) {
    std::vector<ImportError> errs;
    std::ifstream ifs(path);
    if (!ifs) {
        errs.push_back({Severity::Fatal, "FILE_OPEN", "Cannot open: " + path, path, 0, 0});
        return errs;
    }
    std::string line;
    if (std::getline(ifs, line)) {
        auto tl = trim(line);
        if (tl.find("<?xml") == std::string::npos &&
            tl.find("<kml") == std::string::npos) {
            errs.push_back({Severity::Warning, "NO_KML_TAG",
                "File may not be valid KML", path, 1, 0});
        }
    }
    ifs.seekg(0, std::ios::end);
    if (ifs.tellg() == 0) {
        errs.push_back({Severity::Fatal, "EMPTY_FILE", "File is empty", path, 0, 0});
    }
    return errs;
}

ImportResult KmlParser::load(const std::string& path, CancellationToken& token) {
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

    auto elements = parseKmlElements(content);
    std::string currentName;
    std::map<std::string, std::string> currentProps;
    std::vector<GeoPoint> currentCoords;
    bool inPlacemark = false;

    for (const auto& el : elements) {
        if (ctx.isCancelled()) break;

        if (el.name == "Placemark") {
            inPlacemark = true;
            currentName.clear();
            currentProps.clear();
            currentCoords.clear();
        } else if (el.name == "/Placemark") {
            if (inPlacemark) {
                parsePlacemark(ctx, currentName, currentProps, currentCoords);
                inPlacemark = false;
            }
        } else if (inPlacemark) {
            if (el.name == "name") {
                currentName = el.text;
            } else if (el.name == "coordinates" || el.name == "coord") {
                currentCoords = parseCoordinates(el.text);
            } else if (el.name.find("Data") != std::string::npos ||
                       el.name.find("SimpleData") != std::string::npos) {
                for (const auto& [k, v] : el.attrs) {
                    currentProps[k] = v;
                }
                if (!el.text.empty()) currentProps[el.name] = el.text;
            } else {
                currentProps[el.name] = el.text;
            }
        }
    }

    result.data = ctx.data;
    result.errors = std::move(ctx.errors);
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);
    result.status = token.isCancelled() ? ImportStatus::Cancelled :
                    (result.errors.empty() ? ImportStatus::Success : ImportStatus::Warning);

    ctx.data->metadata["format"] = "KML";
    return result;
}

std::vector<KmlParser::KmlElement> KmlParser::parseKmlElements(const std::string& content) {
    std::vector<KmlElement> out;
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

        // Extract tag name (before any space)
        auto space = tag.find(' ');
        std::string name = (space != std::string::npos) ? tag.substr(0, space) : tag;

        // Parse attributes
        std::map<std::string, std::string> attrs;
        if (space != std::string::npos) {
            std::string attrStr = tag.substr(space + 1);
            std::regex attrRe(R"((\w+)=["']([^"']*)["'])");
            std::smatch match;
            std::string::const_iterator searchStart(attrStr.cbegin());
            while (std::regex_search(searchStart, attrStr.cend(), match, attrRe)) {
                attrs[match[1]] = match[2];
                searchStart = match.suffix().first;
            }
        }

        if (closing) {
            out.push_back({"/" + name, {}, {}});
        } else if (selfClose) {
            out.push_back({name, {}, attrs});
        } else {
            // Read text content until closing tag
            auto closeTag = "</" + name + ">";
            auto closePos = content.find(closeTag, gt + 1);
            std::string text;
            if (closePos != std::string::npos) {
                text = content.substr(gt + 1, closePos - gt - 1);
                // Strip CDATA wrapper if present
                if (text.substr(0, 9) == "<![CDATA[") {
                    text = text.substr(9, text.size() - 12);
                }
                pos = closePos + closeTag.size();
            } else {
                pos = gt + 1;
            }
            out.push_back({name, text, attrs});
        }

        if (!closing) {
            pos = gt + 1;
        }
    }
    return out;
}

std::vector<GeoPoint> KmlParser::parseCoordinates(const std::string& text) {
    std::vector<GeoPoint> out;
    std::istringstream iss(text);
    std::string coord;
    while (iss >> coord) {
        auto parts = splitLine(coord, ',', false);
        if (parts.size() >= 2) {
            GeoPoint pt;
            pt.longitude = parseDouble(parts[0]);
            pt.latitude  = parseDouble(parts[1]);
            if (parts.size() >= 3) pt.altitude = parseDouble(parts[2]);
            out.push_back(pt);
        }
    }
    return out;
}

void KmlParser::parsePlacemark(ParseContext& ctx,
                                const std::string& name,
                                const std::map<std::string, std::string>& props,
                                const std::vector<GeoPoint>& coords) {
    // Map KML placemarks to power system elements based on properties
    auto itType = props.find("type");
    std::string type = (itType != props.end()) ? toLower(itType->second) : "unknown";

    if (type == "bus" || type == "substation" || name.find("Bus") != std::string::npos) {
        Bus b;
        b.name = name;
        b.id = static_cast<int64_t>(ctx.data->buses.size()) + 1;
        if (!coords.empty()) b.location = coords[0];
        for (const auto& [k, v] : props) b.attributes[k] = v;

        auto itVkV = props.find("basevoltage");
        if (itVkV != props.end()) b.baseVoltage_kV = parseDouble(itVkV->second);

        ctx.data->buses.push_back(std::move(b));
    } else if (type == "line" || type == "branch" || name.find("Line") != std::string::npos) {
        Branch br;
        br.circuitId = name;
        if (coords.size() >= 2) {
            // First and last coordinates
            br.attributes["from_lat"] = std::to_string(coords.front().latitude);
            br.attributes["from_lon"] = std::to_string(coords.front().longitude);
            br.attributes["to_lat"]   = std::to_string(coords.back().latitude);
            br.attributes["to_lon"]   = std::to_string(coords.back().longitude);
        }
        // Store coords for possible later use
        for (const auto& [k, v] : props) br.attributes[k] = v;
        ctx.data->branches.push_back(std::move(br));
    } else {
        // Generic: store as bus by default
        Bus b;
        b.name = name;
        b.id = static_cast<int64_t>(ctx.data->buses.size()) + 1;
        if (!coords.empty()) b.location = coords[0];
        for (const auto& [k, v] : props) b.attributes[k] = v;
        ctx.data->buses.push_back(std::move(b));
    }
}

std::map<std::string, std::string> KmlParser::parseExtendedData(const std::string&) {
    return {}; // Already handled inline
}

// ============================================================================
// KMZ Parser
// ============================================================================

FileInfo KmzParser::getInfo() const {
    FileInfo info;
    info.formatName = "KMZ (Compressed KML)";
    info.extensions = supportedExtensions();
    info.properties = {{"description", "ZIP-compressed KML package"}};
    return info;
}

std::vector<ImportError> KmzParser::validate(const std::string& path) {
    std::vector<ImportError> errs;
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        errs.push_back({Severity::Fatal, "FILE_OPEN", "Cannot open: " + path, path, 0, 0});
        return errs;
    }
    // Check ZIP magic
    char magic[4];
    ifs.read(magic, 4);
    if (magic[0] != 'P' || magic[1] != 'K' || magic[2] != 0x03 || magic[3] != 0x04) {
        errs.push_back({Severity::Warning, "NOT_ZIP",
            "File does not have ZIP magic number", path, 0, 0});
    }
    return errs;
}

std::string KmzParser::extractKmlFromKmz(const std::string& path,
                                          std::vector<ImportError>& errs) {
    // Simplified KMZ extraction: look for doc.kml or any .kml inside the ZIP
    // Production code should use libzip or miniz
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        errs.push_back({Severity::Fatal, "FILE_OPEN", "Cannot open KMZ: " + path, path, 0, 0});
        return {};
    }

    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());

    // Find "doc.kml" in the ZIP central directory
    auto pos = content.find("doc.kml");
    if (pos == std::string::npos) {
        // Try any .kml file
        pos = content.find(".kml");
        if (pos == std::string::npos) {
            errs.push_back({Severity::Error, "NO_KML_IN_KMZ",
                "No .kml file found inside KMZ archive", path, 0, 0});
            return {};
        }
    }

    // Find the local file header just before this entry
    auto headerPos = content.rfind("PK\x03\x04", pos);
    if (headerPos == std::string::npos) {
        errs.push_back({Severity::Error, "CORRUPT_KMZ",
            "Cannot find ZIP local file header", path, 0, 0});
        return {};
    }

    // Parse local file header (simplified)
    if (headerPos + 30 >= content.size()) return {};
    uint32_t compressedSize = *reinterpret_cast<const uint32_t*>(
        content.data() + headerPos + 18);
    uint32_t uncompressedSize = *reinterpret_cast<const uint32_t*>(
        content.data() + headerPos + 22);
    uint16_t nameLen = *reinterpret_cast<const uint16_t*>(
        content.data() + headerPos + 26);
    uint16_t extraLen = *reinterpret_cast<const uint16_t*>(
        content.data() + headerPos + 28);

    // Check compression method (0 = stored)
    uint16_t method = *reinterpret_cast<const uint16_t*>(
        content.data() + headerPos + 8);

    std::size_t dataOffset = headerPos + 30 + nameLen + extraLen;

    if (method == 0) {
        // Stored (uncompressed)
        return content.substr(dataOffset, uncompressedSize);
    } else {
        errs.push_back({Severity::Warning, "COMPRESSED_KML",
            "KML inside KMZ is compressed – decompression not implemented",
            path, 0, 0});
        return {};
    }
}

ImportResult KmzParser::load(const std::string& path, CancellationToken& token) {
    auto t0 = std::chrono::steady_clock::now();
    ImportResult result;
    result.fileInfo = getInfo();
    result.fileInfo.path = path;

    std::vector<ImportError> extractErrs;
    std::string kmlContent = extractKmlFromKmz(path, extractErrs);

    if (kmlContent.empty()) {
        result.status = ImportStatus::Error;
        result.errors = std::move(extractErrs);
        return result;
    }

    // Delegate to KmlParser
    KmlParser kmlParser;
    // Write temp file (or better: extend KmlParser to accept string)
    std::string tmpPath = path + ".extracted.kml";
    {
        std::ofstream ofs(tmpPath, std::ios::binary);
        ofs << kmlContent;
    }
    result = kmlParser.load(tmpPath, token);
    std::remove(tmpPath.c_str());

    for (auto& e : extractErrs) result.errors.push_back(e);
    result.fileInfo = getInfo();
    result.fileInfo.path = path;
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);
    return result;
}

// ============================================================================
// SHP Parser (ESRI Shapefile)
// ============================================================================

FileInfo ShpParser::getInfo() const {
    FileInfo info;
    info.formatName = "ESRI Shapefile";
    info.extensions = supportedExtensions();
    info.properties = {{"standard", "ESRI Shapefile Technical Description"},
                        {"requires", ".shp + .shx + .dbf (+ .prj)"},
                        {"description", "Geographic vector data format"}};
    return info;
}

ShpParser::ShpHeader ShpParser::readShpHeader(std::ifstream& ifs) {
    ShpHeader h;
    ifs.seekg(0);
    char buf[100];
    ifs.read(buf, 100);
    if (ifs.gcount() < 100) return h;

    h.fileCode = swapInt32(*reinterpret_cast<int32_t*>(buf));
    h.version  = *reinterpret_cast<int32_t*>(buf + 28);
    h.shapeType = *reinterpret_cast<int32_t*>(buf + 32);
    std::memcpy(&h.xMin, buf + 36, 64); // 8 doubles
    return h;
}

ShpParser::ShpRecord ShpParser::readShpRecord(std::ifstream& ifs) {
    ShpRecord rec;
    char header[8];
    ifs.read(header, 8);
    if (ifs.gcount() < 8) return rec;

    rec.recordNumber = swapInt32(*reinterpret_cast<int32_t*>(header));
    rec.contentLength = swapInt32(*reinterpret_cast<int32_t*>(header + 4)) * 2;

    if (rec.contentLength <= 0) return rec;

    std::vector<char> content(rec.contentLength);
    ifs.read(content.data(), rec.contentLength);
    if (static_cast<std::streamsize>(ifs.gcount()) < rec.contentLength) return rec;

    rec.shapeType = *reinterpret_cast<int32_t*>(content.data());

    switch (rec.shapeType) {
        case SHP_POINT: {
            if (rec.contentLength >= 20) {
                double x, y;
                std::memcpy(&x, content.data() + 4, 8);
                std::memcpy(&y, content.data() + 12, 8);
                rec.points.push_back({y, x, 0.0});
            }
            break;
        }
        case SHP_POLYLINE:
        case SHP_POLYGON: {
            if (rec.contentLength >= 44) {
                int32_t numParts  = *reinterpret_cast<int32_t*>(content.data() + 36);
                int32_t numPoints = *reinterpret_cast<int32_t*>(content.data() + 40);
                std::size_t partsOffset = 44;
                std::size_t pointsOffset = partsOffset + numParts * 4;
                for (int i = 0; i < numPoints; ++i) {
                    if (pointsOffset + 16 * (i + 1) > static_cast<std::size_t>(rec.contentLength))
                        break;
                    double x, y;
                    std::memcpy(&x, content.data() + pointsOffset + 16 * i, 8);
                    std::memcpy(&y, content.data() + pointsOffset + 16 * i + 8, 8);
                    rec.points.push_back({y, x, 0.0});
                }
            }
            break;
        }
        case SHP_POINT_Z: {
            if (rec.contentLength >= 28) {
                double x, y, z;
                std::memcpy(&x, content.data() + 4, 8);
                std::memcpy(&y, content.data() + 12, 8);
                std::memcpy(&z, content.data() + 20, 8);
                rec.points.push_back({y, x, z});
            }
            break;
        }
        default:
            break;
    }
    return rec;
}

std::vector<std::map<std::string, std::string>> ShpParser::readDbfRecords(
    const std::string& dbfPath) {
    std::vector<std::map<std::string, std::string>> out;
    std::ifstream ifs(dbfPath, std::ios::binary);
    if (!ifs) return out;

    char header[32];
    ifs.read(header, 32);
    if (ifs.gcount() < 32) return out;

    uint16_t headerSize = *reinterpret_cast<uint16_t*>(header + 8);
    uint16_t recordSize = *reinterpret_cast<uint16_t*>(header + 10);
    int numFields = (headerSize - 33) / 32;

    // Read field descriptors
    struct DbfField {
        std::string name;
        char type;
        uint8_t length;
        uint8_t decimals;
    };
    std::vector<DbfField> fields;
    for (int i = 0; i < numFields; ++i) {
        char fbuf[32];
        ifs.read(fbuf, 32);
        DbfField f;
        f.name = std::string(fbuf, 11);
        auto nul = f.name.find('\0');
        if (nul != std::string::npos) f.name = f.name.substr(0, nul);
        f.type = fbuf[11];
        f.length = static_cast<uint8_t>(fbuf[16]);
        f.decimals = static_cast<uint8_t>(fbuf[17]);
        fields.push_back(f);
    }
    ifs.seekg(headerSize); // skip to records

    while (ifs) {
        char flag;
        ifs.read(&flag, 1);
        if (!ifs || flag == 0x1A) break; // EOF marker
        if (flag == ' ') { // valid record
            std::map<std::string, std::string> record;
            for (const auto& f : fields) {
                std::string val(f.length, '\0');
                ifs.read(val.data(), f.length);
                record[f.name] = trim(val);
            }
            out.push_back(std::move(record));
        } else {
            ifs.seekg(recordSize - 1, std::ios::cur);
        }
    }
    return out;
}

void ShpParser::parseRecord(ParseContext& ctx, const ShpRecord& rec) {
    if (rec.points.empty()) return;

    // Map DBF attributes to power system element
    auto itName = rec.dbfAttributes.find("NAME");
    auto itType = rec.dbfAttributes.find("TYPE");
    std::string name = (itName != rec.dbfAttributes.end()) ? itName->second :
                       "Record_" + std::to_string(rec.recordNumber);
    std::string type = (itType != rec.dbfAttributes.end()) ? toLower(itType->second) : "unknown";

    if (type == "bus" || type == "substation" || type == "node") {
        Bus b;
        b.name = name;
        b.id = static_cast<int64_t>(ctx.data->buses.size()) + 1;
        b.location = rec.points[0];
        for (const auto& [k, v] : rec.dbfAttributes) b.attributes[k] = v;

        auto itVkV = rec.dbfAttributes.find("BASEKV");
        if (itVkV != rec.dbfAttributes.end()) b.baseVoltage_kV = parseDouble(itVkV->second);

        ctx.data->buses.push_back(std::move(b));
    } else if (type == "line" || type == "branch" || type == "transformer") {
        Branch br;
        br.circuitId = name;
        if (rec.points.size() >= 2) {
            // Approximate from/to bus assignment
        }
        for (const auto& [k, v] : rec.dbfAttributes) br.attributes[k] = v;
        ctx.data->branches.push_back(std::move(br));
    } else {
        // Default to bus
        Bus b;
        b.name = name;
        b.id = static_cast<int64_t>(ctx.data->buses.size()) + 1;
        b.location = rec.points[0];
        for (const auto& [k, v] : rec.dbfAttributes) b.attributes[k] = v;
        ctx.data->buses.push_back(std::move(b));
    }
}

std::vector<ImportError> ShpParser::validate(const std::string& path) {
    std::vector<ImportError> errs;
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        errs.push_back({Severity::Fatal, "FILE_OPEN", "Cannot open: " + path, path, 0, 0});
        return errs;
    }
    char magic[4];
    ifs.read(magic, 4);
    int32_t fc = swapInt32(*reinterpret_cast<int32_t*>(magic));
    if (fc != 9994) {
        errs.push_back({Severity::Error, "BAD_SHP_MAGIC",
            "Shapefile magic number mismatch (expected 9994, got " + std::to_string(fc) + ")",
            path, 0, 0});
    }
    // Check for companion DBF
    auto dbf = path.substr(0, path.size() - 4) + ".dbf";
    std::ifstream dbfCheck(dbf);
    if (!dbfCheck) {
        errs.push_back({Severity::Warning, "NO_DBF",
            "Companion .dbf file not found: " + dbf, path, 0, 0});
    }
    return errs;
}

ImportResult ShpParser::load(const std::string& path, CancellationToken& token) {
    auto t0 = std::chrono::steady_clock::now();
    ImportResult result;
    result.fileInfo = getInfo();
    result.fileInfo.path = path;

    ParseContext ctx;
    ctx.data = std::make_shared<PowerSystemData>();
    ctx.token = &token;
    ctx.currentFile = path;

    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        ctx.addError(Severity::Fatal, "FILE_OPEN", "Cannot open: " + path);
        result.status = ImportStatus::Error;
        result.errors = std::move(ctx.errors);
        return result;
    }

    auto header = readShpHeader(ifs);
    if (header.fileCode != 9994) {
        ctx.addError(Severity::Fatal, "BAD_SHP", "Invalid Shapefile header");
        result.status = ImportStatus::Error;
        result.errors = std::move(ctx.errors);
        return result;
    }

    // Read DBF records
    auto dbf = path.substr(0, path.size() - 4) + ".dbf";
    auto dbfRecords = readDbfRecords(dbf);

    // Read shape records
    std::size_t recIdx = 0;
    while (ifs && !token.isCancelled()) {
        auto rec = readShpRecord(ifs);
        if (rec.recordNumber == 0) break;
        ++ctx.recordNum;

        if (recIdx < dbfRecords.size()) {
            rec.dbfAttributes = dbfRecords[recIdx];
        }
        parseRecord(ctx, rec);
        ++recIdx;

        if (recIdx % 1000 == 0) {
            reportProgress("parsing", recIdx, dbfRecords.size(), path);
        }
    }

    result.data = ctx.data;
    result.errors = std::move(ctx.errors);
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);
    result.status = token.isCancelled() ? ImportStatus::Cancelled :
                    (result.errors.empty() ? ImportStatus::Success : ImportStatus::Warning);
    ctx.data->metadata["shapeType"] = std::to_string(header.shapeType);
    return result;
}

int32_t ShpParser::swapInt32(int32_t v) {
    return ((v >> 24) & 0xFF)       |
           ((v >>  8) & 0xFF00)     |
           ((v <<  8) & 0xFF0000)   |
           ((v << 24) & 0xFF000000);
}

double ShpParser::swapDouble(double v) {
    // On little-endian systems, SHP uses little-endian for content
    // but big-endian for headers. This swap is for the header.
    union { double d; uint64_t u; } in{v}, out;
    out.u = ((in.u >> 56) & 0xFFULL)       |
            ((in.u >> 40) & 0xFF00ULL)     |
            ((in.u >> 24) & 0xFF0000ULL)   |
            ((in.u >>  8) & 0xFF000000ULL) |
            ((in.u <<  8) & 0xFF00000000ULL)   |
            ((in.u << 24) & 0xFF0000000000ULL) |
            ((in.u << 40) & 0xFF000000000000ULL) |
            ((in.u << 56) & 0xFF00000000000000ULL);
    return out.d;
}

// ============================================================================
// GeoJSON Parser
// ============================================================================

FileInfo GeoJsonParser::getInfo() const {
    FileInfo info;
    info.formatName = "GeoJSON";
    info.extensions = supportedExtensions();
    info.properties = {{"standard", "RFC 7946"},
                        {"description", "Geographic JSON format"}};
    return info;
}

std::vector<ImportError> GeoJsonParser::validate(const std::string& path) {
    std::vector<ImportError> errs;
    std::ifstream ifs(path);
    if (!ifs) {
        errs.push_back({Severity::Fatal, "FILE_OPEN", "Cannot open: " + path, path, 0, 0});
        return errs;
    }
    std::string line;
    if (std::getline(ifs, line)) {
        if (line.find('"') == std::string::npos && line.find('{') == std::string::npos) {
            errs.push_back({Severity::Warning, "NOT_JSON",
                "File does not appear to be JSON", path, 1, 0});
        }
    }
    ifs.seekg(0, std::ios::end);
    if (ifs.tellg() == 0) {
        errs.push_back({Severity::Fatal, "EMPTY_FILE", "File is empty", path, 0, 0});
    }
    return errs;
}

ImportResult GeoJsonParser::load(const std::string& path, CancellationToken& token) {
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

    parseFeatureCollection(ctx, content);

    result.data = ctx.data;
    result.errors = std::move(ctx.errors);
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);
    result.status = token.isCancelled() ? ImportStatus::Cancelled :
                    (result.errors.empty() ? ImportStatus::Success : ImportStatus::Warning);
    ctx.data->metadata["format"] = "GeoJSON";
    return result;
}

void GeoJsonParser::parseFeatureCollection(ParseContext& ctx, const std::string& json) {
    auto features = extractJsonArray(json, "features");
    if (features.empty()) {
        // Try direct geometry
        auto coords = extractJsonArray(json, "coordinates");
        if (!coords.empty()) {
            auto geom = parseGeometry(coords);
            if (!geom.empty()) {
                Bus b;
                b.name = "Geometry";
                b.id = 1;
                b.location = geom[0];
                ctx.data->buses.push_back(std::move(b));
            }
        }
        return;
    }

    // Split feature array
    std::size_t pos = 0;
    std::vector<std::string> featureStrs;
    int depth = 0;
    std::size_t start = 0;
    while (pos < features.size()) {
        if (features[pos] == '{') {
            if (depth == 0) start = pos;
            ++depth;
        } else if (features[pos] == '}') {
            --depth;
            if (depth == 0) {
                featureStrs.push_back(features.substr(start, pos - start + 1));
            }
        }
        ++pos;
    }

    for (std::size_t i = 0; i < featureStrs.size() && !ctx.isCancelled(); ++i) {
        parseFeature(ctx, featureStrs[i]);
        if (i % 100 == 0) {
            reportProgress("parsing", i, featureStrs.size(), ctx.currentFile);
        }
    }
}

void GeoJsonParser::parseFeature(ParseContext& ctx, const std::string& featureJson) {
    auto geom = extractJsonObject(featureJson, "geometry");
    auto props = extractJsonObject(featureJson, "properties");

    auto coords = parseGeometry(geom);
    auto properties = parseProperties(props);

    if (coords.empty()) return;

    auto itName = properties.find("name");
    std::string name = (itName != properties.end()) ? itName->second : "Feature";
    auto itType = properties.find("type");
    std::string type = (itType != properties.end()) ? toLower(itType->second) : "bus";

    if (type == "line" || type == "branch") {
        Branch br;
        br.circuitId = name;
        for (const auto& [k, v] : properties) br.attributes[k] = v;
        ctx.data->branches.push_back(std::move(br));
    } else {
        Bus b;
        b.name = name;
        b.id = static_cast<int64_t>(ctx.data->buses.size()) + 1;
        b.location = coords[0];

        auto itVkV = properties.find("basevoltage");
        if (itVkV != properties.end()) b.baseVoltage_kV = parseDouble(itVkV->second);

        for (const auto& [k, v] : properties) b.attributes[k] = v;
        ctx.data->buses.push_back(std::move(b));
    }
}

std::vector<GeoPoint> GeoJsonParser::parseGeometry(const std::string& geomJson) {
    std::vector<GeoPoint> out;
    if (geomJson.empty()) return out;

    // Extract coordinates array string
    auto coords = extractJsonArray(geomJson, "coordinates");
    if (coords.empty()) return out;

    // Parse coordinate array: [lon, lat] or [lon, lat, alt] or nested
    std::regex coordRe(R"(-?\d+\.?\d*)");
    std::sregex_iterator it(coords.begin(), coords.end(), coordRe);
    std::sregex_iterator end;
    std::vector<double> nums;
    while (it != end) {
        nums.push_back(std::stod((*it)[0]));
        ++it;
    }

    // GeoJSON: [lon, lat] pairs
    for (std::size_t i = 0; i + 1 < nums.size(); i += 2) {
        out.push_back({nums[i + 1], nums[i], 0.0});
    }
    return out;
}

std::map<std::string, std::string> GeoJsonParser::parseProperties(const std::string& propJson) {
    std::map<std::string, std::string> out;
    if (propJson.empty()) return out;

    std::regex kvRe(R"(("[^"]+")\s*:\s*("[^"]*"|[^,\}]+))");
    std::sregex_iterator it(propJson.begin(), propJson.end(), kvRe);
    std::sregex_iterator end;
    while (it != end) {
        std::string key = (*it)[1];
        std::string val = (*it)[2];
        // Strip quotes
        if (key.size() >= 2 && key.front() == '"') key = key.substr(1, key.size() - 2);
        if (val.size() >= 2 && val.front() == '"') val = val.substr(1, val.size() - 2);
        out[trim(key)] = trim(val);
        ++it;
    }
    return out;
}

std::string GeoJsonParser::extractJsonString(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return {};
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return {};
    ++pos; // skip ':'
    while (pos < json.size() && std::isspace(json[pos])) ++pos;
    if (pos < json.size() && json[pos] == '"') {
        ++pos;
        auto end = json.find('"', pos);
        if (end != std::string::npos) return json.substr(pos, end - pos);
    }
    return {};
}

std::string GeoJsonParser::extractJsonObject(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return {};
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return {};
    ++pos;
    while (pos < json.size() && std::isspace(json[pos])) ++pos;
    if (pos >= json.size() || json[pos] != '{') return {};
    int depth = 1;
    std::size_t start = pos;
    ++pos;
    while (pos < json.size() && depth > 0) {
        if (json[pos] == '{') ++depth;
        else if (json[pos] == '}') --depth;
        else if (json[pos] == '"') {
            ++pos;
            while (pos < json.size() && json[pos] != '"') {
                if (json[pos] == '\\') ++pos;
                ++pos;
            }
        }
        ++pos;
    }
    return json.substr(start, pos - start);
}

std::string GeoJsonParser::extractJsonArray(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return {};
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return {};
    ++pos;
    while (pos < json.size() && std::isspace(json[pos])) ++pos;
    if (pos >= json.size() || json[pos] != '[') return {};
    int depth = 1;
    std::size_t start = pos;
    ++pos;
    while (pos < json.size() && depth > 0) {
        if (json[pos] == '[') ++depth;
        else if (json[pos] == ']') --depth;
        else if (json[pos] == '"') {
            ++pos;
            while (pos < json.size() && json[pos] != '"') {
                if (json[pos] == '\\') ++pos;
                ++pos;
            }
        }
        ++pos;
    }
    return json.substr(start, pos - start);
}

// ============================================================================
// OSM Parser
// ============================================================================

FileInfo OsmParser::getInfo() const {
    FileInfo info;
    info.formatName = "OSM (OpenStreetMap XML)";
    info.extensions = supportedExtensions();
    info.properties = {{"standard", "OpenStreetMap XML Format"},
                        {"description", "OpenStreetMap data export"}};
    return info;
}

std::vector<ImportError> OsmParser::validate(const std::string& path) {
    std::vector<ImportError> errs;
    std::ifstream ifs(path);
    if (!ifs) {
        errs.push_back({Severity::Fatal, "FILE_OPEN", "Cannot open: " + path, path, 0, 0});
        return errs;
    }
    std::string line;
    if (std::getline(ifs, line)) {
        if (line.find("<osm") == std::string::npos && line.find("<?xml") == std::string::npos) {
            errs.push_back({Severity::Warning, "NOT_OSM",
                "File does not appear to be OSM XML", path, 1, 0});
        }
    }
    return errs;
}

ImportResult OsmParser::load(const std::string& path, CancellationToken& token) {
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

    std::map<std::string, std::string> currentNodeAttrs;
    std::map<std::string, std::string> currentWayTags;
    std::vector<int64_t> currentWayNodeRefs;
    std::string currentWayId;
    bool inWay = false;
    bool inNode = false;

    std::string line;
    while (std::getline(ifs, line) && !token.isCancelled()) {
        ++ctx.lineNum;
        auto tl = trim(line);
        if (tl.empty()) continue;

        // Parse node
        if (tl.find("<node") != std::string::npos) {
            inNode = true;
            inWay = false;
            // Extract attributes
            std::regex attrRe(R"((\w+)=["']([^"']*)["'])");
            std::sregex_iterator it(tl.begin(), tl.end(), attrRe);
            std::sregex_iterator end;
            currentNodeAttrs.clear();
            while (it != end) {
                currentNodeAttrs[(*it)[1]] = (*it)[2];
                ++it;
            }
            parseNode(ctx, currentNodeAttrs);

            // Check for inline closing
            if (tl.find("/>") != std::string::npos) {
                inNode = false;
            }
        } else if (tl.find("</node>") != std::string::npos) {
            inNode = false;
        }

        // Parse way
        if (tl.find("<way") != std::string::npos) {
            inWay = true;
            inNode = false;
            currentWayTags.clear();
            currentWayNodeRefs.clear();
            std::regex attrRe(R"((\w+)=["']([^"']*)["'])");
            std::sregex_iterator it(tl.begin(), tl.end(), attrRe);
            std::sregex_iterator end;
            while (it != end) {
                if ((*it)[1] == "id") currentWayId = (*it)[2];
                ++it;
            }
        } else if (inWay && tl.find("<nd") != std::string::npos) {
            auto refPos = tl.find("ref=\"");
            if (refPos != std::string::npos) {
                refPos += 5;
                auto endPos = tl.find('"', refPos);
                if (endPos != std::string::npos) {
                    currentWayNodeRefs.push_back(parseInt64(tl.substr(refPos, endPos - refPos)));
                }
            }
        } else if (inWay && tl.find("<tag") != std::string::npos) {
            std::string k, v;
            auto kPos = tl.find("k=\"");
            if (kPos != std::string::npos) {
                kPos += 3;
                auto kEnd = tl.find('"', kPos);
                if (kEnd != std::string::npos) k = tl.substr(kPos, kEnd - kPos);
            }
            auto vPos = tl.find("v=\"");
            if (vPos != std::string::npos) {
                vPos += 3;
                auto vEnd = tl.find('"', vPos);
                if (vEnd != std::string::npos) v = tl.substr(vPos, vEnd - vPos);
            }
            if (!k.empty()) currentWayTags[k] = v;
        } else if (tl.find("</way>") != std::string::npos) {
            if (inWay) {
                parseWay(ctx, {{"id", currentWayId}}, currentWayNodeRefs, currentWayTags);
                inWay = false;
            }
        }
    }

    result.data = ctx.data;
    result.errors = std::move(ctx.errors);
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);
    result.status = token.isCancelled() ? ImportStatus::Cancelled :
                    (result.errors.empty() ? ImportStatus::Success : ImportStatus::Warning);
    ctx.data->metadata["format"] = "OSM";
    ctx.data->metadata["nodes_parsed"] = std::to_string(ctx.nodes.size());
    return result;
}

void OsmParser::parseNode(ParseContext& ctx, const std::map<std::string, std::string>& attrs) {
    auto itId = attrs.find("id");
    auto itLat = attrs.find("lat");
    auto itLon = attrs.find("lon");
    if (itId == attrs.end() || itLat == attrs.end() || itLon == attrs.end()) return;

    int64_t id = parseInt64(itId->second);
    double lat = parseDouble(itLat->second);
    double lon = parseDouble(itLon->second);
    ctx.nodes[id] = {lat, lon, 0.0};

    // Create bus entry for power-relevant nodes
    Bus b;
    b.name = "Node_" + itId->second;
    b.id = id;
    b.location = {lat, lon, 0.0};
    ctx.data->buses.push_back(std::move(b));
}

void OsmParser::parseWay(ParseContext& ctx,
                          const std::map<std::string, std::string>& attrs,
                          const std::vector<int64_t>& nodeRefs,
                          const std::map<std::string, std::string>& tags) {
    if (nodeRefs.size() < 2) return;

    // Map OSM power tags to power system elements
    auto itPower = tags.find("power");
    std::string powerType = (itPower != tags.end()) ? toLower(itPower->second) : "";

    auto itName = tags.find("name");
    std::string name = (itName != tags.end()) ? itName->second :
                       ("Way_" + (attrs.count("id") ? attrs.at("id") : "?"));

    if (powerType == "line" || powerType == "cable" || powerType == "minor_line") {
        Branch br;
        br.circuitId = name;
        // Find coordinates for first and last nodes
        auto fromIt = ctx.nodes.find(nodeRefs.front());
        auto toIt = ctx.nodes.find(nodeRefs.back());
        if (fromIt != ctx.nodes.end()) {
            br.attributes["from_lat"] = std::to_string(fromIt->second.latitude);
            br.attributes["from_lon"] = std::to_string(fromIt->second.longitude);
        }
        if (toIt != ctx.nodes.end()) {
            br.attributes["to_lat"] = std::to_string(toIt->second.latitude);
            br.attributes["to_lon"] = std::to_string(toIt->second.longitude);
        }
        // Find approximate from/to bus IDs
        br.fromBus = nodeRefs.front();
        br.toBus = nodeRefs.back();

        auto itVoltage = tags.find("voltage");
        if (itVoltage != tags.end()) {
            // Voltage might be semicolon-separated list
            auto vStr = itVoltage->second;
            auto firstV = vStr.find(';') != std::string::npos ?
                          vStr.substr(0, vStr.find(';')) : vStr;
            br.attributes["voltage"] = firstV;
        }

        for (const auto& [k, v] : tags) br.attributes[k] = v;
        ctx.data->branches.push_back(std::move(br));
    } else if (powerType == "substation" || powerType == "sub_station") {
        Bus b;
        b.name = name;
        b.id = parseInt64(attrs.count("id") ? attrs.at("id") : "0");
        auto itVkV = tags.find("voltage");
        if (itVkV != tags.end()) b.baseVoltage_kV = parseDouble(itVkV->second) / 1000.0;
        for (const auto& [k, v] : tags) b.attributes[k] = v;

        // Calculate centroid from nodeRefs
        double sumLat = 0, sumLon = 0;
        int count = 0;
        for (auto ref : nodeRefs) {
            auto it = ctx.nodes.find(ref);
            if (it != ctx.nodes.end()) {
                sumLat += it->second.latitude;
                sumLon += it->second.longitude;
                ++count;
            }
        }
        if (count > 0) {
            b.location = {sumLat / count, sumLon / count, 0.0};
        }
        ctx.data->buses.push_back(std::move(b));
    } else {
        // Default: store as branch with geometry
        Branch br;
        br.circuitId = name;
        br.fromBus = nodeRefs.front();
        br.toBus = nodeRefs.back();
        for (const auto& [k, v] : tags) br.attributes[k] = v;
        ctx.data->branches.push_back(std::move(br));
    }
}

} // namespace powsys365::io
