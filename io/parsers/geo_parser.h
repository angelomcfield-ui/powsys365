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
#include <cmath>
#include <memory>
#include <regex>
#include <stack>
#include <functional>

namespace powsys365::io {

// ============================================================================
// KML Parser (.kml)
// ============================================================================

class KmlParser : public BaseFileImporter {
public:
    KmlParser() = default;
    ~KmlParser() override = default;

    ImportResult load(const std::string& path, CancellationToken& token) override;
    std::vector<ImportError> validate(const std::string& path) override;
    FileInfo getInfo() const override;
    std::vector<std::string> supportedExtensions() const override { return {".kml"}; }

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

    void parsePlacemark(ParseContext& ctx,
                        const std::string& name,
                        const std::map<std::string, std::string>& props,
                        const std::vector<GeoPoint>& coords);
    std::vector<GeoPoint> parseCoordinates(const std::string& text);
    std::map<std::string, std::string> parseExtendedData(const std::string& xml);

    // Simplified XML pull parser for KML
    struct KmlElement {
        std::string name;
        std::string text;
        std::map<std::string, std::string> attrs;
    };
    std::vector<KmlElement> parseKmlElements(const std::string& content);
};

// ============================================================================
// KMZ Parser (.kmz) – ZIP wrapper around KML
// ============================================================================

class KmzParser : public BaseFileImporter {
public:
    KmzParser() = default;
    ~KmzParser() override = default;

    ImportResult load(const std::string& path, CancellationToken& token) override;
    std::vector<ImportError> validate(const std::string& path) override;
    FileInfo getInfo() const override;
    std::vector<std::string> supportedExtensions() const override { return {".kmz"}; }

private:
    // Extract KML string from KMZ (ZIP) file
    // Uses miniz-style interface or falls back to external unzip
    std::string extractKmlFromKmz(const std::string& path,
                                   std::vector<ImportError>& errs);
};

// ============================================================================
// ESRI Shapefile Parser (.shp)
// ============================================================================

class ShpParser : public BaseFileImporter {
public:
    ShpParser() = default;
    ~ShpParser() override = default;

    ImportResult load(const std::string& path, CancellationToken& token) override;
    std::vector<ImportError> validate(const std::string& path) override;
    FileInfo getInfo() const override;
    std::vector<std::string> supportedExtensions() const override { return {".shp"}; }

    void setDbfPath(const std::string& p) { dbfPath_ = p; }
    void setPrjPath(const std::string& p) { prjPath_ = p; }

private:
    std::string dbfPath_;
    std::string prjPath_;

    struct ParseContext {
        std::vector<ImportError> errors;
        std::shared_ptr<PowerSystemData> data;
        std::size_t recordNum = 0;
        std::string currentFile;
        CancellationToken* token = nullptr;

        void addError(Severity sev, const std::string& code, const std::string& msg) {
            errors.push_back({sev, code, msg, currentFile, recordNum, 0});
        }
        bool isCancelled() const { return token && token->isCancelled(); }
    };

    // SHP binary geometry types
    enum ShpShapeType : int {
        SHP_NULL = 0,
        SHP_POINT = 1,
        SHP_POLYLINE = 3,
        SHP_POLYGON = 5,
        SHP_MULTIPOINT = 8,
        SHP_POINT_Z = 11,
        SHP_POLYLINE_Z = 13,
        SHP_POLYGON_Z = 15,
        SHP_MULTIPOINT_Z = 18
    };

    struct ShpHeader {
        int fileCode = 0;
        int version = 0;
        int shapeType = 0;
        double xMin = 0, yMin = 0, xMax = 0, yMax = 0;
        double zMin = 0, zMax = 0, mMin = 0, mMax = 0;
    };

    struct ShpRecord {
        int recordNumber = 0;
        int contentLength = 0;
        int shapeType = 0;
        std::vector<GeoPoint> points;
        std::map<std::string, std::string> dbfAttributes;
    };

    ShpHeader readShpHeader(std::ifstream& ifs);
    ShpRecord readShpRecord(std::ifstream& ifs);
    std::vector<std::map<std::string, std::string>> readDbfRecords(
        const std::string& dbfPath);
    void parseRecord(ParseContext& ctx, const ShpRecord& rec);

    // Byte-swap utilities for big-endian SHP
    static int32_t swapInt32(int32_t v);
    static double swapDouble(double v);
};

// ============================================================================
// GeoJSON Parser (.geojson)
// ============================================================================

class GeoJsonParser : public BaseFileImporter {
public:
    GeoJsonParser() = default;
    ~GeoJsonParser() override = default;

