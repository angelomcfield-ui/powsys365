// alexis_engine.cpp - Alexis Engine: Motor de Plugins para POWSYS365
// Copyright (c) 2025 POWSYS365. All rights reserved.

#include "powsy365/ide/alexis_engine.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QDebug>
#include <QRegularExpression>

namespace powsys365 {
namespace ide {

// ============================
// Permission utilities
// ============================

QString permissionToString(PluginPermission perm) {
    switch (perm) {
        case PluginPermission::Network:      return "network";
        case PluginPermission::Filesystem:   return "filesystem";
        case PluginPermission::Database:     return "database";
        case PluginPermission::GUI:          return "gui";
        case PluginPermission::System:       return "system";
        case PluginPermission::ElectricData: return "electric_data";
        case PluginPermission::Logging:      return "logging";
        case PluginPermission::Configuration: return "configuration";
    }
    return "unknown";
}

PluginPermission stringToPermission(const QString& str) {
    QString s = str.toLower();
    if (s == "network")       return PluginPermission::Network;
    if (s == "filesystem")    return PluginPermission::Filesystem;
    if (s == "database")      return PluginPermission::Database;
    if (s == "gui")           return PluginPermission::GUI;
    if (s == "system")        return PluginPermission::System;
    if (s == "electric_data") return PluginPermission::ElectricData;
    if (s == "logging")       return PluginPermission::Logging;
    if (s == "configuration") return PluginPermission::Configuration;
    return PluginPermission::Network; // default fallback
}

QString lifecycleEventToString(LifecycleEvent event) {
    switch (event) {
        case LifecycleEvent::OnLoad:    return "onLoad";
        case LifecycleEvent::OnUnload:  return "onUnload";
        case LifecycleEvent::OnExecute: return "onExecute";
        case LifecycleEvent::OnEnable:  return "onEnable";
        case LifecycleEvent::OnDisable: return "onDisable";
        case LifecycleEvent::OnError:   return "onError";
    }
    return "unknown";
}

// ============================
// PermissionPolicy Implementation
// ============================

QJsonObject PermissionPolicy::toJson() const {
    QJsonObject obj;
    obj["permission"] = permissionToString(permission);
    obj["granted"] = granted;
    obj["reason"] = reason;
    QJsonArray rest;
    for (const QString& r : restrictions) {
        rest.append(r);
    }
    obj["restrictions"] = rest;
    return obj;
}

PermissionPolicy PermissionPolicy::fromJson(const QJsonObject& obj) {
    PermissionPolicy policy;
    policy.permission = stringToPermission(obj.value("permission").toString());
    policy.granted = obj.value("granted").toBool();
    policy.reason = obj.value("reason").toString();
    QJsonArray rest = obj.value("restrictions").toArray();
    for (const QJsonValue& v : rest) {
        policy.restrictions.append(v.toString());
    }
    return policy;
}

// ============================
// PluginSandbox Implementation
// ============================

QJsonObject PluginSandbox::toJson() const {
    QJsonObject obj;
    obj["pluginId"] = pluginId;
    obj["rootDirectory"] = rootDirectory;
    QJsonArray allowed;
    for (const QString& p : allowedPaths) {
        allowed.append(p);
    }
    obj["allowedPaths"] = allowed;
    QJsonArray blocked;
    for (const QString& p : blockedPaths) {
        blocked.append(p);
    }
    obj["blockedPaths"] = blocked;
    obj["networkAccess"] = networkAccess;
    obj["fileSystemAccess"] = fileSystemAccess;
    obj["databaseAccess"] = databaseAccess;
    obj["guiAccess"] = guiAccess;
    obj["systemAccess"] = systemAccess;
    obj["maxMemoryMB"] = static_cast<int>(maxMemoryMB);
    obj["maxCpuPercent"] = maxCpuPercent;
    obj["createdAt"] = createdAt.toString(Qt::ISODate);
    return obj;
}

PluginSandbox PluginSandbox::fromJson(const QJsonObject& obj) {
    PluginSandbox sb;
    sb.pluginId = obj.value("pluginId").toString();
    sb.rootDirectory = obj.value("rootDirectory").toString();
    QJsonArray allowed = obj.value("allowedPaths").toArray();
    for (const QJsonValue& v : allowed) {
        sb.allowedPaths.append(v.toString());
    }
    QJsonArray blocked = obj.value("blockedPaths").toArray();
    for (const QJsonValue& v : blocked) {
        sb.blockedPaths.append(v.toString());
    }
    sb.networkAccess = obj.value("networkAccess").toBool();
    sb.fileSystemAccess = obj.value("fileSystemAccess").toBool();
    sb.databaseAccess = obj.value("databaseAccess").toBool();
    sb.guiAccess = obj.value("guiAccess").toBool();
    sb.systemAccess = obj.value("systemAccess").toBool();
    sb.maxMemoryMB = obj.value("maxMemoryMB").toInt(512);
    sb.maxCpuPercent = obj.value("maxCpuPercent").toInt(50);
    sb.createdAt = QDateTime::fromString(
        obj.value("createdAt").toString(), Qt::ISODate);
    return sb;
}

// ============================
// Private Data
// ============================

struct AlexisEngine::PrivateData {
    // Plugin registry
    QMap<QString, QJsonObject> pluginManifests; // pluginId -> manifest
    QSet<QString> activePlugins;

