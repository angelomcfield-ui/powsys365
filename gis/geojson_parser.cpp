#include "geojson_parser.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace powsys365::gis {

// ============================================================================
// Utilidades de string
// ============================================================================

std::string GeoJsonParser::trim(const std::string& s) {
    auto start = s.begin();
    while (start != s.end() && std::isspace(*start)) ++start;
    auto end = s.end();
    while (end != start && std::isspace(*(end - 1))) --end;
    return std::string(start, end);
}

std::string GeoJsonParser::extractBlock(const std::string& text, size_t startPos) {
    if (startPos >= text.size()) return "";
    size_t i = startPos;
    while (i < text.size() && std::isspace(text[i])) ++i;
    if (i >= text.size()) return "";

    char openChar = text[i];
    char closeChar;
    if (openChar == '{') closeChar = '}';
    else if (openChar == '[') closeChar = ']';
    else return "";

    int depth = 0;
    bool inString = false;
    for (; i < text.size(); ++i) {
        if (text[i] == '"' && (i == 0 || text[i - 1] != '\\')) {
            inString = !inString;
            continue;
        }
        if (!inString) {
            if (text[i] == openChar) ++depth;
            else if (text[i] == closeChar) {
                --depth;
                if (depth == 0) return text.substr(startPos, i - startPos + 1);
            }
        }
    }
    return "";
}

std::vector<std::string> GeoJsonParser::splitFeatures(const std::string& featuresBlock) {
    std::vector<std::string> features;
    std::string trimmed = trim(featuresBlock);
    if (trimmed.size() < 2 || trimmed[0] != '[' || trimmed[trimmed.size() - 1] != ']') {
        return features;
    }

    std::string inner = trimmed.substr(1, trimmed.size() - 2);
    size_t i = 0;
    while (i < inner.size()) {
        while (i < inner.size() && std::isspace(inner[i])) ++i;
        if (i >= inner.size()) break;
        if (inner[i] == ',') { ++i; continue; }

        std::string block = extractBlock(inner, i);
        if (!block.empty()) {
            features.push_back(block);
            i += block.size();
        } else {
            ++i;
        }
    }
    return features;
}

std::vector<std::string> GeoJsonParser::splitCoordinatePairs(const std::string& coords) {
    std::vector<std::string> pairs;
    std::string trimmed = trim(coords);
    if (trimmed.size() < 2 || trimmed[0] != '[' || trimmed[trimmed.size() - 1] != ']') {
        return pairs;
    }

    std::string inner = trimmed.substr(1, trimmed.size() - 2);
    size_t i = 0;
    int depth = 0;
    bool inString = false;
    size_t start = 0;

    for (; i < inner.size(); ++i) {
        if (inner[i] == '"' && (i == 0 || inner[i - 1] != '\\')) {
            inString = !inString;
            continue;
        }
        if (!inString) {
            if (inner[i] == '[') {
                if (depth == 0) start = i;
                ++depth;
            } else if (inner[i] == ']') {
                --depth;
                if (depth == 0) {
                    pairs.push_back(inner.substr(start, i - start + 1));
                }
            }
        }
    }
    return pairs;
}

// ============================================================================
// Constructor
// ============================================================================

GeoJsonParser::GeoJsonParser() {}

// ============================================================================
// Parseo principal
// ============================================================================

GeoJsonFeatureCollection GeoJsonParser::parse(const std::string& geoJsonText) const {
    return parseInternal(geoJsonText);
}

GeoJsonFeatureCollection GeoJsonParser::parseFile(const std::string& filename) const {
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("No se pudo abrir archivo: " + filename);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return parse(buffer.str());
}

bool GeoJsonParser::isValid(const std::string& geoJsonText) const {
    std::string trimmed = trim(geoJsonText);
    if (trimmed.empty() || trimmed[0] != '{') return false;
    // Verificar tipo FeatureCollection
    return trimmed.find("\"type\"") != std::string::npos &&
           trimmed.find("\"FeatureCollection\"") != std::string::npos;
}

// ============================================================================
// Parseo interno
// ============================================================================

