#pragma once

#include "import_types.h"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace powsys365::io {

// ---------------------------------------------------------------------------
// BaseFileImporter – abstract base for every importer plugin
// ---------------------------------------------------------------------------

class BaseFileImporter {
public:
    BaseFileImporter() = default;
    virtual ~BaseFileImporter() = default;

    // Non-copyable, non-movable (plugins are registered by pointer)
    BaseFileImporter(const BaseFileImporter&) = delete;
    BaseFileImporter& operator=(const BaseFileImporter&) = delete;

    // ----- core interface --------------------------------------------------

    /**
     * Parse the file at `path` and return a fully populated PowerSystemData.
     * If `token` becomes cancelled the implementation should abort early and
     * return a result with status == Cancelled.
     */
    virtual ImportResult load(const std::string& path,
                              CancellationToken& token) = 0;

    /**
     * Fast validation without fully parsing (headers, encoding, row counts).
     */
    virtual std::vector<ImportError> validate(const std::string& path) = 0;

    /**
     * Return human-readable information about this importer.
     */
    virtual FileInfo getInfo() const = 0;

    /**
     * File extensions this importer can read (e.g. ".raw", ".xml").
     */
    virtual std::vector<std::string> supportedExtensions() const = 0;

    // ----- optional helpers ------------------------------------------------

    /**
     * Check whether this importer *claims* to handle the file.
     * Default implementation checks extension + existence.
     */
    virtual bool canHandle(const std::string& path) const {
        auto ext = FileInfo::lowerExtension(path);
        auto sup = supportedExtensions();
        for (const auto& s : sup) {
            std::string sl = s;
            std::transform(sl.begin(), sl.end(), sl.begin(), ::tolower);
            if (sl == ext) return std::ifstream(path).good();
        }
        return false;
    }

    /**
     * Set a progress callback.  The engine calls this before load().
     */
    void setProgressCallback(ProgressCallback cb) { progressCb_ = std::move(cb); }

    /**
     * Access the currently-set progress callback.
     */
    const ProgressCallback& progressCallback() const { return progressCb_; }

protected:
    /**
     * Convenience: report progress to the engine (if a callback is set).
     */
    void reportProgress(const std::string& stage,
                        std::size_t current,
                        std::size_t total,
                        const std::string& currentFile = {}) const {
        if (progressCb_) {
            progressCb_({stage, current, std::max(total, std::size_t(1)), currentFile});
        }
    }

    /**
     * Utility: read entire file into a string.
     */
    static std::string readFileToString(const std::string& path) {
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs) return {};
        std::ostringstream oss;
        oss << ifs.rdbuf();
        return oss.str();
    }

    /**
     * Utility: split a line by delimiter.
     */
    static std::vector<std::string> splitLine(const std::string& line,
                                               char delim = ',',
                                               bool trim = true) {
        std::vector<std::string> parts;
        std::stringstream ss(line);
        std::string item;
        while (std::getline(ss, item, delim)) {
            if (trim) {
                auto a = item.find_first_not_of(" \t\r\n");
                auto b = item.find_last_not_of(" \t\r\n");
                if (a != std::string::npos && b != std::string::npos)
                    item = item.substr(a, b - a + 1);
                else
                    item.clear();
            }
            parts.push_back(item);
        }
        return parts;
    }

    /**
     * Utility: trim whitespace from both ends.
     */
    static std::string trim(const std::string& s) {
        auto a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return {};
        auto b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    }

    /**
     * Utility: convert string to lower case.
     */
    static std::string toLower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        return s;
    }

    /**
     * Utility: safe string to double.
     */
    static double parseDouble(const std::string& s, double defaultVal = 0.0) {
        try { return std::stod(s); } catch (...) { return defaultVal; }
    }

    /**
     * Utility: safe string to int64.
     */
    static int64_t parseInt64(const std::string& s, int64_t defaultVal = 0) {
        try { return std::stoll(s); } catch (...) { return defaultVal; }
    }

    /**
     * Utility: safe string to int.
     */
    static int parseInt(const std::string& s, int defaultVal = 0) {
        try { return std::stoi(s); } catch (...) { return defaultVal; }
    }

private:
    ProgressCallback progressCb_;
};

} // namespace powsys365::io
