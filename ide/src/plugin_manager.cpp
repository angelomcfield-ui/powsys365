// plugin_manager.cpp - Implementacion de PluginManager (Sistema "Alexis")
// Copyright (c) 2025 POWSYS365. All rights reserved.

#include "powsy365/ide/plugin_manager.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDirIterator>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QDateTime>
#include <QDebug>
#include <QLibrary>

namespace powsys365 {
namespace ide {

// ============================
// PluginInfo Implementation
// ============================

QJsonObject PluginInfo::toJson() const {
    QJsonObject obj;
    obj["id"] = id;
    obj["name"] = name;
    obj["version"] = version;
    obj["description"] = description;
    obj["author"] = author;
    obj["language"] = language;
    obj["entryPoint"] = entryPoint;
    obj["isLoaded"] = isLoaded;
    obj["isActive"] = isActive;
    obj["config"] = config;
    obj["directory"] = directory;
    obj["manifestPath"] = manifestPath;
    obj["loadedAt"] = loadedAt.toString(Qt::ISODate);

    QJsonArray perms;
    for (const QString& p : permissions) {
        perms.append(p);
    }
    obj["permissions"] = perms;

    return obj;
}

PluginInfo PluginInfo::fromJson(const QJsonObject& obj, const QString& baseDir) {
    PluginInfo info;
    info.id = obj.value("id").toString();
    info.name = obj.value("name").toString();
    info.version = obj.value("version").toString();
    info.description = obj.value("description").toString();
    info.author = obj.value("author").toString();
    info.language = obj.value("language").toString();
    info.entryPoint = obj.value("entryPoint").toString();
    info.isLoaded = obj.value("isLoaded").toBool();
    info.isActive = obj.value("isActive").toBool();
    info.config = obj.value("config").toObject();
    info.directory = baseDir.isEmpty() ? obj.value("directory").toString() : baseDir;
    info.manifestPath = obj.value("manifestPath").toString();
    info.loadedAt = QDateTime::fromString(
        obj.value("loadedAt").toString(), Qt::ISODate);

    QJsonArray perms = obj.value("permissions").toArray();
    for (const QJsonValue& v : perms) {
        info.permissions.append(v.toString());
    }

    return info;
}

// ============================
// Private Data
// ============================

struct PluginManager::PrivateData {
    QString pluginsDirectory;
    QMap<QString, PluginInfo> loadedPlugins;   // id -> info
    QMap<QString, QProcess*> runningProcesses; // id -> process
    QSet<QString> activePlugins;
    QStringList validationErrors;
};

// ============================
// Constructor / Destructor
// ============================

PluginManager::PluginManager(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<PrivateData>())
{
    d->pluginsDirectory = defaultPluginsDirectory();
}

PluginManager::~PluginManager() {
    // Detener todos los procesos en ejecucion
    for (auto it = d->runningProcesses.begin(); it != d->runningProcesses.end(); ++it) {
        QProcess* proc = it.value();
        if (proc && proc->state() != QProcess::NotRunning) {
            proc->terminate();
            proc->waitForFinished(3000);
            if (proc->state() != QProcess::NotRunning) {
                proc->kill();
            }
        }
        delete proc;
    }
    d->runningProcesses.clear();
}

// ============================
// Directorio de plugins
// ============================

QString PluginManager::defaultPluginsDirectory() {
    return QDir::homePath() + "/.powsy365/alexis";
}

void PluginManager::setPluginsDirectory(const QString& path) {
    d->pluginsDirectory = path;
}

QString PluginManager::pluginsDirectory() const {
    return d->pluginsDirectory;
}

// ============================
// Carga de plugins
// ============================

int PluginManager::loadAllPlugins() {
    QDir dir(d->pluginsDirectory);
    if (!dir.exists()) {
        dir.mkpath(".");
        return 0;
    }

    int loaded = 0;
    QFileInfoList entries = dir.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QFileInfo& entry : entries) {
        if (loadPlugin(entry.absoluteFilePath())) {
            ++loaded;
        }
    }

