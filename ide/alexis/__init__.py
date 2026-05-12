#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Alexis - Sistema de Plugins para POWSYS365

Alexis es el motor de plugins extensible para el entorno de desarrollo
integrado (IDE) de POWSYS365. Proporciona un framework completo para:

- Carga y ejecucion de plugins en multiples lenguajes (Python, C++, JavaScript)
- Gestion del ciclo de vida de plugins con hooks
- Sistema de permisos RBAC (Role-Based Access Control)
- Sandboxing para aislamiento seguro de plugins
- Comunicacion inter-plugin via bus de mensajes
- SDK completo para desarrolladores de plugins

Uso basico:
    from alexis.sdk import AlexisPlugin, PluginContext
    from alexis.sdk.permissions import Permission

    class MiPlugin(AlexisPlugin):
        def on_load(self, ctx: PluginContext):
            self.logger.info("Plugin cargado")

        def on_execute(self, ctx: PluginContext, params: dict):
            return {"status": "ok", "result": 42}

Autor: POWSYS365 Team
Version: 1.0.0
Licencia: Proprietary
"""

__version__ = "1.0.0"
__author__ = "POWSYS365 Team"
__license__ = "Proprietary"

import os
import sys
import json
import logging
from pathlib import Path
from typing import Dict, List, Optional, Any

# Directorio base de Alexis
ALEXIS_HOME = Path.home() / ".powsy365" / "alexis"
PLUGINS_DIR = ALEXIS_HOME / "plugins"
LOGS_DIR = ALEXIS_HOME / "logs"
CONFIG_DIR = ALEXIS_HOME / "config"

# Configuracion de logging global
def _setup_logging() -> logging.Logger:
    """Configura el sistema de logging de Alexis."""
    LOGS_DIR.mkdir(parents=True, exist_ok=True)

    logger = logging.getLogger("alexis")
    logger.setLevel(logging.DEBUG)

    if not logger.handlers:
        # Handler de archivo
        log_file = LOGS_DIR / "alexis.log"
        file_handler = logging.FileHandler(log_file, encoding="utf-8")
        file_handler.setLevel(logging.DEBUG)
        file_formatter = logging.Formatter(
            "%(asctime)s [%(levelname)s] %(name)s - %(message)s",
            datefmt="%Y-%m-%d %H:%M:%S"
        )
        file_handler.setFormatter(file_formatter)
        logger.addHandler(file_handler)

        # Handler de consola
        console_handler = logging.StreamHandler(sys.stdout)
        console_handler.setLevel(logging.INFO)
        console_formatter = logging.Formatter(
            "[Alexis] %(levelname)s: %(message)s"
        )
        console_handler.setFormatter(console_formatter)
        logger.addHandler(console_handler)

    return logger

# Logger global
logger = _setup_logging()


def ensure_directories() -> None:
    """Crea los directorios necesarios para Alexis si no existen."""
    for directory in [ALEXIS_HOME, PLUGINS_DIR, LOGS_DIR, CONFIG_DIR]:
        directory.mkdir(parents=True, exist_ok=True)
        logger.debug(f"Directorio verificado: {directory}")


def get_engine_version() -> str:
    """Retorna la version del motor Alexis."""
    return __version__


def list_plugins() -> List[Dict[str, Any]]:
    """
    Lista todos los plugins disponibles en el directorio de plugins.

    Returns:
        Lista de diccionarios con la informacion de cada plugin.
    """
    plugins = []

    if not PLUGINS_DIR.exists():
        return plugins

    for plugin_dir in PLUGINS_DIR.iterdir():
        if not plugin_dir.is_dir():
            continue

        manifest_path = plugin_dir / "manifest.json"
        if not manifest_path.exists():
            continue

        try:
            with open(manifest_path, "r", encoding="utf-8") as f:
                manifest = json.load(f)
            manifest["_directory"] = str(plugin_dir)
            plugins.append(manifest)
        except (json.JSONDecodeError, IOError) as e:
            logger.warning(f"Error leyendo manifest de {plugin_dir}: {e}")

    return plugins


def get_plugin_info(plugin_id: str) -> Optional[Dict[str, Any]]:
    """
    Obtiene la informacion de un plugin especifico.

    Args:
        plugin_id: Identificador unico del plugin.

    Returns:
        Diccionario con la informacion del plugin, o None si no existe.
    """
    for plugin in list_plugins():
        if plugin.get("id") == plugin_id:
            return plugin
    return None


# Inicializar directorios al importar
ensure_directories()

logger.info(f"Alexis Plugin System v{__version__} inicializado")
logger.debug(f"Directorio de plugins: {PLUGINS_DIR}")
logger.debug(f"Directorio de logs: {LOGS_DIR}")
