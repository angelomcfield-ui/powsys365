#include "language_server_manager.h"
#include "json_rpc.h"
#include <QProcess>
#include <QTcpSocket>
#include <QLocalSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUuid>
#include <QElapsedTimer>
#include <QDebug>
#include <QTimer>
#include <QMutex>
#include <QMutexLocker>

namespace powsys365::ide::lsp {

static const int LSP_TIMEOUT_MS = 30000;
static const int INITIALIZATION_TIMEOUT_MS = 60000;

struct ServerInstance {
    QString language;
    LanguageServerConfig config;
    ServerState state = ServerState::Stopped;
    std::unique_ptr<QProcess> process;
    QTcpSocket* tcpSocket = nullptr;
    std::unique_ptr<JsonRpc> jsonRpc;
    ServerCapabilities capabilities;
    int requestId = 0;
    QMap<int, std::function<void(const QJsonObject&)>> pendingRequests;
    QString workspaceRoot;
    bool initialized = false;
    QMutex mutex;
};

class LanguageServerManager::Impl {
public:
    QMap<QString, std::shared_ptr<ServerInstance>> servers;
    QMap<QString, LanguageServerConfig> configs;
    QMutex serversMutex;
    mutable QMutex capsMutex;

    Impl() {
        registerDefaultLanguages();
    }