    emit allPluginsLoaded(loaded);
    return loaded;
}

bool PluginManager::loadPlugin(const QString& pluginDirPath) {
    QDir pluginDir(pluginDirPath);
    if (!pluginDir.exists()) {
        emit manifestValidationError(pluginDirPath,
            "Directorio del plugin no existe");
        return false;
    }

    QString manifestPath = pluginDir.absoluteFilePath("manifest.json");
    QJsonObject manifest = readManifest(manifestPath);
    if (manifest.isEmpty()) {
        emit manifestValidationError(pluginDirPath,
            "No se pudo leer o parsear manifest.json");
        return false;
    }

    if (!validateManifest(manifest)) {
        emit manifestValidationError(pluginDirPath,
            "El manifest.json no cumple el schema requerido");
        return false;
    }

    PluginInfo info;
    info.id = manifest.value("id").toString();
    info.name = manifest.value("name").toString();
    info.version = manifest.value("version").toString();
    info.description = manifest.value("description").toString();
    info.author = manifest.value("author").toString();
    info.language = manifest.value("language").toString();
    info.entryPoint = manifest.value("entryPoint").toString();
    info.directory = pluginDirPath;
    info.manifestPath = manifestPath;
    info.isLoaded = true;
    info.isActive = true;
    info.loadedAt = QDateTime::currentDateTime();

    // Parsear permisos
    QJsonArray perms = manifest.value("permissions").toArray();
    for (const QJsonValue& v : perms) {
        info.permissions.append(v.toString());
    }

    // Parsear configuracion
    info.config = manifest.value("config").toObject();

    // Si ya existe, sobrescribir
    if (d->loadedPlugins.contains(info.id)) {
        unloadPlugin(info.id);
    }

    d->loadedPlugins[info.id] = info;
    d->activePlugins.insert(info.id);

    emit pluginLoaded(info);
    emit pluginEnabled(info.id);
    return true;
}

bool PluginManager::validateManifest(const QJsonObject& manifest) const {
    QStringList requiredFields;
    requiredFields << "id" << "name" << "version" << "language" << "entryPoint";

    for (const QString& field : requiredFields) {
        if (!manifest.contains(field) || manifest.value(field).isNull() ||
            manifest.value(field).toString().isEmpty()) {
            return false;
        }
    }

    // Validar formato de ID (alphanumeric, guiones, puntos)
    QString id = manifest.value("id").toString();
    QRegularExpression idRegex("^[a-zA-Z0-9._-]+$");
    if (!idRegex.match(id).hasMatch()) {
        return false;
    }

    // Validar lenguaje soportado
    QString lang = manifest.value("language").toString().toLower();
    QStringList supportedLanguages;
    supportedLanguages << "python" << "cpp" << "c++" << "javascript" << "js";
    if (!supportedLanguages.contains(lang)) {
        return false;
    }

    // Validar version semantica basica
    QString version = manifest.value("version").toString();
    QRegularExpression verRegex("^\\d+\\.\\d+\\.\\d+");
    if (!verRegex.match(version).hasMatch()) {
        return false;
    }

    // Validar permisos si existen
    if (manifest.contains("permissions")) {
        QJsonArray perms = manifest.value("permissions").toArray();
        QStringList validPerms;
        validPerms << "network" << "filesystem" << "database" << "gui"
                   << "system" << "electric_data" << "logging" << "configuration";
        for (const QJsonValue& v : perms) {
            if (!validPerms.contains(v.toString().toLower())) {
                return false;
            }
        }
    }

    return true;
}

// ============================
// Ejecucion
// ============================

bool PluginManager::executePlugin(const QString& pluginId,
                                  const QJsonObject& params) {
    if (!d->loadedPlugins.contains(pluginId)) {
        emit pluginExecutionError(pluginId,
            QString("Plugin '%1' no esta cargado").arg(pluginId));
        return false;
    }

    PluginInfo info = d->loadedPlugins[pluginId];
    if (!info.isActive) {
        emit pluginExecutionError(pluginId,
            QString("Plugin '%1' esta deshabilitado").arg(pluginId));
        return false;
    }

    // Limpiar proceso previo si existe
    cleanupProcess(pluginId);

    QString lang = info.language.toLower();
    if (lang == "python") {
        return executePythonPlugin(info, params);
    } else if (lang == "cpp" || lang == "c++") {
        return executeCppPlugin(info, params);
    } else if (lang == "javascript" || lang == "js") {
        return executeJavaScriptPlugin(info, params);
    }

    emit pluginExecutionError(pluginId,
        QString("Lenguaje no soportado: %1").arg(info.language));
    return false;
}

bool PluginManager::executePythonPlugin(const PluginInfo& info,
                                        const QJsonObject& params) {
    QString entryPointPath = QDir(info.directory).absoluteFilePath(info.entryPoint);
    if (!QFile::exists(entryPointPath)) {
        emit pluginExecutionError(info.id,
            QString("Entry point no encontrado: %1").arg(entryPointPath));
        return false;
    }

    QProcess* proc = new QProcess(this);
    proc->setProcessChannelMode(QProcess::SeparateChannels);
    proc->setWorkingDirectory(info.directory);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("POWSYS365_PLUGIN_ID", info.id);
    env.insert("POWSYS365_PLUGIN_DIR", info.directory);
    env.insert("POWSYS365_PLUGIN_PARAMS", QJsonDocument(params).toJson(QJsonDocument::Compact));
    proc->setProcessEnvironment(env);

    connect(proc, &QProcess::readyReadStandardOutput,
            this, &PluginManager::onProcessReadyRead);
    connect(proc, &QProcess::readyReadStandardError,
            this, &PluginManager::onProcessError);
    connect(proc, QOverload<int>::of(&QProcess::finished),
            this, &PluginManager::onProcessFinished);

    // Detectar Python
    QStringList candidates;
#ifdef Q_OS_WIN
    candidates << "python.exe" << "python3.exe" << "py.exe";
#else
    candidates << "python3" << "python";
#endif

    QString python;
    for (const QString& candidate : candidates) {
        QProcess tester;
        tester.start(candidate, QStringList() << "--version");
        if (tester.waitForFinished(3000) && tester.exitCode() == 0) {
            python = candidate;
            break;
        }
    }
    if (python.isEmpty()) {
        python = "python3";
    }

    QStringList args;
    args << entryPointPath;

    // Pasar parametros como JSON
    if (!params.isEmpty()) {
        args << QJsonDocument(params).toJson(QJsonDocument::Compact);
    }

    proc->start(python, args);
    if (!proc->waitForStarted(5000)) {
        emit pluginExecutionError(info.id,
            QString("No se pudo iniciar el interprete Python"));
        delete proc;
        return false;
    }

    d->runningProcesses[info.id] = proc;
    return true;
}

bool PluginManager::executeCppPlugin(const PluginInfo& info,
                                     const QJsonObject& params) {
    QString entryPointPath = QDir(info.directory).absoluteFilePath(info.entryPoint);
    if (!QFile::exists(entryPointPath)) {
        emit pluginExecutionError(info.id,
            QString("Entry point no encontrado: %1").arg(entryPointPath));
        return false;
    }

    // Compilar primero si es un archivo fuente
    QFileInfo fi(entryPointPath);
    QString binaryPath;

    if (fi.suffix() == "cpp" || fi.suffix() == "cc" || fi.suffix() == "cxx") {
        QDir buildDir(info.directory);
        buildDir.mkpath("build");

#ifdef Q_OS_WIN
        binaryPath = buildDir.absoluteFilePath(
            QString("build/%1.exe").arg(fi.completeBaseName()));
#else
        binaryPath = buildDir.absoluteFilePath(
            QString("build/%1").arg(fi.completeBaseName()));
#endif

        QStringList candidates;
#ifdef Q_OS_WIN
        candidates << "g++" << "clang++";
#else
        candidates << "g++" << "clang++" << "c++";
#endif

        QString compiler;
        for (const QString& candidate : candidates) {
            QProcess tester;
            tester.start(candidate, QStringList() << "--version");
            if (tester.waitForFinished(3000) && tester.exitCode() == 0) {
                compiler = candidate;
                break;
            }
        }
        if (compiler.isEmpty()) {
            compiler = "g++";
        }

        QStringList compileArgs;
        compileArgs << "-std=c++17" << "-O2" << "-o" << binaryPath << entryPointPath;

        QProcess compileProc;
        compileProc.start(compiler, compileArgs);
        if (!compileProc.waitForFinished(60000) || compileProc.exitCode() != 0) {
            QString err = QString::fromUtf8(compileProc.readAllStandardError());
            emit pluginExecutionError(info.id,
                QString("Compilacion fallida:\n%1").arg(err));
            return false;
        }
    } else {
        binaryPath = entryPointPath;
    }

    // Ejecutar el binario
    QProcess* proc = new QProcess(this);
    proc->setProcessChannelMode(QProcess::SeparateChannels);
    proc->setWorkingDirectory(info.directory);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("POWSYS365_PLUGIN_ID", info.id);
    env.insert("POWSYS365_PLUGIN_DIR", info.directory);
    proc->setProcessEnvironment(env);

    connect(proc, &QProcess::readyReadStandardOutput,
            this, &PluginManager::onProcessReadyRead);
    connect(proc, &QProcess::readyReadStandardError,
            this, &PluginManager::onProcessError);
    connect(proc, QOverload<int>::of(&QProcess::finished),
            this, &PluginManager::onProcessFinished);

    QStringList args;
    if (!params.isEmpty()) {
        args << QJsonDocument(params).toJson(QJsonDocument::Compact);
    }

    proc->start(binaryPath, args);
    if (!proc->waitForStarted(5000)) {
        emit pluginExecutionError(info.id,
            QString("No se pudo iniciar el binario: %1").arg(binaryPath));
        delete proc;
        return false;
    }

    d->runningProcesses[info.id] = proc;
    return true;
}

bool PluginManager::executeJavaScriptPlugin(const PluginInfo& info,
                                            const QJsonObject& params) {
    QString entryPointPath = QDir(info.directory).absoluteFilePath(info.entryPoint);
    if (!QFile::exists(entryPointPath)) {
        emit pluginExecutionError(info.id,
            QString("Entry point no encontrado: %1").arg(entryPointPath));
        return false;
    }

    QProcess* proc = new QProcess(this);
    proc->setProcessChannelMode(QProcess::SeparateChannels);
    proc->setWorkingDirectory(info.directory);

    connect(proc, &QProcess::readyReadStandardOutput,
            this, &PluginManager::onProcessReadyRead);
    connect(proc, &QProcess::readyReadStandardError,
            this, &PluginManager::onProcessError);
    connect(proc, QOverload<int>::of(&QProcess::finished),
            this, &PluginManager::onProcessFinished);

    // Detectar Node.js
    QStringList candidates;
#ifdef Q_OS_WIN
    candidates << "node.exe" << "node";
#else
    candidates << "node" << "nodejs";
#endif

    QString node;
    for (const QString& candidate : candidates) {
        QProcess tester;
        tester.start(candidate, QStringList() << "--version");
        if (tester.waitForFinished(3000) && tester.exitCode() == 0) {
            node = candidate;
            break;
        }
    }
    if (node.isEmpty()) {
        emit pluginExecutionError(info.id,
            "Node.js no encontrado. Instale Node.js para ejecutar plugins JavaScript.");
        delete proc;
        return false;
    }

    QStringList args;
    args << entryPointPath;
    if (!params.isEmpty()) {
        args << QJsonDocument(params).toJson(QJsonDocument::Compact);
    }

    proc->start(node, args);
    if (!proc->waitForStarted(5000)) {
        emit pluginExecutionError(info.id,
            QString("No se pudo iniciar Node.js"));
        delete proc;
        return false;
    }

    d->runningProcesses[info.id] = proc;
    return true;
}

// ============================
// Gestion de estado
// ============================

bool PluginManager::enablePlugin(const QString& pluginId) {
    if (!d->loadedPlugins.contains(pluginId)) {
        return false;
    }
    d->loadedPlugins[pluginId].isActive = true;
    d->activePlugins.insert(pluginId);
    emit pluginEnabled(pluginId);
    return true;
}

bool PluginManager::disablePlugin(const QString& pluginId) {
    if (!d->loadedPlugins.contains(pluginId)) {
        return false;
    }
    d->loadedPlugins[pluginId].isActive = false;
    d->activePlugins.remove(pluginId);

    // Detener proceso si esta corriendo
    cleanupProcess(pluginId);

    emit pluginDisabled(pluginId);
    return true;
}

bool PluginManager::unloadPlugin(const QString& pluginId) {
    if (!d->loadedPlugins.contains(pluginId)) {
        return false;
    }

    cleanupProcess(pluginId);

    d->activePlugins.remove(pluginId);
    d->loadedPlugins[pluginId].isLoaded = false;
    d->loadedPlugins[pluginId].isActive = false;

    emit pluginUnloaded(pluginId);
    d->loadedPlugins.remove(pluginId);
    return true;
}

bool PluginManager::isPluginLoaded(const QString& pluginId) const {
    return d->loadedPlugins.contains(pluginId) && d->loadedPlugins[pluginId].isLoaded;
}

bool PluginManager::isPluginActive(const QString& pluginId) const {
    return d->activePlugins.contains(pluginId);
}

// ============================
// Consultas
// ============================

QList<PluginInfo> PluginManager::getLoadedPlugins() const {
    return d->loadedPlugins.values();
}

QList<PluginInfo> PluginManager::getActivePlugins() const {
    QList<PluginInfo> result;
    for (const QString& id : d->activePlugins) {
        if (d->loadedPlugins.contains(id)) {
            result.append(d->loadedPlugins[id]);
        }
    }
    return result;
}

QJsonArray PluginManager::getLoadedPluginsJson() const {
    QJsonArray arr;
    for (const PluginInfo& info : d->loadedPlugins.values()) {
        arr.append(info.toJson());
    }
    return arr;
}

PluginInfo PluginManager::getPluginInfo(const QString& pluginId) const {
    if (d->loadedPlugins.contains(pluginId)) {
        return d->loadedPlugins[pluginId];
    }
    return PluginInfo();
}

bool PluginManager::hasPlugin(const QString& pluginId) const {
    return d->loadedPlugins.contains(pluginId);
}

// ============================
// Slots privados
// ============================

void PluginManager::onProcessReadyRead() {
    QProcess* proc = qobject_cast<QProcess*>(sender());
    if (!proc) return;

    QByteArray output = proc->readAllStandardOutput();
    QString outputStr = QString::fromUtf8(output);

    // Encontrar el plugin asociado a este proceso
    for (auto it = d->runningProcesses.begin();
         it != d->runningProcesses.end(); ++it) {
        if (it.value() == proc) {
            emit pluginOutput(it.key(), outputStr);
            break;
        }
    }
}

void PluginManager::onProcessError() {
    QProcess* proc = qobject_cast<QProcess*>(sender());
    if (!proc) return;

    QByteArray error = proc->readAllStandardError();
    QString errorStr = QString::fromUtf8(error);

    for (auto it = d->runningProcesses.begin();
         it != d->runningProcesses.end(); ++it) {
        if (it.value() == proc) {
            emit pluginExecutionError(it.key(), errorStr);
            break;
        }
    }
}

void PluginManager::onProcessFinished(int exitCode) {
    QProcess* proc = qobject_cast<QProcess*>(sender());
    if (!proc) return;

    for (auto it = d->runningProcesses.begin();
         it != d->runningProcesses.end(); ++it) {
        if (it.value() == proc) {
            QJsonObject result;
            result["exitCode"] = exitCode;
            result["pluginId"] = it.key();
            result["finishedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);

            emit pluginExecuted(it.key(), result);
            cleanupProcess(it.key());
            break;
        }
    }
}

// ============================
// Helpers privados
// ============================

QJsonObject PluginManager::readManifest(const QString& manifestPath) const {
    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QJsonObject();
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject()) {
        return QJsonObject();
    }

    return doc.object();
}

void PluginManager::cleanupProcess(const QString& pluginId) {
    if (!d->runningProcesses.contains(pluginId)) {
        return;
    }

    QProcess* proc = d->runningProcesses[pluginId];
    if (proc) {
        if (proc->state() != QProcess::NotRunning) {
            proc->terminate();
            if (!proc->waitForFinished(3000)) {
                proc->kill();
            }
        }
        delete proc;
    }
    d->runningProcesses.remove(pluginId);
}

} // namespace ide
} // namespace powsys365
