#include "format_registry.h"
#include <set>
#include "parsers/psse_raw_parser.h"
#include "parsers/cim_parser.h"
#include "parsers/geo_parser.h"
#include "parsers/tabular_parser.h"
#include "exporters/psse_raw_exporter.h"
#include "exporters/cim_exporter.h"
#include "exporters/geo_exporter.h"
#include "exporters/tabular_exporter.h"
#include <iostream>

namespace powsys365::io {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

std::string FormatRegistry::normaliseExt(const std::string& ext) {
    std::string out;
    out.reserve(ext.size() + 1);
    // ensure leading dot
    if (ext.empty() || ext.front() != '.') out.push_back('.');
    for (char c : ext) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

// ---------------------------------------------------------------------------
// Importer registration
// ---------------------------------------------------------------------------

bool FormatRegistry::registerImporter(std::unique_ptr<BaseFileImporter> importer) {
    if (!importer) return false;
    auto exts = importer->supportedExtensions();
    if (exts.empty()) return false;

    std::unique_lock lock(importerMutex_);
    for (const auto& e : exts) {
        auto key = normaliseExt(e);
        if (importers_.count(key)) return false; // collision
    }
    auto* raw = importer.get();
    ownedImporters_.push_back(std::move(importer));
    for (const auto& e : exts) {
        importers_[normaliseExt(e)] = raw;
    }
    return true;
}

bool FormatRegistry::registerImporter(BaseFileImporter* importer) {
    if (!importer) return false;
    auto exts = importer->supportedExtensions();
    if (exts.empty()) return false;

    std::unique_lock lock(importerMutex_);
    for (const auto& e : exts) {
        auto key = normaliseExt(e);
        if (importers_.count(key)) return false;
    }
    for (const auto& e : exts) {
        importers_[normaliseExt(e)] = importer;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Exporter registration
// ---------------------------------------------------------------------------

bool FormatRegistry::registerExporter(std::unique_ptr<BaseFileExporter> exporter) {
    if (!exporter) return false;
    auto exts = exporter->supportedExtensions();
    if (exts.empty()) return false;

    std::unique_lock lock(exporterMutex_);
    for (const auto& e : exts) {
        auto key = normaliseExt(e);
        if (exporters_.count(key)) return false;
    }
    auto* raw = exporter.get();
    ownedExporters_.push_back(std::move(exporter));
    for (const auto& e : exts) {
        exporters_[normaliseExt(e)] = raw;
    }
    return true;
}

bool FormatRegistry::registerExporter(BaseFileExporter* exporter) {
    if (!exporter) return false;
    auto exts = exporter->supportedExtensions();
    if (exts.empty()) return false;

    std::unique_lock lock(exporterMutex_);
    for (const auto& e : exts) {
        auto key = normaliseExt(e);
        if (exporters_.count(key)) return false;
    }
    for (const auto& e : exts) {
        exporters_[normaliseExt(e)] = exporter;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Lookup
// ---------------------------------------------------------------------------

BaseFileImporter* FormatRegistry::findImporter(const std::string& ext) const {
    std::shared_lock lock(importerMutex_);
    auto it = importers_.find(normaliseExt(ext));
    return (it != importers_.end()) ? it->second : nullptr;
}

BaseFileExporter* FormatRegistry::findExporter(const std::string& ext) const {
    std::shared_lock lock(exporterMutex_);
    auto it = exporters_.find(normaliseExt(ext));
    return (it != exporters_.end()) ? it->second : nullptr;
}

// ---------------------------------------------------------------------------
// Introspection
// ---------------------------------------------------------------------------

std::vector<std::string> FormatRegistry::listFormats() const {
    std::vector<std::string> out;
    {
        std::shared_lock lock(importerMutex_);
        for (const auto& [ext, _] : importers_) out.push_back(ext);
    }
    {
        std::shared_lock lock(exporterMutex_);
        for (const auto& [ext, _] : exporters_) {
            if (std::find(out.begin(), out.end(), ext) == out.end())
                out.push_back(ext);
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::vector<std::string> FormatRegistry::listImporterExtensions() const {
    std::shared_lock lock(importerMutex_);
    std::vector<std::string> out;
    for (const auto& [ext, _] : importers_) out.push_back(ext);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> FormatRegistry::listExporterExtensions() const {
    std::shared_lock lock(exporterMutex_);
    std::vector<std::string> out;
    for (const auto& [ext, _] : exporters_) out.push_back(ext);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<FileInfo> FormatRegistry::listImporterInfos() const {
    std::shared_lock lock(importerMutex_);
    std::vector<FileInfo> out;
    out.reserve(importers_.size());
    std::set<BaseFileImporter*> seen;
    for (const auto& [_, imp] : importers_) {
        if (seen.insert(imp).second) out.push_back(imp->getInfo());
    }
    return out;
}

std::vector<FileInfo> FormatRegistry::listExporterInfos() const {
    std::shared_lock lock(exporterMutex_);
    std::vector<FileInfo> out;
    out.reserve(exporters_.size());
    std::set<BaseFileExporter*> seen;
    for (const auto& [_, exp] : exporters_) {
        if (seen.insert(exp).second) out.push_back(exp->getInfo());
    }
    return out;
}

// ---------------------------------------------------------------------------
// Bulk operations
// ---------------------------------------------------------------------------

void FormatRegistry::clear() noexcept {
    {
        std::unique_lock lock(importerMutex_);
        importers_.clear();
        ownedImporters_.clear();
    }
    {
        std::unique_lock lock(exporterMutex_);
        exporters_.clear();
        ownedExporters_.clear();
    }
}

std::size_t FormatRegistry::importerCount() const {
    std::shared_lock lock(importerMutex_);
    // count unique importer objects, not extensions
    std::set<BaseFileImporter*> uniq;
    for (const auto& [_, p] : importers_) uniq.insert(p);
    return uniq.size();
}

std::size_t FormatRegistry::exporterCount() const {
    std::shared_lock lock(exporterMutex_);
    std::set<BaseFileExporter*> uniq;
    for (const auto& [_, p] : exporters_) uniq.insert(p);
    return uniq.size();
}

// ---------------------------------------------------------------------------
// Built-in format registration
// ---------------------------------------------------------------------------

// Forward declarations of all built-in parsers/exporters
namespace {
    // Parsers
    std::unique_ptr<powsys365::io::PsseRawParser>   g_psseParser;
    std::unique_ptr<powsys365::io::CimParser>       g_cimParser;
    std::unique_ptr<powsys365::io::KmlParser>       g_kmlParser;
    std::unique_ptr<powsys365::io::KmzParser>       g_kmzParser;
    std::unique_ptr<powsys365::io::ShpParser>       g_shpParser;
    std::unique_ptr<powsys365::io::GeoJsonParser>   g_geoJsonParser;
    std::unique_ptr<powsys365::io::OsmParser>       g_osmParser;
    std::unique_ptr<powsys365::io::CsvParser>       g_csvParser;
    std::unique_ptr<powsys365::io::XlsxParser>      g_xlsxParser;
    std::unique_ptr<powsys365::io::JsonParser>      g_jsonParser;
    std::unique_ptr<powsys365::io::XmlParser>       g_xmlParser;
    // Combined
    std::unique_ptr<powsys365::io::GeoImporter>     g_geoImporter;
    std::unique_ptr<powsys365::io::TabularImporter> g_tabularImporter;
    // Exporters
    std::unique_ptr<powsys365::io::PsseRawExporter> g_psseExporter;
    std::unique_ptr<powsys365::io::CimExporter>     g_cimExporter;
    std::unique_ptr<powsys365::io::KmlExporter>     g_kmlExporter;
    std::unique_ptr<powsys365::io::GeoJsonExporter> g_geoJsonExporter;
    std::unique_ptr<powsys365::io::CsvExporter>     g_csvExporter;
    std::unique_ptr<powsys365::io::JsonExporter>    g_jsonExporter;
    std::unique_ptr<powsys365::io::XmlExporter>     g_xmlExporter;
    std::unique_ptr<powsys365::io::XlsxExporter>    g_xlsxExporter;
    // Combined
    std::unique_ptr<powsys365::io::GeoExporter>     g_geoExporter;
    std::unique_ptr<powsys365::io::TabularExporter> g_tabularExporter;
}

void FormatRegistry::registerBuiltinFormats() {
    // ---- Parsers -----------------------------------------------------------
    g_psseParser = std::make_unique<powsys365::io::PsseRawParser>();
    registerImporter(g_psseParser.get());

    g_cimParser = std::make_unique<powsys365::io::CimParser>();
    registerImporter(g_cimParser.get());

    g_kmlParser = std::make_unique<powsys365::io::KmlParser>();
    registerImporter(g_kmlParser.get());

    g_kmzParser = std::make_unique<powsys365::io::KmzParser>();
    registerImporter(g_kmzParser.get());

    g_shpParser = std::make_unique<powsys365::io::ShpParser>();
    registerImporter(g_shpParser.get());

    g_geoJsonParser = std::make_unique<powsys365::io::GeoJsonParser>();
    registerImporter(g_geoJsonParser.get());

    g_osmParser = std::make_unique<powsys365::io::OsmParser>();
    registerImporter(g_osmParser.get());

    g_csvParser = std::make_unique<powsys365::io::CsvParser>();
    registerImporter(g_csvParser.get());

    g_xlsxParser = std::make_unique<powsys365::io::XlsxParser>();
    registerImporter(g_xlsxParser.get());

    g_jsonParser = std::make_unique<powsys365::io::JsonParser>();
    registerImporter(g_jsonParser.get());

    g_xmlParser = std::make_unique<powsys365::io::XmlParser>();
    registerImporter(g_xmlParser.get());

    // Combined importers
    g_geoImporter = std::make_unique<powsys365::io::GeoImporter>();
    registerImporter(g_geoImporter.get());

    g_tabularImporter = std::make_unique<powsys365::io::TabularImporter>();
    registerImporter(g_tabularImporter.get());

    // ---- Exporters ---------------------------------------------------------
    g_psseExporter = std::make_unique<powsys365::io::PsseRawExporter>();
    registerExporter(g_psseExporter.get());

    g_cimExporter = std::make_unique<powsys365::io::CimExporter>();
    registerExporter(g_cimExporter.get());

    g_kmlExporter = std::make_unique<powsys365::io::KmlExporter>();
    registerExporter(g_kmlExporter.get());

    g_geoJsonExporter = std::make_unique<powsys365::io::GeoJsonExporter>();
    registerExporter(g_geoJsonExporter.get());

    g_csvExporter = std::make_unique<powsys365::io::CsvExporter>();
    registerExporter(g_csvExporter.get());

    g_jsonExporter = std::make_unique<powsys365::io::JsonExporter>();
    registerExporter(g_jsonExporter.get());

    g_xmlExporter = std::make_unique<powsys365::io::XmlExporter>();
    registerExporter(g_xmlExporter.get());

    g_xlsxExporter = std::make_unique<powsys365::io::XlsxExporter>();
    registerExporter(g_xlsxExporter.get());

    // Combined exporters
    g_geoExporter = std::make_unique<powsys365::io::GeoExporter>();
    registerExporter(g_geoExporter.get());

    g_tabularExporter = std::make_unique<powsys365::io::TabularExporter>();
    registerExporter(g_tabularExporter.get());
}

} // namespace powsys365::io
