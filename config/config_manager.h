#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <stdexcept>
#include <functional>

namespace powsys365::config {

// Forward declarations for panel types
class AIConfigPanel;
class UIConfigPanel;
class PaymentConfigPanel;
class IDEConfigPanel;

/**
 * @brief Central configuration manager for POWSYS365.
 *
 * Manages 7+ configuration panels with JSON persistence,
 * import/export, and Qt6-compatible signal-style callbacks.
 *
 * Panels:
 *  - AI:       LLM selection, API keys, RAG settings
 *  - UI:       Theme, language, font, layout, colors, animations
 *  - Payment:  Stripe/PayPal, pricing, license activation
 *  - IDE:      Editor settings, LSP, debugger, plugins
 *  - iTalk:    Voice/video call preferences, chat history
 *  - Sounds:   Audio themes, notification sounds, volume
 *  - OSS:      Open-source licenses, contribution settings
 */
class ConfigManager {
public:
    /**
     * @brief Panel identifier enumeration.
     */
    enum class PanelId {
        AI       = 0,
        UI       = 1,
        Payment  = 2,
        IDE      = 3,
        iTalk    = 4,
        Sounds   = 5,
        OSS      = 6
    };

    static std::string panelIdToString(PanelId id);
    static PanelId     stringToPanelId(const std::string& s);

    /**
     * @brief Change notification callback.
     */
    using ChangeCallback = std::function<void(PanelId, const std::string& key)>;

    /**
     * @brief Construct with optional persistence path.
     * @param configPath Directory for JSON config files.
     */
    explicit ConfigManager(const std::string& configPath = "./config");
    ~ConfigManager();

    // Non-copyable
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    // Movable
    ConfigManager(ConfigManager&&) noexcept;
    ConfigManager& operator=(ConfigManager&&) noexcept;

    // ----------------------------------------------------------------
    //  Panel access
    // ----------------------------------------------------------------

    /**
     * @brief Get typed panel reference.
     */
    AIConfigPanel&       aiPanel();
    UIConfigPanel&       uiPanel();
    PaymentConfigPanel&  paymentPanel();
    IDEConfigPanel&      idePanel();

    /**
     * @brief Generic panel access by ID.
     * Returns the panel settings as a JSON string.
     */
    std::string getPanel(PanelId id) const;

    /**
     * @brief Save settings to a panel (JSON string).
     */
    void savePanel(PanelId id, const std::string& jsonSettings);

    /**
     * @brief Check if a panel has unsaved changes.
     */
    bool hasUnsavedChanges(PanelId id) const;

    /**
     * @brief Reset a panel to factory defaults.
     */
    void resetPanel(PanelId id);

    /**
     * @brief Reset all panels to factory defaults.
     */
    void resetAllPanels();

    // ----------------------------------------------------------------
    //  Persistence
    // ----------------------------------------------------------------

    /**
     * @brief Load all panels from JSON files on disk.
     */
    void loadAll();

    /**
     * @brief Save all panels to JSON files on disk.
     */
    void saveAll();

    /**
     * @brief Auto-save: persist immediately after each change.
     */
    void setAutoSave(bool enabled);
    bool autoSave() const;

    /**
     * @brief Set the config directory path.
     */
    void setConfigPath(const std::string& path);
    std::string configPath() const;

    // ----------------------------------------------------------------
    //  Import / Export
    // ----------------------------------------------------------------

    /**
     * @brief Export all settings to a single JSON file.
     * @param filePath Destination file path.
     * @return true on success.
     */
    bool exportSettings(const std::string& filePath) const;

    /**
     * @brief Import all settings from a JSON file.
     * @param filePath Source file path.
     * @return true on success.
     */
    bool importSettings(const std::string& filePath);

    /**
     * @brief Export a single panel to JSON.
     */
    bool exportPanel(PanelId id, const std::string& filePath) const;

    /**
     * @brief Import a single panel from JSON.
     */
    bool importPanel(PanelId id, const std::string& filePath);

    /**
     * @brief Create a backup of current configuration.
     * @return Path to the backup file.
     */
    std::string createBackup() const;

    /**
     * @brief Restore from a backup file.
     */
    bool restoreFromBackup(const std::string& backupPath);

    // ----------------------------------------------------------------
    //  Change notifications
    // ----------------------------------------------------------------

    /**
     * @brief Register a callback for configuration changes.
     */
    void onChanged(ChangeCallback cb);

    /**
     * @brief Notify that a setting has changed.
     */
    void notifyChanged(PanelId id, const std::string& key);

    // ----------------------------------------------------------------
    //  Validation
    // ----------------------------------------------------------------

    /**
     * @brief Validate all panel settings.
     * @return Vector of (panelId, error_message) for invalid entries.
     */
    std::vector<std::pair<PanelId, std::string>> validateAll() const;

    /**
     * @brief Get number of configured panels.
     */
    size_t panelCount() const;

    /**
     * @brief Get list of available panel IDs.
     */
    std::vector<PanelId> availablePanels() const;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

} // namespace powsys365::config
