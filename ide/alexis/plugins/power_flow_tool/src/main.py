#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Power Flow Analysis Tool - Plugin de ejemplo para POWSYS365 Alexis

Este plugin ejecuta un flujo de carga completo usando el SDK de Alexis,
visualiza los resultados en formato tabular, y exporta a CSV.

Soporta los metodos:
    - Newton-Raphson
    - Gauss-Seidel
    - Fast Decoupled

Uso:
    python main.py '{"method": "newton_raphson", "export": true}'

Autor: POWSYS365 Team
Version: 1.0.0
"""

import json
import csv
import math
import os
import sys
import time
from pathlib import Path
from typing import Dict, List, Optional, Any

# Agregar el SDK de Alexis al path
ALEXIS_SDK_PATH = Path(__file__).resolve().parents[3] / "sdk"
if str(ALEXIS_SDK_PATH) not in sys.path:
    sys.path.insert(0, str(ALEXIS_SDK_PATH))

from api import PluginAPI, PluginContext, AlexisPlugin
from hooks import on_plugin_load, on_plugin_execute, on_plugin_unload
from permissions import Permission, require_permission


# ============================
# Constantes
# ============================

PLUGIN_ID = "power_flow_tool"
PLUGIN_NAME = "Power Flow Analysis Tool"
PLUGIN_VERSION = "1.0.0"

# Colores ANSI para terminal
class Colors:
    HEADER = "\033[95m"
    BLUE = "\033[94m"
    CYAN = "\033[96m"
    GREEN = "\033[92m"
    YELLOW = "\033[93m"
    RED = "\033[91m"
    ENDC = "\033[0m"
    BOLD = "\033[1m"
    DIM = "\033[2m"


# ============================
# PowerFlowPlugin
# ============================

class PowerFlowPlugin(AlexisPlugin):
    """
    Plugin de analisis de flujo de carga para POWSYS365.

    Ejecuta flujos de carga usando diferentes metodos, visualiza
    resultados y exporta datos a CSV.
    """

    def __init__(self):
        super().__init__()
        self.name = PLUGIN_NAME
        self.version = PLUGIN_VERSION
        self.author = "POWSYS365 Team"
        self.description = "Herramienta de analisis de flujo de carga"
        self.results_dir = Path.home() / "powsy365" / "results"
        self.results_dir.mkdir(parents=True, exist_ok=True)

    def on_load(self, ctx: PluginContext) -> Dict[str, Any]:
        """Inicializacion del plugin."""
        super().on_load(ctx)
        ctx.logger.info(f"{Colors.GREEN}Power Flow Tool v{PLUGIN_VERSION} cargado{Colors.ENDC}")

        # Mostrar info del sistema
        buses = ctx.api.get_buses()
        branches = ctx.api.get_branches()
        generators = ctx.api.get_generators()

        ctx.logger.info(f"Sistema: {len(buses)} barras, {len(branches)} ramas, {len(generators)} generadores")
        return {"status": "loaded", "system": f"{len(buses)} buses, {len(branches)} branches"}

    def on_execute(self, ctx: PluginContext, params: Dict[str, Any]) -> Dict[str, Any]:
        """
        Ejecuta el analisis de flujo de carga.

        Args:
            ctx: Contexto de ejecucion
            params: Parametros de ejecucion
                - method: Metodo de solucion ('newton_raphson', 'gauss_seidel', 'fast_decoupled')
                - export: Si se debe exportar a CSV (bool)
                - export_format: Formato de exportacion ('csv', 'json')
                - visualize: Si se debe mostrar tabla de resultados (bool)

        Returns:
            Diccionario con los resultados
        """
        ctx.logger.info(f"{Colors.CYAN}Iniciando analisis de flujo de carga...{Colors.ENDC}")

        # Extraer parametros
        method = params.get("method", "newton_raphson")
        export = params.get("export", True)
        export_format = params.get("export_format", "csv")
        visualize = params.get("visualize", True)

        ctx.logger.info(f"Metodo: {method}, Export: {export}, Visualize: {visualize}")

        # Validar metodo
        valid_methods = ["newton_raphson", "gauss_seidel", "fast_decoupled"]
        if method not in valid_methods:
            error_msg = f"Metodo invalido: {method}. Use: {valid_methods}"
            ctx.logger.error(error_msg)
            return {"status": "error", "message": error_msg}

        # Ejecutar flujo de carga
        ctx.logger.info(f"{Colors.YELLOW}Ejecutando flujo de carga ({method})...{Colors.ENDC}")

        try:
            result = ctx.api.run_power_flow(
                method=method,
                max_iterations=20,
                tolerance=1e-6
            )
        except Exception as e:
            ctx.logger.error(f"Error en flujo de carga: {e}")
            return {"status": "error", "message": str(e)}

        # Verificar convergencia
        if not result.get("converged", False):
            msg = f"Flujo de carga NO convergio. Mismatch: {result.get('mismatch_mva', 'N/A')}"
            ctx.logger.warning(msg)
            print(f"\n{Colors.RED}{'='*60}{Colors.ENDC}")
            print(f"{Colors.RED}  FLUJO DE CARGA NO CONVERGIO{Colors.ENDC}")
            print(f"{Colors.RED}{'='*60}{Colors.ENDC}")
            print(f"  Metodo: {method}")
            print(f"  Iteraciones: {result.get('iterations', 0)}")
            print(f"  Mismatch final: {result.get('mismatch_mva', 'N/A'):.6f} MVA")
            print(f"{Colors.RED}{'='*60}{Colors.ENDC}\n")

            return {"status": "not_converged", "result": result}

        # Flujo convergio
        ctx.logger.info(f"{Colors.GREEN}Flujo de carga convergio en {result['iterations']} iteraciones{Colors.ENDC}")

        # Visualizar resultados
        if visualize:
            self._visualize_results(ctx, result, method)

        # Exportar resultados
        export_path = None
        if export:
            timestamp = time.strftime("%Y%m%d_%H%M%S")
            if export_format == "csv":
                export_path = self._export_to_csv(ctx, result, timestamp)
            elif export_format == "json":
                export_path = self._export_to_json(ctx, result, timestamp)

        # Publicar evento
        ctx.notifications.publish("power_flow.completed", {
            "method": method,
            "converged": True,
            "iterations": result["iterations"],
            "elapsed_ms": result["elapsed_ms"],
            "total_generation_mw": result.get("total_generation_mw", 0),
            "total_load_mw": result.get("total_load_mw", 0),
            "total_losses_mw": result.get("total_losses_mw", 0),
        })

        return {
            "status": "success",
            "method": method,
            "converged": True,
            "iterations": result["iterations"],
            "elapsed_ms": result["elapsed_ms"],
            "mismatch_mva": result["mismatch_mva"],
            "total_generation_mw": result.get("total_generation_mw", 0),
            "total_load_mw": result.get("total_load_mw", 0),
            "total_losses_mw": result.get("total_losses_mw", 0),
            "export_path": str(export_path) if export_path else None,
            "result": result,
        }

    def on_unload(self, ctx: PluginContext) -> Dict[str, Any]:
        """Limpieza al descargar."""
        ctx.logger.info(f"{Colors.YELLOW}Power Flow Tool descargado{Colors.ENDC}")
        return super().on_unload(ctx)

    # ============================
    # Visualizacion
    # ============================

    def _visualize_results(self, ctx: PluginContext, result: Dict[str, Any],
                           method: str) -> None:
        """Visualiza los resultados del flujo de carga en la terminal."""

        # Header
        print(f"\n{Colors.CYAN}{'='*70}{Colors.ENDC}")
        print(f"{Colors.CYAN}  RESULTADOS DEL FLUJO DE CARGA - {method.upper()}{Colors.ENDC}")
        print(f"{Colors.CYAN}{'='*70}{Colors.ENDC}")
        print(f"  Convergencia: {Colors.GREEN}OK{Colors.ENDC}")
        print(f"  Iteraciones: {result['iterations']}")
        print(f"  Tiempo: {result['elapsed_ms']:.2f} ms")
        print(f"  Mismatch: {result['mismatch_mva']:.2e} MVA")
        print()

        # Resumen de generacion/carga/perdidas
        gen_mw = result.get("total_generation_mw", 0)
        load_mw = result.get("total_load_mw", 0)
        loss_mw = result.get("total_losses_mw", 0)

        print(f"{Colors.YELLOW}  RESUMEN DE POTENCIA:{Colors.ENDC}")
        print(f"    Generacion total: {gen_mw:>10.2f} MW")
        print(f"    Carga total:      {load_mw:>10.2f} MW")
        print(f"    Perdidas:         {loss_mw:>10.2f} MW ({100*loss_mw/gen_mw:.2f}%)")
        print()

        # Tabla de voltajes de barras
        bus_results = result.get("bus_results", [])
        if bus_results:
            print(f"{Colors.YELLOW}  VOLTAJES DE BARRAS:{Colors.ENDC}")
            print(f"  {'ID':>4} {'Nombre':<12} {'Tipo':<8} {'V (pu)':>8} {'Ang (deg)':>10} {'Estado':<10}")
            print(f"  {'-'*4} {'-'*12} {'-'*8} {'-'*8} {'-'*10} {'-'*10}")

            for bus in bus_results:
                bus_id = bus.get("id", 0)
                name = bus.get("name", "")[:12]
                btype = bus.get("bus_type", "PQ")[:8]
                vm = bus.get("vm_pu", 0)
                va = bus.get("va_deg", 0)

                # Color segun voltaje
                if vm > 1.05 or vm < 0.95:
                    color = Colors.RED
                    status = "ALERTA"
                elif vm > 1.02 or vm < 0.98:
                    color = Colors.YELLOW
                    status = "ADVERT"
                else:
                    color = Colors.GREEN
                    status = "OK"

                print(f"  {bus_id:>4} {name:<12} {btype:<8} {color}{vm:>8.4f}{Colors.ENDC} "
                      f"{va:>10.3f} {color}{status:<10}{Colors.ENDC}")
            print()

        # Tabla de generadores
        gen_results = result.get("gen_results", [])
        if gen_results:
            print(f"{Colors.YELLOW}  GENERADORES:{Colors.ENDC}")
            print(f"  {'ID':>4} {'Bus':>4} {'Nombre':<12} {'P (MW)':>10} {'Q (MVAr)':>10} {'Vset (pu)':>10}")
            print(f"  {'-'*4} {'-'*4} {'-'*12} {'-'*10} {'-'*10} {'-'*10}")

            for gen in gen_results:
                gen_id = gen.get("id", 0)
                bus_id = gen.get("bus_id", 0)
                name = gen.get("name", "")[:12]
                pg = gen.get("pg_mw", 0)
                qg = gen.get("qg_mvar", 0)
                vset = gen.get("v_set_pu", 1.0)

                print(f"  {gen_id:>4} {bus_id:>4} {name:<12} {pg:>10.2f} {qg:>10.2f} {vset:>10.4f}")
            print()

        # Ramas con sobrecarga
        branch_results = result.get("branch_results", [])
        if branch_results:
            overloaded = [b for b in branch_results
                         if b.get("loading_percent", 0) > 100]

            if overloaded:
                print(f"{Colors.RED}  RAMAS SOBRECARGADAS:{Colors.ENDC}")
                for br in overloaded:
                    print(f"    {Colors.RED}Branch {br['id']}: {br['from_bus']}->{br['to_bus']} "
                          f"Loading: {br['loading_percent']:.1f}%{Colors.ENDC}")
                print()
            else:
                print(f"  {Colors.GREEN}Sin ramas sobrecargadas.{Colors.ENDC}\n")

        print(f"{Colors.CYAN}{'='*70}{Colors.ENDC}\n")

    # ============================
    # Exportacion
    # ============================

    @require_permission(Permission.FILESYSTEM)
    def _export_to_csv(self, ctx: PluginContext, result: Dict[str, Any],
                       timestamp: str) -> Path:
        """
        Exporta los resultados a formato CSV.

        Args:
            ctx: Contexto de ejecucion
            result: Resultados del flujo de carga
            timestamp: Marca de tiempo para el nombre del archivo

        Returns:
            Ruta al archivo exportado
        """
        export_path = self.results_dir / f"power_flow_{timestamp}.csv"

        try:
            with open(export_path, "w", newline="", encoding="utf-8") as f:
                writer = csv.writer(f)

                # Metadatos
                writer.writerow(["# POWSYS365 Power Flow Results"])
                writer.writerow(["#", f"Method: {result.get('method', 'unknown')}"])
                writer.writerow(["#", f"Iterations: {result.get('iterations', 0)}"])
                writer.writerow(["#", f"Elapsed: {result.get('elapsed_ms', 0):.2f} ms"])
                writer.writerow(["#", f"Converged: {result.get('converged', False)}"])
                writer.writerow(["#", f"Mismatch: {result.get('mismatch_mva', 0):.6f} MVA"])
                writer.writerow([])

                # Barras
                writer.writerow(["## BUSES"])
                writer.writerow(["Bus_ID", "Name", "Type", "Vm_pu", "Va_deg",
                                "Base_kV", "Vmax", "Vmin", "Area", "Zone"])

                for bus in result.get("bus_results", []):
                    writer.writerow([
                        bus.get("id", ""),
                        bus.get("name", ""),
                        bus.get("bus_type", ""),
                        f"{bus.get('vm_pu', 0):.6f}",
                        f"{bus.get('va_deg', 0):.4f}",
                        bus.get("base_kv", 0),
                        bus.get("vmax", 1.1),
                        bus.get("vmin", 0.9),
                        bus.get("area", 1),
                        bus.get("zone", 1),
                    ])
                writer.writerow([])

                # Ramas
                writer.writerow(["## BRANCHES"])
                writer.writerow(["Branch_ID", "From_Bus", "To_Bus", "R_pu", "X_pu",
                                "B_pu", "Rate_A", "P_from_MW", "Q_from_MVAr",
                                "P_to_MW", "Q_to_MVAr", "Loading_%"])

                for br in result.get("branch_results", []):
                    writer.writerow([
                        br.get("id", ""),
                        br.get("from_bus", ""),
                        br.get("to_bus", ""),
                        f"{br.get('r_pu', 0):.6f}",
                        f"{br.get('x_pu', 0):.6f}",
                        f"{br.get('b_pu', 0):.6f}",
                        br.get("rate_a", 0),
                        f"{br.get('p_from', 0):.4f}",
                        f"{br.get('q_from', 0):.4f}",
                        f"{br.get('p_to', 0):.4f}",
                        f"{br.get('q_to', 0):.4f}",
                        f"{br.get('loading_percent', 0):.2f}",
                    ])
                writer.writerow([])

                # Generadores
                writer.writerow(["## GENERATORS"])
                writer.writerow(["Gen_ID", "Bus_ID", "Name", "Pg_MW", "Qg_MVAr",
                                "Qmax_MVAr", "Qmin_MVAr", "Vset_pu", "Status"])

                for gen in result.get("gen_results", []):
                    writer.writerow([
                        gen.get("id", ""),
                        gen.get("bus_id", ""),
                        gen.get("name", ""),
                        f"{gen.get('pg_mw', 0):.4f}",
                        f"{gen.get('qg_mvar', 0):.4f}",
                        gen.get("q_max_mvar", 0),
                        gen.get("q_min_mvar", 0),
                        f"{gen.get('v_set_pu', 1.0):.4f}",
                        gen.get("status", "COMMITTED"),
                    ])
                writer.writerow([])

                # Resumen
                writer.writerow(["## SUMMARY"])
                writer.writerow(["Metric", "Value"])
                writer.writerow(["Total_Generation_MW", f"{result.get('total_generation_mw', 0):.4f}"])
                writer.writerow(["Total_Load_MW", f"{result.get('total_load_mw', 0):.4f}"])
                writer.writerow(["Total_Losses_MW", f"{result.get('total_losses_mw', 0):.4f}"])
                writer.writerow(["Generation_MVAR", f"{result.get('total_generation_mvar', 0):.4f}"])
                writer.writerow(["Load_MVAR", f"{result.get('total_load_mvar', 0):.4f}"])
                writer.writerow(["Losses_MVAR", f"{result.get('total_losses_mvar', 0):.4f}"])

            ctx.logger.info(f"{Colors.GREEN}Resultados exportados a: {export_path}{Colors.ENDC}")
            print(f"  {Colors.GREEN}Exportado: {export_path}{Colors.ENDC}")

        except Exception as e:
            ctx.logger.error(f"Error exportando CSV: {e}")
            raise

        return export_path

    @require_permission(Permission.FILESYSTEM)
    def _export_to_json(self, ctx: PluginContext, result: Dict[str, Any],
                        timestamp: str) -> Path:
        """
        Exporta los resultados a formato JSON.

        Args:
            ctx: Contexto de ejecucion
            result: Resultados del flujo de carga
            timestamp: Marca de tiempo

        Returns:
            Ruta al archivo exportado
        """
        export_path = self.results_dir / f"power_flow_{timestamp}.json"

        try:
            export_data = {
                "metadata": {
                    "plugin": PLUGIN_NAME,
                    "version": PLUGIN_VERSION,
                    "timestamp": timestamp,
                    "method": result.get("method", "unknown"),
                    "converged": result.get("converged", False),
                    "iterations": result.get("iterations", 0),
                    "elapsed_ms": result.get("elapsed_ms", 0),
                    "mismatch_mva": result.get("mismatch_mva", 0),
                },
                "summary": {
                    "total_generation_mw": result.get("total_generation_mw", 0),
                    "total_load_mw": result.get("total_load_mw", 0),
                    "total_losses_mw": result.get("total_losses_mw", 0),
                    "total_generation_mvar": result.get("total_generation_mvar", 0),
                    "total_load_mvar": result.get("total_load_mvar", 0),
                    "total_losses_mvar": result.get("total_losses_mvar", 0),
                },
                "buses": result.get("bus_results", []),
                "branches": result.get("branch_results", []),
                "generators": result.get("gen_results", []),
            }

            with open(export_path, "w", encoding="utf-8") as f:
                json.dump(export_data, f, indent=2, default=str)

            ctx.logger.info(f"{Colors.GREEN}Resultados exportados a: {export_path}{Colors.ENDC}")
            print(f"  {Colors.GREEN}Exportado: {export_path}{Colors.ENDC}")

        except Exception as e:
            ctx.logger.error(f"Error exportando JSON: {e}")
            raise

        return export_path


# ============================
# Entry Point
# ============================

def main():
    """
    Punto de entrada principal del plugin.

    Lee los parametros de ejecucion y ejecuta el analisis.
    """
    # Leer parametros del entorno
    params_str = os.environ.get("POWSYS365_PLUGIN_PARAMS", "{}")

    # Si se pasaron argumentos de linea de comandos, usarlos
    if len(sys.argv) > 1:
        try:
            cli_params = json.loads(sys.argv[1])
            if isinstance(cli_params, dict):
                params_str = sys.argv[1]
        except json.JSONDecodeError:
            pass

    try:
        params = json.loads(params_str)
    except json.JSONDecodeError:
        params = {}

    # Crear contexto
    ctx = PluginContext(plugin_id=PLUGIN_ID, params=params)

    # Crear y ejecutar plugin
    plugin = PowerFlowPlugin()

    # Ciclo de vida
    plugin.on_load(ctx)
    result = plugin.on_execute(ctx, params)
    plugin.on_unload(ctx)

    # Imprimir resultado final como JSON
    print(json.dumps(result, indent=2, default=str))

    # Codigo de salida
    sys.exit(0 if result.get("status") == "success" else 1)


if __name__ == "__main__":
    main()
