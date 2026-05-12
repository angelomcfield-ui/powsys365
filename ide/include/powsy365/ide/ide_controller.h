// ide_controller.h - POWSYS365 IDE Controller
// Copyright (c) 2025 POWSYS365. All rights reserved.
#pragma once

#include <QObject>
#include <QProcess>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QMap>
#include <memory>
#include <vector>

namespace powsys365 {
namespace ide {

// Pre-declaraciones
class PluginManager;
class AlexisEngine;
class TerminalWidget;
class MonacoBridge;

/**
 * @brief IDEController - Controlador principal del IDE de POWSYS365.
 *
 * Gestiona el editor Monaco, terminal integrada, ejecucion de scripts,
 * y el sistema de plugins "Alexis". Es el punto central de coordinacion
 * entre todos los componentes del entorno de desarrollo.
 */
class IDEController : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool isTerminalRunning READ isTerminalRunning NOTIFY terminalRunningChanged)
    Q_PROPERTY(QString currentFile READ currentFile NOTIFY currentFileChanged)

public:
    explicit IDEController(QObject* parent = nullptr);
    ~IDEController();

    // Q_PROPERTY getters
    bool isTerminalRunning() const;
    QString currentFile() const;

    // ============================
    // Monaco Editor
    // ============================
public slots:
    void openFile(const QString& filePath);
    bool saveFile(const QString& filePath, const QString& content);
    QString createNewFile(const QString& directory, const QString& fileName,
                          const QString& templateContent = QString());
    QString getFileContent(const QString& filePath) const;
    void closeFile(const QString& filePath);

    // ============================
    // Terminal
    // ============================
    void startTerminal();
    void stopTerminal();
    void sendToTerminal(const QString& command);
    void clearTerminal();

    // ============================
    // Ejecucion de Scripts
    // ============================
    void runPythonScript(const QString& scriptPath,
                         const QStringList& arguments = QStringList());
    void runCppScript(const QString& sourcePath,
                      const QStringList& arguments = QStringList());
    void stopScript();

    // ============================
    // Plugins Alexis
    // ============================
    void loadAllPlugins();
    void executePlugin(const QString& pluginId,
                       const QJsonObject& params = QJsonObject());
    QJsonArray getLoadedPlugins() const;
    bool enablePlugin(const QString& pluginId);
    bool disablePlugin(const QString& pluginId);
    bool unloadPlugin(const QString& pluginId);

signals:
    void terminalRunningChanged(bool running);
    void currentFileChanged(const QString& filePath);
    void fileOpened(const QString& filePath, const QString& content);
    void fileSaved(const QString& filePath);
    void terminalOutput(const QString& output);
    void scriptOutput(const QString& output);
    void scriptError(const QString& error);
    void scriptFinished(int exitCode);
    void pluginExecuted(const QString& pluginId, const QJsonObject& result);
    void pluginError(const QString& pluginId, const QString& error);
    void pluginsLoaded(const QJsonArray& plugins);
    void editorContentChanged(const QString& filePath, const QString& content);

private slots:
    void onTerminalReadyRead();
    void onTerminalError();
    void onTerminalFinished(int exitCode);
    void onScriptReadyRead();
    void onScriptError();
    void onScriptFinished(int exitCode);

private:
    void setTerminalRunning(bool running);
    void setCurrentFile(const QString& filePath);
    QString detectShell() const;
    QString detectPythonInterpreter() const;
    QString detectCppCompiler() const;
    bool compileCpp(const QString& sourcePath, QString& outBinaryPath);

    struct PrivateData;
    std::unique_ptr<PrivateData> d;
};

} // namespace ide
} // namespace powsys365
