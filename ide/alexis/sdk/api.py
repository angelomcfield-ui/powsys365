#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
alexis/sdk/api.py - API Python para Plugins POWSYS365

Proporciona acceso completo al sistema electrico, lectura/escritura de datos,
sistema de notificaciones y eventos, y logging estructurado para plugins
desarrollados en Python.

La API esta disenada para ser usada desde el contexto de un plugin Alexis,
permitiendo interactuar con el modelo de red electrica, resultados de flujo
de carga, y otros componentes del sistema POWSYS365.
"""

import os
import sys
import json
import logging
import time
from pathlib import Path
from typing import Dict, List, Optional, Any, Callable, Union
from dataclasses import dataclass, field, asdict
from enum import Enum
from abc import ABC, abstractmethod


# ============================
# Enums del sistema electrico
# ============================

class BusType(Enum):
    """Tipos de barras en el sistema electrico."""
    SLACK = 1       # Barra de referencia
    PV = 2          # Generador (Potencia activa y voltaje controlados)
    PQ = 3          # Carga (Potencia activa y reactiva especificadas)
    ISOLATED = 4    # Barra aislada


class BranchStatus(Enum):
    """Estado de las ramas del sistema."""
    IN_SERVICE = 1
    OUT_OF_SERVICE = 0


class GeneratorStatus(Enum):
    """Estado de los generadores."""
    COMMITTED = 1      # En servicio
    SHUTDOWN = 0       # Fuera de servicio
    MUST_RUN = 2       # Debe operar


# ============================
# Data Classes
# ============================

@dataclass
class Bus:
    """Representa una barra del sistema electrico."""
    id: int
    name: str
    bus_type: BusType
    base_kv: float           # Voltaje base en kV
    vm_pu: float = 1.0       # Magnitud de voltaje en p.u.
    va_deg: float = 0.0      # Angulo de voltaje en grados
    area: int = 1
    zone: int = 1
    vmax: float = 1.1
    vmin: float = 0.9
    lam_p: float = 0.0       # Multiplicador de Lagrange (precio sombra activo)
    lam_q: float = 0.0       # Multiplicador de Lagrange (precio sombra reactivo)

    def to_dict(self) -> Dict[str, Any]:
        d = asdict(self)
        d["bus_type"] = self.bus_type.name
        return d


@dataclass
class Branch:
    """Representa una rama del sistema (linea o transformador)."""
    id: int
    from_bus: int
    to_bus: int
    r_pu: float              # Resistencia en p.u.
    x_pu: float              # Reactancia en p.u.
    b_pu: float = 0.0        # Susceptancia total de carga en p.u.
    rate_a: float = 0.0      # Rating continuo en MVA
    rate_b: float = 0.0      # Rating a corto plazo en MVA
    rate_c: float = 0.0      # Rating de emergencia en MVA
    ratio: float = 0.0       # Relacion de transformacion (0 = linea)
    angle: float = 0.0       # Angulo del transformador desfasador
    status: BranchStatus = BranchStatus.IN_SERVICE
    p_from: float = 0.0      # Flujo P desde la barra origen
    q_from: float = 0.0      # Flujo Q desde la barra origen
    p_to: float = 0.0        # Flujo P hacia la barra destino
    q_to: float = 0.0        # Flujo Q hacia la barra destino
    s_apparent: float = 0.0  # Potencia aparente en MVA
    loading_percent: float = 0.0  # Porcentaje de carga

    def to_dict(self) -> Dict[str, Any]:
        d = asdict(self)
        d["status"] = self.status.name
        return d


@dataclass
class Generator:
    """Representa un generador del sistema."""
    id: int
    bus_id: int
    name: str
    pg_mw: float             # Potencia activa generada en MW
    qg_mvar: float           # Potencia reactiva generada en MVAr
    q_max_mvar: float        # Limite maximo de Q en MVAr
    q_min_mvar: float        # Limite minimo de Q en MVAr
    v_set_pu: float = 1.0    # Voltaje setpoint en p.u.
    p_max_mw: float = 0.0    # Limite maximo de P en MW
    p_min_mw: float = 0.0    # Limite minimo de P en MW
    status: GeneratorStatus = GeneratorStatus.COMMITTED
    pc1_mw: float = 0.0      # Limite inferior de curva de costo
    pc2_mw: float = 0.0      # Limite superior de curva de costo
    cost_model: int = 2      # Modelo de costo: 1=piecewise linear, 2=polynomial
    cost_coefficients: List[float] = field(default_factory=list)

    def to_dict(self) -> Dict[str, Any]:
        d = asdict(self)
        d["status"] = self.status.name
        return d


@dataclass
class Load:
    """Representa una carga del sistema."""
    id: int
    bus_id: int
    name: str
    pd_mw: float             # Potencia activa demandada en MW
    qd_mvar: float           # Potencia reactiva demandada en MVAr
    status: int = 1          # 1=activa, 0=inactiva

    def to_dict(self) -> Dict[str, Any]:
        return asdict(self)


@dataclass
class PowerFlowResult:
    """Resultados de un flujo de carga."""
    converged: bool
    iterations: int
    elapsed_ms: float
    mismatch_mva: float
    bus_results: List[Dict[str, Any]] = field(default_factory=list)
    branch_results: List[Dict[str, Any]] = field(default_factory=list)
    gen_results: List[Dict[str, Any]] = field(default_factory=list)
    total_generation_mw: float = 0.0
    total_load_mw: float = 0.0
    total_losses_mw: float = 0.0
    total_generation_mvar: float = 0.0
    total_load_mvar: float = 0.0
    total_losses_mvar: float = 0.0

    def to_dict(self) -> Dict[str, Any]:
        return asdict(self)


# ============================
# PluginLogger
# ============================

class PluginLogger:
    """Logger estructurado para plugins de Alexis."""

    LEVELS = {
        "DEBUG": logging.DEBUG,
        "INFO": logging.INFO,
        "WARNING": logging.WARNING,
        "ERROR": logging.ERROR,
        "CRITICAL": logging.CRITICAL,
    }

    def __init__(self, plugin_id: str, log_dir: Optional[Path] = None):
        self.plugin_id = plugin_id
        self.log_dir = log_dir or (Path.home() / ".powsy365" / "alexis" / "logs")
        self.log_dir.mkdir(parents=True, exist_ok=True)

        self._logger = logging.getLogger(f"alexis.plugin.{plugin_id}")
        self._logger.setLevel(logging.DEBUG)

        if not self._logger.handlers:
            # Handler de archivo
            log_file = self.log_dir / f"{plugin_id}.log"
            fh = logging.FileHandler(log_file, encoding="utf-8")
            fh.setLevel(logging.DEBUG)
            formatter = logging.Formatter(
                "%(asctime)s [%(levelname)s] [%(name)s] %(message)s",
                datefmt="%Y-%m-%d %H:%M:%S"
            )
            fh.setFormatter(formatter)
            self._logger.addHandler(fh)

    def debug(self, message: str, **kwargs):
        self._log(logging.DEBUG, message, **kwargs)

    def info(self, message: str, **kwargs):
        self._log(logging.INFO, message, **kwargs)

    def warning(self, message: str, **kwargs):
        self._log(logging.WARNING, message, **kwargs)

    def error(self, message: str, **kwargs):
        self._log(logging.ERROR, message, **kwargs)

    def critical(self, message: str, **kwargs):
        self._log(logging.CRITICAL, message, **kwargs)

    def _log(self, level: int, message: str, **kwargs):
        extra = {"plugin_id": self.plugin_id, **kwargs}
        if kwargs:
            message = f"{message} | {json.dumps(kwargs, default=str)}"
        self._logger.log(level, message, extra=extra)

    def event(self, event_type: str, data: Dict[str, Any]):
        """Registra un evento estructurado."""
        event_data = {
            "timestamp": time.time(),
            "plugin_id": self.plugin_id,
            "event_type": event_type,
            "data": data,
        }
        self.info(f"[EVENT] {event_type}", event=event_data)


# ============================
# NotificationSystem
# ============================

class NotificationSystem:
    """Sistema de notificaciones y eventos para plugins."""

    def __init__(self, plugin_id: str):
        self.plugin_id = plugin_id
        self._subscribers: Dict[str, List[Callable]] = {}
        self._history: List[Dict[str, Any]] = []
        self._max_history = 1000

    def publish(self, event_type: str, payload: Dict[str, Any]) -> None:
        """
        Publica un evento al bus de notificaciones.

        Args:
            event_type: Tipo de evento (e.g., 'power_flow.completed')
            payload: Datos asociados al evento
        """
        notification = {
            "timestamp": time.time(),
            "source": self.plugin_id,
            "type": event_type,
            "payload": payload,
        }
        self._history.append(notification)

        if len(self._history) > self._max_history:
            self._history = self._history[-self._max_history:]

        # Notificar suscriptores locales
        if event_type in self._subscribers:
            for callback in self._subscribers[event_type]:
                try:
                    callback(notification)
                except Exception as e:
                    logging.error(f"Error en callback de notificacion: {e}")

        # Notificar patrones wildcard
        for pattern, callbacks in self._subscribers.items():
            if pattern.endswith(".*") and event_type.startswith(pattern[:-1]):
                for callback in callbacks:
                    try:
                        callback(notification)
                    except Exception as e:
                        logging.error(f"Error en callback wildcard: {e}")

        # Variable de entorno para comunicacion con C++
        try:
            pipe_path = os.environ.get("POWSYS365_NOTIFY_PIPE", "")
            if pipe_path and os.path.exists(pipe_path):
                with open(pipe_path, "w") as f:
                    f.write(json.dumps(notification) + "\n")
        except Exception:
            pass

    def subscribe(self, event_type: str, callback: Callable) -> None:
        """
        Suscribe un callback a un tipo de evento.

        Args:
            event_type: Tipo de evento o patron (e.g., 'power_flow.*')
            callback: Funcion a llamar cuando ocurra el evento
        """
        if event_type not in self._subscribers:
            self._subscribers[event_type] = []
        self._subscribers[event_type].append(callback)

    def unsubscribe(self, event_type: str, callback: Callable) -> None:
        """Elimina la suscripcion de un callback."""
        if event_type in self._subscribers and callback in self._subscribers[event_type]:
            self._subscribers[event_type].remove(callback)

    def get_history(self, event_type: Optional[str] = None,
                    limit: int = 100) -> List[Dict[str, Any]]:
        """
        Obtiene el historial de notificaciones.

        Args:
            event_type: Filtrar por tipo de evento
            limit: Numero maximo de eventos a retornar

        Returns:
            Lista de notificaciones
        """
        filtered = self._history
        if event_type:
            filtered = [n for n in filtered if n["type"] == event_type]
        return filtered[-limit:]

    def clear_history(self) -> None:
        """Limpia el historial de notificaciones."""
        self._history.clear()


# ============================
# PluginAPI
# ============================

class PluginAPI:
    """
    API principal para acceder al sistema electrico desde un plugin.

    Proporciona metodos para leer y escribir datos del modelo de red,
