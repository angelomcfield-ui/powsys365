#include "ide_config.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdlib>

namespace powsys365::config {

/* ================================================================
   String helpers
   ================================================================ */

std::string IDEConfigPanel::themeToString(EditorTheme t) {
    switch (t) { case EditorTheme::Default: return "default"; case EditorTheme::Dark: return "dark";
        case EditorTheme::Light: return "light"; case EditorTheme::Monokai: return "monokai";
        case EditorTheme::Solarized: return "solarized"; case EditorTheme::Dracula: return "dracula";
        case EditorTheme::OneDark: return "onedark"; case EditorTheme::Custom: return "custom"; }
    return "default";
}
IDEConfigPanel::EditorTheme IDEConfigPanel::stringToTheme(const std::string& s) {
    std::string l; for (char c : s) l += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (l == "dark") return EditorTheme::Dark; if (l == "light") return EditorTheme::Light;
    if (l == "monokai") return EditorTheme::Monokai; if (l == "solarized") return EditorTheme::Solarized;
    if (l == "dracula") return EditorTheme::Dracula; if (l == "onedark") return EditorTheme::OneDark;
    if (l == "custom") return EditorTheme::Custom;
    return EditorTheme::Default;
}

/* ================================================================
   PIMPL
   ================================================================ */

class IDEConfigPanel::Impl {
public:
    EditorSettings  editor_;
    LSPConfig       lspGlobal_;
    std::vector<LSPServer> lspServers_;
    DebuggerConfig  debugger_;
    std::vector<Plugin> plugins_;
    bool            hasChanges_ = false;

