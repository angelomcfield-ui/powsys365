// alexis_engine.h - Alexis Engine: Motor de Plugins para POWSYS365
// Copyright (c) 2025 POWSYS365. All rights reserved.
#pragma once

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QSet>
#include <QStringList>
#include <QDateTime>
#include <QFile>
#include <QDir>
#include <functional>
#include <memory>
#include <vector>

namespace powsys365 {
namespace ide {

// ============================
// Permission System
// ============================

/**
 * @brief Permisos disponibles en el sistema Alexis RBAC.
 */
enum class PluginPermission {
    Network,      // Acceso a red
    Filesystem,   // Lectura/escritura de archivos
    Database,     // Acceso a base de datos
    GUI,          // Interaccion con la interfaz grafica
    System,       // Ejecucion de comandos del sistema
    ElectricData, // Acceso a datos del sistema electrico
    Logging,      // Logging y notificaciones
    Configuration // Modificacion de configuracion
};

QString permissionToString(PluginPermission perm);
PluginPermission stringToPermission(const QString& str);

// ============================
// Permission Policy
// ============================

struct PermissionPolicy {
    PluginPermission permission;
    bool granted = false;
    QString reason;          // Justificacion del permiso
    QStringList restrictions; // Restricciones especificas

    QJsonObject toJson() const;
    static PermissionPolicy fromJson(const QJsonObject& obj);
};

// ============================
// PluginSandbox
// ============================

/**
 * @brief Entorno sandbox para aislamiento de plugins.
 */
struct PluginSandbox {
    QString pluginId;
    QString rootDirectory;       // Directorio raiz del plugin
    QStringList allowedPaths;    // Rutas de acceso permitidas
    QStringList blockedPaths;    // Rutas bloqueadas
    bool networkAccess = false;
    bool fileSystemAccess = false;
    bool databaseAccess = false;
    bool guiAccess = false;
    bool systemAccess = false;
    qint64 maxMemoryMB = 512;    // Limite de memoria
    int maxCpuPercent = 50;      // Limite de CPU
    QDateTime createdAt;

    QJsonObject toJson() const;
    static PluginSandbox fromJson(const QJsonObject& obj);
};

// ============================
// Lifecycle Hooks
// ============================

enum class LifecycleEvent {
    OnLoad,
    OnUnload,
    OnExecute,
    OnEnable,
    OnDisable,
    OnError
};

QString lifecycleEventToString(LifecycleEvent event);

// ============================
// AlexisEngine
// ============================

class AlexisEngine : public QObject {
    Q_OBJECT

public:
    using LifecycleHook = std::function<void(const QString& pluginId, LifecycleEvent event,
                                               const QJsonObject& context)>;
    using PluginMessageHandler = std::function<void(const QString& fromPluginId,
                                                      const QString& toPluginId,
                                                      const QJsonObject& message)>;

    explicit AlexisEngine(QObject* parent = nullptr);
    ~AlexisEngine();

    // ============================
    // Ciclo de vida
    // ============================
    bool registerPlugin(const QString& pluginId, const QJsonObject& manifest);
    bool unregisterPlugin(const QString& pluginId);
    bool activatePlugin(const QString& pluginId);
    bool deactivatePlugin(const QString& pluginId);

    // ============================
    // Hooks
    // ============================
    void registerHook(LifecycleEvent event, const QString& pluginId,
                      LifecycleHook hook);
    void unregisterHooks(const QString& pluginId);
    void triggerHook(const QString& pluginId, LifecycleEvent event,
                     const QJsonObject& context = QJsonObject());

    // ============================
    // Permisos RBAC
    // ============================
    bool setPermissions(const QString& pluginId,
                        const QList<PermissionPolicy>& policies);
    bool checkPermission(const QString& pluginId,
                         PluginPermission permission) const;
    bool grantPermission(const QString& pluginId,
                         PluginPermission permission,
                         const QString& reason = QString());
    bool revokePermission(const QString& pluginId,
                          PluginPermission permission);
    QList<PermissionPolicy> getPermissions(const QString& pluginId) const;

    // ============================
    // Sandboxing
    // ============================
    bool createSandbox(const QString& pluginId,
                       const QJsonObject& restrictions = QJsonObject());
    bool destroySandbox(const QString& pluginId);
    bool updateSandbox(const QString& pluginId,
                       const QJsonObject& restrictions);
    PluginSandbox getSandbox(const QString& pluginId) const;
    bool isSandboxed(const QString& pluginId) const;
    bool validateSandboxAccess(const QString& pluginId,
                               const QString& resourcePath,
                               const QString& accessType) const;

    // ============================
    // Comunicacion entre plugins
    // ============================
    bool sendMessage(const QString& fromPluginId, const QString& toPluginId,
                     const QJsonObject& message);
    void broadcastMessage(const QString& fromPluginId,
                          const QJsonObject& message,
                          const QStringList& excludeIds = QStringList());
    void registerMessageHandler(const QString& pluginId,
                                PluginMessageHandler handler);
    void unregisterMessageHandler(const QString& pluginId);

    // ============================
    // Consultas
    // ============================
    QStringList registeredPlugins() const;
    bool isRegistered(const QString& pluginId) const;
    bool isActive(const QString& pluginId) const;
    QJsonObject getPluginManifest(const QString& pluginId) const;
    QJsonObject getEngineStatus() const;
    void clear();

signals:
    void pluginRegistered(const QString& pluginId);
    void pluginUnregistered(const QString& pluginId);
    void pluginActivated(const QString& pluginId);
    void pluginDeactivated(const QString& pluginId);
    void hookTriggered(const QString& pluginId, const QString& event);
    void permissionDenied(const QString& pluginId, const QString& permission);
    void messageReceived(const QString& fromPluginId, const QString& toPluginId,
                         const QJsonObject& message);
    void broadcastReceived(const QString& fromPluginId, const QJsonObject& message);
    void sandboxViolation(const QString& pluginId, const QString& violation);
    void engineError(const QString& error);

private:
    struct PrivateData;
    std::unique_ptr<PrivateData> d;
};

} // namespace ide
} // namespace powsys365
