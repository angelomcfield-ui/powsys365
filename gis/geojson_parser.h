#pragma once

#include "coordinate_converter.h"
#include "map_renderer.h"
#include <string>
#include <vector>
#include <map>
#include <variant>
#include <memory>
#include <stdexcept>
#include <sstream>

namespace powsys365::gis {

// ============================================================================
// Tipos GeoJSON
// ============================================================================

using GeoJsonValue = std::variant<
    std::nullptr_t,
    bool,
    int,
    double,
    std::string,
    std::vector<double>,                          // Coordenadas
    std::vector<std::vector<double>>,             // LineString coords
    std::vector<std::vector<std::vector<double>>>,// Polygon coords
    std::map<std::string, std::string>,           // Properties simples
    std::shared_ptr<void>                         // Referencias externas
>;

struct GeoJsonFeature {
    std::string type;       // "Point", "LineString", "Polygon"
    std::vector<std::vector<double>> coordinates; // [lon, lat, elev]
    std::map<std::string, std::string> properties;
};

struct GeoJsonFeatureCollection {
    std::string name;
    std::vector<GeoJsonFeature> features;
};

// ============================================================================
// GeoJsonParser
// ============================================================================

class GeoJsonParser {
public:
    GeoJsonParser();

    // --- Parseo ---
    GeoJsonFeatureCollection parse(const std::string& geoJsonText) const;
    GeoJsonFeatureCollection parseFile(const std::string& filename) const;

    // --- Validacion ---
    bool isValid(const std::string& geoJsonText) const;

    // --- Conversion a PowerSystem ---
    PowerSystem toPowerSystem(const GeoJsonFeatureCollection& collection) const;

    // --- Utilidades de parseo ---
    static std::string extractString(const std::string& json, const std::string& key);
    static double extractDouble(const std::string& json, const std::string& key);

private:
    // Parseo interno (mini-parser JSON optimizado para GeoJSON)
    GeoJsonFeatureCollection parseInternal(const std::string& text) const;

    // Extraer array de features
    std::vector<GeoJsonFeature> parseFeatures(const std::string& featuresBlock) const;

    // Parsear una feature individual
    GeoJsonFeature parseFeature(const std::string& featureBlock) const;

    // Parsear geometria
    std::vector<std::vector<double>> parseCoordinates(const std::string& coordsBlock) const;
    std::vector<std::vector<std::vector<double>>> parsePolygonCoords(const std::string& coordsBlock) const;

    // Parsear propiedades
    std::map<std::string, std::string> parseProperties(const std::string& propsBlock) const;

    // Utilidades de string
    static std::string trim(const std::string& s);
    static std::string extractBlock(const std::string& text, size_t startPos);
    static std::vector<std::string> splitFeatures(const std::string& featuresBlock);
    static std::vector<std::string> splitCoordinatePairs(const std::string& coords);
};

// ============================================================================
// GeoJsonExporter
// ============================================================================

class GeoJsonExporter {
public:
    GeoJsonExporter();

    // --- Exportacion ---
    std::string exportToString(const PowerSystem& sys) const;
    std::string exportToString(const GeoJsonFeatureCollection& collection) const;
    bool        exportToFile(const PowerSystem& sys, const std::string& filename) const;
    bool        exportToFile(const GeoJsonFeatureCollection& collection, const std::string& filename) const;

    // --- Configuracion ---
    void setPrettyPrint(bool pretty);
    void setPrecision(int decimals);

    // --- Conversion ---
    GeoJsonFeatureCollection fromPowerSystem(const PowerSystem& sys) const;

    // --- BBOX ---
    std::string generateBbox(const GeoJsonFeatureCollection& collection) const;

private:
    bool prettyPrint_;
    int  precision_;

    std::string featureToString(const GeoJsonFeature& feature, int indent) const;
    std::string coordsToString(const std::vector<std::vector<double>>& coords) const;
    std::string propertiesToString(const std::map<std::string, std::string>& props, int indent) const;

    std::string escapeJson(const std::string& text) const;
    std::string indent(int level) const;
};

} // namespace powsys365::gis
