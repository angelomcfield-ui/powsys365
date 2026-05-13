#pragma once

#include <string>
#include <vector>
#include <unordered_map>

namespace powsys365::config {

/**
 * @brief IDE configuration panel – editor settings, LSP, debugger,
 *        plugin management.
 *
 * Qt6-compatible with QSettings-style key-value storage.
 */
class IDEConfigPanel {
public:
    /**
     * @brief Editor theme for syntax highlighting.
     */
    enum class EditorTheme {
        Default,
        Dark,
        Light,
        Monokai,
        Solarized,
        Dracula,
        OneDark,
        Custom
    };

    static std::string themeToString(EditorTheme t);
    static EditorTheme stringToTheme(const std::string& s);

    /**
     * @brief Indentation style.
     */
    enum class IndentStyle {
        Spaces,
        Tabs
    };

    /**
     * @brief End-of-line style.
     */
    enum class EOLStyle {
        LF,     // Unix
        CRLF,   // Windows
        CR,     // Classic Mac
        Auto
    };

    /**
     * @brief Editor settings.
     */
    struct EditorSettings {
        int         tabSize              = 4;
        IndentStyle indentStyle          = IndentStyle::Spaces;
        int         fontSize             = 12;
        std::string fontFamily           = "JetBrains Mono";
        bool        wordWrap             = true;
        bool        lineNumbers          = true;
        bool        minimap              = true;
        bool        bracketMatching      = true;
        bool        autoIndent           = true;
        bool        smartComments        = true;
        bool        trimTrailingWhitespace = true;
        bool        insertFinalNewline   = true;
        EOLStyle    eolStyle             = EOLStyle::Auto;
        int         maxLineLength        = 120;
        bool        showRulers           = true;
        std::vector<int> rulers;       // Column positions
    };

    /**
     * @brief LSP (Language Server Protocol) configuration.
     */
    struct LSPConfig {
        bool        enabled             = true;
        int         timeoutMs           = 10000;
        bool        diagnosticsOnType   = true;
        bool        diagnosticsOnSave   = true;
        bool        hoverEnabled        = true;
        bool        completionEnabled   = true;
        bool        signatureHelp       = true;
        bool        codeLens            = true;
        bool        formatOnSave        = true;
        int         maxCompletions      = 50;
        std::vector<std::string> additionalArgs;
    };

    /**
     * @brief Per-language LSP server config.
     */
    struct LSPServer {
        std::string language;       // "cpp", "python", "javascript"...
        std::string command;        // Executable path
        std::vector<std::string> args;
        std::string rootMarkers;    // "compile_commands.json,.git"
        int         port = 0;       // TCP mode (0 = stdio)
        LSPConfig   settings;
    };

    /**
     * @brief Debugger configuration.
     */
    struct DebuggerConfig {
        bool        enabled           = true;
        std::string backend           = "gdb"; // "gdb", "lldb", "cdb"
        int         timeoutMs         = 30000;
        bool        breakOnException  = true;
        bool        showDisassembly   = false;
        int         maxStackFrames    = 100;
        bool        evaluateOnHover   = true;
        std::vector<std::string> startupCommands;
    };

    /**
     * @brief Plugin descriptor.
     */
    struct Plugin {
        std::string id;
        std::string name;
        std::string version;
        std::string author;
        std::string description;
        std::string installPath;
        bool        enabled = false;
        bool        bundled = false;
        std::vector<std::string> dependencies;
    };

    IDEConfigPanel();
    ~IDEConfigPanel() = default;

    IDEConfigPanel(const IDEConfigPanel&) = default;
    IDEConfigPanel& operator=(const IDEConfigPanel&) = default;
    IDEConfigPanel(IDEConfigPanel&&) noexcept = default;
    IDEConfigPanel& operator=(IDEConfigPanel&&) noexcept = default;

    // ----------------------------------------------------------------
    //  Editor
    // ----------------------------------------------------------------

    void setEditorSettings(const EditorSettings& settings);
    EditorSettings editorSettings() const;

    void setTabSize(int size);
    int  tabSize() const;

    void setFontSize(int size);
    int  fontSize() const;

    void setFontFamily(const std::string& family);
    std::string fontFamily() const;

    void setWordWrap(bool enabled);
    bool wordWrap() const;

    void setLineNumbers(bool enabled);
    bool lineNumbers() const;

    void setMinimap(bool enabled);
    bool minimap() const;

    void setEditorTheme(EditorTheme theme);
    EditorTheme editorTheme() const;

    // ----------------------------------------------------------------
    //  LSP
    // ----------------------------------------------------------------

    void addLSPServer(const LSPServer& server);
    void removeLSPServer(const std::string& language);
    std::vector<LSPServer> lspServers() const;
    LSPServer lspServer(const std::string& language) const;

    void setLSPGlobalConfig(const LSPConfig& config);
    LSPConfig lspGlobalConfig() const;

    void setLSPEnabled(bool enabled);
    bool lspEnabled() const;

    // ----------------------------------------------------------------
    //  Debugger
    // ----------------------------------------------------------------

    void setDebuggerConfig(const DebuggerConfig& config);
    DebuggerConfig debuggerConfig() const;

    void setDebuggerEnabled(bool enabled);
    bool debuggerEnabled() const;

    void setDebuggerBackend(const std::string& backend);
    std::string debuggerBackend() const;

    // ----------------------------------------------------------------
    //  Plugins
    // ----------------------------------------------------------------

    void addPlugin(const Plugin& plugin);
    void removePlugin(const std::string& id);
    void enablePlugin(const std::string& id, bool enabled);
    std::vector<Plugin> installedPlugins() const;
    Plugin plugin(const std::string& id) const;
    bool hasPlugin(const std::string& id) const;

    // ----------------------------------------------------------------
    //  Serialisation
    // ----------------------------------------------------------------

    std::string toJSON() const;
    void fromJSON(const std::string& json);

    // ----------------------------------------------------------------
    //  Validation
    // ----------------------------------------------------------------

    std::vector<std::string> validate() const;

    void resetToDefaults();
    bool hasChanges() const;
    void markSaved();

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

} // namespace powsys365::config
