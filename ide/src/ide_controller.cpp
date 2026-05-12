// ide_controller.cpp - Implementacion de IDEController para POWSYS365
// Copyright (c) 2025 POWSYS365. All rights reserved.

#include "powsy365/ide/ide_controller.h"
#include "powsy365/ide/plugin_manager.h"
#include "powsy365/ide/alexis_engine.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTextStream>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QTemporaryFile>
#include <QRegularExpression>
#include <QDebug>

namespace powsys365 {
namespace ide {

// ============================
// Private Data (PIMPL)
// ============================

struct IDEController::PrivateData {
    bool terminalRunning = false;
    QString currentFilePath;
    QProcess* terminalProcess = nullptr;
    QProcess* scriptProcess = nullptr;
    QStringList openFiles;
    QMap<QString, QString> fileContents; // filePath -> content cache
    PluginManager* pluginManager = nullptr;
    AlexisEngine* alexisEngine = nullptr;
    QString lastPythonInterpreter;
    QString lastCppCompiler;
    QString lastShell;
};

// ============================
// Constructor / Destructor
// ============================

IDEController::IDEController(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<PrivateData>())
{
    d->pluginManager = new PluginManager(this);
    d->alexisEngine = new AlexisEngine(this);

    // Conectar senales del PluginManager
    connect(d->pluginManager, &PluginManager::pluginExecuted,
            this, [this](const QString& pluginId, const QJsonObject& result) {
                emit pluginExecuted(pluginId, result);
            });
    connect(d->pluginManager, &PluginManager::pluginExecutionError,
            this, [this](const QString& pluginId, const QString& error) {
                emit pluginError(pluginId, error);
            });
    connect(d->pluginManager, &PluginManager::pluginOutput,
            this, [this](const QString&, const QString& output) {
                emit scriptOutput(output);
            });

    // Conectar senales del AlexisEngine
    connect(d->alexisEngine, &AlexisEngine::permissionDenied,
            this, [this](const QString& pluginId, const QString& permission) {
                emit pluginError(pluginId,
                    QString("Permission denied: %1").arg(permission));
            });
    connect(d->alexisEngine, &AlexisEngine::sandboxViolation,
            this, [this](const QString& pluginId, const QString& violation) {
                emit pluginError(pluginId,
                    QString("Sandbox violation: %1").arg(violation));
            });
}

IDEController::~IDEController() {
    if (d->terminalProcess && d->terminalProcess->state() != QProcess::NotRunning) {
        d->terminalProcess->terminate();
        if (!d->terminalProcess->waitForFinished(3000)) {
            d->terminalProcess->kill();
        }
    }
    if (d->scriptProcess && d->scriptProcess->state() != QProcess::NotRunning) {
        d->scriptProcess->terminate();
        if (!d->scriptProcess->waitForFinished(3000)) {
            d->scriptProcess->kill();
        }
    }
}

// ============================
// Q_PROPERTY getters
// ============================

bool IDEController::isTerminalRunning() const {
    return d->terminalRunning;
}

QString IDEController::currentFile() const {
    return d->currentFilePath;
}

// ============================
// Monaco Editor
// ============================

void IDEController::openFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit editorContentChanged(filePath, QString());
        return;
    }

    QTextStream stream(&file);
    QString content = stream.readAll();
    file.close();

    if (!d->openFiles.contains(filePath)) {
        d->openFiles.append(filePath);
    }
    d->fileContents[filePath] = content;

    setCurrentFile(filePath);
    emit fileOpened(filePath, content);
    emit editorContentChanged(filePath, content);
}

bool IDEController::saveFile(const QString& filePath, const QString& content) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream stream(&file);
    stream << content;
    file.close();

    d->fileContents[filePath] = content;
    emit fileSaved(filePath);
    return true;
}

QString IDEController::createNewFile(const QString& directory,
                                      const QString& fileName,
                                      const QString& templateContent) {
    QDir dir(directory);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    QString filePath = dir.absoluteFilePath(fileName);
    int counter = 1;
    QString baseName = fileName;

    while (QFile::exists(filePath)) {
        QFileInfo fi(fileName);
        QString suffix = fi.suffix();
        QString name = fi.completeBaseName();
        if (!suffix.isEmpty()) {
            filePath = dir.absoluteFilePath(
                QString("%1_%2.%3").arg(name).arg(counter).arg(suffix));
        } else {
            filePath = dir.absoluteFilePath(QString("%1_%2").arg(name).arg(counter));
        }
        ++counter;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return QString();
    }

    QTextStream stream(&file);
    stream << templateContent;
    file.close();

    d->openFiles.append(filePath);
    d->fileContents[filePath] = templateContent;
    setCurrentFile(filePath);
    emit fileOpened(filePath, templateContent);
    return filePath;
}