    // Permissions RBAC
    QMap<QString, QList<PermissionPolicy>> pluginPermissions; // pluginId -> policies

    // Sandboxes
    QMap<QString, PluginSandbox> sandboxes;

    // Hooks: pluginId -> { event -> hook }
    QMap<QString, QMap<LifecycleEvent, QList<LifecycleHook>>> hooks;

    // Message handlers
    QMap<QString, PluginMessageHandler> messageHandlers;

    // Global hooks (any plugin)
    QList<QPair<LifecycleEvent, LifecycleHook>> globalHooks;
};

// ============================
// Constructor / Destructor
// ============================

AlexisEngine::AlexisEngine(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<PrivateData>())
{
}

AlexisEngine::~AlexisEngine() {
}

// ============================
// Ciclo de vida de plugins
// ============================

bool AlexisEngine::registerPlugin(const QString& pluginId,
                                  const QJsonObject& manifest) {
    if (pluginId.isEmpty()) {
        emit engineError("No se puede registrar un plugin con ID vacio");
        return false;
    }

    d->pluginManifests[pluginId] = manifest;
    emit pluginRegistered(pluginId);

    // Trigger onLoad hook
    QJsonObject ctx;
    ctx["manifest"] = manifest;
    ctx["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    triggerHook(pluginId, LifecycleEvent::OnLoad, ctx);

    return true;
}

bool AlexisEngine::unregisterPlugin(const QString& pluginId) {
    if (!d->pluginManifests.contains(pluginId)) {
        return false;
    }

    // Trigger onUnload hook
    QJsonObject ctx;
    ctx["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    triggerHook(pluginId, LifecycleEvent::OnUnload, ctx);

    // Cleanup
    d->activePlugins.remove(pluginId);
    d->pluginManifests.remove(pluginId);
    d->pluginPermissions.remove(pluginId);
    d->hooks.remove(pluginId);
    d->messageHandlers.remove(pluginId);

    if (d->sandboxes.contains(pluginId)) {
        destroySandbox(pluginId);
    }

    emit pluginUnregistered(pluginId);
    return true;
}

bool AlexisEngine::activatePlugin(const QString& pluginId) {
    if (!d->pluginManifests.contains(pluginId)) {
        emit engineError(QString("Plugin '%1' no esta registrado").arg(pluginId));
        return false;
    }

    d->activePlugins.insert(pluginId);

    QJsonObject ctx;
    ctx["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    triggerHook(pluginId, LifecycleEvent::OnEnable, ctx);

    emit pluginActivated(pluginId);
    return true;
}

bool AlexisEngine::deactivatePlugin(const QString& pluginId) {
    if (!d->pluginManifests.contains(pluginId)) {
        return false;
    }

    d->activePlugins.remove(pluginId);

    QJsonObject ctx;
    ctx["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    triggerHook(pluginId, LifecycleEvent::OnDisable, ctx);

    emit pluginDeactivated(pluginId);
    return true;
}

// ============================
// Hooks
// ============================

void AlexisEngine::registerHook(LifecycleEvent event, const QString& pluginId,
                                LifecycleHook hook) {
    d->hooks[pluginId][event].append(hook);
}

void AlexisEngine::unregisterHooks(const QString& pluginId) {
    d->hooks.remove(pluginId);
}

void AlexisEngine::triggerHook(const QString& pluginId, LifecycleEvent event,
                               const QJsonObject& context) {
    // Hooks especificos del plugin
    if (d->hooks.contains(pluginId) && d->hooks[pluginId].contains(event)) {
        for (const LifecycleHook& hook : d->hooks[pluginId][event]) {
            try {
                hook(pluginId, event, context);
            } catch (...) {
                emit engineError(QString("Hook error en plugin '%1' evento '%2'")
                                 .arg(pluginId).arg(lifecycleEventToString(event)));
            }
        }
    }

    // Global hooks
    for (const auto& pair : d->globalHooks) {
        if (pair.first == event) {
            try {
                pair.second(pluginId, event, context);
            } catch (...) {
                emit engineError("Global hook error");
            }
        }
    }

    emit hookTriggered(pluginId, lifecycleEventToString(event));
}

// ============================
// Permisos RBAC
// ============================

bool AlexisEngine::setPermissions(const QString& pluginId,
                                  const QList<PermissionPolicy>& policies) {
    if (!d->pluginManifests.contains(pluginId)) {
        return false;
    }

    d->pluginPermissions[pluginId] = policies;

    // Sincronizar permisos con el sandbox
    if (d->sandboxes.contains(pluginId)) {
        PluginSandbox& sb = d->sandboxes[pluginId];
        for (const PermissionPolicy& policy : policies) {
            switch (policy.permission) {
                case PluginPermission::Network:
                    sb.networkAccess = policy.granted;
                    break;
                case PluginPermission::Filesystem:
                    sb.fileSystemAccess = policy.granted;
                    break;
                case PluginPermission::Database:
                    sb.databaseAccess = policy.granted;
                    break;
                case PluginPermission::GUI:
                    sb.guiAccess = policy.granted;
                    break;
                case PluginPermission::System:
                    sb.systemAccess = policy.granted;
                    break;
                default:
                    break;
            }
        }
    }

    return true;
}

bool AlexisEngine::checkPermission(const QString& pluginId,
                                   PluginPermission permission) const {
    if (!d->pluginPermissions.contains(pluginId)) {
        return false; // Sin permisos = denegado por defecto
    }

    for (const PermissionPolicy& policy : d->pluginPermissions[pluginId]) {
        if (policy.permission == permission) {
            return policy.granted;
        }
    }
    return false;
}

bool AlexisEngine::grantPermission(const QString& pluginId,
                                   PluginPermission permission,
                                   const QString& reason) {
    if (!d->pluginManifests.contains(pluginId)) {
        return false;
    }

    QList<PermissionPolicy>& policies = d->pluginPermissions[pluginId];

    // Buscar si ya existe
    for (PermissionPolicy& policy : policies) {
        if (policy.permission == permission) {
            policy.granted = true;
            policy.reason = reason;
            return true;
        }
    }

    // Crear nueva politica
    PermissionPolicy newPolicy;
    newPolicy.permission = permission;
    newPolicy.granted = true;
    newPolicy.reason = reason;
    policies.append(newPolicy);

    // Sincronizar con sandbox
    if (d->sandboxes.contains(pluginId)) {
        PluginSandbox& sb = d->sandboxes[pluginId];
        switch (permission) {
            case PluginPermission::Network: sb.networkAccess = true; break;
            case PluginPermission::Filesystem: sb.fileSystemAccess = true; break;
            case PluginPermission::Database: sb.databaseAccess = true; break;
            case PluginPermission::GUI: sb.guiAccess = true; break;
            case PluginPermission::System: sb.systemAccess = true; break;
            default: break;
        }
    }

    return true;
}

bool AlexisEngine::revokePermission(const QString& pluginId,
                                    PluginPermission permission) {
    if (!d->pluginPermissions.contains(pluginId)) {
        return false;
    }

    QList<PermissionPolicy>& policies = d->pluginPermissions[pluginId];
    for (PermissionPolicy& policy : policies) {
        if (policy.permission == permission) {
            policy.granted = false;

            // Sincronizar con sandbox
            if (d->sandboxes.contains(pluginId)) {
                PluginSandbox& sb = d->sandboxes[pluginId];
                switch (permission) {
                    case PluginPermission::Network: sb.networkAccess = false; break;
                    case PluginPermission::Filesystem: sb.fileSystemAccess = false; break;
                    case PluginPermission::Database: sb.databaseAccess = false; break;
                    case PluginPermission::GUI: sb.guiAccess = false; break;
                    case PluginPermission::System: sb.systemAccess = false; break;
                    default: break;
                }
            }

            return true;
        }
    }
    return false;
}

QList<PermissionPolicy> AlexisEngine::getPermissions(const QString& pluginId) const {
    return d->pluginPermissions.value(pluginId);
}

// ============================
// Sandboxing
// ============================

bool AlexisEngine::createSandbox(const QString& pluginId,
                                 const QJsonObject& restrictions) {
    if (!d->pluginManifests.contains(pluginId)) {
        return false;
    }

    PluginSandbox sb;
    sb.pluginId = pluginId;
    sb.createdAt = QDateTime::currentDateTime();

    if (restrictions.contains("rootDirectory")) {
        sb.rootDirectory = restrictions.value("rootDirectory").toString();
    }

    if (restrictions.contains("allowedPaths")) {
        QJsonArray arr = restrictions.value("allowedPaths").toArray();
        for (const QJsonValue& v : arr) {
            sb.allowedPaths.append(v.toString());
        }
    }

    if (restrictions.contains("blockedPaths")) {
        QJsonArray arr = restrictions.value("blockedPaths").toArray();
        for (const QJsonValue& v : arr) {
            sb.blockedPaths.append(v.toString());
        }
    }

    if (restrictions.contains("networkAccess")) {
        sb.networkAccess = restrictions.value("networkAccess").toBool();
    }
    if (restrictions.contains("fileSystemAccess")) {
        sb.fileSystemAccess = restrictions.value("fileSystemAccess").toBool();
    }
    if (restrictions.contains("databaseAccess")) {
        sb.databaseAccess = restrictions.value("databaseAccess").toBool();
    }
    if (restrictions.contains("guiAccess")) {
        sb.guiAccess = restrictions.value("guiAccess").toBool();
    }
    if (restrictions.contains("systemAccess")) {
        sb.systemAccess = restrictions.value("systemAccess").toBool();
    }
    if (restrictions.contains("maxMemoryMB")) {
        sb.maxMemoryMB = restrictions.value("maxMemoryMB").toInt();
    }
    if (restrictions.contains("maxCpuPercent")) {
        sb.maxCpuPercent = restrictions.value("maxCpuPercent").toInt();
    }

    // Sincronizar permisos existentes
    if (d->pluginPermissions.contains(pluginId)) {
        for (const PermissionPolicy& policy : d->pluginPermissions[pluginId]) {
            switch (policy.permission) {
                case PluginPermission::Network: sb.networkAccess = policy.granted; break;
                case PluginPermission::Filesystem: sb.fileSystemAccess = policy.granted; break;
                case PluginPermission::Database: sb.databaseAccess = policy.granted; break;
                case PluginPermission::GUI: sb.guiAccess = policy.granted; break;
                case PluginPermission::System: sb.systemAccess = policy.granted; break;
                default: break;
            }
        }
    }

    d->sandboxes[pluginId] = sb;
    return true;
}

bool AlexisEngine::destroySandbox(const QString& pluginId) {
    if (!d->sandboxes.contains(pluginId)) {
        return false;
    }
    d->sandboxes.remove(pluginId);
    return true;
}

bool AlexisEngine::updateSandbox(const QString& pluginId,
                                 const QJsonObject& restrictions) {
    destroySandbox(pluginId);
    return createSandbox(pluginId, restrictions);
}

PluginSandbox AlexisEngine::getSandbox(const QString& pluginId) const {
    return d->sandboxes.value(pluginId);
}

bool AlexisEngine::isSandboxed(const QString& pluginId) const {
    return d->sandboxes.contains(pluginId);
}

bool AlexisEngine::validateSandboxAccess(const QString& pluginId,
                                         const QString& resourcePath,
                                         const QString& accessType) const {
    if (!d->sandboxes.contains(pluginId)) {
        return true; // Sin sandbox = acceso permitido (legacy mode)
    }

    const PluginSandbox& sb = d->sandboxes[pluginId];

    // Verificar paths bloqueados
    for (const QString& blocked : sb.blockedPaths) {
        if (resourcePath.startsWith(blocked)) {
            emit const_cast<AlexisEngine*>(this)->sandboxViolation(
                pluginId,
                QString("Acceso a ruta bloqueada: %1").arg(resourcePath));
            return false;
        }
    }

    // Verificar paths permitidos (si hay lista blanca)
    if (!sb.allowedPaths.isEmpty()) {
        bool allowed = false;
        for (const QString& path : sb.allowedPaths) {
            if (resourcePath.startsWith(path)) {
                allowed = true;
                break;
            }
        }
        if (!allowed) {
            emit const_cast<AlexisEngine*>(this)->sandboxViolation(
                pluginId,
                QString("Acceso denegado a: %1").arg(resourcePath));
            return false;
        }
    }

    // Verificar permisos de tipo de acceso
    if (accessType == "network" && !sb.networkAccess) {
        emit const_cast<AlexisEngine*>(this)->sandboxViolation(
            pluginId, "Acceso a red denegado");
        return false;
    }
    if (accessType == "filesystem" && !sb.fileSystemAccess) {
        emit const_cast<AlexisEngine*>(this)->sandboxViolation(
            pluginId, "Acceso a filesystem denegado");
        return false;
    }
    if (accessType == "database" && !sb.databaseAccess) {
        emit const_cast<AlexisEngine*>(this)->sandboxViolation(
            pluginId, "Acceso a base de datos denegado");
        return false;
    }
    if (accessType == "gui" && !sb.guiAccess) {
        emit const_cast<AlexisEngine*>(this)->sandboxViolation(
            pluginId, "Acceso a GUI denegado");
        return false;
    }
    if (accessType == "system" && !sb.systemAccess) {
        emit const_cast<AlexisEngine*>(this)->sandboxViolation(
            pluginId, "Acceso a sistema denegado");
        return false;
    }

    return true;
}

// ============================
// Comunicacion entre plugins
// ============================

bool AlexisEngine::sendMessage(const QString& fromPluginId,
                               const QString& toPluginId,
                               const QJsonObject& message) {
    // Validar sandbox
    if (d->sandboxes.contains(fromPluginId)) {
        if (!checkPermission(fromPluginId, PluginPermission::Network)) {
            emit permissionDenied(fromPluginId, "network");
            return false;
        }
    }

    if (!d->pluginManifests.contains(fromPluginId)) {
        emit engineError(QString("Plugin origen '%1' no registrado").arg(fromPluginId));
        return false;
    }

    if (!d->pluginManifests.contains(toPluginId)) {
        emit engineError(QString("Plugin destino '%1' no registrado").arg(toPluginId));
        return false;
    }

    // Enviar a handler registrado
    if (d->messageHandlers.contains(toPluginId)) {
        d->messageHandlers[toPluginId](fromPluginId, toPluginId, message);
    }

    emit messageReceived(fromPluginId, toPluginId, message);
    return true;
}

void AlexisEngine::broadcastMessage(const QString& fromPluginId,
                                    const QJsonObject& message,
                                    const QStringList& excludeIds) {
    if (d->sandboxes.contains(fromPluginId)) {
        if (!checkPermission(fromPluginId, PluginPermission::Network)) {
            emit permissionDenied(fromPluginId, "network");
            return;
        }
    }

    for (const QString& pluginId : d->activePlugins) {
        if (pluginId == fromPluginId) continue;
        if (excludeIds.contains(pluginId)) continue;

        if (d->messageHandlers.contains(pluginId)) {
            d->messageHandlers[pluginId](fromPluginId, pluginId, message);
        }
    }

    emit broadcastReceived(fromPluginId, message);
}

void AlexisEngine::registerMessageHandler(const QString& pluginId,
                                          PluginMessageHandler handler) {
    d->messageHandlers[pluginId] = handler;
}

void AlexisEngine::unregisterMessageHandler(const QString& pluginId) {
    d->messageHandlers.remove(pluginId);
}

// ============================
// Consultas
// ============================

QStringList AlexisEngine::registeredPlugins() const {
    return d->pluginManifests.keys();
}

bool AlexisEngine::isRegistered(const QString& pluginId) const {
    return d->pluginManifests.contains(pluginId);
}

bool AlexisEngine::isActive(const QString& pluginId) const {
    return d->activePlugins.contains(pluginId);
}

QJsonObject AlexisEngine::getPluginManifest(const QString& pluginId) const {
    return d->pluginManifests.value(pluginId);
}

QJsonObject AlexisEngine::getEngineStatus() const {
    QJsonObject status;
    status["registeredPlugins"] = QJsonArray::fromStringList(d->pluginManifests.keys());

    QJsonArray active;
    for (const QString& id : d->activePlugins) {
        active.append(id);
    }
    status["activePlugins"] = active;
    status["sandboxCount"] = static_cast<int>(d->sandboxes.size());

    QJsonObject perms;
    for (auto it = d->pluginPermissions.begin();
         it != d->pluginPermissions.end(); ++it) {
        QJsonArray arr;
        for (const PermissionPolicy& policy : it.value()) {
            arr.append(policy.toJson());
        }
        perms[it.key()] = arr;
    }
    status["permissions"] = perms;

    QJsonObject sbs;
    for (auto it = d->sandboxes.begin(); it != d->sandboxes.end(); ++it) {
        sbs[it.key()] = it.value().toJson();
    }
    status["sandboxes"] = sbs;

    return status;
}

void AlexisEngine::clear() {
    // Detener todos los plugins
    for (const QString& id : d->activePlugins) {
        QJsonObject ctx;
        ctx["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        triggerHook(id, LifecycleEvent::OnUnload, ctx);
        emit pluginUnregistered(id);
    }

    d->pluginManifests.clear();
    d->activePlugins.clear();
    d->pluginPermissions.clear();
    d->sandboxes.clear();
    d->hooks.clear();
    d->messageHandlers.clear();
    d->globalHooks.clear();
}

} // namespace ide
} // namespace powsys365
