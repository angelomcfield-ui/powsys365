#pragma once

#include <QObject>
#include <QProcess>
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include <QString>
#include <QTcpSocket>
#include <QLocalSocket>
#include <memory>
#include <functional>

namespace powsys365::ide::lsp {

/**
 * @brief Supported language server configuration
 */
struct LanguageServerConfig {
    QString languageId;
    QString executableName;
    QStringList arguments;
    QString rootUri;
    QJsonObject initializationOptions;
    bool useTcp = false;
    QString tcpHost = "127.0.0.1";
    int tcpPort = 0;
    bool useStdio = true;
};

/**
 * @brief Server connection state
 */
enum class ServerState {
    Stopped,
    Starting,
    Initializing,
    Running,
    ShuttingDown,
    Error
};

/**
 * @brief LSP Server capabilities
 */
struct ServerCapabilities {
    bool textDocumentSync = false;
    bool completionProvider = false;
    bool hoverProvider = false;
    bool signatureHelpProvider = false;
    bool definitionProvider = false;
    bool typeDefinitionProvider = false;
    bool implementationProvider = false;
    bool referencesProvider = false;
    bool documentHighlightProvider = false;
    bool documentSymbolProvider = false;
    bool codeActionProvider = false;
    bool codeLensProvider = false;
    bool documentFormattingProvider = false;
    bool documentRangeFormattingProvider = false;
    bool documentOnTypeFormattingProvider = false;
    bool renameProvider = false;
    bool foldingRangeProvider = false;
    bool executeCommandProvider = false;
    bool selectionRangeProvider = false;
    bool semanticTokensProvider = false;
    bool linkedEditingRangeProvider = false;
    bool callHierarchyProvider = false;
    bool monikerProvider = false;
    bool workspaceSymbolProvider = false;
    bool workspaceFolders = false;
    bool configuration = false;
    bool diagnosticProvider = false;
    bool inlayHintProvider = false;
    bool inlineValueProvider = false;
    QJsonObject rawCapabilities;
};

/**
 * @brief Manages Language Servers via LSP for 60+ languages
 */
class LanguageServerManager : public QObject {
    Q_OBJECT

public:
    explicit LanguageServerManager(QObject* parent = nullptr);
    ~LanguageServerManager();

    // Server lifecycle
    bool startServer(const QString& language);
    bool stopServer(const QString& language);
    bool restartServer(const QString& language);
    bool stopAllServers();

    // LSP protocol operations
    bool initialize(const QString& language);
    bool shutdown(const QString& language);

    // Capabilities
    ServerCapabilities capabilities(const QString& language) const;
    bool hasCapability(const QString& language, const QString& cap) const;

    // Server state
    ServerState serverState(const QString& language) const;
    QStringList activeServers() const;
    bool isServerRunning(const QString& language) const;

    // Text Document operations
    void didOpen(const QString& language, const QString& uri,
                 const QString& text, const QString& languageId = QString());
    void didClose(const QString& language, const QString& uri);
    void didChange(const QString& language, const QString& uri,
                   const QVector<QJsonObject>& contentChanges);
    void didSave(const QString& language, const QString& uri);

    // LSP features (request-based)
    void requestCompletion(const QString& language, const QString& uri,
                          const QJsonObject& position,
                          std::function<void(const QJsonArray&)> callback);
    void requestHover(const QString& language, const QString& uri,
                      const QJsonObject& position,
                      std::function<void(const QJsonObject&)> callback);
    void requestDefinition(const QString& language, const QString& uri,
                           const QJsonObject& position,
                           std::function<void(const QJsonArray&)> callback);
    void requestReferences(const QString& language, const QString& uri,
                           const QJsonObject& position, bool includeDeclaration,
                           std::function<void(const QJsonArray&)> callback);
    void requestDocumentSymbols(const QString& language, const QString& uri,
                                std::function<void(const QJsonArray&)> callback);
    void requestFormatting(const QString& language, const QString& uri,
                           std::function<void(const QJsonArray&)> callback);
    void requestSignatureHelp(const QString& language, const QString& uri,
                              const QJsonObject& position,
                              std::function<void(const QJsonObject&)> callback);
    void requestCodeAction(const QString& language, const QString& uri,
                           const QJsonObject& range, const QJsonObject& context,
                           std::function<void(const QJsonArray&)> callback);
    void requestRename(const QString& language, const QString& uri,
                       const QJsonObject& position, const QString& newName,
                       std::function<void(const QJsonObject&)> callback);
    void requestInlayHints(const QString& language, const QString& uri,
                           const QJsonObject& range,
                           std::function<void(const QJsonArray&)> callback);

    // Configuration
    void registerLanguage(const QString& language, const LanguageServerConfig& config);
    void unregisterLanguage(const QString& language);
    QStringList supportedLanguages() const;
    LanguageServerConfig getConfig(const QString& language) const;

Q_SIGNALS:
    void serverStarted(const QString& language);
    void serverStopped(const QString& language);
    void serverError(const QString& language, const QString& error);
    void serverInitialized(const QString& language, const ServerCapabilities& caps);
    void diagnosticReceived(const QString& language, const QString& uri, const QJsonArray& diagnostics);
    void logMessage(const QString& language, const QString& message);
    void showMessage(const QString& language, const QJsonObject& message);

private:
    class Impl;
    std::unique_ptr<Impl> d;
};

} // namespace powsys365::ide::lsp
