#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
alexis/sdk/hooks.py - Sistema de Hooks del Ciclo de Vida de Plugins

Proporciona un mecanismo de hooks para el ciclo de vida de los plugins,
permitiendo ejecutar codigo en momentos especificos: carga, descarga,
ejecucion, habilitacion y deshabilitacion.

El sistema de hooks sigue el patron Observer, permitiendo registrar
multiples callbacks para cada evento del ciclo de vida.

Ejemplo de uso:
    from alexis.sdk.hooks import on_plugin_load, on_plugin_execute, HookRegistry

    @on_plugin_load
    def mi_init(ctx):
        ctx.logger.info("Plugin inicializado")

    @on_plugin_execute
    def mi_proceso(ctx, params):
        return {"status": "ok"}
"""

import functools
import logging
from typing import Dict, Any, Callable, List, Optional
from enum import Enum, auto

logger = logging.getLogger("alexis.hooks")


# ============================
# Eventos del Ciclo de Vida
# ============================

class LifecycleEvent(Enum):
    """Eventos del ciclo de vida de un plugin."""
    ON_LOAD = auto()
    ON_UNLOAD = auto()
    ON_EXECUTE = auto()
    ON_ENABLE = auto()
    ON_DISABLE = auto()
    ON_ERROR = auto()

    def __str__(self) -> str:
        return self.name.lower()


# ============================
// HookRegistry
// ============================

class HookRegistry:
    """
    Registro central de hooks para el sistema de plugins.

    Mantiene un registro de todos los callbacks asociados a cada evento