GeoJsonFeatureCollection GeoJsonParser::parseInternal(const std::string& text) const {
    GeoJsonFeatureCollection collection;

    // Extraer nombre
    size_t namePos = text.find("\"name\"");
    if (namePos != std::string::npos) {
        size_t colonPos = text.find(':', namePos);
        size_t quotePos = text.find('"', colonPos + 1);
        size_t endQuote = text.find('"', quotePos + 1);
        if (quotePos != std::string::npos && endQuote != std::string::npos) {
            collection.name = text.substr(quotePos + 1, endQuote - quotePos - 1);
        }
    }
    if (collection.name.empty()) collection.name = "POWSYS365 Network";

    // Extraer features
    size_t featuresPos = text.find("\"features\"");
    if (featuresPos == std::string::npos) return collection;

    size_t bracketPos = text.find('[', featuresPos);
    if (bracketPos == std::string::npos) return collection;

    std::string featuresBlock = extractBlock(text, bracketPos);
    collection.features = parseFeatures(featuresBlock);

    return collection;
}

std::vector<GeoJsonFeature> GeoJsonParser::parseFeatures(const std::string& featuresBlock) const {
    std::vector<GeoJsonFeature> features;
    std::vector<std::string> featureStrings = splitFeatures(featuresBlock);

    for (const auto& fs : featureStrings) {
        try {
            GeoJsonFeature f = parseFeature(fs);
            if (!f.type.empty()) {
                features.push_back(f);
            }
        } catch (...) {
            // Ignorar features invalidas
        }
    }
    return features;
}

GeoJsonFeature GeoJsonParser::parseFeature(const std::string& featureBlock) const {
    GeoJsonFeature feature;

    // Extraer tipo de geometria
    size_t geomPos = featureBlock.find("\"geometry\"");
    if (geomPos != std::string::npos) {
        size_t typePos = featureBlock.find("\"type\"", geomPos);
        if (typePos != std::string::npos) {
            size_t colonPos = featureBlock.find(':', typePos);
            size_t quotePos = featureBlock.find('"', colonPos + 1);
            size_t endQuote = featureBlock.find('"', quotePos + 1);
            if (quotePos != std::string::npos && endQuote != std::string::npos) {
                feature.type = featureBlock.substr(quotePos + 1, endQuote - quotePos - 1);
            }
        }

        // Extraer coordenadas
        size_t coordsPos = featureBlock.find("\"coordinates\"", geomPos);
        if (coordsPos != std::string::npos) {
            size_t bracketPos = featureBlock.find('[', coordsPos);
            if (bracketPos != std::string::npos) {
                std::string coordsBlock = extractBlock(featureBlock, bracketPos);
                if (feature.type == "Point") {
                    feature.coordinates = parseCoordinates(coordsBlock);
                } else if (feature.type == "LineString") {
                    auto pairs = splitCoordinatePairs(coordsBlock);
                    for (const auto& p : pairs) {
                        auto c = parseCoordinates(p);
                        if (!c.empty()) feature.coordinates.push_back(c[0]);
                    }
                } else if (feature.type == "Polygon") {
                    // Simplificado: tomar el primer anillo
                    auto rings = splitCoordinatePairs(coordsBlock);
                    if (!rings.empty()) {
                        auto pairs = splitCoordinatePairs(rings[0]);
                        for (const auto& p : pairs) {
                            auto c = parseCoordinates(p);
                            if (!c.empty()) feature.coordinates.push_back(c[0]);
                        }
                    }
                }
            }
        }
    }

    // Extraer propiedades
    size_t propsPos = featureBlock.find("\"properties\"");
    if (propsPos != std::string::npos) {
        size_t bracketPos = featureBlock.find('{', propsPos);
        if (bracketPos != std::string::npos) {
            std::string propsBlock = extractBlock(featureBlock, bracketPos);
            feature.properties = parseProperties(propsBlock);
        }
    }

    return feature;
}

std::vector<std::vector<double>> GeoJsonParser::parseCoordinates(const std::string& coordsBlock) const {
    std::vector<std::vector<double>> result;
    std::string trimmed = trim(coordsBlock);
    if (trimmed.size() < 2 || trimmed[0] != '[' || trimmed[trimmed.size() - 1] != ']') {
        return result;
    }

    std::string inner = trimmed.substr(1, trimmed.size() - 2);
    std::vector<double> values;
    std::stringstream ss(inner);
    std::string token;

    while (std::getline(ss, token, ',')) {
        std::string t = trim(token);
        if (!t.empty()) {
            try {
                values.push_back(std::stod(t));
            } catch (...) {
                // Ignorar valores no numericos
            }
        }
    }

    if (!values.empty()) {
        result.push_back(values);
    }
    return result;
}

