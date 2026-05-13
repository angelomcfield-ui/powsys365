#pragma once

#include "import_types.h"
#include "base_importer.h"
#include "base_exporter.h"
#include <map>
#include <vector>
#include <string>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <algorithm>

namespace powsys365::io {

// ---------------------------------------------------------------------------
// FormatRegistry – thread-safe plugin catalogue
// ---------------------------------------------------------------------------

class FormatRegistry {
public:
    FormatRegistry() = default;
    ~FormatRegistry() = default;

    // No copy / move (global singleton pattern compatible)
    FormatRegistry(const FormatRegistry&) = delete;
    FormatRegistry& operator=(const FormatRegistry&) = delete;

    // ----- importer registration -------------------------------------------

    /**
     * Register an importer.  The registry takes ownership.
     * @return false if an importer for one of the extensions already exists.
     */
    bool registerImporter(std::unique_ptr<BaseFileImporter> importer);

    /**
     * Register an importer (raw pointer version for external lifetime mgmt).
     * The registry does NOT take ownership; caller must ensure lifetime.
     */
    bool registerImporter(BaseFileImporter* importer);

    // ----- exporter registration -------------------------------------------

    bool registerExporter(std::unique_ptr<BaseFileExporter> exporter);
    bool registerExporter(BaseFileExporter* exporter);

    // ----- lookup ----------------------------------------------------------

    /**
     * Find importer by file extension, e.g. ".raw".
     * Thread-safe.  Returns nullptr if not found.
     */
    BaseFileImporter* findImporter(const std::string& ext) const;

    /**
     * Find exporter by file extension.
     */
    BaseFileExporter* findExporter(const std::string& ext) const;

    /**
     * Find importer by full path (uses extension).
     */
    BaseFileImporter* findImporterForPath(const std::string& path) const {
        return findImporter(FileInfo::lowerExtension(path));
    }

    /**
     * Find exporter by full path (uses extension).
     */
    BaseFileExporter* findExporterForPath(const std::string& path) const {
        return findExporter(FileInfo::lowerExtension(path));
    }

    // ----- introspection ---------------------------------------------------

    /**
     * Return a list of all registered format names.
     */
    std::vector<std::string> listFormats() const;

    /**
     * Return all registered importer extensions.
     */
    std::vector<std::string> listImporterExtensions() const;

    /**
     * Return all registered exporter extensions.
     */
    std::vector<std::string> listExporterExtensions() const;

    /**
     * Return FileInfo for every registered importer.
     */
    std::vector<FileInfo> listImporterInfos() const;

    /**
     * Return FileInfo for every registered exporter.
     */
    std::vector<FileInfo> listExporterInfos() const;

    // ----- bulk operations -------------------------------------------------

    /**
     * Remove every registered plugin (useful for unit tests).
     */
    void clear() noexcept;

    /**
     * Total number of unique extensions with importers.
     */
    std::size_t importerCount() const;

    /**
     * Total number of unique extensions with exporters.
     */
    std::size_t exporterCount() const;

    /**
     * Convenience: register the built-in formats that ship with powsys365::io.
     * Call once at application startup.
     */
    void registerBuiltinFormats();

private:
    mutable std::shared_mutex importerMutex_;
    mutable std::shared_mutex exporterMutex_;

    // key = lower-case extension including leading dot, e.g. ".raw"
    std::map<std::string, BaseFileImporter*> importers_;
    std::map<std::string, BaseFileExporter*> exporters_;

    // owned copies (for unique_ptr registration path)
    std::vector<std::unique_ptr<BaseFileImporter>> ownedImporters_;
    std::vector<std::unique_ptr<BaseFileExporter>> ownedExporters_;

    // internal helpers
    static std::string normaliseExt(const std::string& ext);
};

} // namespace powsys365::io