QString IDEController::getFileContent(const QString& filePath) const {
    if (d->fileContents.contains(filePath)) {
        return d->fileContents[filePath];
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    QTextStream stream(&file);
    QString content = stream.readAll();
    file.close();
    return content;
}

void IDEController::closeFile(const QString& filePath) {
    d->openFiles.removeAll(filePath);
    d->fileContents.remove(filePath);

    if (d->currentFilePath == filePath) {
        if (!d->openFiles.isEmpty()) {
            setCurrentFile(d->openFiles.last());
        } else {
            setCurrentFile(QString());
        }
    }
}

// ============================
// Terminal
// ============================

void IDEController::startTerminal() {
    if (d->terminalRunning) {
        return;
    }

    if (d->terminalProcess) {
        delete d->terminalProcess;
    }

    d->terminalProcess = new QProcess(this);
    d->terminalProcess->setProcessChannelMode(QProcess::MergedChannels);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("POWSYS365_IDE", "1");
    env.insert("POWSYS365_VERSION", "1.0.0");
    d->terminalProcess->setProcessEnvironment(env);
    d->terminalProcess->setWorkingDirectory(QDir::homePath());

    connect(d->terminalProcess, &QProcess::readyReadStandardOutput,
            this, &IDEController::onTerminalReadyRead);
    connect(d->terminalProcess, QOverload<int>::of(&QProcess::finished),
            this, &IDEController::onTerminalFinished);
    connect(d->terminalProcess, &QProcess::errorOccurred,
            this, &IDEController::onTerminalError);

    QString shell = detectShell();
    d->terminalProcess->start(shell);

    if (!d->terminalProcess->waitForStarted(5000)) {
        setTerminalRunning(false);
        emit terminalOutput(QString("[ERROR] No se pudo iniciar el shell: %1\n")
                            .arg(shell));
        return;
    }

    setTerminalRunning(true);
}

void IDEController::stopTerminal() {
    if (!d->terminalProcess || d->terminalProcess->state() == QProcess::NotRunning) {
        setTerminalRunning(false);
        return;
    }

    d->terminalProcess->terminate();
    if (!d->terminalProcess->waitForFinished(3000)) {
        d->terminalProcess->kill();
        d->terminalProcess->waitForFinished(1000);
    }

    setTerminalRunning(false);
}

void IDEController::sendToTerminal(const QString& command) {
    if (!d->terminalRunning || !d->terminalProcess) {
        emit terminalOutput("[ERROR] Terminal no esta en ejecucion.\n");
        return;
    }

    d->terminalProcess->write(command.toUtf8());
    if (!command.endsWith('\n')) {
        d->terminalProcess->write("\n");
    }
    d->terminalProcess->waitForBytesWritten(1000);
}

void IDEController::clearTerminal() {
    emit terminalOutput("\033[2J\033[H");
}

// ============================
// Ejecucion de Scripts
// ============================

void IDEController::runPythonScript(const QString& scriptPath,
                                     const QStringList& arguments) {
    if (d->scriptProcess && d->scriptProcess->state() != QProcess::NotRunning) {
        stopScript();
    }

    if (!QFile::exists(scriptPath)) {
        emit scriptError(QString("[ERROR] Script no encontrado: %1").arg(scriptPath));
        return;
    }

    d->scriptProcess = new QProcess(this);
    d->scriptProcess->setProcessChannelMode(QProcess::SeparateChannels);

    connect(d->scriptProcess, &QProcess::readyReadStandardOutput,
            this, &IDEController::onScriptReadyRead);
    connect(d->scriptProcess, &QProcess::readyReadStandardError,
            this, &IDEController::onScriptError);
    connect(d->scriptProcess, QOverload<int>::of(&QProcess::finished),
            this, &IDEController::onScriptFinished);

    QString python = detectPythonInterpreter();
    QStringList args;
    args << scriptPath << arguments;

    d->scriptProcess->start(python, args);

    if (!d->scriptProcess->waitForStarted(5000)) {
        emit scriptError(QString("[ERROR] No se pudo iniciar Python: %1").arg(python));
    }
}

void IDEController::runCppScript(const QString& sourcePath,
                                  const QStringList& arguments) {
    if (d->scriptProcess && d->scriptProcess->state() != QProcess::NotRunning) {
        stopScript();
    }

    if (!QFile::exists(sourcePath)) {
        emit scriptError(QString("[ERROR] Archivo fuente no encontrado: %1")
                         .arg(sourcePath));
        return;
    }

    QString binaryPath;
    if (!compileCpp(sourcePath, binaryPath)) {
        return;
    }

    d->scriptProcess = new QProcess(this);
    d->scriptProcess->setProcessChannelMode(QProcess::SeparateChannels);

    connect(d->scriptProcess, &QProcess::readyReadStandardOutput,
            this, &IDEController::onScriptReadyRead);
    connect(d->scriptProcess, &QProcess::readyReadStandardError,
            this, &IDEController::onScriptError);
    connect(d->scriptProcess, QOverload<int>::of(&QProcess::finished),
            this, &IDEController::onScriptFinished);

    d->scriptProcess->start(binaryPath, arguments);

    if (!d->scriptProcess->waitForStarted(5000)) {
        emit scriptError(QString("[ERROR] No se pudo ejecutar el binario: %1")
                         .arg(binaryPath));
    }
}

void IDEController::stopScript() {
    if (!d->scriptProcess || d->scriptProcess->state() == QProcess::NotRunning) {
        return;
    }

    d->scriptProcess->terminate();
    if (!d->scriptProcess->waitForFinished(3000)) {
        d->scriptProcess->kill();
        d->scriptProcess->waitForFinished(1000);
    }
}

// ============================
// Plugins Alexis
// ============================

void IDEController::loadAllPlugins() {
    int count = d->pluginManager->loadAllPlugins();
    QJsonArray plugins = d->pluginManager->getLoadedPluginsJson();
    emit pluginsLoaded(plugins);
}

void IDEController::executePlugin(const QString& pluginId,
                                  const QJsonObject& params) {
    // Verificar permisos en el motor Alexis antes de ejecutar
    if (!d->alexisEngine->isActive(pluginId)) {
        // Intentar activar
        if (!d->alexisEngine->activatePlugin(pluginId)) {
            emit pluginError(pluginId,
                "Plugin no registrado o no puede ser activado en Alexis Engine");
            return;
        }
    }

    // Ejecutar via PluginManager
    bool success = d->pluginManager->executePlugin(pluginId, params);
    if (!success) {
        emit pluginError(pluginId, "Error al ejecutar el plugin via PluginManager");
    }
}

QJsonArray IDEController::getLoadedPlugins() const {
    return d->pluginManager->getLoadedPluginsJson();
}

bool IDEController::enablePlugin(const QString& pluginId) {
    if (!d->pluginManager->enablePlugin(pluginId)) {
        return false;
    }
    d->alexisEngine->activatePlugin(pluginId);
    return true;
}

bool IDEController::disablePlugin(const QString& pluginId) {
    d->alexisEngine->deactivatePlugin(pluginId);
    return d->pluginManager->disablePlugin(pluginId);
}

bool IDEController::unloadPlugin(const QString& pluginId) {
    d->alexisEngine->deactivatePlugin(pluginId);
    d->alexisEngine->unregisterPlugin(pluginId);
    return d->pluginManager->unloadPlugin(pluginId);
}

// ============================
// Slots privados
// ============================

void IDEController::onTerminalReadyRead() {
    if (!d->terminalProcess) return;
    QByteArray output = d->terminalProcess->readAllStandardOutput();
    emit terminalOutput(QString::fromUtf8(output));
}

void IDEController::onTerminalError() {
    if (!d->terminalProcess) return;
    QByteArray error = d->terminalProcess->readAllStandardError();
    emit terminalOutput(QString::fromUtf8(error));
}

void IDEController::onTerminalFinished(int exitCode) {
    setTerminalRunning(false);
    emit terminalOutput(QString("\n[Terminal finalizada con codigo %1]\n").arg(exitCode));
}

void IDEController::onScriptReadyRead() {
    if (!d->scriptProcess) return;
    QByteArray output = d->scriptProcess->readAllStandardOutput();
    emit scriptOutput(QString::fromUtf8(output));
}

void IDEController::onScriptError() {
    if (!d->scriptProcess) return;
    QByteArray error = d->scriptProcess->readAllStandardError();
    emit scriptError(QString::fromUtf8(error));
}

void IDEController::onScriptFinished(int exitCode) {
    emit scriptFinished(exitCode);
    emit scriptOutput(QString("\n[Script finalizado con codigo %1]\n").arg(exitCode));
}

// ============================
// Metodos privados
// ============================

void IDEController::setTerminalRunning(bool running) {
    if (d->terminalRunning != running) {
        d->terminalRunning = running;
        emit terminalRunningChanged(running);
    }
}

void IDEController::setCurrentFile(const QString& filePath) {
    if (d->currentFilePath != filePath) {
        d->currentFilePath = filePath;
        emit currentFileChanged(filePath);
    }
}

QString IDEController::detectShell() const {
    if (!d->lastShell.isEmpty()) {
        return d->lastShell;
    }

#ifdef Q_OS_MACOS
    d->lastShell = "/bin/zsh";
    if (!QFile::exists(d->lastShell)) {
        d->lastShell = "/bin/bash";
    }
#elif defined(Q_OS_LINUX)
    d->lastShell = qEnvironmentVariable("SHELL", "/bin/bash");
    if (!QFile::exists(d->lastShell)) {
        d->lastShell = "/bin/bash";
    }
#elif defined(Q_OS_WIN)
    d->lastShell = qEnvironmentVariable("COMSPEC", "cmd.exe");
    if (!QFile::exists(d->lastShell)) {
        d->lastShell = "C:\\Windows\\System32\\cmd.exe";
    }
#else
    d->lastShell = "/bin/sh";
#endif

    return d->lastShell;
}

QString IDEController::detectPythonInterpreter() const {
    if (!d->lastPythonInterpreter.isEmpty()) {
        return d->lastPythonInterpreter;
    }

    QStringList candidates;
#ifdef Q_OS_WIN
    candidates << "python.exe" << "python3.exe" << "py.exe";
#else
    candidates << "python3" << "python";
#endif

    for (const QString& candidate : candidates) {
        QProcess tester;
        tester.start(candidate, QStringList() << "--version");
        if (tester.waitForFinished(3000) && tester.exitCode() == 0) {
            d->lastPythonInterpreter = candidate;
            return candidate;
        }
    }

    d->lastPythonInterpreter = "python3";
    return d->lastPythonInterpreter;
}

QString IDEController::detectCppCompiler() const {
    if (!d->lastCppCompiler.isEmpty()) {
        return d->lastCppCompiler;
    }

    QStringList candidates;
#ifdef Q_OS_WIN
    candidates << "g++" << "cl.exe" << "clang++";
#else
    candidates << "g++" << "clang++" << "c++";
#endif

    for (const QString& candidate : candidates) {
        QProcess tester;
        tester.start(candidate, QStringList() << "--version");
        if (tester.waitForFinished(3000) && tester.exitCode() == 0) {
            d->lastCppCompiler = candidate;
            return candidate;
        }
    }

    d->lastCppCompiler = "g++";
    return d->lastCppCompiler;
}

bool IDEController::compileCpp(const QString& sourcePath, QString& outBinaryPath) {
    QFileInfo fi(sourcePath);
    QString baseName = fi.completeBaseName();
    QDir buildDir = fi.dir();
    buildDir.mkpath("build");

#ifdef Q_OS_WIN
    outBinaryPath = buildDir.absoluteFilePath(QString("build/%1.exe").arg(baseName));
#else
    outBinaryPath = buildDir.absoluteFilePath(QString("build/%1").arg(baseName));
#endif

    QString compiler = detectCppCompiler();
    QStringList args;
    args << "-std=c++17" << "-O2" << "-o" << outBinaryPath << sourcePath;

    QProcess compileProcess;
    compileProcess.start(compiler, args);

    if (!compileProcess.waitForFinished(60000)) {
        emit scriptError(QString("[ERROR] Timeout durante la compilacion"));
        return false;
    }

    if (compileProcess.exitCode() != 0) {
        QString errorOutput = QString::fromUtf8(compileProcess.readAllStandardError());
        emit scriptError(QString("[ERROR] Compilacion fallida:\n%1").arg(errorOutput));
        return false;
    }

    return true;
}

} // namespace ide
} // namespace powsys365