    ImportResult load(const std::string& path, CancellationToken& token) override;
    std::vector<ImportError> validate(const std::string& path) override;
    FileInfo getInfo() const override;
    std::vector<std::string> supportedExtensions() const override { return {".geojson"}; }

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

    void parseFeatureCollection(ParseContext& ctx, const std::string& json);
    void parseFeature(ParseContext& ctx, const std::string& featureJson);
    std::vector<GeoPoint> parseGeometry(const std::string& geomJson);
    std::map<std::string, std::string> parseProperties(const std::string& propJson);

    // Lightweight JSON helpers
    std::string extractJsonString(const std::string& json, const std::string& key);
    std::string extractJsonObject(const std::string& json, const std::string& key);
    std::string extractJsonArray(const std::string& json, const std::string& key);
};

// ============================================================================
// OSM Parser (.osm)
// ============================================================================

class OsmParser : public BaseFileImporter {
public:
    OsmParser() = default;
    ~OsmParser() override = default;

    ImportResult load(const std::string& path, CancellationToken& token) override;
    std::vector<ImportError> validate(const std::string& path) override;
    FileInfo getInfo() const override;
    std::vector<std::string> supportedExtensions() const override { return {".osm"}; }

private:
    struct ParseContext {
        std::vector<ImportError> errors;
        std::shared_ptr<PowerSystemData> data;
        std::size_t lineNum = 0;
        std::string currentFile;
        CancellationToken* token = nullptr;

        // OSM node store
        std::map<int64_t, GeoPoint> nodes;

        void addError(Severity sev, const std::string& code, const std::string& msg) {
            errors.push_back({sev, code, msg, currentFile, lineNum, 0});
        }
        bool isCancelled() const { return token && token->isCancelled(); }
    };

    void parseNode(ParseContext& ctx, const std::map<std::string, std::string>& attrs);
    void parseWay(ParseContext& ctx,
                   const std::map<std::string, std::string>& attrs,
                   const std::vector<int64_t>& nodeRefs,
                   const std::map<std::string, std::string>& tags);
};

// ============================================================================
// Convenience combined GeoImporter
// ============================================================================

class GeoImporter : public BaseFileImporter {
public:
    GeoImporter() {
        kml_  = std::make_unique<KmlParser>();
        kmz_  = std::make_unique<KmzParser>();
        shp_  = std::make_unique<ShpParser>();
        geo_  = std::make_unique<GeoJsonParser>();
        osm_  = std::make_unique<OsmParser>();
    }
    ~GeoImporter() override = default;

    ImportResult load(const std::string& path, CancellationToken& token) override {
        auto ext = FileInfo::lowerExtension(path);
        if (ext == ".kml")      return kml_->load(path, token);
        if (ext == ".kmz")      return kmz_->load(path, token);
        if (ext == ".shp")      return shp_->load(path, token);
        if (ext == ".geojson")  return geo_->load(path, token);
        if (ext == ".osm")      return osm_->load(path, token);
        ImportResult r;
        r.status = ImportStatus::Error;
        r.errors.push_back({Severity::Fatal, "BAD_EXT", "Unknown geo extension: " + ext, path, 0, 0});
        return r;
    }

    std::vector<ImportError> validate(const std::string& path) override {
        auto ext = FileInfo::lowerExtension(path);
        if (ext == ".kml")      return kml_->validate(path);
        if (ext == ".kmz")      return kmz_->validate(path);
        if (ext == ".shp")      return shp_->validate(path);
        if (ext == ".geojson")  return geo_->validate(path);
        if (ext == ".osm")      return osm_->validate(path);
        return {{Severity::Fatal, "BAD_EXT", "Unknown geo extension: " + ext, path, 0, 0}};
    }

    FileInfo getInfo() const override {
        FileInfo info;
        info.formatName = "Geographic Data (KML/KMZ/SHP/GeoJSON/OSM)";
        info.extensions = supportedExtensions();
        info.properties = {{"description", "Combined geographic format importer"}};
        return info;
    }

    std::vector<std::string> supportedExtensions() const override {
        return {".kml", ".kmz", ".shp", ".geojson", ".osm"};
    }

private:
    std::unique_ptr<KmlParser>      kml_;
    std::unique_ptr<KmzParser>      kmz_;
    std::unique_ptr<ShpParser>      shp_;
    std::unique_ptr<GeoJsonParser>  geo_;
    std::unique_ptr<OsmParser>      osm_;
};

} // namespace powsys365::io
