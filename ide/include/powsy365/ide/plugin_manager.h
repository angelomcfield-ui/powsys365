// plugin_manager.h - Sistema de Plugins "Alexis" para POWSYS365
// Copyright (c) 2025 POWSYS365. All rights reserved.
#pragma once

#include <QObject>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QDir>
#include <QMap>
#include <QList>
#include <QDateTime>
#include <functional>
#include <memory>
#include <vector>

namespace powsys365 {
namespace ide {

// ============================
// Estructura PluginInfo
// ============================

/**
 * @brief Representa la informacion completa de un plugin registrado.
 */
struct PluginInfo {
    QString id;                // Identificador unico (e.g., "power_flow_tool")
    QString name;              // Nombre descriptivo
    QString version;           // Version semantica (e.g., "1.2.0")
    QString description;       // Descripcion del plugin
    QString author;            // Autor del plugin
    QString language;          // "python", "cpp", "javascript"
    QString entryPoint;        // Ruta al archivo de entrada (main.py, etc.)
    bool isLoaded = false;     // Cargado en memoria
    bool isActive = false;     // Habilitado para ejecucion
    QStringList permissions;   // Permisos RBAC solicitados
    QJsonObject config;        // Configuracion del plugin
    QString directory;         // Directorio base del plugin
    QString manifestPath;      // Ruta al manifest.json
    QDateTime loadedAt;        // Fecha/hora de carga

    QJsonObject toJson() const;
    static PluginInfo fromJson(const QJsonObject& obj, const QString& baseDir = QString());
};

// ============================
// PluginManager
// ============================

class PluginManager : public QObject {
    Q_OBJECT

public:
    explicit PluginManager(QObject* parent = nullptr);
    ~PluginManager();

    // Directorio base de plugins (~/.powsy365/alexis/)
    static QString defaultPluginsDirectory();
    void setPluginsDirectory(const QString& path);
    QString pluginsDirectory() const;

    // ============================
    // Carga de plugins
    // ============================
    int loadAllPlugins();
    bool loadPlugin(const QString& pluginDirPath);
    bool validateManifest(const QJsonObject& manifest) const;

    // ============================
    // Ejecucion
    // ============================
    bool executePlugin(const QString& pluginId, const QJsonObject& params = QJsonObject());

    // ============================
    // Gestion de estado
    // ============================
    bool enablePlugin(const QString& pluginId);
    bool disablePlugin(const QString& pluginId);
    bool unloadPlugin(const QString& pluginId);
    bool isPluginLoaded(const QString& pluginId) const;
    bool isPluginActive(const QString& pluginId) const;

    // ============================
    // Consultas
    // ============================
    QList<PluginInfo> getLoadedPlugins() const;
    QList<PluginInfo> getActivePlugins() const;
    QJsonArray getLoadedPluginsJson() const;
    PluginInfo getPluginInfo(const QString& pluginId) const;
    bool hasPlugin(const QString& pluginId) const;

signals:
    void pluginLoaded(const PluginInfo& info);
    void pluginUnloaded(const QString& pluginId);
    void pluginEnabled(const QString& pluginId);
    void pluginDisabled(const QString& pluginId);
    void pluginExecuted(const QString& pluginId, const QJsonObject& result);
    void pluginExecutionError(const QString& pluginId, const QString& error);
    void pluginOutput(const QString& pluginId, const QString& output);
    void allPluginsLoaded(int count);
    void manifestValidationError(const QString& pluginPath, const QString& error);

private slots:
    void onProcessReadyRead();
    void onProcessError();
    void onProcessFinished(int exitCode);

private:
    bool executePythonPlugin(const PluginInfo& info, const QJsonObject& params);
    bool executeCppPlugin(const PluginInfo& info, const QJsonObject& params);
    bool executeJavaScriptPlugin(const PluginInfo& info, const QJsonObject& params);
    QJsonObject readManifest(const QString& manifestPath) const;
    void cleanupProcess(const QString& pluginId);

    struct PrivateData;
    std::unique_ptr<PrivateData> d;
};

} // namespace ide
} // namespace powsys365