std::map<std::string, std::string> GeoJsonParser::parseProperties(const std::string& propsBlock) const {
    std::map<std::string, std::string> props;
    std::string trimmed = trim(propsBlock);
    if (trimmed.size() < 2 || trimmed[0] != '{' || trimmed[trimmed.size() - 1] != '}') {
        return props;
    }

    std::string inner = trimmed.substr(1, trimmed.size() - 2);
    size_t i = 0;
    while (i < inner.size()) {
        // Buscar clave
        size_t keyStart = inner.find('"', i);
        if (keyStart == std::string::npos) break;
        size_t keyEnd = inner.find('"', keyStart + 1);
        if (keyEnd == std::string::npos) break;
        std::string key = inner.substr(keyStart + 1, keyEnd - keyStart - 1);

        // Buscar valor
        size_t colonPos = inner.find(':', keyEnd);
        if (colonPos == std::string::npos) break;

        size_t valStart = colonPos + 1;
        while (valStart < inner.size() && std::isspace(inner[valStart])) ++valStart;

        std::string value;
        if (valStart < inner.size() && inner[valStart] == '"') {
            size_t valEnd = inner.find('"', valStart + 1);
            if (valEnd != std::string::npos) {
                value = inner.substr(valStart + 1, valEnd - valStart - 1);
                i = valEnd + 1;
            } else {
                break;
            }
        } else {
            size_t valEnd = inner.find(',', valStart);
            if (valEnd == std::string::npos) valEnd = inner.size();
            value = trim(inner.substr(valStart, valEnd - valStart));
            i = valEnd;
        }

        props[key] = value;

        // Saltar coma
        while (i < inner.size() && (std::isspace(inner[i]) || inner[i] == ',')) ++i;
    }

    return props;
}

std::string GeoJsonParser::extractString(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";
    size_t colonPos = json.find(':', pos);
    if (colonPos == std::string::npos) return "";
    size_t quoteStart = json.find('"', colonPos + 1);
    if (quoteStart == std::string::npos) return "";
    size_t quoteEnd = json.find('"', quoteStart + 1);
    if (quoteEnd == std::string::npos) return "";
    return json.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
}

double GeoJsonParser::extractDouble(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return 0.0;
    size_t colonPos = json.find(':', pos);
    if (colonPos == std::string::npos) return 0.0;
    size_t valStart = colonPos + 1;
    while (valStart < json.size() && std::isspace(json[valStart])) ++valStart;
    try {
        return std::stod(json.substr(valStart));
    } catch (...) {
        return 0.0;
    }
}

// ============================================================================
// Conversion a PowerSystem
// ============================================================================

PowerSystem GeoJsonParser::toPowerSystem(const GeoJsonFeatureCollection& collection) const {
    PowerSystem sys;
    sys.name = collection.name;

    std::map<int, PowerBus> busMap;
    int nextBusId = 1;
    int nextLineId = 1;

    for (const auto& feature : collection.features) {
        if (feature.type == "Point") {
            PowerBus bus;
            auto it = feature.properties.find("busId");
            if (it != feature.properties.end()) {
                bus.busId = std::stoi(it->second);
            } else {
                bus.busId = nextBusId++;
            }

            auto nameIt = feature.properties.find("name");
            bus.name = (nameIt != feature.properties.end()) ? nameIt->second : ("Bus_" + std::to_string(bus.busId));

            auto voltIt = feature.properties.find("voltageKv");
            bus.voltageKv = (voltIt != feature.properties.end()) ? std::stod(voltIt->second) : 0.0;

            auto capIt = feature.properties.find("capacityMw");
            bus.capacityMw = (capIt != feature.properties.end()) ? std::stod(capIt->second) : 0.0;

            auto subIt = feature.properties.find("isSubstation");
            bus.isSubstation = (subIt != feature.properties.end()) ? (subIt->second == "true" || subIt->second == "1") : false;

            if (!feature.coordinates.empty() && !feature.coordinates[0].empty()) {
                bus.location.longitude = feature.coordinates[0][0];
                bus.location.latitude  = feature.coordinates[0].size() > 1 ? feature.coordinates[0][1] : 0.0;
                bus.location.elevation = feature.coordinates[0].size() > 2 ? feature.coordinates[0][2] : 0.0;
            }

            busMap[bus.busId] = bus;
            sys.buses.push_back(bus);
        } else if (feature.type == "LineString") {
            PowerLine line;
            auto idIt = feature.properties.find("lineId");
            line.lineId = (idIt != feature.properties.end()) ? std::stoi(idIt->second) : nextLineId++;

            auto nameIt = feature.properties.find("name");
            line.name = (nameIt != feature.properties.end()) ? nameIt->second : ("Line_" + std::to_string(line.lineId));

            auto fbIt = feature.properties.find("fromBus");
            line.fromBus = (fbIt != feature.properties.end()) ? std::stoi(fbIt->second) : 0;

            auto tbIt = feature.properties.find("toBus");
            line.toBus = (tbIt != feature.properties.end()) ? std::stoi(tbIt->second) : 0;

            auto vIt = feature.properties.find("voltageKv");
            line.voltageKv = (vIt != feature.properties.end()) ? std::stod(vIt->second) : 0.0;

            auto pIt = feature.properties.find("powerMw");
            line.powerMw = (pIt != feature.properties.end()) ? std::stod(pIt->second) : 0.0;

            // Coordenadas como waypoints
            for (const auto& coord : feature.coordinates) {
                if (coord.size() >= 2) {
                    GeographicCoord wp;
                    wp.longitude = coord[0];
                    wp.latitude  = coord[1];
                    wp.elevation = coord.size() > 2 ? coord[2] : 0.0;
                    line.waypoints.push_back(wp);
                }
            }

            sys.lines.push_back(line);
        }
    }

    return sys;
}