    void registerDefaultLanguages() {
        // === Systems Languages ===
        configs.insert("c", LanguageServerConfig{
            "c", "clangd",
            {"--background-index", "--compile-commands-dir=build",
             "--completion-style=bundled", "--pch-storage=memory",
             "--cross-file-rename", "--header-insertion=iwyu"},
            QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("cpp", LanguageServerConfig{
            "cpp", "clangd",
            {"--background-index", "--compile-commands-dir=build",
             "--completion-style=bundled", "--pch-storage=memory",
             "--cross-file-rename", "--header-insertion=iwyu"},
            QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("objc", LanguageServerConfig{
            "objc", "clangd",
            {"--background-index"}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("cuda", LanguageServerConfig{
            "cuda", "clangd",
            {"--background-index"}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        // === Rust ===
        configs.insert("rust", LanguageServerConfig{
            "rust", "rust-analyzer",
            {}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        // === Go ===
        configs.insert("go", LanguageServerConfig{
            "go", "gopls",
            {"serve", "-rpc.trace", "--debug=0.0.0.0:3650"},
            QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        // === JavaScript / TypeScript ===
        configs.insert("javascript", LanguageServerConfig{
            "javascript", "typescript-language-server",
            {"--stdio"}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("typescript", LanguageServerConfig{
            "typescript", "typescript-language-server",
            {"--stdio"}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("javascriptreact", LanguageServerConfig{
            "javascriptreact", "typescript-language-server",
            {"--stdio"}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("typescriptreact", LanguageServerConfig{
            "typescriptreact", "typescript-language-server",
            {"--stdio"}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        // === Python ===
        configs.insert("python", LanguageServerConfig{
            "python", "pylsp",
            {}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("python2", LanguageServerConfig{
            "python2", "pylsp",
            {}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        // === Java ===
        configs.insert("java", LanguageServerConfig{
            "java", "java",
            {"-Declipse.application=org.eclipse.jdt.ls.core.id1",
             "-Dosgi.bundles.defaultStartLevel=4",
             "-Declipse.product=org.eclipse.jdt.ls.core.product"},
            QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("kotlin", LanguageServerConfig{
            "kotlin", "kotlin-language-server",
            {}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("scala", LanguageServerConfig{
            "scala", "metals",
            {}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("groovy", LanguageServerConfig{
            "groovy", "groovy-language-server",
            {}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        // === .NET / C# ===
        configs.insert("csharp", LanguageServerConfig{
            "csharp", "OmniSharp",
            {"--languageserver", "-s", "."},
            QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("fsharp", LanguageServerConfig{
            "fsharp", "fsautocomplete",
            {"--background-service-enabled"},
            QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        // === Functional Languages ===
        configs.insert("haskell", LanguageServerConfig{
            "haskell", "haskell-language-server-wrapper",
            {"--lsp"}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("ocaml", LanguageServerConfig{
            "ocaml", "ocamllsp",
            {}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("elixir", LanguageServerConfig{
            "elixir", "elixir-ls",
            {}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("erlang", LanguageServerConfig{
            "erlang", "erlang_ls",
            {}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("clojure", LanguageServerConfig{
            "clojure", "clojure-lsp",
            {}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("lisp", LanguageServerConfig{
            "lisp", "cl-lsp",
            {}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        // === Web Technologies ===
        configs.insert("html", LanguageServerConfig{
            "html", "vscode-html-language-server",
            {"--stdio"}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("css", LanguageServerConfig{
            "css", "vscode-css-language-server",
            {"--stdio"}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("scss", LanguageServerConfig{
            "scss", "vscode-css-language-server",
            {"--stdio"}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("less", LanguageServerConfig{
            "less", "vscode-css-language-server",
            {"--stdio"}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("json", LanguageServerConfig{
            "json", "vscode-json-language-server",
            {"--stdio"}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("jsonc", LanguageServerConfig{
            "jsonc", "vscode-json-language-server",
            {"--stdio"}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("yaml", LanguageServerConfig{
            "yaml", "yaml-language-server",
            {"--stdio"}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("xml", LanguageServerConfig{
            "xml", "lemminx",
            {}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("vue", LanguageServerConfig{
            "vue", "vue-language-server",
            {"--stdio"}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("svelte", LanguageServerConfig{
            "svelte", "svelteserver",
            {"--stdio"}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("angular", LanguageServerConfig{
            "angular", "ngserver",
            {}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        // === Shell / Scripting ===
        configs.insert("bash", LanguageServerConfig{
            "bash", "bash-language-server",
            {"start"}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("zsh", LanguageServerConfig{
            "zsh", "bash-language-server",
            {"start"}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("powershell", LanguageServerConfig{
            "powershell", "PowerShellEditorServices",
            {}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("perl", LanguageServerConfig{
            "perl", "perl-langserver",
            {}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("ruby", LanguageServerConfig{
            "ruby", "solargraph",
            {"stdio"}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("lua", LanguageServerConfig{
            "lua", "lua-language-server",
            {}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("php", LanguageServerConfig{
            "php", "intelephense",
            {"--stdio"}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        // === Data / Config Languages ===
        configs.insert("sql", LanguageServerConfig{
            "sql", "sqls",
            {}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("graphql", LanguageServerConfig{
            "graphql", "graphql-lsp",
            {"server", "-m", "stream"}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("protobuf", LanguageServerConfig{
            "protobuf", "bufls",
            {"serve"}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("dockerfile", LanguageServerConfig{
            "dockerfile", "docker-langserver",
            {"--stdio"}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("terraform", LanguageServerConfig{
            "terraform", "terraform-ls",
            {"serve"}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("nix", LanguageServerConfig{
            "nix", "rnix-lsp",
            {}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        // === Markup / Documentation ===
        configs.insert("markdown", LanguageServerConfig{
            "markdown", "marksman",
            {"server"}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("tex", LanguageServerConfig{
            "tex", "texlab",
            {}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("latex", LanguageServerConfig{
            "latex", "texlab",
            {}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        // === Other Languages ===
        configs.insert("swift", LanguageServerConfig{
            "swift", "sourcekit-lsp",
            {}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("dart", LanguageServerConfig{
            "dart", "dart",
            {"language-server", "--protocol=lsp"},
            QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("crystal", LanguageServerConfig{
            "crystal", "crystalline",
            {}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("nim", LanguageServerConfig{
            "nim", "nimlangserver",
            {}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("zig", LanguageServerConfig{
            "zig", "zls",
            {}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("v", LanguageServerConfig{
            "v", "vls",
            {}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("d", LanguageServerConfig{
            "d", "serve-d",
            {}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("fortran", LanguageServerConfig{
            "fortran", "fortls",
            {}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("julia", LanguageServerConfig{
            "julia", "julia",
            {"--startup-file=no", "--history-file=no", "-e",
             "using LanguageServer; runserver()"},
            QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("r", LanguageServerConfig{
            "r", "R",
            {"--slave", "-e", "languageserver::run()"},
            QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("matlab", LanguageServerConfig{
            "matlab", "matlab-language-server",
            {}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("arduino", LanguageServerConfig{
            "arduino", "arduino-language-server",
            {}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        // === Assembly ===
        configs.insert("asm", LanguageServerConfig{
            "asm", "asm-lsp",
            {}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        // === Make ===
        configs.insert("makefile", LanguageServerConfig{
            "makefile", "cmake-language-server",
            {}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });

        configs.insert("cmake", LanguageServerConfig{
            "cmake", "cmake-language-server",
            {}, QString(), QJsonObject(), false, "127.0.0.1", 0, true
        });
    }
};

LanguageServerManager::LanguageServerManager(QObject* parent)
    : QObject(parent), d(std::make_unique<Impl>())
{
}

LanguageServerManager::~LanguageServerManager() {
    stopAllServers();
}

bool LanguageServerManager::startServer(const QString& language) {
    QMutexLocker lock(&d->serversMutex);

    auto it = d->configs.find(language);
    if (it == d->configs.end()) {
        qWarning() << "[LSP] No config for language:" << language;
        return false;
    }

    auto sit = d->servers.find(language);
    if (sit != d->servers.end() && sit.value()->state == ServerState::Running) {
        return true;
    }

    auto instance = std::make_shared<ServerInstance>();
    instance->language = language;
    instance->config = it.value();
    instance->state = ServerState::Starting;

    instance->process = std::make_unique<QProcess>(this);
    instance->process->setProgram(instance->config.executableName);
    instance->process->setArguments(instance->config.arguments);

    connect(instance->process.get(), &QProcess::started, this, [this, instance]() {
        instance->state = ServerState::Initializing;
        Q_EMIT logMessage(instance->language, "Process started");

        instance->jsonRpc = std::make_unique<JsonRpc>();
        if (instance->config.useStdio && instance->process) {
            instance->jsonRpc->setIODevice(
                instance->process.get(),
                instance->process.get()
            );
        }

        connect(instance->jsonRpc.get(), &JsonRpc::messageReceived, this,
                [this, instance](const QJsonObject& msg) {
            QString method = msg.value("method").toString();
            if (method == "textDocument/publishDiagnostics") {
                QJsonObject params = msg.value("params").toObject();
                QString uri = params.value("uri").toString();
                QJsonArray diagnostics = params.value("diagnostics").toArray();
                Q_EMIT diagnosticReceived(instance->language, uri, diagnostics);
            } else if (method == "window/showMessage") {
                Q_EMIT showMessage(instance->language, msg.value("params").toObject());
            } else if (method == "window/logMessage") {
                QJsonObject p = msg.value("params").toObject();
                Q_EMIT logMessage(instance->language, p.value("message").toString());
            } else if (msg.contains("id")) {
                int msgId = msg.value("id").toInt(-1);
                QMutexLocker ml(&instance->mutex);
                auto cb = instance->pendingRequests.take(msgId);
                ml.unlock();
                if (cb) {
                    cb(msg.value("result").toObject());
                }
            }
        });

        connect(instance->jsonRpc.get(), &JsonRpc::error, this,
                [this, instance](const QString& err) {
            instance->state = ServerState::Error;
            Q_EMIT serverError(instance->language, err);
        });

        instance->jsonRpc->startReading();
        initialize(language);
    });

    connect(instance->process.get(), &QProcess::errorOccurred, this,
            [this, instance](QProcess::ProcessError err) {
        instance->state = ServerState::Error;
        Q_EMIT serverError(instance->language,
            QString("Process error: %1").arg(static_cast<int>(err)));
    });

    connect(instance->process.get(), QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, instance](int exitCode, QProcess::ExitStatus) {
        instance->state = ServerState::Stopped;
        Q_EMIT serverStopped(instance->language);
    });

    instance->process->start();
    if (!instance->process->waitForStarted(LSP_TIMEOUT_MS)) {
        instance->state = ServerState::Error;
        Q_EMIT serverError(instance->language, "Failed to start process");
        return false;
    }

    d->servers.insert(language, instance);
    Q_EMIT serverStarted(language);
    return true;
}

bool LanguageServerManager::stopServer(const QString& language) {
    QMutexLocker lock(&d->serversMutex);

    auto it = d->servers.find(language);
    if (it == d->servers.end()) return false;

    auto instance = it.value();
    shutdown(language);

    QTimer::singleShot(2000, this, [instance]() {
        if (instance->process && instance->process->state() != QProcess::NotRunning) {
            instance->process->terminate();
            if (!instance->process->waitForFinished(3000)) {
                instance->process->kill();
            }
        }
        if (instance->tcpSocket) {
            instance->tcpSocket->close();
        }
        instance->state = ServerState::Stopped;
        instance->jsonRpc.reset();
    });

    return true;
}

bool LanguageServerManager::restartServer(const QString& language) {
    stopServer(language);
    QThread::msleep(500);
    return startServer(language);
}

bool LanguageServerManager::stopAllServers() {
    QMutexLocker lock(&d->serversMutex);
    QStringList langs;
    for (auto it = d->servers.begin(); it != d->servers.end(); ++it) {
        langs.append(it.key());
    }
    lock.unlock();
    for (const QString& lang : langs) {
        stopServer(lang);
    }
    d->servers.clear();
    return true;
}

bool LanguageServerManager::initialize(const QString& language) {
    auto it = d->servers.find(language);
    if (it == d->servers.end() || !it.value()->jsonRpc) return false;

    auto instance = it.value();
    QString rootPath = instance->config.rootUri.isEmpty()
        ? QDir::currentPath() : instance->config.rootUri;
    instance->workspaceRoot = rootPath;

    QJsonObject workspaceFolders;
    workspaceFolders["uri"] = QUrl::fromLocalFile(rootPath).toString();
    workspaceFolders["name"] = QFileInfo(rootPath).fileName();

    QJsonObject params;
    params["processId"] = QCoreApplication::applicationPid();
    params["clientInfo"] = QJsonObject{{"name", "POWSYS365 IDE"}, {"version", "1.0.0"}};
    params["locale"] = "en-US";
    params["rootPath"] = rootPath;
    params["rootUri"] = QUrl::fromLocalFile(rootPath).toString();
    params["workspaceFolders"] = QJsonArray{workspaceFolders};

    QJsonObject textDoc;
    QJsonObject syncOpt; syncOpt["openClose"] = true; syncOpt["change"] = 2;
    textDoc["synchronization"] = syncOpt;
    textDoc["completion"] = QJsonObject{{"dynamicRegistration", true},
        {"completionItem", QJsonObject{{"snippetSupport", true}, {"commitCharactersSupport", true},
         {"documentationFormat", QJsonArray{"markdown", "plaintext"}},
         {"deprecatedSupport", true}, {"preselectSupport", true}}});
    textDoc["hover"] = QJsonObject{{"contentFormat", QJsonArray{"markdown", "plaintext"}}};
    textDoc["signatureHelp"] = QJsonObject{{"dynamicRegistration", true}};
    textDoc["definition"] = QJsonObject{{"dynamicRegistration", true},
        {"linkSupport", true}};
    textDoc["typeDefinition"] = QJsonObject{{"dynamicRegistration", true},
        {"linkSupport", true}};
    textDoc["implementation"] = QJsonObject{{"dynamicRegistration", true},
        {"linkSupport", true}};
    textDoc["references"] = QJsonObject{{"dynamicRegistration", true}};
    textDoc["documentHighlight"] = QJsonObject{{"dynamicRegistration", true}};
    textDoc["documentSymbol"] = QJsonObject{{"dynamicRegistration", true}};
    textDoc["codeAction"] = QJsonObject{{"dynamicRegistration", true},
        {"codeActionLiteralSupport", QJsonObject{{"codeActionKind", QJsonObject{
            {"valueSet", QJsonArray{"quickfix", "refactor", "source"}}}}}};
    textDoc["formatting"] = QJsonObject{{"dynamicRegistration", true}};
    textDoc["rangeFormatting"] = QJsonObject{{"dynamicRegistration", true}};
    textDoc["onTypeFormatting"] = QJsonObject{{"dynamicRegistration", true}};
    textDoc["rename"] = QJsonObject{{"dynamicRegistration", true},
        {"prepareSupport", true}};
    textDoc["foldingRange"] = QJsonObject{{"dynamicRegistration", true}};
    textDoc["inlayHint"] = QJsonObject{{"dynamicRegistration", true}};
    textDoc["diagnostic"] = QJsonObject{{"dynamicRegistration", true}};

    QJsonObject workspace;
    workspace["workspaceFolders"] = true;
    workspace["configuration"] = true;
    workspace["didChangeConfiguration"] = QJsonObject{{"dynamicRegistration", true}};
    workspace["didChangeWatchedFiles"] = QJsonObject{{"dynamicRegistration", true}};
    workspace["executeCommand"] = QJsonObject{{"dynamicRegistration", true}};
    workspace["semanticTokens"] = QJsonObject{{"refreshSupport", true}};

    QJsonObject clientCapabilities;
    clientCapabilities["textDocument"] = textDoc;
    clientCapabilities["workspace"] = workspace;
    clientCapabilities["general"] = QJsonObject{
        {"staleRequestSupport", QJsonObject{{"cancel", true}, {"retryOnContentModified", QJsonArray()}}},
        {"regularExpressions", QJsonObject{{"engine", "ECMAScript"}, {"version", "ES2020"}}},
        {"markdown", QJsonObject{{"parser", "marked"}, {"version", "1.1.1"}}}
    };

    params["capabilities"] = clientCapabilities;

    if (!instance->config.initializationOptions.isEmpty()) {
        params["initializationOptions"] = instance->config.initializationOptions;
    }

    int reqId = ++instance->requestId;
    {
        QMutexLocker ml(&instance->mutex);
        instance->pendingRequests[reqId] = [this, instance, language](const QJsonObject& result) {
            QJsonObject caps = result.value("capabilities").toObject();
            {
                QMutexLocker ml2(&instance->mutex);
                instance->capabilities.rawCapabilities = caps;
                instance->capabilities.textDocumentSync = caps.contains("textDocumentSync");
                instance->capabilities.completionProvider = caps.contains("completionProvider");
                instance->capabilities.hoverProvider = caps.contains("hoverProvider");
                instance->capabilities.signatureHelpProvider = caps.contains("signatureHelpProvider");
                instance->capabilities.definitionProvider = caps.contains("definitionProvider");
                instance->capabilities.typeDefinitionProvider = caps.contains("typeDefinitionProvider");
                instance->capabilities.implementationProvider = caps.contains("implementationProvider");
                instance->capabilities.referencesProvider = caps.contains("referencesProvider");
                instance->capabilities.documentHighlightProvider = caps.contains("documentHighlightProvider");
                instance->capabilities.documentSymbolProvider = caps.contains("documentSymbolProvider");
                instance->capabilities.codeActionProvider = caps.contains("codeActionProvider");
                instance->capabilities.codeLensProvider = caps.contains("codeLensProvider");
                instance->capabilities.documentFormattingProvider = caps.contains("documentFormattingProvider");
                instance->capabilities.documentRangeFormattingProvider = caps.contains("documentRangeFormattingProvider");
                instance->capabilities.documentOnTypeFormattingProvider = caps.contains("documentOnTypeFormattingProvider");
                instance->capabilities.renameProvider = caps.contains("renameProvider");
                instance->capabilities.foldingRangeProvider = caps.contains("foldingRangeProvider");
                instance->capabilities.executeCommandProvider = caps.contains("executeCommandProvider");
                instance->capabilities.selectionRangeProvider = caps.contains("selectionRangeProvider");
                instance->capabilities.semanticTokensProvider = caps.contains("semanticTokensProvider");
                instance->capabilities.linkedEditingRangeProvider = caps.contains("linkedEditingRangeProvider");
                instance->capabilities.callHierarchyProvider = caps.contains("callHierarchyProvider");
                instance->capabilities.workspaceSymbolProvider = caps.contains("workspaceSymbolProvider");
                instance->capabilities.inlayHintProvider = caps.contains("inlayHintProvider");
                instance->capabilities.inlineValueProvider = caps.contains("inlineValueProvider");
                instance->capabilities.diagnosticProvider = caps.contains("diagnosticProvider");
                instance->capabilities.workspaceFolders = caps.value("workspace").toObject()
                    .value("workspaceFolders").toObject().value("supported").toBool(false);
                instance->capabilities.configuration = caps.value("workspace").toObject()
                    .value("configuration").toBool(false);
                instance->state = ServerState::Running;
                instance->initialized = true;
            }

            // Send initialized notification
            instance->jsonRpc->sendNotification("initialized", QJsonObject());

            Q_EMIT serverInitialized(language, instance->capabilities);
        };
    }

    instance->jsonRpc->sendRequest("initialize", params, reqId);
    return true;
}

bool LanguageServerManager::shutdown(const QString& language) {
    auto it = d->servers.find(language);
    if (it == d->servers.end() || !it.value()->jsonRpc) return false;

    auto instance = it.value();
    instance->state = ServerState::ShuttingDown;

    int reqId = ++instance->requestId;
    instance->jsonRpc->sendRequest("shutdown", QJsonObject(), reqId);

    // Wait for shutdown response
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < LSP_TIMEOUT_MS) {
        QMutexLocker ml(&instance->mutex);
        if (!instance->pendingRequests.contains(reqId)) break;
        ml.unlock();
        QThread::msleep(50);
    }

    // Send exit notification
    instance->jsonRpc->sendNotification("exit", QJsonObject());
    return true;
}

ServerCapabilities LanguageServerManager::capabilities(const QString& language) const {
    QMutexLocker lock(&d->capsMutex);
    auto it = d->servers.find(language);
    if (it != d->servers.end()) {
        QMutexLocker ml(&it.value()->mutex);
        return it.value()->capabilities;
    }
    return ServerCapabilities{};
}

bool LanguageServerManager::hasCapability(const QString& language, const QString& cap) const {
    auto caps = capabilities(language);
    if (cap == "textDocumentSync") return caps.textDocumentSync;
    if (cap == "completion") return caps.completionProvider;
    if (cap == "hover") return caps.hoverProvider;
    if (cap == "signatureHelp") return caps.signatureHelpProvider;
    if (cap == "definition") return caps.definitionProvider;
    if (cap == "typeDefinition") return caps.typeDefinitionProvider;
    if (cap == "implementation") return caps.implementationProvider;
    if (cap == "references") return caps.referencesProvider;
    if (cap == "documentHighlight") return caps.documentHighlightProvider;
    if (cap == "documentSymbol") return caps.documentSymbolProvider;
    if (cap == "codeAction") return caps.codeActionProvider;
    if (cap == "codeLens") return caps.codeLensProvider;
    if (cap == "formatting") return caps.documentFormattingProvider;
    if (cap == "rangeFormatting") return caps.documentRangeFormattingProvider;
    if (cap == "rename") return caps.renameProvider;
    if (cap == "foldingRange") return caps.foldingRangeProvider;
    if (cap == "semanticTokens") return caps.semanticTokensProvider;
    if (cap == "inlayHint") return caps.inlayHintProvider;
    if (cap == "diagnostic") return caps.diagnosticProvider;
    return caps.rawCapabilities.contains(cap);
}

ServerState LanguageServerManager::serverState(const QString& language) const {
    QMutexLocker lock(&d->serversMutex);
    auto it = d->servers.find(language);
    if (it != d->servers.end()) {
        return it.value()->state;
    }
    return ServerState::Stopped;
}

QStringList LanguageServerManager::activeServers() const {
    QMutexLocker lock(&d->serversMutex);
    QStringList result;
    for (auto it = d->servers.begin(); it != d->servers.end(); ++it) {
        if (it.value()->state == ServerState::Running) {
            result.append(it.key());
        }
    }
    return result;
}

bool LanguageServerManager::isServerRunning(const QString& language) const {
    return serverState(language) == ServerState::Running;
}

void LanguageServerManager::didOpen(const QString& language, const QString& uri,
                                    const QString& text, const QString& languageId) {
    auto it = d->servers.find(language);
    if (it == d->servers.end() || !it.value()->jsonRpc) return;

    QJsonObject params;
    QJsonObject td;
    td["uri"] = uri;
    td["languageId"] = languageId.isEmpty() ? language : languageId;
    td["version"] = 1;
    td["text"] = text;
    params["textDocument"] = td;

    it.value()->jsonRpc->sendNotification("textDocument/didOpen", params);
}

void LanguageServerManager::didClose(const QString& language, const QString& uri) {
    auto it = d->servers.find(language);
    if (it == d->servers.end() || !it.value()->jsonRpc) return;

    QJsonObject params;
    QJsonObject td; td["uri"] = uri;
    params["textDocument"] = td;

    it.value()->jsonRpc->sendNotification("textDocument/didClose", params);
}

void LanguageServerManager::didChange(const QString& language, const QString& uri,
                                      const QVector<QJsonObject>& contentChanges) {
    auto it = d->servers.find(language);
    if (it == d->servers.end() || !it.value()->jsonRpc) return;

    QJsonArray changes;
    for (const auto& c : contentChanges) changes.append(c);

    QJsonObject params;
    QJsonObject td; td["uri"] = uri; td["version"] = QDateTime::currentDateTimeUtc().toSecsSinceEpoch();
    params["textDocument"] = td;
    params["contentChanges"] = changes;

    it.value()->jsonRpc->sendNotification("textDocument/didChange", params);
}

void LanguageServerManager::didSave(const QString& language, const QString& uri) {
    auto it = d->servers.find(language);
    if (it == d->servers.end() || !it.value()->jsonRpc) return;

    QJsonObject params;
    QJsonObject td; td["uri"] = uri;
    params["textDocument"] = td;

    it.value()->jsonRpc->sendNotification("textDocument/didSave", params);
}

void LanguageServerManager::requestCompletion(const QString& language, const QString& uri,
                                               const QJsonObject& position,
                                               std::function<void(const QJsonArray&)> callback) {
    auto it = d->servers.find(language);
    if (it == d->servers.end() || !it.value()->jsonRpc) return;

    QJsonObject params;
    QJsonObject td; td["uri"] = uri;
    params["textDocument"] = td;
    params["position"] = position;

    int reqId = ++it.value()->requestId;
    {
        QMutexLocker ml(&it.value()->mutex);
        it.value()->pendingRequests[reqId] = [callback](const QJsonObject& result) {
            if (result.contains("items")) {
                callback(result.value("items").toArray());
            } else {
                callback(QJsonArray());
            }
        };
    }
    it.value()->jsonRpc->sendRequest("textDocument/completion", params, reqId);
}

void LanguageServerManager::requestHover(const QString& language, const QString& uri,
                                          const QJsonObject& position,
                                          std::function<void(const QJsonObject&)> callback) {
    auto it = d->servers.find(language);
    if (it == d->servers.end() || !it.value()->jsonRpc) return;

    QJsonObject params;
    QJsonObject td; td["uri"] = uri;
    params["textDocument"] = td;
    params["position"] = position;

    int reqId = ++it.value()->requestId;
    {
        QMutexLocker ml(&it.value()->mutex);
        it.value()->pendingRequests[reqId] = [callback](const QJsonObject& result) {
            callback(result);
        };
    }
    it.value()->jsonRpc->sendRequest("textDocument/hover", params, reqId);
}

void LanguageServerManager::requestDefinition(const QString& language, const QString& uri,
                                               const QJsonObject& position,
                                               std::function<void(const QJsonArray&)> callback) {
    auto it = d->servers.find(language);
    if (it == d->servers.end() || !it.value()->jsonRpc) return;

    QJsonObject params;
    QJsonObject td; td["uri"] = uri;
    params["textDocument"] = td;
    params["position"] = position;

    int reqId = ++it.value()->requestId;
    {
        QMutexLocker ml(&it.value()->mutex);
        it.value()->pendingRequests[reqId] = [callback](const QJsonObject& result) {
            QJsonValue val = result.value("locations");
            if (val.isArray()) {
                callback(val.toArray());
            } else if (result.contains("uri")) {
                callback(QJsonArray{result});
            } else {
                callback(QJsonArray());
            }
        };
    }
    it.value()->jsonRpc->sendRequest("textDocument/definition", params, reqId);
}

void LanguageServerManager::requestReferences(const QString& language, const QString& uri,
                                               const QJsonObject& position, bool includeDeclaration,
                                               std::function<void(const QJsonArray&)> callback) {
    auto it = d->servers.find(language);
    if (it == d->servers.end() || !it.value()->jsonRpc) return;

    QJsonObject params;
    QJsonObject td; td["uri"] = uri;
    params["textDocument"] = td;
    params["position"] = position;
    params["context"] = QJsonObject{{"includeDeclaration", includeDeclaration}};

    int reqId = ++it.value()->requestId;
    {
        QMutexLocker ml(&it.value()->mutex);
        it.value()->pendingRequests[reqId] = [callback](const QJsonObject& result) {
            callback(result.value("locations").toArray());
        };
    }
    it.value()->jsonRpc->sendRequest("textDocument/references", params, reqId);
}

void LanguageServerManager::requestDocumentSymbols(const QString& language, const QString& uri,
                                                    std::function<void(const QJsonArray&)> callback) {
    auto it = d->servers.find(language);
    if (it == d->servers.end() || !it.value()->jsonRpc) return;

    QJsonObject params;
    QJsonObject td; td["uri"] = uri;
    params["textDocument"] = td;

    int reqId = ++it.value()->requestId;
    {
        QMutexLocker ml(&it.value()->mutex);
        it.value()->pendingRequests[reqId] = [callback](const QJsonObject& result) {
            if (result.contains("items")) {
                callback(result.value("items").toArray());
            } else if (result.contains("children")) {
                callback(QJsonArray{result});
            } else {
                callback(QJsonArray());
            }
        };
    }
    it.value()->jsonRpc->sendRequest("textDocument/documentSymbol", params, reqId);
}

void LanguageServerManager::requestFormatting(const QString& language, const QString& uri,
                                               std::function<void(const QJsonArray&)> callback) {
    auto it = d->servers.find(language);
    if (it == d->servers.end() || !it.value()->jsonRpc) return;

    QJsonObject params;
    QJsonObject td; td["uri"] = uri;
    params["textDocument"] = td;
    params["options"] = QJsonObject{{"tabSize", 4}, {"insertSpaces", true}, {"trimTrailingWhitespace", true}};

    int reqId = ++it.value()->requestId;
    {
        QMutexLocker ml(&it.value()->mutex);
        it.value()->pendingRequests[reqId] = [callback](const QJsonObject& result) {
            callback(result.value("items").toArray());
        };
    }
    it.value()->jsonRpc->sendRequest("textDocument/formatting", params, reqId);
}

void LanguageServerManager::requestSignatureHelp(const QString& language, const QString& uri,
                                                  const QJsonObject& position,
                                                  std::function<void(const QJsonObject&)> callback) {
    auto it = d->servers.find(language);
    if (it == d->servers.end() || !it.value()->jsonRpc) return;

    QJsonObject params;
    QJsonObject td; td["uri"] = uri;
    params["textDocument"] = td;
    params["position"] = position;

    int reqId = ++it.value()->requestId;
    {
        QMutexLocker ml(&it.value()->mutex);
        it.value()->pendingRequests[reqId] = [callback](const QJsonObject& result) {
            callback(result);
        };
    }
    it.value()->jsonRpc->sendRequest("textDocument/signatureHelp", params, reqId);
}

void LanguageServerManager::requestCodeAction(const QString& language, const QString& uri,
                                               const QJsonObject& range, const QJsonObject& context,
                                               std::function<void(const QJsonArray&)> callback) {
    auto it = d->servers.find(language);
    if (it == d->servers.end() || !it.value()->jsonRpc) return;

    QJsonObject params;
    QJsonObject td; td["uri"] = uri;
    params["textDocument"] = td;
    params["range"] = range;
    params["context"] = context;

    int reqId = ++it.value()->requestId;
    {
        QMutexLocker ml(&it.value()->mutex);
        it.value()->pendingRequests[reqId] = [callback](const QJsonObject& result) {
            callback(result.value("items").toArray());
        };
    }
    it.value()->jsonRpc->sendRequest("textDocument/codeAction", params, reqId);
}

void LanguageServerManager::requestRename(const QString& language, const QString& uri,
                                           const QJsonObject& position, const QString& newName,
                                           std::function<void(const QJsonObject&)> callback) {
    auto it = d->servers.find(language);
    if (it == d->servers.end() || !it.value()->jsonRpc) return;

    QJsonObject params;
    QJsonObject td; td["uri"] = uri;
    params["textDocument"] = td;
    params["position"] = position;
    params["newName"] = newName;

    int reqId = ++it.value()->requestId;
    {
        QMutexLocker ml(&it.value()->mutex);
        it.value()->pendingRequests[reqId] = [callback](const QJsonObject& result) {
            callback(result);
        };
    }
    it.value()->jsonRpc->sendRequest("textDocument/rename", params, reqId);
}

void LanguageServerManager::requestInlayHints(const QString& language, const QString& uri,
                                               const QJsonObject& range,
                                               std::function<void(const QJsonArray&)> callback) {
    auto it = d->servers.find(language);
    if (it == d->servers.end() || !it.value()->jsonRpc) return;

    QJsonObject params;
    QJsonObject td; td["uri"] = uri;
    params["textDocument"] = td;
    params["range"] = range;

    int reqId = ++it.value()->requestId;
    {
        QMutexLocker ml(&it.value()->mutex);
        it.value()->pendingRequests[reqId] = [callback](const QJsonObject& result) {
            callback(result.value("items").toArray());
        };
    }
    it.value()->jsonRpc->sendRequest("textDocument/inlayHint", params, reqId);
}

void LanguageServerManager::registerLanguage(const QString& language,
                                              const LanguageServerConfig& config) {
    QMutexLocker lock(&d->serversMutex);
    d->configs.insert(language, config);
}

void LanguageServerManager::unregisterLanguage(const QString& language) {
    stopServer(language);
    QMutexLocker lock(&d->serversMutex);
    d->configs.remove(language);
    d->servers.remove(language);
}

QStringList LanguageServerManager::supportedLanguages() const {
    return d->configs.keys();
}

LanguageServerConfig LanguageServerManager::getConfig(const QString& language) const {
    return d->configs.value(language);
}

} // namespace powsys365::ide::lsp