    Impl() {
        editor_.rulers = {80, 120};

        // Default LSP servers
        lspServers_.push_back({"cpp", "clangd", {}, "compile_commands.json,.git", 0, {}});
        lspServers_.push_back({"python", "pylsp", {}, "setup.py,.git", 0, {}});
        lspServers_.push_back({"javascript", "typescript-language-server", {"--stdio"}, "package.json,.git", 0, {}});
        lspServers_.push_back({"rust", "rust-analyzer", {}, "Cargo.toml,.git", 0, {}});
        lspServers_.push_back({"go", "gopls", {}, "go.mod,.git", 0, {}});

        // Default plugins
        plugins_.push_back({"editor.git", "Git Integration", "1.0.0", "POWSYS365",
                             "Git blame, diff, and log viewer", "", true, true, {}});
        plugins_.push_back({"editor.linter", "Code Linter", "1.0.0", "POWSYS365",
                             "Real-time code linting", "", true, true, {}});
        plugins_.push_back({"editor.formatter", "Code Formatter", "1.0.0", "POWSYS365",
                             "Auto-format on save", "", true, true, {}});
    }
};

/* ================================================================
   Construction
   ================================================================ */

IDEConfigPanel::IDEConfigPanel() : pImpl(std::make_unique<Impl>()) {}
IDEConfigPanel::~IDEConfigPanel() = default;

/* ================================================================
   Editor
   ================================================================ */

void IDEConfigPanel::setEditorSettings(const EditorSettings& s) { pImpl->editor_ = s; pImpl->hasChanges_ = true; }
IDEConfigPanel::EditorSettings IDEConfigPanel::editorSettings() const { return pImpl->editor_; }

void IDEConfigPanel::setTabSize(int s)         { pImpl->editor_.tabSize = s; pImpl->hasChanges_ = true; }
int  IDEConfigPanel::tabSize() const           { return pImpl->editor_.tabSize; }

void IDEConfigPanel::setFontSize(int s)        { pImpl->editor_.fontSize = s; pImpl->hasChanges_ = true; }
int  IDEConfigPanel::fontSize() const          { return pImpl->editor_.fontSize; }

void IDEConfigPanel::setFontFamily(const std::string& f) { pImpl->editor_.fontFamily = f; pImpl->hasChanges_ = true; }
std::string IDEConfigPanel::fontFamily() const   { return pImpl->editor_.fontFamily; }

void IDEConfigPanel::setWordWrap(bool v)       { pImpl->editor_.wordWrap = v; pImpl->hasChanges_ = true; }
bool IDEConfigPanel::wordWrap() const          { return pImpl->editor_.wordWrap; }

void IDEConfigPanel::setLineNumbers(bool v)    { pImpl->editor_.lineNumbers = v; pImpl->hasChanges_ = true; }
bool IDEConfigPanel::lineNumbers() const       { return pImpl->editor_.lineNumbers; }

void IDEConfigPanel::setMinimap(bool v)        { pImpl->editor_.minimap = v; pImpl->hasChanges_ = true; }
bool IDEConfigPanel::minimap() const           { return pImpl->editor_.minimap; }

void IDEConfigPanel::setEditorTheme(EditorTheme t) { pImpl->editor_.theme = t; pImpl->hasChanges_ = true; }
IDEConfigPanel::EditorTheme IDEConfigPanel::editorTheme() const { return pImpl->editor_.theme; }

/* ================================================================
   LSP
   ================================================================ */

void IDEConfigPanel::addLSPServer(const LSPServer& s) {
    // Remove existing for same language
    removeLSPServer(s.language);
    pImpl->lspServers_.push_back(s);
    pImpl->hasChanges_ = true;
}

void IDEConfigPanel::removeLSPServer(const std::string& language) {
    pImpl->lspServers_.erase(
        std::remove_if(pImpl->lspServers_.begin(), pImpl->lspServers_.end(),
            [&language](const LSPServer& s) { return s.language == language; }),
        pImpl->lspServers_.end());
    pImpl->hasChanges_ = true;
}

std::vector<IDEConfigPanel::LSPServer> IDEConfigPanel::lspServers() const { return pImpl->lspServers_; }

IDEConfigPanel::LSPServer IDEConfigPanel::lspServer(const std::string& language) const {
    for (const auto& s : pImpl->lspServers_) {
        if (s.language == language) return s;
    }
    return LSPServer{};
}

void IDEConfigPanel::setLSPGlobalConfig(const LSPConfig& c) { pImpl->lspGlobal_ = c; pImpl->hasChanges_ = true; }
IDEConfigPanel::LSPConfig IDEConfigPanel::lspGlobalConfig() const { return pImpl->lspGlobal_; }

void IDEConfigPanel::setLSPEnabled(bool e)     { pImpl->lspGlobal_.enabled = e; pImpl->hasChanges_ = true; }
bool IDEConfigPanel::lspEnabled() const        { return pImpl->lspGlobal_.enabled; }

/* ================================================================
   Debugger
   ================================================================ */

void IDEConfigPanel::setDebuggerConfig(const DebuggerConfig& c) { pImpl->debugger_ = c; pImpl->hasChanges_ = true; }
IDEConfigPanel::DebuggerConfig IDEConfigPanel::debuggerConfig() const { return pImpl->debugger_; }

void IDEConfigPanel::setDebuggerEnabled(bool e) { pImpl->debugger_.enabled = e; pImpl->hasChanges_ = true; }
bool IDEConfigPanel::debuggerEnabled() const    { return pImpl->debugger_.enabled; }

void IDEConfigPanel::setDebuggerBackend(const std::string& b) { pImpl->debugger_.backend = b; pImpl->hasChanges_ = true; }
std::string IDEConfigPanel::debuggerBackend() const { return pImpl->debugger_.backend; }

/* ================================================================
   Plugins
   ================================================================ */

void IDEConfigPanel::addPlugin(const Plugin& p) {
    removePlugin(p.id);
    pImpl->plugins_.push_back(p);
    pImpl->hasChanges_ = true;
}

void IDEConfigPanel::removePlugin(const std::string& id) {
    pImpl->plugins_.erase(
        std::remove_if(pImpl->plugins_.begin(), pImpl->plugins_.end(),
            [&id](const Plugin& p) { return p.id == id; }),
        pImpl->plugins_.end());
    pImpl->hasChanges_ = true;
}

void IDEConfigPanel::enablePlugin(const std::string& id, bool enabled) {
    for (auto& p : pImpl->plugins_) {
        if (p.id == id) { p.enabled = enabled; pImpl->hasChanges_ = true; break; }
    }
}

std::vector<IDEConfigPanel::Plugin> IDEConfigPanel::installedPlugins() const { return pImpl->plugins_; }

IDEConfigPanel::Plugin IDEConfigPanel::plugin(const std::string& id) const {
    for (const auto& p : pImpl->plugins_) {
        if (p.id == id) return p;
    }
    return Plugin{};
}

bool IDEConfigPanel::hasPlugin(const std::string& id) const {
    for (const auto& p : pImpl->plugins_) {
        if (p.id == id) return true;
    }
    return false;
}

/* ================================================================
   JSON Serialisation
   ================================================================ */

namespace {
    std::string iesc(const std::string& s) {
        std::string r;
        for (char c : s) {
            switch (c) { case '"': r += "\\\""; break; case '\\': r += "\\\\"; break;
            case '\n': r += "\\n"; break; case '\r': r += "\\r"; break; case '\t': r += "\\t"; break;
            default: r += c; break; }
        }
        return r;
    }
}

std::string IDEConfigPanel::toJSON() const {
    std::ostringstream oss;
    oss << "{";
    // Editor
    oss << "\\"editor\\":{";
    oss << "\\"tabSize\\":" << pImpl->editor_.tabSize << ",";
    oss << "\\"indentStyle\\":\\"" << (pImpl->editor_.indentStyle == IndentStyle::Spaces ? "spaces" : "tabs") << "\\",";
    oss << "\\"fontSize\\":" << pImpl->editor_.fontSize << ",";
    oss << "\\"fontFamily\\":\\"" << iesc(pImpl->editor_.fontFamily) << "\\",";
    oss << "\\"wordWrap\\":" << (pImpl->editor_.wordWrap ? "true" : "false") << ",";
    oss << "\\"lineNumbers\\":" << (pImpl->editor_.lineNumbers ? "true" : "false") << ",";
    oss << "\\"minimap\\":" << (pImpl->editor_.minimap ? "true" : "false") << ",";
    oss << "\\"bracketMatching\\":" << (pImpl->editor_.bracketMatching ? "true" : "false") << ",";
    oss << "\\"autoIndent\\":" << (pImpl->editor_.autoIndent ? "true" : "false") << ",";
    oss << "\\"smartComments\\":" << (pImpl->editor_.smartComments ? "true" : "false") << ",";
    oss << "\\"trimTrailingWhitespace\\":" << (pImpl->editor_.trimTrailingWhitespace ? "true" : "false") << ",";
    oss << "\\"insertFinalNewline\\":" << (pImpl->editor_.insertFinalNewline ? "true" : "false") << ",";
    oss << "\\"eolStyle\\":\\"";
    switch (pImpl->editor_.eolStyle) {
        case EOLStyle::LF: oss << "lf"; break; case EOLStyle::CRLF: oss << "crlf"; break;
        case EOLStyle::CR: oss << "cr"; break; default: oss << "auto"; break;
    }
    oss << "\\",";
    oss << "\\"maxLineLength\\":" << pImpl->editor_.maxLineLength << ",";
    oss << "\\"showRulers\\":" << (pImpl->editor_.showRulers ? "true" : "false") << ",";
    oss << "\\"rulers\\":[";
    for (size_t i = 0; i < pImpl->editor_.rulers.size(); ++i) {
        if (i > 0) oss << ",";
        oss << pImpl->editor_.rulers[i];
    }
    oss << "],";
    oss << "\\"theme\\":\\"" << themeToString(pImpl->editor_.theme) << "\\"";
    oss << "},";
    // LSP Global
    oss << "\\"lsp\\":{";
    oss << "\\"enabled\\":" << (pImpl->lspGlobal_.enabled ? "true" : "false") << ",";
    oss << "\\"timeoutMs\\":" << pImpl->lspGlobal_.timeoutMs << ",";
    oss << "\\"diagnosticsOnType\\":" << (pImpl->lspGlobal_.diagnosticsOnType ? "true" : "false") << ",";
    oss << "\\"diagnosticsOnSave\\":" << (pImpl->lspGlobal_.diagnosticsOnSave ? "true" : "false") << ",";
    oss << "\\"hoverEnabled\\":" << (pImpl->lspGlobal_.hoverEnabled ? "true" : "false") << ",";
    oss << "\\"completionEnabled\\":" << (pImpl->lspGlobal_.completionEnabled ? "true" : "false") << ",";
    oss << "\\"signatureHelp\\":" << (pImpl->lspGlobal_.signatureHelp ? "true" : "false") << ",";
    oss << "\\"codeLens\\":" << (pImpl->lspGlobal_.codeLens ? "true" : "false") << ",";
    oss << "\\"formatOnSave\\":" << (pImpl->lspGlobal_.formatOnSave ? "true" : "false") << ",";
    oss << "\\"maxCompletions\\":" << pImpl->lspGlobal_.maxCompletions;
    oss << "},";
    // LSP Servers
    oss << "\\"lspServers\\":[";
    for (size_t i = 0; i < pImpl->lspServers_.size(); ++i) {
        if (i > 0) oss << ",";
        const auto& s = pImpl->lspServers_[i];
        oss << "{";
        oss << "\\"language\\":\\"" << iesc(s.language) << "\\",";
        oss << "\\"command\\":\\"" << iesc(s.command) << "\\",";
        oss << "\\"args\\":[";
        for (size_t j = 0; j < s.args.size(); ++j) { if (j > 0) oss << ","; oss << "\\"" << iesc(s.args[j]) << "\\""; }
        oss << "],";
        oss << "\\"rootMarkers\\":\\"" << iesc(s.rootMarkers) << "\\",";
        oss << "\\"port\\":" << s.port;
        oss << "}";
    }
    oss << "],";
    // Debugger
    oss << "\\"debugger\\":{";
    oss << "\\"enabled\\":" << (pImpl->debugger_.enabled ? "true" : "false") << ",";
    oss << "\\"backend\\":\\"" << iesc(pImpl->debugger_.backend) << "\\",";
    oss << "\\"timeoutMs\\":" << pImpl->debugger_.timeoutMs << ",";
    oss << "\\"breakOnException\\":" << (pImpl->debugger_.breakOnException ? "true" : "false") << ",";
    oss << "\\"showDisassembly\\":" << (pImpl->debugger_.showDisassembly ? "true" : "false") << ",";
    oss << "\\"maxStackFrames\\":" << pImpl->debugger_.maxStackFrames << ",";
    oss << "\\"evaluateOnHover\\":" << (pImpl->debugger_.evaluateOnHover ? "true" : "false");
    oss << "},";
    // Plugins
    oss << "\\"plugins\\":[";
    for (size_t i = 0; i < pImpl->plugins_.size(); ++i) {
        if (i > 0) oss << ",";
        const auto& p = pImpl->plugins_[i];
        oss << "{";
        oss << "\\"id\\":\\"" << iesc(p.id) << "\\",";
        oss << "\\"name\\":\\"" << iesc(p.name) << "\\",";
        oss << "\\"version\\":\\"" << iesc(p.version) << "\\",";
        oss << "\\"author\\":\\"" << iesc(p.author) << "\\",";
        oss << "\\"description\\":\\"" << iesc(p.description) << "\\",";
        oss << "\\"enabled\\":" << (p.enabled ? "true" : "false") << ",";
        oss << "\\"bundled\\":" << (p.bundled ? "true" : "false");
        oss << "}";
    }
    oss << "]}";
    return oss.str();
}

void IDEConfigPanel::fromJSON(const std::string& json) {
    auto extractStr = [&](const std::string& k) -> std::string {
        size_t p = json.find("\\"" + k + "\\":");
        if (p == std::string::npos) return "";
        p = json.find('"', p + k.length() + 3);
        if (p == std::string::npos) return "";
        ++p; size_t e = json.find('"', p);
        return (e == std::string::npos) ? "" : json.substr(p, e - p);
    };
    auto extractBool = [&](const std::string& k) -> bool {
        size_t p = json.find("\\"" + k + "\\":");
        if (p == std::string::npos) return false;
        p = json.find(':', p + k.length() + 3); if (p == std::string::npos) return false;
        ++p; while (p < json.size() && json[p] == ' ') ++p;
        return json.substr(p, 4) == "true";
    };
    auto extractInt = [&](const std::string& k) -> int {
        size_t p = json.find("\\"" + k + "\\":");
        if (p == std::string::npos) return 0;
        p = json.find(':', p + k.length() + 3); if (p == std::string::npos) return 0;
        ++p; return std::atoi(json.c_str() + p);
    };

    // Editor
    int ts = extractInt("tabSize"); if (ts > 0) pImpl->editor_.tabSize = ts;
    std::string is = extractStr("indentStyle");
    pImpl->editor_.indentStyle = (is == "tabs") ? IndentStyle::Tabs : IndentStyle::Spaces;
    int fs = extractInt("fontSize"); if (fs > 0) pImpl->editor_.fontSize = fs;
    std::string ff = extractStr("fontFamily"); if (!ff.empty()) pImpl->editor_.fontFamily = ff;
    pImpl->editor_.wordWrap = extractBool("wordWrap");
    pImpl->editor_.lineNumbers = extractBool("lineNumbers");
    pImpl->editor_.minimap = extractBool("minimap");
    pImpl->editor_.autoIndent = extractBool("autoIndent");
    pImpl->editor_.trimTrailingWhitespace = extractBool("trimTrailingWhitespace");
    pImpl->editor_.insertFinalNewline = extractBool("insertFinalNewline");
    int mll = extractInt("maxLineLength"); if (mll > 0) pImpl->editor_.maxLineLength = mll;
    pImpl->editor_.showRulers = extractBool("showRulers");
    std::string et = extractStr("theme"); if (!et.empty()) pImpl->editor_.theme = stringToTheme(et);

    // LSP
    pImpl->lspGlobal_.enabled = extractBool("enabled");
    int lto = extractInt("timeoutMs"); if (lto > 0) pImpl->lspGlobal_.timeoutMs = lto;
    pImpl->lspGlobal_.diagnosticsOnType = extractBool("diagnosticsOnType");
    pImpl->lspGlobal_.diagnosticsOnSave = extractBool("diagnosticsOnSave");
    pImpl->lspGlobal_.hoverEnabled = extractBool("hoverEnabled");
    pImpl->lspGlobal_.completionEnabled = extractBool("completionEnabled");
    pImpl->lspGlobal_.formatOnSave = extractBool("formatOnSave");
    int mc = extractInt("maxCompletions"); if (mc > 0) pImpl->lspGlobal_.maxCompletions = mc;

    // Debugger
    pImpl->debugger_.enabled = extractBool("enabled");
    std::string db = extractStr("backend"); if (!db.empty()) pImpl->debugger_.backend = db;
    int dto = extractInt("timeoutMs"); if (dto > 0) pImpl->debugger_.timeoutMs = dto;
    pImpl->debugger_.breakOnException = extractBool("breakOnException");
    pImpl->debugger_.evaluateOnHover = extractBool("evaluateOnHover");

    pImpl->hasChanges_ = false;
}

/* ================================================================
   Validation
   ================================================================ */

std::vector<std::string> IDEConfigPanel::validate() const {
    std::vector<std::string> errors;
    if (pImpl->editor_.tabSize < 1 || pImpl->editor_.tabSize > 16) {
        errors.push_back("Tab size must be between 1 and 16");
    }
    if (pImpl->editor_.fontSize < 4 || pImpl->editor_.fontSize > 72) {
        errors.push_back("Font size must be between 4 and 72");
    }
    if (pImpl->editor_.maxLineLength < 40 || pImpl->editor_.maxLineLength > 500) {
        errors.push_back("Max line length must be between 40 and 500");
    }
    if (pImpl->lspGlobal_.timeoutMs < 1000 || pImpl->lspGlobal_.timeoutMs > 120000) {
        errors.push_back("LSP timeout must be between 1000 and 120000 ms");
    }
    if (pImpl->debugger_.timeoutMs < 5000 || pImpl->debugger_.timeoutMs > 300000) {
        errors.push_back("Debugger timeout must be between 5000 and 300000 ms");
    }
    return errors;
}

void IDEConfigPanel::resetToDefaults() { pImpl = std::make_unique<Impl>(); }
bool IDEConfigPanel::hasChanges() const { return pImpl->hasChanges_; }
void IDEConfigPanel::markSaved()        { pImpl->hasChanges_ = false; }

} // namespace powsys365::config
