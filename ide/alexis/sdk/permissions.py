#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
alexis/sdk/permissions.py - Sistema de Permisos RBAC para Plugins POWSYS365

Implementa un sistema de control de acceso basado en roles (RBAC) para
los plugins del sistema Alexis. Cada plugin solicita permisos en su
manifest y el usuario/administrador decide cuales otorgar.

Permisos disponibles:
    - network: Acceso a la red (peticiones HTTP, sockets)
    - filesystem: Lectura/escritura de archivos
    - database: Acceso a bases de datos
    - gui: Interaccion con la interfaz grafica
    - system: Ejecucion de comandos del sistema
    - electric_data: Acceso a datos del sistema electrico
    - logging: Logging y notificaciones
    - configuration: Modificacion de configuracion del sistema

Ejemplo de uso:
    from alexis.sdk.permissions import Permission, PermissionSet, require_permission

    @require_permission(Permission.NETWORK)
    def fetch_data(ctx, url):
        import urllib.request
        return urllib.request.urlopen(url).read()
"""

import functools
import json
import logging
from typing import Dict, List, Optional, Set, Any, Callable
from enum import Enum, auto
from dataclasses import dataclass, field

logger = logging.getLogger("alexis.permissions")


# ============================
# Permisos
# ============================

class Permission(Enum):
    """Permisos disponibles en el sistema Alexis."""
    NETWORK = auto()
    FILESYSTEM = auto()
    DATABASE = auto()
    GUI = auto()
    SYSTEM = auto()
    ELECTRIC_DATA = auto()
    LOGGING = auto()
    CONFIGURATION = auto()

    def __str__(self) -> str:
        return self.name.lower()

    @classmethod
    def from_string(cls, name: str) -> "Permission":
        """Crea un Permission desde su representacion en string."""
        mapping = {
            "network": cls.NETWORK,
            "filesystem": cls.FILESYSTEM,
            "database": cls.DATABASE,
            "gui": cls.GUI,
            "system": cls.SYSTEM,
            "electric_data": cls.ELECTRIC_DATA,
            "logging": cls.LOGGING,
            "configuration": cls.CONFIGURATION,
        }
        return mapping.get(name.lower(), cls.NETWORK)


# ============================
# PermissionDenied Exception
# ============================

class PermissionDenied(Exception):
    """
    Excepcion lanzada cuando un plugin intenta realizar una accion
    sin el permiso necesario.
    """

    def __init__(self, permission: Permission, plugin_id: str = "",
                 resource: str = "", message: str = ""):
        self.permission = permission
        self.plugin_id = plugin_id
        self.resource = resource
        self.message = message or f"Permission denied: {permission}"
        super().__init__(self.message)

    def to_dict(self) -> Dict[str, str]:
        return {
            "error": "permission_denied",
            "permission": str(self.permission),
            "plugin_id": self.plugin_id,
            "resource": self.resource,
            "message": self.message,
        }

    def __str__(self) -> str:
        return f"[PermissionDenied] {self.plugin_id}: {self.message}"


# ============================
# PermissionPolicy
# ============================

@dataclass
class PermissionPolicy:
    """
    Politica de permiso individual.

    Define si un permiso esta otorgado, con restricciones y justificacion.
    """
    permission: Permission
    granted: bool = False
    reason: str = ""
    restrictions: List[str] = field(default_factory=list)

    def to_dict(self) -> Dict[str, Any]:
        return {
            "permission": str(self.permission),
            "granted": self.granted,
            "reason": self.reason,
            "restrictions": self.restrictions,
        }

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "PermissionPolicy":
        return cls(
            permission=Permission.from_string(data.get("permission", "network")),
            granted=data.get("granted", False),
            reason=data.get("reason", ""),
            restrictions=data.get("restrictions", []),
        )


# ============================
# PermissionSet
# ============================

class PermissionSet:
    """
    Conjunto de permisos asociados a un plugin.

    Mantiene un registro de todas las politicas de permisos y proporciona
    metodos para verificar, otorgar y revocar permisos.
    """

    def __init__(self, plugin_id: str):
        self.plugin_id = plugin_id
        self._policies: Dict[Permission, PermissionPolicy] = {}
        self._granted: Set[Permission] = set()

    def add_policy(self, policy: PermissionPolicy) -> None:
        """
        Agrega una politica de permiso.

        Args:
            policy: Politica a agregar
        """
        self._policies[policy.permission] = policy
        if policy.granted:
            self._granted.add(policy.permission)
        else:
            self._granted.discard(policy.permission)

    def grant(self, permission: Permission, reason: str = "") -> None:
        """
        Otorga un permiso.

        Args:
            permission: Permiso a otorgar
            reason: Justificacion del otorgamiento
        """
        if permission in self._policies:
            self._policies[permission].granted = True
            self._policies[permission].reason = reason
        else:
            self._policies[permission] = PermissionPolicy(
                permission=permission, granted=True, reason=reason
            )
        self._granted.add(permission)
        logger.info(f"Permiso '{permission}' otorgado a '{self.plugin_id}'"
                    + (f": {reason}" if reason else ""))

    def revoke(self, permission: Permission) -> None:
        """
        Revoca un permiso.

        Args:
            permission: Permiso a revocar
        """
        if permission in self._policies:
            self._policies[permission].granted = False
        self._granted.discard(permission)
        logger.info(f"Permiso '{permission}' revocado a '{self.plugin_id}'")

    def has(self, permission: Permission) -> bool:
        """
        Verifica si un permiso esta otorgado.

        Args:
            permission: Permiso a verificar

        Returns:
            True si el permiso esta otorgado
        """
        return permission in self._granted

    def check(self, permission: Permission, resource: str = "") -> None:
        """
        Verifica un permiso y lanza PermissionDenied si no esta otorgado.

        Args:
            permission: Permiso requerido
            resource: Recurso al que se intenta acceder

        Raises:
            PermissionDenied: Si el permiso no esta otorgado
        """
        if not self.has(permission):
            raise PermissionDenied(
                permission=permission,
                plugin_id=self.plugin_id,
                resource=resource,
                message=f"El plugin '{self.plugin_id}' requiere el permiso '{permission}'"
            )

    def require_all(self, permissions: List[Permission]) -> None:
        """
        Verifica que todos los permisos esten otorgados.

        Args:
            permissions: Lista de permisos requeridos

        Raises:
            PermissionDenied: Si algun permiso no esta otorgado
        """
        for perm in permissions:
            self.check(perm)

    def require_any(self, permissions: List[Permission]) -> bool:
        """
        Verifica que al menos un permiso este otorgado.

        Args:
            permissions: Lista de permisos

        Returns:
            True si al menos un permiso esta otorgado
        """
        return any(self.has(p) for p in permissions)

    def get_granted(self) -> List[Permission]:
        """Retorna la lista de permisos otorgados."""
        return sorted(self._granted, key=lambda p: p.value)

    def get_all_policies(self) -> List[PermissionPolicy]:
        """Retorna todas las politicas."""
        return list(self._policies.values())

    def to_dict(self) -> Dict[str, Any]:
        """Serializa el PermissionSet a diccionario."""
        return {
            "plugin_id": self.plugin_id,
            "granted": [str(p) for p in self._granted],
            "policies": [p.to_dict() for p in self._policies.values()],
        }

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "PermissionSet":
        """Deserializa un PermissionSet desde diccionario."""
        ps = cls(data.get("plugin_id", "unknown"))
        for policy_data in data.get("policies", []):
            ps.add_policy(PermissionPolicy.from_dict(policy_data))
        return ps

    @classmethod
    def from_string_list(cls, plugin_id: str, permissions: List[str]) -> "PermissionSet":
        """
        Crea un PermissionSet desde una lista de strings de permisos.

        Args:
            plugin_id: ID del plugin
            permissions: Lista de nombres de permisos

        Returns:
            PermissionSet con los permisos otorgados
        """
        ps = cls(plugin_id)
        for perm_str in permissions:
            try:
                perm = Permission.from_string(perm_str)
                ps.grant(perm, f"Solicitado en manifest")
            except (ValueError, KeyError):
                logger.warning(f"Permiso desconocido ignorado: {perm_str}")
        return ps

    def __repr__(self) -> str:
        granted = [str(p) for p in self._granted]
        return f"PermissionSet({self.plugin_id}: {', '.join(granted)})"


# ============================
# Registro global de permisos
# ============================

class PermissionRegistry:
    """
    Registro global de permisos para todos los plugins.

    Mantiene un PermissionSet para cada plugin registrado.
    """

    _instance = None

    def __new__(cls):
        if cls._instance is None:
            cls._instance = super().__new__(cls)
            cls._instance._permissions: Dict[str, PermissionSet] = {}
        return cls._instance

    def register_plugin(self, plugin_id: str, permissions: List[str] = None) -> PermissionSet:
        """
        Registra un plugin con sus permisos.

        Args:
            plugin_id: ID del plugin
            permissions: Lista de permisos solicitados

        Returns:
            PermissionSet del plugin
        """
        if plugin_id not in self._permissions:
            if permissions:
                self._permissions[plugin_id] = PermissionSet.from_string_list(
                    plugin_id, permissions
                )
            else:
                self._permissions[plugin_id] = PermissionSet(plugin_id)

        return self._permissions[plugin_id]

    def get_permissions(self, plugin_id: str) -> Optional[PermissionSet]:
        """
        Obtiene el PermissionSet de un plugin.

        Args:
            plugin_id: ID del plugin

        Returns:
            PermissionSet o None si no esta registrado
        """
        return self._permissions.get(plugin_id)

    def check_permission(self, plugin_id: str, permission: Permission,
                         resource: str = "") -> bool:
        """
        Verifica si un plugin tiene un permiso.

        Args:
            plugin_id: ID del plugin
            permission: Permiso a verificar
            resource: Recurso al que se accede

        Returns:
            True si el permiso esta otorgado

        Raises:
            PermissionDenied: Si el plugin no tiene el permiso
        """
        ps = self._permissions.get(plugin_id)
        if ps is None:
            raise PermissionDenied(
                permission=permission,
                plugin_id=plugin_id,
                resource=resource,
                message=f"Plugin '{plugin_id}' no registrado en el sistema de permisos"
            )
        ps.check(permission, resource)
        return True

    def grant_permission(self, plugin_id: str, permission: Permission,
                         reason: str = "") -> None:
        """Otorga un permiso a un plugin."""
        if plugin_id not in self._permissions:
            self._permissions[plugin_id] = PermissionSet(plugin_id)
        self._permissions[plugin_id].grant(permission, reason)

    def revoke_permission(self, plugin_id: str, permission: Permission) -> None:
        """Revoca un permiso de un plugin."""
        if plugin_id in self._permissions:
            self._permissions[plugin_id].revoke(permission)

    def unregister_plugin(self, plugin_id: str) -> None:
        """Elimina un plugin del registro."""
        if plugin_id in self._permissions:
            del self._permissions[plugin_id]

    def list_plugins(self) -> List[str]:
        """Lista todos los plugins registrados."""
        return list(self._permissions.keys())

    def to_dict(self) -> Dict[str, Any]:
        """Serializa el registro completo."""
        return {
            plugin_id: ps.to_dict()
            for plugin_id, ps in self._permissions.items()
        }


# ============================
# Decoradores
# ============================

def require_permission(permission: Permission):
    """
    Decorador que requiere un permiso para ejecutar una funcion.

    Args:
        permission: Permiso requerido

    Ejemplo:
        @require_permission(Permission.NETWORK)
        def download_data(ctx, url):
            return requests.get(url).text
    """
    def decorator(func: Callable) -> Callable:
        @functools.wraps(func)
        def wrapper(*args, **kwargs):
            # Obtener plugin_id del contexto
            ctx = None
            for arg in args:
                if hasattr(arg, "plugin_id"):
                    ctx = arg
                    break

            if ctx is None:
                logger.warning(f"No se encontro contexto para verificar permiso {permission}")
                return func(*args, **kwargs)

            registry = PermissionRegistry()
            registry.check_permission(ctx.plugin_id, permission)

            return func(*args, **kwargs)
        return wrapper
    return decorator


def require_permissions(*permissions: Permission):
    """
    Decorador que requiere multiples permisos.

    Args:
        *permissions: Permisos requeridos
    """
    def decorator(func: Callable) -> Callable:
        @functools.wraps(func)
        def wrapper(*args, **kwargs):
            ctx = None
            for arg in args:
                if hasattr(arg, "plugin_id"):
                    ctx = arg
                    break

            if ctx is None:
                return func(*args, **kwargs)

            registry = PermissionRegistry()
            for perm in permissions:
                registry.check_permission(ctx.plugin_id, perm)

            return func(*args, **kwargs)
        return wrapper
    return decorator


# ============================
# Funciones de conveniencia
# ============================

def check_permission(plugin_id: str, permission_name: str) -> bool:
    """
    Verifica si un plugin tiene un permiso (por nombre).

    Args:
        plugin_id: ID del plugin
        permission_name: Nombre del permiso

    Returns:
        True si el permiso esta otorgado
    """
    try:
        perm = Permission.from_string(permission_name)
        registry = PermissionRegistry()
        return registry.check_permission(plugin_id, perm)
    except PermissionDenied:
        return False
    except Exception as e:
        logger.error(f"Error verificando permiso: {e}")
        return False


def get_plugin_permissions(plugin_id: str) -> Optional[PermissionSet]:
    """
    Obtiene todos los permisos de un plugin.

    Args:
        plugin_id: ID del plugin

    Returns:
        PermissionSet o None
    """
    registry = PermissionRegistry()
    return registry.get_permissions(plugin_id)


def grant_permission_to_plugin(plugin_id: str, permission_name: str,
                                reason: str = "") -> None:
    """Otorga un permiso a un plugin por nombre."""
    perm = Permission.from_string(permission_name)
    registry = PermissionRegistry()
    registry.grant_permission(plugin_id, perm, reason)


def revoke_permission_from_plugin(plugin_id: str, permission_name: str) -> None:
    """Revoca un permiso de un plugin por nombre."""
    perm = Permission.from_string(permission_name)
    registry = PermissionRegistry()
    registry.revoke_permission(plugin_id, perm)


# Instancia global
_global_permission_registry = PermissionRegistry()