// ============================================================================
// GeoJsonExporter
// ============================================================================

GeoJsonExporter::GeoJsonExporter() : prettyPrint_(true), precision_(6) {}

void GeoJsonExporter::setPrettyPrint(bool pretty) {
    prettyPrint_ = pretty;
}

void GeoJsonExporter::setPrecision(int decimals) {
    precision_ = decimals;
}

std::string GeoJsonExporter::escapeJson(const std::string& text) const {
    std::string result;
    result.reserve(text.size() * 2);
    for (char c : text) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b";  break;
            case '\f': result += "\\f";  break;
            case '\n': result += "\\n";  break;
            case '\r': result += "\\r";  break;
            case '\t': result += "\\t";  break;
            default: result += c; break;
        }
    }
    return result;
}

std::string GeoJsonExporter::indent(int level) const {
    if (!prettyPrint_) return "";
    return std::string(level * 2, ' ');
}

std::string GeoJsonExporter::coordsToString(const std::vector<std::vector<double>>& coords) const {
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < coords.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << "[";
        for (size_t j = 0; j < coords[i].size(); ++j) {
            if (j > 0) oss << ", ";
            oss << std::fixed << std::setprecision(precision_) << coords[i][j];
        }
        oss << "]";
    }
    oss << "]";
    return oss.str();
}

std::string GeoJsonExporter::propertiesToString(const std::map<std::string, std::string>& props, int indentLevel) const {
    std::ostringstream oss;
    oss << "{";
    bool first = true;
    for (const auto& kv : props) {
        if (!first) oss << ",";
        first = false;
        if (prettyPrint_) oss << "\n" << indent(indentLevel + 1);
        oss << "\"" << escapeJson(kv.first) << "\": \"" << escapeJson(kv.second) << "\"";
    }
    if (prettyPrint_) oss << "\n" << indent(indentLevel);
    oss << "}";
    return oss.str();
}

std::string GeoJsonExporter::featureToString(const GeoJsonFeature& feature, int indentLevel) const {
    std::ostringstream oss;
    oss << indent(indentLevel) << "{";
    if (prettyPrint_) oss << "\n";

    // type
    if (prettyPrint_) oss << indent(indentLevel + 1);
    oss << "\"type\": \"Feature\"";

    // geometry
    if (prettyPrint_) oss << ",\n" << indent(indentLevel + 1);
    else oss << ", ";
    oss << "\"geometry\": {";
    if (prettyPrint_) oss << "\n" << indent(indentLevel + 2);
    oss << "\"type\": \"" << feature.type << "\"";
    if (prettyPrint_) oss << ",\n" << indent(indentLevel + 2);
    else oss << ", ";
    oss << "\"coordinates\": " << coordsToString(feature.coordinates);
    if (prettyPrint_) oss << "\n" << indent(indentLevel + 1);
    oss << "}";

    // properties
    if (prettyPrint_) oss << ",\n" << indent(indentLevel + 1);
    else oss << ", ";
    oss << "\"properties\": " << propertiesToString(feature.properties, indentLevel + 1);

    if (prettyPrint_) oss << "\n" << indent(indentLevel);
    oss << "}";
    return oss.str();
}

