#pragma once

#include "../base_exporter.h"
#include "../import_types.h"
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <map>

namespace powsys365::io {

// ============================================================================
// KML Exporter
// ============================================================================

class KmlExporter : public BaseFileExporter {
public:
    KmlExporter() = default;
    ~KmlExporter() override = default;

    ExportResult save(const std::string& path,
                      const PowerSystemData& data,
                      CancellationToken& token) override;
    std::vector<ImportError> validate(const PowerSystemData& data) override;
    FileInfo getInfo() const override;
    std::vector<std::string> supportedExtensions() const override { return {".kml"}; }

    void setNetworkName(const std::string& name) { networkName_ = name; }

private:
    std::string networkName_ = "POWSYS365 Network";

    std::string escapeXml(const std::string& s);
    std::string exportHeader();
    std::string exportFooter();
    std::string exportBuses(const PowerSystemData& data, CancellationToken& token);
    std::string exportBranches(const PowerSystemData& data, CancellationToken& token);
    std::string exportGenerators(const PowerSystemData& data, CancellationToken& token);
    std::string exportLoads(const PowerSystemData& data, CancellationToken& token);
    std::string exportShunts(const PowerSystemData& data, CancellationToken& token);
};

// ============================================================================
// GeoJSON Exporter
// ============================================================================

class GeoJsonExporter : public BaseFileExporter {
public:
    GeoJsonExporter() = default;
    ~GeoJsonExporter() override = default;

    ExportResult save(const std::string& path,
                      const PowerSystemData& data,
                      CancellationToken& token) override;
    std::vector<ImportError> validate(const PowerSystemData& data) override;
    FileInfo getInfo() const override;
    std::vector<std::string> supportedExtensions() const override { return {".geojson"}; }

private:
    std::string escapeJson(const std::string& s);
    std::string exportFeatureCollection(const PowerSystemData& data, CancellationToken& token);
    std::string exportBusFeatures(const PowerSystemData& data, CancellationToken& token);
    std::string exportBranchFeatures(const PowerSystemData& data, CancellationToken& token);
};

// ============================================================================
// Combined GeoExporter
// ============================================================================

class GeoExporter : public BaseFileExporter {
public:
    GeoExporter() {
        kml_  = std::make_unique<KmlExporter>();
        geo_  = std::make_unique<GeoJsonExporter>();
    }
    ~GeoExporter() override = default;

    ExportResult save(const std::string& path,
                      const PowerSystemData& data,
                      CancellationToken& token) override {
        auto ext = FileInfo::lowerExtension(path);
        if (ext == ".kml")      return kml_->save(path, data, token);
        if (ext == ".geojson")  return geo_->save(path, data, token);
        ExportResult r;
        r.status = ImportStatus::Error;
        r.errors.push_back({Severity::Fatal, "BAD_EXT",
            "Unknown geo extension: " + ext, path, 0, 0});
        return r;
    }

    std::vector<ImportError> validate(const PowerSystemData& data) override {
        return kml_->validate(data);
    }

    FileInfo getInfo() const override {
        FileInfo info;
        info.formatName = "Geographic Data (KML/GeoJSON)";
        info.extensions = supportedExtensions();
        info.properties = {{"description", "Combined geographic format exporter"}};
        return info;
    }

    std::vector<std::string> supportedExtensions() const override {
        return {".kml", ".geojson"};
    }

private:
    std::unique_ptr<KmlExporter>     kml_;
    std::unique_ptr<GeoJsonExporter> geo_;
};

} // namespace powsys365::io