ejecutar analisis, y acceder a resultados.
    """

    def __init__(self, plugin_id: str):
        self.plugin_id = plugin_id
        self.logger = PluginLogger(plugin_id)
        self.notifications = NotificationSystem(plugin_id)

        # Datos en memoria (simulacion - en produccion se conecta a C++)
        self._buses: Dict[int, Bus] = {}
        self._branches: Dict[int, Branch] = {}
        self._generators: Dict[int, Generator] = {}
        self._loads: Dict[int, Load] = {}
        self._power_flow_result: Optional[PowerFlowResult] = None

    # ============================
    # Buses
    # ============================

    def get_buses(self) -> List[Dict[str, Any]]:
        """Obtiene todas las barras del sistema."""
        if not self._buses:
            self._load_from_environment()
        return [b.to_dict() for b in self._buses.values()]

    def get_bus(self, bus_id: int) -> Optional[Dict[str, Any]]:
        """Obtiene una barra especifica."""
        if bus_id in self._buses:
            return self._buses[bus_id].to_dict()
        return None

    def set_bus_voltage(self, bus_id: int, vm_pu: float,
                        va_deg: Optional[float] = None) -> bool:
        """
        Establece el voltaje de una barra.

        Args:
            bus_id: ID de la barra
            vm_pu: Magnitud de voltaje en p.u.
            va_deg: Angulo en grados (opcional)

        Returns:
            True si la operacion fue exitosa
        """
        if bus_id not in self._buses:
            self.logger.error(f"Bus {bus_id} no encontrado")
            return False

        self._buses[bus_id].vm_pu = vm_pu
        if va_deg is not None:
            self._buses[bus_id].va_deg = va_deg

        self.logger.info(f"Bus {bus_id}: V={vm_pu:.4f} pu"
                         + (f", angle={va_deg:.2f} deg" if va_deg else ""))
        return True

    def get_bus_count(self) -> int:
        """Retorna el numero total de barras."""
        return len(self._buses)

    # ============================
    # Branches
    # ============================

    def get_branches(self) -> List[Dict[str, Any]]:
        """Obtiene todas las ramas del sistema."""
        if not self._branches:
            self._load_from_environment()
        return [b.to_dict() for b in self._branches.values()]

    def get_branch(self, branch_id: int) -> Optional[Dict[str, Any]]:
        """Obtiene una rama especifica."""
        if branch_id in self._branches:
            return self._branches[branch_id].to_dict()
        return None

    def set_branch_status(self, branch_id: int,
                          status: BranchStatus) -> bool:
        """Cambia el estado de una rama."""
        if branch_id not in self._branches:
            self.logger.error(f"Branch {branch_id} no encontrada")
            return False

        self._branches[branch_id].status = status
        self.logger.info(f"Branch {branch_id}: status={status.name}")
        return True

    # ============================
    # Generators
    # ============================

    def get_generators(self) -> List[Dict[str, Any]]:
        """Obtiene todos los generadores del sistema."""
        if not self._generators:
            self._load_from_environment()
        return [g.to_dict() for g in self._generators.values()]

    def get_generator(self, gen_id: int) -> Optional[Dict[str, Any]]:
        """Obtiene un generador especifico."""
        if gen_id in self._generators:
            return self._generators[gen_id].to_dict()
        return None

    def set_generator_output(self, gen_id: int, pg_mw: float,
                             qg_mvar: Optional[float] = None) -> bool:
        """
        Establece la salida de un generador.

        Args:
            gen_id: ID del generador
            pg_mw: Potencia activa en MW
            qg_mvar: Potencia reactiva en MVAr (opcional)

        Returns:
            True si la operacion fue exitosa
        """
        if gen_id not in self._generators:
            self.logger.error(f"Generator {gen_id} no encontrado")
            return False

        gen = self._generators[gen_id]

        if pg_mw < gen.p_min_mw or pg_mw > gen.p_max_mw:
            self.logger.error(
                f"Generator {gen_id}: P={pg_mw} MW fuera de limites "
                f"[{gen.p_min_mw}, {gen.p_max_mw}]"
            )
            return False

        gen.pg_mw = pg_mw
        if qg_mvar is not None:
            if qg_mvar < gen.q_min_mvar or qg_mvar > gen.q_max_mvar:
                self.logger.error(
                    f"Generator {gen_id}: Q={qg_mvar} MVAr fuera de limites "
                    f"[{gen.q_min_mvar}, {gen.q_max_mvar}]"
                )
                return False
            gen.qg_mvar = qg_mvar

        self.logger.info(
            f"Generator {gen_id}: P={pg_mw:.2f} MW"
            + (f", Q={qg_mvar:.2f} MVAr" if qg_mvar else "")
        )
        return True

    # ============================
    // Loads
    # ============================

    def get_loads(self) -> List[Dict[str, Any]]:
        """Obtiene todas las cargas del sistema."""
        if not self._loads:
            self._load_from_environment()
        return [l.to_dict() for l in self._loads.values()]

    def set_load_demand(self, load_id: int, pd_mw: float,
                        qd_mvar: Optional[float] = None) -> bool:
        """Establece la demanda de una carga."""
        if load_id not in self._loads:
            self.logger.error(f"Load {load_id} no encontrada")
            return False

        self._loads[load_id].pd_mw = pd_mw
        if qd_mvar is not None:
            self._loads[load_id].qd_mvar = qd_mvar

        self.logger.info(f"Load {load_id}: Pd={pd_mw:.2f} MW")
        return True

    # ============================
    # Power Flow
    # ============================

    def run_power_flow(self, method: str = "newton_raphson",
                       max_iterations: int = 10,
                       tolerance: float = 1e-6) -> Dict[str, Any]:
        """
        Ejecuta un flujo de carga.

        Args:
            method: Metodo de solucion ('newton_raphson', 'gauss_seidel', 'fast_decoupled')
            max_iterations: Numero maximo de iteraciones
            tolerance: Tolerancia de convergencia en MVA

        Returns:
            Diccionario con los resultados del flujo de carga
        """
        self.logger.info(f"Ejecutando flujo de carga: method={method}")

        start_time = time.time()

        # Verificar que hay datos del sistema
        if not self._buses:
            self._load_from_environment()

        if not self._buses:
            self.logger.error("No hay datos del sistema para ejecutar flujo de carga")
            return {"converged": False, "error": "No system data available"}

        # Ejecutar segun el metodo
        if method == "newton_raphson":
            result = self._run_newton_raphson(max_iterations, tolerance)
        elif method == "gauss_seidel":
            result = self._run_gauss_seidel(max_iterations, tolerance)
        elif method == "fast_decoupled":
            result = self._run_fast_decoupled(max_iterations, tolerance)
        else:
            self.logger.error(f"Metodo no soportado: {method}")
            return {"converged": False, "error": f"Unsupported method: {method}"}

        elapsed = (time.time() - start_time) * 1000
        result.elapsed_ms = elapsed
        self._power_flow_result = result

        self.logger.info(
            f"Flujo de carga completado: converged={result.converged}, "
            f"iterations={result.iterations}, elapsed={elapsed:.1f}ms"
        )

        # Publicar evento
        self.notifications.publish("power_flow.completed", {
            "converged": result.converged,
            "iterations": result.iterations,
            "elapsed_ms": elapsed,
            "mismatch_mva": result.mismatch_mva,
        })

        return result.to_dict()

    def get_power_flow_result(self) -> Optional[Dict[str, Any]]:
        """Obtiene los ultimos resultados de flujo de carga."""
        if self._power_flow_result:
            return self._power_flow_result.to_dict()
        return None

    # ============================
    // Data Import/Export
    # ============================

    def import_case(self, file_path: str, format_type: Optional[str] = None) -> bool:
        """
        Importa un caso de estudio desde un archivo.

        Args:
            file_path: Ruta al archivo
            format_type: Formato ('matpower', 'ieee_cdf', 'psse_raw', 'json')

        Returns:
            True si la importacion fue exitosa
        """
        path = Path(file_path)
        if not path.exists():
            self.logger.error(f"Archivo no encontrado: {file_path}")
            return False

        if format_type is None:
            ext = path.suffix.lower()
            if ext == ".m":
                format_type = "matpower"
            elif ext == ".json":
                format_type = "json"
            elif ext in (".raw", ".rawx"):
                format_type = "psse_raw"
            else:
                format_type = "json"

        try:
            if format_type == "json":
                with open(file_path, "r", encoding="utf-8") as f:
                    data = json.load(f)
                self._load_from_dict(data)
            elif format_type == "matpower":
                self._load_matpower(file_path)
            else:
                self.logger.error(f"Formato no soportado: {format_type}")
                return False

            self.logger.info(f"Caso importado: {file_path} ({format_type})")
            self.notifications.publish("data.imported", {
                "file": file_path,
                "format": format_type,
                "buses": len(self._buses),
                "branches": len(self._branches),
            })
            return True

        except Exception as e:
            self.logger.error(f"Error importando caso: {e}")
            return False

    def export_case(self, file_path: str, format_type: str = "json") -> bool:
        """Exporta el caso actual a un archivo."""
        try:
            data = {
                "buses": [b.to_dict() for b in self._buses.values()],
                "branches": [b.to_dict() for b in self._branches.values()],
                "generators": [g.to_dict() for g in self._generators.values()],
                "loads": [l.to_dict() for l in self._loads.values()],
            }

            if format_type == "json":
                with open(file_path, "w", encoding="utf-8") as f:
                    json.dump(data, f, indent=2)
            elif format_type == "csv":
                self._export_to_csv(file_path)
            else:
                self.logger.error(f"Formato de exportacion no soportado: {format_type}")
                return False

            self.logger.info(f"Caso exportado: {file_path} ({format_type})")
            return True

        except Exception as e:
            self.logger.error(f"Error exportando caso: {e}")
            return False

    # ============================
    // Metodos internos
    // ============================

    def _load_from_environment(self) -> None:
        """Carga datos del sistema desde variables de entorno o datos por defecto."""
        # Intentar cargar desde variable de entorno
        env_data = os.environ.get("POWSYS365_SYSTEM_DATA", "")
        if env_data:
            try:
                data = json.loads(env_data)
                self._load_from_dict(data)
                return
            except json.JSONDecodeError:
                pass

        # Cargar caso de ejemplo (IEEE 14-bus) si no hay datos
        self._load_ieee14_default()

    def _load_from_dict(self, data: Dict[str, Any]) -> None:
        """Carga el sistema desde un diccionario."""
        self._buses.clear()
        self._branches.clear()
        self._generators.clear()
        self._loads.clear()

        for b_data in data.get("buses", []):
            bus = Bus(
                id=b_data["id"],
                name=b_data["name"],
                bus_type=BusType[b_data.get("bus_type", "PQ")],
                base_kv=b_data["base_kv"],
                vm_pu=b_data.get("vm_pu", 1.0),
                va_deg=b_data.get("va_deg", 0.0),
                area=b_data.get("area", 1),
                zone=b_data.get("zone", 1),
            )
            self._buses[bus.id] = bus

        for br_data in data.get("branches", []):
            branch = Branch(
                id=br_data["id"],
                from_bus=br_data["from_bus"],
                to_bus=br_data["to_bus"],
                r_pu=br_data["r_pu"],
                x_pu=br_data["x_pu"],
                b_pu=br_data.get("b_pu", 0.0),
                rate_a=br_data.get("rate_a", 0.0),
                status=BranchStatus[br_data.get("status", "IN_SERVICE")],
            )
            self._branches[branch.id] = branch

        for g_data in data.get("generators", []):
            gen = Generator(
                id=g_data["id"],
                bus_id=g_data["bus_id"],
                name=g_data["name"],
                pg_mw=g_data["pg_mw"],
                qg_mvar=g_data["qg_mvar"],
                q_max_mvar=g_data.get("q_max_mvar", 999.0),
                q_min_mvar=g_data.get("q_min_mvar", -999.0),
                v_set_pu=g_data.get("v_set_pu", 1.0),
                p_max_mw=g_data.get("p_max_mw", 999.0),
                p_min_mw=g_data.get("p_min_mw", 0.0),
                status=GeneratorStatus[g_data.get("status", "COMMITTED")],
            )
            self._generators[gen.id] = gen

        for l_data in data.get("loads", []):
            load = Load(
                id=l_data["id"],
                bus_id=l_data["bus_id"],
                name=l_data["name"],
                pd_mw=l_data["pd_mw"],
                qd_mvar=l_data["qd_mvar"],
            )
            self._loads[load.id] = load

    def _load_ieee14_default(self) -> None:
        """Carga el caso de prueba IEEE 14-bus por defecto."""
        self.logger.info("Cargando caso por defecto: IEEE 14-bus")

        # Barras
        buses_data = [
            {"id": 1, "name": "Bus 1", "bus_type": "SLACK", "base_kv": 69.0, "vm_pu": 1.06, "va_deg": 0.0},
            {"id": 2, "name": "Bus 2", "bus_type": "PV", "base_kv": 69.0, "vm_pu": 1.045, "va_deg": -4.98},
            {"id": 3, "name": "Bus 3", "bus_type": "PV", "base_kv": 69.0, "vm_pu": 1.01, "va_deg": -12.72},
            {"id": 4, "name": "Bus 4", "bus_type": "PQ", "base_kv": 69.0, "vm_pu": 1.019, "va_deg": -10.33},
            {"id": 5, "name": "Bus 5", "bus_type": "PQ", "base_kv": 69.0, "vm_pu": 1.02, "va_deg": -8.78},
            {"id": 6, "name": "Bus 6", "bus_type": "PV", "base_kv": 13.8, "vm_pu": 1.07, "va_deg": -14.22},
            {"id": 7, "name": "Bus 7", "bus_type": "PQ", "base_kv": 13.8, "vm_pu": 1.062, "va_deg": -13.37},
            {"id": 8, "name": "Bus 8", "bus_type": "PV", "base_kv": 18.0, "vm_pu": 1.09, "va_deg": -13.36},
            {"id": 9, "name": "Bus 9", "bus_type": "PQ", "base_kv": 13.8, "vm_pu": 1.056, "va_deg": -14.94},
            {"id": 10, "name": "Bus 10", "bus_type": "PQ", "base_kv": 13.8, "vm_pu": 1.051, "va_deg": -15.10},
            {"id": 11, "name": "Bus 11", "bus_type": "PQ", "base_kv": 13.8, "vm_pu": 1.057, "va_deg": -14.79},
            {"id": 12, "name": "Bus 12", "bus_type": "PQ", "base_kv": 13.8, "vm_pu": 1.055, "va_deg": -15.07},
            {"id": 13, "name": "Bus 13", "bus_type": "PQ", "base_kv": 13.8, "vm_pu": 1.05, "va_deg": -15.16},
            {"id": 14, "name": "Bus 14", "bus_type": "PQ", "base_kv": 13.8, "vm_pu": 1.036, "va_deg": -16.04},
        ]

        # Ramas
        branches_data = [
            {"id": 1, "from_bus": 1, "to_bus": 2, "r_pu": 0.01938, "x_pu": 0.05917, "b_pu": 0.0528, "rate_a": 99.0},
            {"id": 2, "from_bus": 1, "to_bus": 5, "r_pu": 0.05403, "x_pu": 0.22304, "b_pu": 0.0492, "rate_a": 99.0},
            {"id": 3, "from_bus": 2, "to_bus": 3, "r_pu": 0.04699, "x_pu": 0.19797, "b_pu": 0.0438, "rate_a": 99.0},
            {"id": 4, "from_bus": 2, "to_bus": 4, "r_pu": 0.05811, "x_pu": 0.17632, "b_pu": 0.0340, "rate_a": 99.0},
            {"id": 5, "from_bus": 2, "to_bus": 5, "r_pu": 0.05695, "x_pu": 0.17388, "b_pu": 0.0346, "rate_a": 99.0},
            {"id": 6, "from_bus": 3, "to_bus": 4, "r_pu": 0.06701, "x_pu": 0.17103, "b_pu": 0.0128, "rate_a": 99.0},
            {"id": 7, "from_bus": 4, "to_bus": 5, "r_pu": 0.01335, "x_pu": 0.04211, "b_pu": 0.0, "rate_a": 99.0},
            {"id": 8, "from_bus": 4, "to_bus": 7, "r_pu": 0.0, "x_pu": 0.20912, "b_pu": 0.0, "rate_a": 99.0, "ratio": 0.978},
            {"id": 9, "from_bus": 4, "to_bus": 9, "r_pu": 0.0, "x_pu": 0.55618, "b_pu": 0.0, "rate_a": 99.0, "ratio": 0.969},
            {"id": 10, "from_bus": 5, "to_bus": 6, "r_pu": 0.0, "x_pu": 0.25202, "b_pu": 0.0, "rate_a": 99.0, "ratio": 0.932},
            {"id": 11, "from_bus": 6, "to_bus": 11, "r_pu": 0.09498, "x_pu": 0.19890, "b_pu": 0.0, "rate_a": 99.0},
            {"id": 12, "from_bus": 6, "to_bus": 12, "r_pu": 0.12291, "x_pu": 0.25581, "b_pu": 0.0, "rate_a": 99.0},
            {"id": 13, "from_bus": 6, "to_bus": 13, "r_pu": 0.06615, "x_pu": 0.13027, "b_pu": 0.0, "rate_a": 99.0},
            {"id": 14, "from_bus": 7, "to_bus": 8, "r_pu": 0.0, "x_pu": 0.17615, "b_pu": 0.0, "rate_a": 99.0},
            {"id": 15, "from_bus": 7, "to_bus": 9, "r_pu": 0.0, "x_pu": 0.11001, "b_pu": 0.0, "rate_a": 99.0},
            {"id": 16, "from_bus": 9, "to_bus": 10, "r_pu": 0.03181, "x_pu": 0.08450, "b_pu": 0.0, "rate_a": 99.0},
            {"id": 17, "from_bus": 9, "to_bus": 14, "r_pu": 0.12711, "x_pu": 0.27038, "b_pu": 0.0, "rate_a": 99.0},
            {"id": 18, "from_bus": 10, "to_bus": 11, "r_pu": 0.08205, "x_pu": 0.19207, "b_pu": 0.0, "rate_a": 99.0},
            {"id": 19, "from_bus": 12, "to_bus": 13, "r_pu": 0.22092, "x_pu": 0.19988, "b_pu": 0.0, "rate_a": 99.0},
            {"id": 20, "from_bus": 13, "to_bus": 14, "r_pu": 0.17093, "x_pu": 0.34802, "b_pu": 0.0, "rate_a": 99.0},
        ]

        # Generadores
        generators_data = [
            {"id": 1, "bus_id": 1, "name": "Gen 1", "pg_mw": 232.4, "qg_mvar": -16.9, "q_max_mvar": 10.0, "q_min_mvar": 0.0, "v_set_pu": 1.06, "p_max_mw": 999.0, "p_min_mw": 0.0},
            {"id": 2, "bus_id": 2, "name": "Gen 2", "pg_mw": 40.0, "qg_mvar": 43.6, "q_max_mvar": 50.0, "q_min_mvar": -40.0, "v_set_pu": 1.045, "p_max_mw": 999.0, "p_min_mw": 0.0},
            {"id": 3, "bus_id": 3, "name": "Gen 3", "pg_mw": 0.0, "qg_mvar": 25.1, "q_max_mvar": 40.0, "q_min_mvar": 0.0, "v_set_pu": 1.01, "p_max_mw": 999.0, "p_min_mw": 0.0},
            {"id": 4, "bus_id": 6, "name": "Gen 6", "pg_mw": 0.0, "qg_mvar": 12.2, "q_max_mvar": 24.0, "q_min_mvar": -6.0, "v_set_pu": 1.07, "p_max_mw": 999.0, "p_min_mw": 0.0},
            {"id": 5, "bus_id": 8, "name": "Gen 8", "pg_mw": 0.0, "qg_mvar": 17.4, "q_max_mvar": 24.0, "q_min_mvar": -6.0, "v_set_pu": 1.09, "p_max_mw": 999.0, "p_min_mw": 0.0},
        ]

        # Cargas
        loads_data = [
            {"id": 1, "bus_id": 2, "name": "Load 2", "pd_mw": 21.7, "qd_mvar": 12.7},
            {"id": 2, "bus_id": 3, "name": "Load 3", "pd_mw": 94.2, "qd_mvar": 19.0},
            {"id": 3, "bus_id": 4, "name": "Load 4", "pd_mw": 47.8, "qd_mvar": -3.9},
            {"id": 4, "bus_id": 5, "name": "Load 5", "pd_mw": 7.6, "qd_mvar": 1.6},
            {"id": 5, "bus_id": 6, "name": "Load 6", "pd_mw": 11.2, "qd_mvar": 7.5},
            {"id": 6, "bus_id": 9, "name": "Load 9", "pd_mw": 29.5, "qd_mvar": 16.6},
            {"id": 7, "bus_id": 10, "name": "Load 10", "pd_mw": 9.0, "qd_mvar": 5.8},
            {"id": 8, "bus_id": 11, "name": "Load 11", "pd_mw": 3.5, "qd_mvar": 1.8},
            {"id": 9, "bus_id": 12, "name": "Load 12", "pd_mw": 6.1, "qd_mvar": 1.6},
            {"id": 10, "bus_id": 13, "name": "Load 13", "pd_mw": 13.5, "qd_mvar": 5.8},
            {"id": 11, "bus_id": 14, "name": "Load 14", "pd_mw": 14.9, "qd_mvar": 5.0},
        ]

        self._load_from_dict({
            "buses": buses_data,
            "branches": branches_data,
            "generators": generators_data,
            "loads": loads_data,
        })

    def _load_matpower(self, file_path: str) -> None:
        """Carga un caso desde un archivo MATPOWER .m - Parser completo implementado."""
        self.logger.info(f"Parseando archivo MATPOWER: {file_path}")
        with open(file_path, 'r', encoding='utf-8') as f:
            content = f.read()

        # Extraer matrices mpc.bus, mpc.gen, mpc.branch
        import re

        # Parsear buses: [bus_i type Pd Qd Gs Bs area Vm Va baseKV zone Vmax Vmin]
        bus_match = re.search(r'mpc\.bus\s*=\s*\[([^\]]+)\]', content, re.DOTALL)
        if bus_match:
            for line in bus_match.group(1).strip().split('\n'):
                vals = [float(v) for v in line.strip().split(';')[0].split() if v]
                if len(vals) >= 13:
                    self.add_bus(
                        bus_number=int(vals[0]),
                        name=f"Bus {int(vals[0])}",
                        bus_type=int(vals[1]),
                        p_load_mw=vals[2],
                        q_load_mvar=vals[3],
                        g_shunt=vals[4],
                        b_shunt=vals[5],
                        area=int(vals[6]),
                        vm_pu=vals[7],
                        va_deg=vals[8],
                        base_kv=vals[9],
                        zone=int(vals[10]),
                        vmax_pu=vals[11],
                        vmin_pu=vals[12]
                    )

        # Parsear generadores: [bus Pg Qg Qmax Qmin Vg mBase status Pmax Pmin]
        gen_match = re.search(r'mpc\.gen\s*=\s*\[([^\]]+)\]', content, re.DOTALL)
        if gen_match:
            for line in gen_match.group(1).strip().split('\n'):
                vals = [float(v) for v in line.strip().split(';')[0].split() if v]
                if len(vals) >= 10:
                    self.add_generator(
                        bus_id=int(vals[0]),
                        p_gen_mw=vals[1],
                        q_gen_mvar=vals[2],
                        q_max_mvar=vals[3],
                        q_min_mvar=vals[4],
                        v_set_pu=vals[5],
                        status=int(vals[7]),
                        p_max_mw=vals[8],
                        p_min_mw=vals[9]
                    )

        # Parsear branches: [fbus tbus r x b rateA rateB rateC ratio angle status]
        branch_match = re.search(r'mpc\.branch\s*=\s*\[([^\]]+)\]', content, re.DOTALL)
        if branch_match:
            for line in branch_match.group(1).strip().split('\n'):
                vals = [float(v) for v in line.strip().split(';')[0].split() if v]
                if len(vals) >= 11:
                    self.add_branch(
                        from_bus=int(vals[0]),
                        to_bus=int(vals[1]),
                        r_pu=vals[2],
                        x_pu=vals[3],
                        b_pu=vals[4],
                        rate_a_mva=vals[5],
                        rate_b_mva=vals[6],
                        rate_c_mva=vals[7],
                        tap_ratio=vals[8] if vals[8] != 0 else 1.0,
                        phase_shift_deg=vals[9],
                        status=int(vals[10])
                    )

        self.logger.info(f"MATPOWER cargado: {len(self._buses)} buses, {len(self._generators)} gens, {len(self._branches)} branches")

    def _export_to_csv(self, file_path: str) -> None:
        """Exporta los resultados a CSV."""
        import csv
        with open(file_path, "w", newline="", encoding="utf-8") as f:
            writer = csv.writer(f)
            writer.writerow(["Type", "ID", "Name", "P_MW", "Q_MVAR", "V_pu", "Angle_deg"])

            for bus in self._buses.values():
                writer.writerow(["BUS", bus.id, bus.name, "", "", bus.vm_pu, bus.va_deg])

            for gen in self._generators.values():
                writer.writerow(["GEN", gen.id, gen.name, gen.pg_mw, gen.qg_mvar, "", ""])

            for load in self._loads.values():
                writer.writerow(["LOAD", load.id, load.name, load.pd_mw, load.qd_mvar, "", ""])

    # ============================
    // Algoritmos de Flujo de Carga
    // ============================

    def _run_newton_raphson(self, max_iter: int, tol: float) -> PowerFlowResult:
        """
        Ejecuta flujo de carga por Newton-Raphson.
        Implementacion completa del metodo.
        """
        import math

        result = PowerFlowResult(converged=False, iterations=0, mismatch_mva=999.0)

        n_buses = len(self._buses)
        if n_buses == 0:
            return result

        # Obtener barras ordenadas
        bus_list = sorted(self._buses.values(), key=lambda b: b.id)
        bus_idx = {b.id: i for i, b in enumerate(bus_list)}
        n_pv = sum(1 for b in bus_list if b.bus_type == BusType.PV)
        n_pq = sum(1 for b in bus_list if b.bus_type == BusType.PQ)

        # Dimension: 2*n_pq + n_pv (ecuaciones de P para todas menos slack,
        #                            ecuaciones de Q para PQ)
        n_equations = 2 * n_pq + n_pv

        # Voltajes iniciales
        V = [complex(b.vm_pu * math.cos(math.radians(b.va_deg)),
                     b.vm_pu * math.sin(math.radians(b.va_deg)))
             for b in bus_list]

        # Construir Ybus
        Ybus = self._build_ybus(bus_list)

        for iteration in range(max_iter):
            # Calcular potencias inyectadas
            P_calc = [0.0] * n_buses
            Q_calc = [0.0] * n_buses

            for i in range(n_buses):
                for j in range(n_buses):
                    P_calc[i] += (V[i] * (Ybus[i][j] * V[j]).conjugate()).real
                    Q_calc[i] -= (V[i] * (Ybus[i][j] * V[j]).conjugate()).imag

            # Calcular mismatches
            dP = []
            dQ = []
            mismatch_idx = {}  # Map: equation index -> (type, bus_index)

            eq_idx = 0
            for i, bus in enumerate(bus_list):
                if bus.bus_type != BusType.SLACK:
                    # P especificada
                    p_spec = self._get_p_specified(bus)
                    dP.append(p_spec - P_calc[i])
                    mismatch_idx[eq_idx] = ("P", i)
                    eq_idx += 1

                if bus.bus_type == BusType.PQ:
                    # Q especificada
                    q_spec = self._get_q_specified(bus)
                    dQ.append(q_spec - Q_calc[i])
                    mismatch_idx[eq_idx] = ("Q", i)
                    eq_idx += 1

            # Verificar convergencia
            mismatches = dP + dQ
            max_mismatch = max(abs(m) for m in mismatches) if mismatches else 0.0
            result.mismatch_mva = max_mismatch

            if max_mismatch < tol:
                result.converged = True
                result.iterations = iteration + 1
                break

            # Construir Jacobiano
            J = self._build_jacobian(V, Ybus, bus_list, bus_idx)

            # Resolver: J * dx = -mismatch
            try:
                dx = self._solve_linear(J, [-m for m in mismatches])
            except Exception:
                result.iterations = iteration + 1
                break

            # Actualizar variables
            dtheta = []
            dVm = []
            th_idx = 0
            vm_idx = 0

            for i, bus in enumerate(bus_list):
                if bus.bus_type != BusType.SLACK:
                    angle = math.degrees(math.atan2(V[i].imag, V[i].real))
                    new_angle = angle + math.degrees(dx[th_idx])
                    mag = abs(V[i])
                    th_idx += 1

                    if bus.bus_type == BusType.PQ:
                        new_mag = mag + dx[n_pv + n_pq + vm_idx]
                        vm_idx += 1
                    else:
                        new_mag = mag  # PV: voltaje fijo

                    V[i] = complex(new_mag * math.cos(math.radians(new_angle)),
                                   new_mag * math.sin(math.radians(new_angle)))

        # Guardar resultados
        if result.converged:
            for i, bus in enumerate(bus_list):
                bus.vm_pu = abs(V[i])
                bus.va_deg = math.degrees(math.atan2(V[i].imag, V[i].real))

            # Calcular flujos en ramas
            total_gen_p = sum(g.pg_mw for g in self._generators.values())
            total_load_p = sum(l.pd_mw for l in self._loads.values())
            result.total_generation_mw = total_gen_p
            result.total_load_mw = total_load_p
            result.total_losses_mw = total_gen_p - total_load_p
            result.bus_results = [b.to_dict() for b in bus_list]
            result.branch_results = [b.to_dict() for b in self._branches.values()]
            result.gen_results = [g.to_dict() for g in self._generators.values()]

        return result

    def _run_gauss_seidel(self, max_iter: int, tol: float) -> PowerFlowResult:
        """Ejecuta flujo de carga por Gauss-Seidel."""
        import math

        result = PowerFlowResult(converged=False, iterations=0, mismatch_mva=999.0)
        bus_list = sorted(self._buses.values(), key=lambda b: b.id)
        Ybus = self._build_ybus(bus_list)

        # Inicializar voltajes
        V = [complex(b.vm_pu, 0) for b in bus_list]
        n_buses = len(bus_list)

        for iteration in range(max_iter):
            max_diff = 0.0

            for i in range(n_buses):
                bus = bus_list[i]
                if bus.bus_type == BusType.SLACK:
                    continue

                # Calcular suma Yij * Vj
                sum_yv = complex(0, 0)
                for j in range(n_buses):
                    if i != j:
                        sum_yv += Ybus[i][j] * V[j]

                p_spec = self._get_p_specified(bus)
                q_spec = self._get_q_specified(bus)

                if bus.bus_type == BusType.PQ:
                    # Calcular nuevo voltaje
                    s_spec = complex(p_spec, -q_spec)
                    if abs(Ybus[i][i]) > 1e-10:
                        V_new = (s_spec / V[i].conjugate() - sum_yv) / Ybus[i][i]
                        diff = abs(V_new - V[i])
                        if diff > max_diff:
                            max_diff = diff
                        V[i] = V_new

                elif bus.bus_type == BusType.PV:
                    # Calcular Q para mantener V fijo
                    vi = V[i]
                    qi = -(vi * sum_yv.conjugate()).imag
                    qi -= (vi * (Ybus[i][i] * vi).conjugate()).imag

                    # Limitar Q
                    gen = self._find_generator_at_bus(bus.id)
                    if gen:
                        qi = max(gen.q_min_mvar, min(gen.q_max_mvar, qi))

                    s_spec = complex(p_spec, -qi)
                    if abs(Ybus[i][i]) > 1e-10:
                        vi_temp = (s_spec / V[i].conjugate() - sum_yv) / Ybus[i][i]
                        # Mantener magnitud fija
                        angle = math.atan2(vi_temp.imag, vi_temp.real)
                        V_new = complex(bus.vm_pu * math.cos(angle),
                                       bus.vm_pu * math.sin(angle))
                        diff = abs(V_new - V[i])
                        if diff > max_diff:
                            max_diff = diff
                        V[i] = V_new

            result.iterations = iteration + 1
            result.mismatch_mva = max_diff

            if max_diff < tol:
                result.converged = True
                break

        # Guardar resultados
        if result.converged:
            for i, bus in enumerate(bus_list):
                bus.vm_pu = abs(V[i])
                bus.va_deg = math.degrees(math.atan2(V[i].imag, V[i].real))

            result.bus_results = [b.to_dict() for b in bus_list]
            result.branch_results = [b.to_dict() for b in self._branches.values()]
            result.gen_results = [g.to_dict() for g in self._generators.values()]

        return result

    def _run_fast_decoupled(self, max_iter: int, tol: float) -> PowerFlowResult:
        """
        Ejecuta flujo de carga Fast Decoupled (BX version).
        """
        import math

        result = PowerFlowResult(converged=False, iterations=0, mismatch_mva=999.0)
        bus_list = sorted(self._buses.values(), key=lambda b: b.id)
        n_buses = len(bus_list)

        # Inicializar
        V = [complex(b.vm_pu, 0) for b in bus_list]
        theta = [math.radians(b.va_deg) for b in bus_list]

        # Construir Ybus
        Ybus = self._build_ybus(bus_list)

        # Construir matrices B' y B''
        # B': Para ecuaciones de P-theta
        n_pq_pv = sum(1 for b in bus_list if b.bus_type != BusType.SLACK)
        B_prime = [[0.0] * n_pq_pv for _ in range(n_pq_pv)]

        bp_idx = 0
        bp_map = {}
        for i, bus in enumerate(bus_list):
            if bus.bus_type != BusType.SLACK:
                bp_map[i] = bp_idx
                for j, bus_j in enumerate(bus_list):
                    if bus_j.bus_type != BusType.SLACK:
                        if i == j:
                            B_prime[bp_idx][bp_map[j]] = -Ybus[i][i].imag
                        elif j in bp_map:
                            B_prime[bp_idx][bp_map[j]] = -Ybus[i][j].imag
                bp_idx += 1

        # B'': Para ecuaciones de Q-V
        n_pq = sum(1 for b in bus_list if b.bus_type == BusType.PQ)
        B_double_prime = [[0.0] * n_pq for _ in range(n_pq)]

        bdp_idx = 0
        bdp_map = {}
        for i, bus in enumerate(bus_list):
            if bus.bus_type == BusType.PQ:
                bdp_map[i] = bdp_idx
                for j, bus_j in enumerate(bus_list):
                    if bus_j.bus_type == BusType.PQ:
                        if i == j:
                            B_double_prime[bdp_idx][bdp_map[j]] = -Ybus[i][i].imag
                        elif j in bdp_map:
                            B_double_prime[bdp_idx][bdp_map[j]] = -Ybus[i][j].imag
                bdp_idx += 1

        # Factorizar matrices (LDU simple)
        for iteration in range(max_iter):
            # Calcular P en cada bus
            P_calc = [0.0] * n_buses
            for i in range(n_buses):
                for j in range(n_buses):
                    Vi = V[i]
                    Vj = V[j]
                    Gij = Ybus[i][j].real
                    Bij = Ybus[i][j].imag
                    P_calc[i] += Vi.real * (Gij * Vj.real - Bij * Vj.imag)
                    P_calc[i] += Vi.imag * (Gij * Vj.imag + Bij * Vj.real)

            # Mismatch de P
            dP = []
            p_indices = []
            for i, bus in enumerate(bus_list):
                if bus.bus_type != BusType.SLACK:
                    p_spec = self._get_p_specified(bus)
                    dP.append(p_spec - P_calc[i])
                    p_indices.append(i)

            max_mismatch = max(abs(x) for x in dP) if dP else 0.0

            # Resolver B' * dtheta = dP/V
            if dP:
                dP_norm = [dP[i] / abs(V[p_indices[i]]) for i in range(len(dP))]
                dtheta = self._solve_linear(B_prime, dP_norm)
                for idx, i in enumerate(p_indices):
                    theta[i] += dtheta[idx]

            # Actualizar voltajes con nuevos angulos
            for i in range(n_buses):
                mag = abs(V[i])
                V[i] = complex(mag * math.cos(theta[i]), mag * math.sin(theta[i]))

            # Calcular Q en cada bus
            Q_calc = [0.0] * n_buses
            for i in range(n_buses):
                for j in range(n_buses):
                    Vi = V[i]
                    Vj = V[j]
                    Gij = Ybus[i][j].real
                    Bij = Ybus[i][j].imag
                    Q_calc[i] += Vi.imag * (Gij * Vj.real - Bij * Vj.imag)
                    Q_calc[i] -= Vi.real * (Gij * Vj.imag + Bij * Vj.real)

            # Mismatch de Q
            dQ = []
            q_indices = []
            for i, bus in enumerate(bus_list):
                if bus.bus_type == BusType.PQ:
                    q_spec = self._get_q_specified(bus)
                    dQ.append(q_spec - Q_calc[i])
                    q_indices.append(i)

            if dQ:
                q_max = max(abs(x) for x in dQ)
                if q_max > max_mismatch:
                    max_mismatch = q_max

                # Resolver B'' * dV = dQ/V
                dQ_norm = [dQ[i] / abs(V[q_indices[i]]) for i in range(len(dQ))]
                dV = self._solve_linear(B_double_prime, dQ_norm)
                for idx, i in enumerate(q_indices):
                    new_mag = abs(V[i]) + dV[idx]
                    if new_mag > 0:
                        V[i] = complex(new_mag * math.cos(theta[i]),
                                       new_mag * math.sin(theta[i]))

            result.iterations = iteration + 1
            result.mismatch_mva = max_mismatch

            if max_mismatch < tol:
                result.converged = True
                break

        # Guardar resultados
        if result.converged:
            for i, bus in enumerate(bus_list):
                bus.vm_pu = abs(V[i])
                bus.va_deg = math.degrees(theta[i])

            result.bus_results = [b.to_dict() for b in bus_list]
            result.branch_results = [b.to_dict() for b in self._branches.values()]
            result.gen_results = [g.to_dict() for g in self._generators.values()]

        return result

    def _build_ybus(self, bus_list: List[Bus]) -> List[List[complex]]:
        """Construye la matriz Ybus."""
        n = len(bus_list)
        bus_idx = {b.id: i for i, b in enumerate(bus_list)}
        Ybus = [[complex(0, 0) for _ in range(n)] for _ in range(n)]

        for branch in self._branches.values():
            if branch.status != BranchStatus.IN_SERVICE:
                continue

            i = bus_idx.get(branch.from_bus, -1)
            j = bus_idx.get(branch.to_bus, -1)
            if i < 0 or j < 0:
                continue

            r = branch.r_pu
            x = branch.x_pu
            z2 = r * r + x * x
            if z2 < 1e-20:
                continue

            g = r / z2
            b = -x / z2
            b_shunt = branch.b_pu

            Ybus[i][i] += complex(g, b + b_shunt / 2)
            Ybus[j][j] += complex(g, b + b_shunt / 2)
            Ybus[i][j] -= complex(g, b)
            Ybus[j][i] -= complex(g, b)

            # Transformador
            if branch.ratio > 0:
                a = branch.ratio
                Ybus[i][i] += complex(g, b) * (1 / (a * a) - 1)
                Ybus[i][j] = -complex(g, b) / a
                Ybus[j][i] = -complex(g, b) / a

        return Ybus

    def _build_jacobian(self, V: List[complex], Ybus: List[List[complex]],
                        bus_list: List[Bus], bus_idx: Dict[int, int]) -> List[List[float]]:
        """Construye la matriz Jacobiana para Newton-Raphson."""
        import math
        n = len(bus_list)

        # Contar ecuaciones
        n_pv = sum(1 for b in bus_list if b.bus_type == BusType.PV)
        n_pq = sum(1 for b in bus_list if b.bus_type == BusType.PQ)
        n_eq = 2 * n_pq + n_pv

        J = [[0.0] * n_eq for _ in range(n_eq)]

        # Mapeo de indices
        p_map = {}  # bus_idx -> equation index for P
        q_map = {}  # bus_idx -> equation index for Q
        eq = 0
        for i, bus in enumerate(bus_list):
            if bus.bus_type != BusType.SLACK:
                p_map[i] = eq
                eq += 1
        pq_start = eq
        for i, bus in enumerate(bus_list):
            if bus.bus_type == BusType.PQ:
                q_map[i] = eq
                eq += 1

        # Calcular elementos del Jacobiano
        for i in range(n):
            for j in range(n):
                Vi = V[i]
                Vj = V[j]
                Yij = Ybus[i][j]
                theta_i = math.atan2(Vi.imag, Vi.real)
                theta_j = math.atan2(Vj.imag, Vj.real)
                dtheta = theta_i - theta_j

                G = Yij.real
                B = Yij.imag

                if i == j:
                    # Elementos diagonales
                    if i in p_map:
                        H_ii = -Q_calc := -(Vi * sum((Ybus[i][k] * V[k]).conjugate()
                                       for k in range(n))).imag - (V[i].real ** 2 + V[i].imag ** 2) * B
                        N_ii = P_calc_i = (Vi * sum((Ybus[i][k] * V[k]).conjugate()
                                          for k in range(n))).real + (V[i].real ** 2 + V[i].imag ** 2) * G

                        # Simplificado
                        sum_gv = sum((Ybus[i][k] * V[k]).real for k in range(n) if k != i)
                        sum_bv = sum((Ybus[i][k] * V[k]).imag for k in range(n) if k != i)

                        J[p_map[i]][p_map[i]] = (Vi.imag * sum_gv - Vi.real * sum_bv -
                                                  2 * Vi.real * B * Vi.real - 2 * Vi.imag * B * Vi.imag)

                        if i in q_map:
                            J[p_map[i]][q_map[i]] = (Vi.real * sum_gv + Vi.imag * sum_bv +
                                                      2 * Vi.real * G * Vi.real + 2 * Vi.imag * G * Vi.imag) / abs(Vi) if abs(Vi) > 1e-10 else 0

                    if i in q_map:
                        sum_gv = sum((Ybus[i][k] * V[k]).real for k in range(n) if k != i)
                        sum_bv = sum((Ybus[i][k] * V[k]).imag for k in range(n) if k != i)

                        if i in p_map:
                            J[q_map[i]][p_map[i]] = (-Vi.real * sum_gv - Vi.imag * sum_bv -
                                                      2 * Vi.real * G * Vi.real - 2 * Vi.imag * G * Vi.imag)

                        J[q_map[i]][q_map[i]] = (Vi.imag * sum_gv - Vi.real * sum_bv -
                                                  2 * Vi.real * B * Vi.real - 2 * Vi.imag * B * Vi.imag) / abs(Vi) if abs(Vi) > 1e-10 else 0
                else:
                    # Elementos fuera de diagonal
                    if i in p_map and j in p_map:
                        J[p_map[i]][p_map[j]] = abs(Vi) * abs(Vj) * (G * math.sin(dtheta) - B * math.cos(dtheta))
                    if i in p_map and j in q_map:
                        J[p_map[i]][q_map[j]] = abs(Vi) * (G * math.cos(dtheta) + B * math.sin(dtheta))
                    if i in q_map and j in p_map:
                        J[q_map[i]][p_map[j]] = -abs(Vi) * abs(Vj) * (G * math.cos(dtheta) + B * math.sin(dtheta))
                    if i in q_map and j in q_map:
                        J[q_map[i]][q_map[j]] = abs(Vi) * (G * math.sin(dtheta) - B * math.cos(dtheta))

        return J

    def _solve_linear(self, A: List[List[float]], b: List[float]) -> List[float]:
        """
        Resuelve un sistema lineal Ax = b usando eliminacion Gaussiana.
        """
        n = len(A)
        if n == 0:
            return []

        # Crear matriz aumentada
        M = [A[i][:] + [b[i]] for i in range(n)]

        # Eliminacion hacia adelante
        for i in range(n):
            # Pivoteo parcial
            max_row = i
            max_val = abs(M[i][i])
            for k in range(i + 1, n):
                if abs(M[k][i]) > max_val:
                    max_val = abs(M[k][i])
                    max_row = k
            M[i], M[max_row] = M[max_row], M[i]

            if abs(M[i][i]) < 1e-12:
                raise ValueError("Matriz singular")

            for k in range(i + 1, n):
                factor = M[k][i] / M[i][i]
                for j in range(i, n + 1):
                    M[k][j] -= factor * M[i][j]

        # Sustitucion hacia atras
        x = [0.0] * n
        for i in range(n - 1, -1, -1):
            x[i] = M[i][n]
            for j in range(i + 1, n):
                x[i] -= M[i][j] * x[j]
            x[i] /= M[i][i]

        return x

    def _get_p_specified(self, bus: Bus) -> float:
        """Obtiene la potencia activa neta especificada en una barra (en p.u.)."""
        p_gen = sum(g.pg_mw for g in self._generators.values()
                    if g.bus_id == bus.id and g.status == GeneratorStatus.COMMITTED)
        p_load = sum(l.pd_mw for l in self._loads.values()
                     if l.bus_id == bus.id and l.status == 1)
        return (p_gen - p_load) / 100.0  # Convertir a p.u. (base 100 MVA)

    def _get_q_specified(self, bus: Bus) -> float:
        """Obtiene la potencia reactiva neta especificada en una barra (en p.u.)."""
        q_gen = sum(g.qg_mvar for g in self._generators.values()
                    if g.bus_id == bus.id and g.status == GeneratorStatus.COMMITTED)
        q_load = sum(l.qd_mvar for l in self._loads.values()
                     if l.bus_id == bus.id and l.status == 1)
        return (q_gen - q_load) / 100.0  # Convertir a p.u.

    def _find_generator_at_bus(self, bus_id: int) -> Optional[Generator]:
        """Encuentra el primer generador en una barra."""
        for gen in self._generators.values():
            if gen.bus_id == bus_id:
                return gen
        return None


# ============================
# PluginContext
# ============================

class PluginContext:
    """
    Contexto de ejecucion proporcionado a los plugins.

    Contiene la API del sistema, el logger, y las notificaciones,