del ciclo de vida, permitiendo registrarlos, eliminarlos y ejecutarlos.

    Es un singleton para mantener un registro global de hooks.
    """

    _instance = None

    def __new__(cls):
        if cls._instance is None:
            cls._instance = super().__new__(cls)
            cls._instance._hooks: Dict[LifecycleEvent, List[Callable]] = {
                event: [] for event in LifecycleEvent
            }
            cls._instance._plugin_hooks: Dict[str, Dict[LifecycleEvent, List[Callable]]] = {}
        return cls._instance

    def register(self, event: LifecycleEvent, callback: Callable,
                 plugin_id: Optional[str] = None) -> None:
        """
        Registra un callback para un evento del ciclo de vida.

        Args:
            event: Evento al que suscribirse
            callback: Funcion a ejecutar
            plugin_id: ID del plugin (opcional, para aislamiento)
        """
        if plugin_id:
            if plugin_id not in self._plugin_hooks:
                self._plugin_hooks[plugin_id] = {
                    event: [] for event in LifecycleEvent
                }
            if callback not in self._plugin_hooks[plugin_id][event]:
                self._plugin_hooks[plugin_id][event].append(callback)
                logger.debug(f"Hook registrado: {event} para plugin '{plugin_id}'")
        else:
            if callback not in self._hooks[event]:
                self._hooks[event].append(callback)
                logger.debug(f"Hook global registrado: {event}")

    def unregister(self, event: LifecycleEvent, callback: Callable,
                   plugin_id: Optional[str] = None) -> None:
        """
        Elimina un callback de un evento.

        Args:
            event: Evento del que desuscribirse
            callback: Funcion a eliminar
            plugin_id: ID del plugin (si aplica)
        """
        if plugin_id and plugin_id in self._plugin_hooks:
            hooks_list = self._plugin_hooks[plugin_id][event]
            if callback in hooks_list:
                hooks_list.remove(callback)
        else:
            if callback in self._hooks[event]:
                self._hooks[event].remove(callback)

    def unregister_all(self, plugin_id: str) -> None:
        """
        Elimina todos los hooks de un plugin.

        Args:
            plugin_id: ID del plugin
        """
        if plugin_id in self._plugin_hooks:
            del self._plugin_hooks[plugin_id]
            logger.debug(f"Todos los hooks eliminados para plugin '{plugin_id}'")

    def execute(self, event: LifecycleEvent, *args, **kwargs) -> List[Any]:
        """
        Ejecuta todos los callbacks registrados para un evento.

        Args:
            event: Evento a ejecutar
            *args, **kwargs: Argumentos pasados a los callbacks

        Returns:
            Lista de resultados de cada callback
        """
        results = []

        # Ejecutar hooks globales
        for callback in self._hooks.get(event, []):
            try:
                result = callback(*args, **kwargs)
                results.append(result)
            except Exception as e:
                logger.error(f"Error en hook global {event}: {e}")
                self._execute_error_hooks(e, event, *args, **kwargs)

        # Ejecutar hooks especificos del plugin
        plugin_id = kwargs.get("plugin_id") or (args[0].plugin_id if args else None)
        if plugin_id and plugin_id in self._plugin_hooks:
            for callback in self._plugin_hooks[plugin_id].get(event, []):
                try:
                    result = callback(*args, **kwargs)
                    results.append(result)
                except Exception as e:
                    logger.error(f"Error en hook de plugin '{plugin_id}' {event}: {e}")
                    self._execute_error_hooks(e, event, *args, **kwargs)

        return results

    def _execute_error_hooks(self, error: Exception, original_event: LifecycleEvent,
                             *args, **kwargs) -> None:
        """Ejecuta los hooks de error registrados."""
        for callback in self._hooks.get(LifecycleEvent.ON_ERROR, []):
            try:
                callback(error, original_event, *args, **kwargs)
            except Exception as e:
                logger.error(f"Error en hook de error: {e}")

    def list_hooks(self, plugin_id: Optional[str] = None) -> Dict[str, List[str]]:
        """
        Lista los hooks registrados.

        Args:
            plugin_id: Filtrar por plugin

        Returns:
            Diccionario con los hooks organizados por evento
        """
        result = {}

        if plugin_id and plugin_id in self._plugin_hooks:
            for event, callbacks in self._plugin_hooks[plugin_id].items():
                result[str(event)] = [c.__name__ for c in callbacks]
        else:
            for event, callbacks in self._hooks.items():
                if callbacks:
                    result[str(event)] = [c.__name__ for c in callbacks]

        return result

    def clear(self) -> None:
        """Elimina todos los hooks registrados."""
        for event in LifecycleEvent:
            self._hooks[event].clear()
        self._plugin_hooks.clear()
        logger.info("Todos los hooks han sido eliminados")


# ============================
# Decoradores de conveniencia
# ============================

def register_hook(event: LifecycleEvent, plugin_id: Optional[str] = None):
    """
    Decorador para registrar una funcion como hook.

    Args:
        event: Evento del ciclo de vida
        plugin_id: ID del plugin (opcional)

    Ejemplo:
        @register_hook(LifecycleEvent.ON_LOAD)
        def mi_hook(ctx):
            ctx.logger.info("Cargado!")
    """
    def decorator(func: Callable) -> Callable:
        registry = HookRegistry()
        registry.register(event, func, plugin_id)

        @functools.wraps(func)
        def wrapper(*args, **kwargs):
            return func(*args, **kwargs)
        return wrapper
    return decorator


def on_plugin_load(func: Callable) -> Callable:
    """
    Decorador para registrar un hook de carga.

    Ejemplo:
        @on_plugin_load
        def mi_init(ctx):
            ctx.logger.info("Plugin cargado")
    """
    return register_hook(LifecycleEvent.ON_LOAD)(func)


def on_plugin_unload(func: Callable) -> Callable:
    """
    Decorador para registrar un hook de descarga.

    Ejemplo:
        @on_plugin_unload
        def mi_cleanup(ctx):
            ctx.logger.info("Plugin descargado")
    """
    return register_hook(LifecycleEvent.ON_UNLOAD)(func)


def on_plugin_execute(func: Callable) -> Callable:
    """
    Decorador para registrar un hook de ejecucion.

    Ejemplo:
        @on_plugin_execute
        def mi_run(ctx, params):
            return ctx.api.run_power_flow()
    """
    return register_hook(LifecycleEvent.ON_EXECUTE)(func)


def on_plugin_enable(func: Callable) -> Callable:
    """Decorador para registrar un hook de habilitacion."""
    return register_hook(LifecycleEvent.ON_ENABLE)(func)


def on_plugin_disable(func: Callable) -> Callable:
    """Decorador para registrar un hook de deshabilitacion."""
    return register_hook(LifecycleEvent.ON_DISABLE)(func)


def on_plugin_error(func: Callable) -> Callable:
    """
    Decorador para registrar un hook de manejo de errores.

    Ejemplo:
        @on_plugin_error
        def mi_error_handler(error, event, ctx):
            ctx.logger.error(f"Error en {event}: {error}")
    """
    return register_hook(LifecycleEvent.ON_ERROR)(func)


# ============================
# Funciones de utilidad
# ============================

def trigger_hook(event: LifecycleEvent, *args, **kwargs) -> List[Any]:
    """
    Ejecuta manualmente todos los hooks de un evento.

    Args:
        event: Evento a ejecutar
        *args, **kwargs: Argumentos para los callbacks

    Returns:
        Lista de resultados
    """
    registry = HookRegistry()
    return registry.execute(event, *args, **kwargs)


def clear_all_hooks() -> None:
    """Elimina todos los hooks registrados."""
    registry = HookRegistry()
    registry.clear()


# Instancia global del registro
_global_registry = HookRegistry()
