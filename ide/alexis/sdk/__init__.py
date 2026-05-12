#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Alexis SDK - Kit de Desarrollo de Software para Plugins POWSYS365

El SDK de Alexis proporciona las herramientas y abstracciones necesarias
para desarrollar plugins que se integren con el ecosistema POWSYS365.

Modulos disponibles:
    api         - API de acceso al sistema electrico y datos
    hooks       - Hooks del ciclo de vida de plugins
    permissions - Sistema de permisos RBAC

Clases principales:
    AlexisPlugin    - Clase base para todos los plugins
    PluginContext   - Contexto de ejecucion del plugin
    PluginAPI       - Interfaz de acceso al sistema

Ejemplo de uso:
    from alexis.sdk import AlexisPlugin, PluginContext
    from alexis.sdk.permissions import Permission, PermissionSet

    class MiPlugin(AlexisPlugin):
        def __init__(self):
            super().__init__()
            self.name = "Mi Plugin"
            self.version = "1.0.0"

        def on_execute(self, ctx: PluginContext, params: dict):
            buses = ctx.api.get_buses()
            return {"buses": len(buses)}
"""

__version__ = "1.0.0"

from alexis.sdk.api import PluginAPI, PluginContext, AlexisPlugin
from alexis.sdk.hooks import (
    on_plugin_load,
    on_plugin_unload,
    on_plugin_execute,
    on_plugin_enable,
    on_plugin_disable,
    register_hook,
    HookRegistry,
)
from alexis.sdk.permissions import (
    Permission,
    PermissionSet,
    PermissionDenied,
    check_permission,
    require_permission,
    get_plugin_permissions,
)

__all__ = [
    # API
    "PluginAPI",
    "PluginContext",
    "AlexisPlugin",
    # Hooks
    "on_plugin_load",
    "on_plugin_unload",
    "on_plugin_execute",
    "on_plugin_enable",
    "on_plugin_disable",
    "register_hook",
    "HookRegistry",
    # Permissions
    "Permission",
    "PermissionSet",
    "PermissionDenied",
    "check_permission",
    "require_permission",
    "get_plugin_permissions",
]
