#pragma once

#include "import_types.h"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>

namespace powsys365::io {

// ---------------------------------------------------------------------------
// BaseFileExporter – abstract base for every exporter plugin
// ---------------------------------------------------------------------------

class BaseFileExporter {
public:
    BaseFileExporter() = default;
    virtual ~BaseFileExporter() = default;

    BaseFileExporter(const BaseFileExporter&) = delete;
    BaseFileExporter& operator=(const BaseFileExporter&) = delete;

    // ----- core interface --------------------------------------------------

    /**
     * Serialize `data` to `path`.  If `token` becomes cancelled the
     * implementation should abort early and return status == Cancelled.
     */
    virtual ExportResult save(const std::string& path,
                              const PowerSystemData& data,
                              CancellationToken& token) = 0;

    /**
     * Validate that `data` can be exported (e.g. all required fields present).
     */
    virtual std::vector<ImportError> validate(const PowerSystemData& data) = 0;

    /**
     * Return human-readable information about this exporter.
     */
    virtual FileInfo getInfo() const = 0;

    /**
     * File extensions this exporter can write.
     */
    virtual std::vector<std::string> supportedExtensions() const = 0;

    // ----- optional helpers ------------------------------------------------

    virtual bool canHandle(const std::string& path) const {
        auto ext = FileInfo::lowerExtension(path);
        auto sup = supportedExtensions();
        for (const auto& s : sup) {
            std::string sl = s;
            std::transform(sl.begin(), sl.end(), sl.begin(), ::tolower);
            if (sl == ext) return true;
        }
        return false;
    }

    void setProgressCallback(ProgressCallback cb) { progressCb_ = std::move(cb); }
    const ProgressCallback& progressCallback() const { return progressCb_; }

protected:
    void reportProgress(const std::string& stage,
                        std::size_t current,
                        std::size_t total,
                        const std::string& currentFile = {}) const {
        if (progressCb_) {
            progressCb_({stage, current, std::max(total, std::size_t(1)), currentFile});
        }
    }

    // Utility: format a double with fixed precision
    static std::string fmtDouble(double v, int prec = 6) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(prec) << v;
        return oss.str();
    }

    // Utility: pad / truncate string to exact width
    static std::string fmtFixedWidth(const std::string& s, std::size_t w) {
        if (s.size() >= w) return s.substr(0, w);
        return s + std::string(w - s.size(), ' ');
    }

    // Utility: write string to file
    static bool writeStringToFile(const std::string& path, const std::string& content) {
        std::ofstream ofs(path, std::ios::binary);
        if (!ofs) return false;
        ofs << content;
        return ofs.good();
    }

private:
    ProgressCallback progressCb_;
};

} // namespace powsys365::io