GeoJsonFeatureCollection GeoJsonExporter::fromPowerSystem(const PowerSystem& sys) const {
    GeoJsonFeatureCollection collection;
    collection.name = sys.name;

    // Buses como Points
    for (const auto& bus : sys.buses) {
        GeoJsonFeature feature;
        feature.type = "Point";
        feature.coordinates = {{bus.location.longitude, bus.location.latitude, bus.location.elevation}};
        feature.properties["busId"] = std::to_string(bus.busId);
        feature.properties["name"] = bus.name;
        feature.properties["voltageKv"] = std::to_string(bus.voltageKv);
        feature.properties["capacityMw"] = std::to_string(bus.capacityMw);
        feature.properties["isSubstation"] = bus.isSubstation ? "true" : "false";
        feature.properties["type"] = "bus";
        collection.features.push_back(feature);
    }

    // Lineas como LineStrings
    for (const auto& line : sys.lines) {
        GeoJsonFeature feature;
        feature.type = "LineString";

        // Construir coordenadas: origen + waypoints + destino
        const PowerBus* from = nullptr;
        const PowerBus* to = nullptr;
        for (const auto& bus : sys.buses) {
            if (bus.busId == line.fromBus) from = &bus;
            if (bus.busId == line.toBus)   to   = &bus;
        }

        if (from) {
            feature.coordinates.push_back({from->location.longitude, from->location.latitude, from->location.elevation});
        }
        for (const auto& wp : line.waypoints) {
            feature.coordinates.push_back({wp.longitude, wp.latitude, wp.elevation});
        }
        if (to) {
            feature.coordinates.push_back({to->location.longitude, to->location.latitude, to->location.elevation});
        }

        feature.properties["lineId"] = std::to_string(line.lineId);
        feature.properties["name"] = line.name;
        feature.properties["fromBus"] = std::to_string(line.fromBus);
        feature.properties["toBus"] = std::to_string(line.toBus);
        feature.properties["voltageKv"] = std::to_string(line.voltageKv);
        feature.properties["powerMw"] = std::to_string(line.powerMw);
        feature.properties["currentA"] = std::to_string(line.currentA);
        feature.properties["type"] = "line";
        collection.features.push_back(feature);
    }

    return collection;
}

std::string GeoJsonExporter::exportToString(const GeoJsonFeatureCollection& collection) const {
    std::ostringstream oss;
    oss << "{";
    if (prettyPrint_) oss << "\n";

    if (prettyPrint_) oss << indent(1);
    oss << "\"type\": \"FeatureCollection\"";

    if (prettyPrint_) oss << ",\n" << indent(1);
    else oss << ", ";
    oss << "\"name\": \"" << escapeJson(collection.name) << "\"";

    // BBOX
    if (prettyPrint_) oss << ",\n" << indent(1);
    else oss << ", ";
    oss << "\"bbox\": " << generateBbox(collection);

    // Features
    if (prettyPrint_) oss << ",\n" << indent(1);
    else oss << ", ";
    oss << "\"features\": [";
    if (prettyPrint_ && !collection.features.empty()) oss << "\n";

    for (size_t i = 0; i < collection.features.size(); ++i) {
        oss << featureToString(collection.features[i], 2);
        if (i + 1 < collection.features.size()) {
            oss << ",";
        }
        if (prettyPrint_) oss << "\n";
    }

    if (prettyPrint_ && !collection.features.empty()) oss << indent(1);
    oss << "]";

    if (prettyPrint_) oss << "\n";
    oss << "}";
    return oss.str();
}

std::string GeoJsonExporter::exportToString(const PowerSystem& sys) const {
    return exportToString(fromPowerSystem(sys));
}

bool GeoJsonExporter::exportToFile(const GeoJsonFeatureCollection& collection, const std::string& filename) const {
    std::ofstream file(filename, std::ios::out | std::ios::trunc);
    if (!file.is_open()) return false;
    file << exportToString(collection);
    file.close();
    return !file.fail();
}

bool GeoJsonExporter::exportToFile(const PowerSystem& sys, const std::string& filename) const {
    return exportToFile(fromPowerSystem(sys), filename);
}

std::string GeoJsonExporter::generateBbox(const GeoJsonFeatureCollection& collection) const {
    double minLon = 180.0, minLat = 90.0, maxLon = -180.0, maxLat = -90.0;

    for (const auto& f : collection.features) {
        for (const auto& c : f.coordinates) {
            if (c.size() >= 2) {
                minLon = std::min(minLon, c[0]);
                maxLon = std::max(maxLon, c[0]);
                minLat = std::min(minLat, c[1]);
                maxLat = std::max(maxLat, c[1]);
            }
        }
    }

    std::ostringstream oss;
    oss << "[" << std::fixed << std::setprecision(precision_)
        << minLon << ", " << minLat << ", " << maxLon << ", " << maxLat << "]";
    return oss.str();
}

} // namespace powsys365::gis