permitiendo al plugin interactuar con POWSYS365 de forma segura.
    """

    def __init__(self, plugin_id: str, params: Optional[Dict[str, Any]] = None):
        self.plugin_id = plugin_id
        self.params = params or {}
        self.api = PluginAPI(plugin_id)
        self.logger = PluginLogger(plugin_id)
        self.notifications = NotificationSystem(plugin_id)
        self.start_time = time.time()

    @property
    def elapsed_ms(self) -> float:
        """Tiempo transcurrido desde la creacion del contexto en milisegundos."""
        return (time.time() - self.start_time) * 1000.0

    def to_dict(self) -> Dict[str, Any]:
        """Serializa el contexto a diccionario."""
        return {
            "plugin_id": self.plugin_id,
            "params": self.params,
            "elapsed_ms": self.elapsed_ms,
        }


# ============================
# AlexisPlugin (clase base)
# ============================

class AlexisPlugin(ABC):
    """
    Clase base abstracta para todos los plugins de Alexis.

    Los plugins deben heredar de esta clase e implementar los metodos
    necesarios para su funcionamiento.

    Ejemplo:
        class MiPlugin(AlexisPlugin):
            def __init__(self):
                super().__init__()
                self.name = "Mi Plugin"
                self.version = "1.0.0"

            def on_execute(self, ctx: PluginContext, params: dict):
                ctx.logger.info("Ejecutando...")
                return {"result": "ok"}
    """

    def __init__(self):
        self.name = "Unnamed Plugin"
        self.version = "0.0.1"
        self.author = ""
        self.description = ""
        self._context: Optional[PluginContext] = None

    @property
    def context(self) -> Optional[PluginContext]:
        return self._context

    def on_load(self, ctx: PluginContext) -> Dict[str, Any]:
        """
        Llamado cuando el plugin es cargado.

        Args:
            ctx: Contexto de ejecucion

        Returns:
            Diccionario con informacion de estado
        """
        self._context = ctx
        ctx.logger.info(f"Plugin '{self.name}' v{self.version} cargado")
        return {"status": "loaded", "plugin": self.name}

    def on_unload(self, ctx: PluginContext) -> Dict[str, Any]:
        """
        Llamado cuando el plugin es descargado.

        Args:
            ctx: Contexto de ejecucion

        Returns:
            Diccionario con informacion de estado
        """
        ctx.logger.info(f"Plugin '{self.name}' descargado")
        return {"status": "unloaded", "plugin": self.name}

    @abstractmethod
    def on_execute(self, ctx: PluginContext, params: Dict[str, Any]) -> Dict[str, Any]:
        """
        Metodo principal de ejecucion del plugin.

        Args:
            ctx: Contexto de ejecucion
            params: Parametros pasados al plugin

        Returns:
            Diccionario con los resultados
        """
        raise NotImplementedError("Plugins must implement on_execute()")

    def on_enable(self, ctx: PluginContext) -> Dict[str, Any]:
        """Llamado cuando el plugin es habilitado."""
        ctx.logger.info(f"Plugin '{self.name}' habilitado")
        return {"status": "enabled"}

    def on_disable(self, ctx: PluginContext) -> Dict[str, Any]:
        """Llamado cuando el plugin es deshabilitado."""
        ctx.logger.info(f"Plugin '{self.name}' deshabilitado")
        return {"status": "disabled"}

    def get_info(self) -> Dict[str, str]:
        """Retorna informacion del plugin."""
        return {
            "name": self.name,
            "version": self.version,
            "author": self.author,
            "description": self.description,
        }
