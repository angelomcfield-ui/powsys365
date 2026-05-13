#include "import_export_engine.h"
#include <chrono>
#include <iostream>

namespace powsys365::io {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ImportExportEngine::ImportExportEngine() {
    // Register all built-in formats automatically
    registry_.registerBuiltinFormats();
}

// ---------------------------------------------------------------------------
// Import
// ---------------------------------------------------------------------------

ImportResult ImportExportEngine::importFile(const std::string& path) {
    internalToken_.reset();
    return importFile(path, internalToken_);
}

ImportResult ImportExportEngine::importFile(const std::string& path,
                                             CancellationToken& token) {
    auto t0 = std::chrono::steady_clock::now();
    ImportResult result;
    result.fileInfo.path = path;

    // --- locate importer ---------------------------------------------------
    auto ext = FileInfo::lowerExtension(path);
    result.fileInfo.extensions = {ext};

    auto* imp = registry_.findImporter(ext);
    if (!imp) {
        // try without leading dot or with different normalisation
        imp = registry_.findImporter(ext.empty() ? "" : ext.substr(1));
    }
    if (!imp) {
        result.status = ImportStatus::Error;
        result.errors.push_back({Severity::Fatal, "NO_IMPORTER",
            "No importer registered for extension '" + ext + "'", path, 0, 0});
        result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0);
        return result;
    }

    result.fileInfo = imp->getInfo();
    result.fileInfo.path = path;
    result.fileInfo.extensions = imp->supportedExtensions();

    injectProgressCallback(imp);

    // --- pre-validation (cheap) -------------------------------------------
    auto preErrs = imp->validate(path);
    bool hasFatal = false;
    for (const auto& e : preErrs) {
        if (e.severity == Severity::Fatal) hasFatal = true;
        result.errors.push_back(e);
    }
    if (hasFatal) {
        result.status = ImportStatus::Error;
        result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0);
        return result;
    }

    // --- parse ------------------------------------------------------------
    result = imp->load(path, token);
    result.fileInfo = imp->getInfo();
    result.fileInfo.path = path;

    if (token.isCancelled()) {
        result.status = ImportStatus::Cancelled;
        result.errors.push_back({Severity::Warning, "CANCELLED",
            "Import cancelled by user", path, 0, 0});
    }

    // --- post-import validation -------------------------------------------
    if (autoValidate_ && result.data && !token.isCancelled()) {
        auto postErrs = validator_.validate(*result.data);
        for (const auto& e : postErrs) result.errors.push_back(e);
        if (!postErrs.empty() && result.status == ImportStatus::Success) {
            result.status = ImportStatus::Warning;
        }
    }

    // --- compute duration & final status ----------------------------------
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);

    if (!result.errors.empty() && result.status == ImportStatus::Success) {
        bool onlyInfo = true;
        for (const auto& e : result.errors) {
            if (e.severity > Severity::Info) { onlyInfo = false; break; }
        }
        if (!onlyInfo) result.status = ImportStatus::Warning;
    }

    return result;
}

// ---------------------------------------------------------------------------
// Export
// ---------------------------------------------------------------------------

ExportResult ImportExportEngine::exportFile(const std::string& path,
                                             const PowerSystemData& data) {
    internalToken_.reset();
    return exportFile(path, data, internalToken_);
}

ExportResult ImportExportEngine::exportFile(const std::string& path,
                                             const PowerSystemData& data,
                                             CancellationToken& token) {
    auto t0 = std::chrono::steady_clock::now();
    ExportResult result;
    result.outputPath = path;

    // --- locate exporter ---------------------------------------------------
    auto ext = FileInfo::lowerExtension(path);
    auto* exp = registry_.findExporter(ext);
    if (!exp) {
        result.status = ImportStatus::Error;
        result.errors.push_back({Severity::Fatal, "NO_EXPORTER",
            "No exporter registered for extension '" + ext + "'", path, 0, 0});
        result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0);
        return result;
    }

    injectProgressCallback(exp);

    // --- validate data before export --------------------------------------
    auto preErrs = exp->validate(data);
    bool hasFatal = false;
    for (const auto& e : preErrs) {
        if (e.severity == Severity::Fatal) hasFatal = true;
        result.errors.push_back(e);
    }
    if (hasFatal) {
        result.status = ImportStatus::Error;
        result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0);
        return result;
    }

    // --- export ------------------------------------------------------------
    result = exp->save(path, data, token);
    result.outputPath = path;

    if (token.isCancelled()) {
        result.status = ImportStatus::Cancelled;
        result.errors.push_back({Severity::Warning, "CANCELLED",
            "Export cancelled by user", path, 0, 0});
    }

    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);
    return result;
}

// ---------------------------------------------------------------------------
// Plugin management
// ---------------------------------------------------------------------------

bool ImportExportEngine::registerImporter(std::unique_ptr<BaseFileImporter> importer) {
    return registry_.registerImporter(std::move(importer));
}

bool ImportExportEngine::registerImporter(BaseFileImporter* importer) {
    return registry_.registerImporter(importer);
}

bool ImportExportEngine::registerExporter(std::unique_ptr<BaseFileExporter> exporter) {
    return registry_.registerExporter(std::move(exporter));
}

bool ImportExportEngine::registerExporter(BaseFileExporter* exporter) {
    return registry_.registerExporter(exporter);
}

// ---------------------------------------------------------------------------
// Introspection
// ---------------------------------------------------------------------------

std::vector<std::string> ImportExportEngine::listSupportedFormats() const {
    return registry_.listFormats();
}

std::vector<FileInfo> ImportExportEngine::listImporterInfos() const {
    return registry_.listImporterInfos();
}

std::vector<FileInfo> ImportExportEngine::listExporterInfos() const {
    return registry_.listExporterInfos();
}

// ---------------------------------------------------------------------------
// Cancellation
// ---------------------------------------------------------------------------

void ImportExportEngine::cancel() noexcept {
    internalToken_.cancel();
}

void ImportExportEngine::resetCancellation() noexcept {
    internalToken_.reset();
}

bool ImportExportEngine::isCancelled() const noexcept {
    return internalToken_.isCancelled();
}

// ---------------------------------------------------------------------------
// Progress callback
// ---------------------------------------------------------------------------

void ImportExportEngine::setProgressCallback(ProgressCallback cb) {
    progressCb_ = std::move(cb);
}

void ImportExportEngine::injectProgressCallback(BaseFileImporter* imp) {
    if (imp && progressCb_) imp->setProgressCallback(progressCb_);
}

void ImportExportEngine::injectProgressCallback(BaseFileExporter* exp) {
    if (exp && progressCb_) exp->setProgressCallback(progressCb_);
}

// ---------------------------------------------------------------------------
// Post-import validation
// ---------------------------------------------------------------------------

void ImportExportEngine::setAutoValidate(bool enable) noexcept {
    autoValidate_ = enable;
}

std::vector<ImportError> ImportExportEngine::validateImportedData(
    const PowerSystemData& data) const {
    return validator_.validate(data);
}

} // namespace powsys365::io
