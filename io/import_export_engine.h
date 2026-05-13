#pragma once

#include "import_types.h"
#include "base_importer.h"
#include "base_exporter.h"
#include "format_registry.h"
#include "post_import_validator.h"
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <shared_mutex>

namespace powsys365::io {

// ---------------------------------------------------------------------------
// ImportExportEngine – façade for all I/O operations
// ---------------------------------------------------------------------------

class ImportExportEngine {
public:
    ImportExportEngine();
    ~ImportExportEngine() = default;

    ImportExportEngine(const ImportExportEngine&) = delete;
    ImportExportEngine& operator=(const ImportExportEngine&) = delete;

    // ===== Import ==========================================================

    /**
     * Import a file.  The extension is used to select the appropriate parser.
     */
    ImportResult importFile(const std::string& path);

    /**
     * Import with an externally-supplied cancellation token.
     */
    ImportResult importFile(const std::string& path, CancellationToken& token);

    // ===== Export ==========================================================

    /**
     * Export data to a file.  Extension selects the exporter.
     */
    ExportResult exportFile(const std::string& path, const PowerSystemData& data);

    /**
     * Export with cancellation support.
     */
    ExportResult exportFile(const std::string& path,
                            const PowerSystemData& data,
                            CancellationToken& token);

    // ===== Plugin management ===============================================

    /**
     * Register an importer at runtime.  The engine takes ownership.
     */
    bool registerImporter(std::unique_ptr<BaseFileImporter> importer);

    /**
     * Register an importer (caller retains ownership).
     */
    bool registerImporter(BaseFileImporter* importer);

    /**
     * Register an exporter at runtime.  The engine takes ownership.
     */
    bool registerExporter(std::unique_ptr<BaseFileExporter> exporter);

    /**
     * Register an exporter (caller retains ownership).
     */
    bool registerExporter(BaseFileExporter* exporter);

    // ===== Introspection ===================================================

    /**
     * Return a human-readable list of supported formats.
     */
    std::vector<std::string> listSupportedFormats() const;

    /**
     * Return FileInfo for every importer.
     */
    std::vector<FileInfo> listImporterInfos() const;

    /**
     * Return FileInfo for every exporter.
     */
    std::vector<FileInfo> listExporterInfos() const;

    // ===== Cancellation ====================================================

    /**
     * Request cancellation of the *next* operation.
     */
    void cancel() noexcept;

    /**
     * Reset cancellation flag.
     */
    void resetCancellation() noexcept;

    /**
     * Query cancellation state.
     */
    bool isCancelled() const noexcept;

    // ===== Progress callback ===============================================

    void setProgressCallback(ProgressCallback cb);

    // ===== Post-import validation ==========================================

    /**
     * Enable / disable automatic post-import validation (default: true).
     */
    void setAutoValidate(bool enable) noexcept;

    /**
     * Run validation manually on already-imported data.
     */
    std::vector<ImportError> validateImportedData(const PowerSystemData& data) const;

    /**
     * Access the underlying format registry directly (advanced use).
     */
    FormatRegistry& registry() noexcept { return registry_; }
    const FormatRegistry& registry() const noexcept { return registry_; }

private:
    FormatRegistry registry_;
    CancellationToken internalToken_;
    PostImportValidator validator_;
    ProgressCallback progressCb_;
    bool autoValidate_ = true;

    void injectProgressCallback(BaseFileImporter* imp);
    void injectProgressCallback(BaseFileExporter* exp);
};

} // namespace powsys365::io
