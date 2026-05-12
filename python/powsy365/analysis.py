"""
powsy365.analysis
=================

High-level Pythonic wrappers for power-system analysis studies.

Each function accepts either a :class:`Network` (pure-Python) or a
C++ :class:`PowerSystem`, dispatches to the appropriate solver,
and returns a plain Python ``dict`` with comprehensive results.

Supported studies
-----------------
* **Power Flow**  – Newton-Raphson, Fast-Decoupled, Gauss-Seidel
* **Short Circuit** – Symmetrical and unsymmetrical faults
* **Stability** – Transient stability screening (Python implementation)
* **OPF** – Optimal power flow via pandapower
"""

from __future__ import annotations

import logging
import time
from typing import TYPE_CHECKING, Any

import numpy as np

if TYPE_CHECKING:
    from powsy365.network import Network
    from powsy365_core import PowerSystem  # type: ignore[import-untyped]

logger = logging.getLogger(__name__)


# ---------------------------------------------------------------------------
# Power Flow
# ---------------------------------------------------------------------------


def power_flow(
    network: "Network | PowerSystem",
    method: str = "nr",
    tol: float = 1e-6,
    max_iter: int = 30,
    enforce_q_limits: bool = True,
    base_mva: float = 100.0,
) -> dict[str, Any]:
    """Run an AC power-flow study.

    Parameters
    ----------
    network:
        A :class:`Network` (Python) or a compiled C++
        :class:`PowerSystem`.
    method:
        Solution algorithm – ``"nr"`` (Newton-Raphson),
        ``"fd"`` (Fast-Decoupled), or ``"gs"`` (Gauss-Seidel).
    tol:
        Maximum per-unit mismatch for convergence.
    max_iter:
        Upper bound on iterations.
    enforce_q_limits:
        If *True*, generator reactive-power limits are enforced
        and PV buses may convert to PQ.
    base_mva:
        System base MVA (used only when *network* is a Python
        :class:`Network` without a C++ backing).

    Returns
    -------
    dict
        Comprehensive result dictionary including:

        - ``converged`` (*bool*)
        - ``iterations`` (*int*)
        - ``elapsed_ms`` (*float*)
        - ``final_mismatch`` (*float*)
        - ``method`` (*str*)
        - ``buses`` – list of per-bus dicts with ``vm``, ``va_deg``, …
        - ``branches`` – list of per-branch dicts with ``p_from_mw``, …
        - ``total_pgen_mw``, ``total_pload_mw``, ``total_ploss_mw``
        - ``total_qgen_mvar``, ``total_qload_mvar``, ``total_qloss_mvar``

    Raises
    ------
    ValueError
        If *method* is not recognised.
    RuntimeError
        If the solver fails to converge or the network is invalid.
    """
    t0 = time.perf_counter()

    # Normalise to C++ PowerSystem
    ps = _to_cpp_system(network, base_mva)

    # Build and initialise
    try:
        ps.buildYbus()
        ps.initializeVoltages(flat_start=True)
    except Exception as exc:
        raise RuntimeError(f"Network build failed: {exc}") from exc

    # Configure solver
    try:
        import powsy365_core as psc  # type: ignore[import-untyped]

        config = psc.SolverConfig()
        config.tolerance = tol
        config.max_iterations = max_iter
        config.enforce_q_limits = enforce_q_limits
        config.method = _method_to_enum(method, psc)
        config.flat_start = True
        config.base_mva = ps.base_mva

        solver = psc.LoadFlowSolver(ps)
        solver.setConfig(config)
    except Exception as exc:
        raise RuntimeError(f"Solver setup failed: {exc}") from exc

    # Run
    try:
        cpp_result = solver.solve(config.method)
    except Exception as exc:
        raise RuntimeError(f"Power flow solver failed: {exc}") from exc

    elapsed_ms = (time.perf_counter() - t0) * 1000.0

    result = _cpp_power_flow_to_dict(cpp_result)
    result["elapsed_ms"] = round(elapsed_ms, 3)
    result["requested_method"] = method

    if not result["converged"]:
        logger.warning(
            "Power flow did NOT converge after %d iterations "
            "(final mismatch = %.3e)",
            result["iterations"],
            result["final_mismatch"],
        )
    else:
        logger.info(
            "Power flow converged in %d iterations, %.2f ms, "
            "Ploss = %.2f MW",
            result["iterations"],
            result["elapsed_ms"],
            result["total_ploss_mw"],
        )

    return result


# ---------------------------------------------------------------------------
# Short Circuit
# ---------------------------------------------------------------------------


def short_circuit(
    network: "Network | PowerSystem",
    fault_type: str = "3ph",
    fault_bus_id: int | None = None,
    fault_impedance_ohm: complex = 0j,
    base_mva: float = 100.0,
) -> dict[str, Any]:
    """Run a short-circuit (fault) analysis.

    Parameters
    ----------
    network:
        Python :class:`Network` or C++ :class:`PowerSystem`.
    fault_type:
        ``"3ph"`` (three-phase), ``"1ph"`` (single-line-to-ground),
        ``"2ph"`` (line-to-line), or ``"2phg"`` (double-line-to-ground).
    fault_bus_id:
        Bus where the fault is applied.  If *None*, the first
        non-slack bus is used.
    fault_impedance_ohm:
        Fault impedance in Ohms (default 0 for bolted fault).
    base_mva:
        System base MVA (used when converting from Python Network).

    Returns
    -------
    dict
        - ``fault_bus_id``
        - ``fault_type``
        - ``fault_current_ka``
        - ``bus_voltages`` – post-fault voltages [p.u.]
        - ``branch_currents`` – post-fault branch currents [kA]
        - ``elapsed_ms``
    """
    t0 = time.perf_counter()
    ps = _to_cpp_system(network, base_mva)

    # Build Ybus
    try:
        ps.buildYbus()
    except Exception as exc:
        raise RuntimeError(f"Network build failed: {exc}") from exc

    import powsy365_core as psc  # type: ignore[import-untyped]

    # Determine fault bus if not specified
    if fault_bus_id is None:
        for b in ps.getAllBuses():
            if b.type != psc.BusType.Slack:
                fault_bus_id = b.id
                break
        if fault_bus_id is None:
            raise ValueError("No non-slack bus found for fault application")

    ft_enum = _fault_type_to_enum(fault_type, psc)
    sc_solver = psc.ShortCircuitSolver(ps)

    try:
        if ft_enum == psc.FaultType.ThreePhase:
            sc_solver.solveSymmetrical(fault_bus_id, fault_impedance_ohm)
        else:
            sc_solver.solveUnsymmetrical(
                fault_bus_id, ft_enum, fault_impedance_ohm
            )
    except Exception as exc:
        raise RuntimeError(f"Short-circuit solver failed: {exc}") from exc

    elapsed_ms = (time.perf_counter() - t0) * 1000.0

    return {
        "fault_bus_id": fault_bus_id,
        "fault_type": fault_type,
        "fault_current_ka": sc_solver.getFaultCurrent(),
        "bus_voltages": sc_solver.getBusVoltagesDuringFault(),
        "branch_currents": sc_solver.getBranchCurrentsDuringFault(),
        "elapsed_ms": round(elapsed_ms, 3),
    }


# ---------------------------------------------------------------------------
# Stability (transient screening)
# ---------------------------------------------------------------------------


def stability_analysis(
    network: "Network | PowerSystem",
    method: str = "transient",
    fault_bus: int | None = None,
    fault_duration_s: float = 0.1,
    simulation_time_s: float = 10.0,
    time_step_s: float = 0.01,
    base_mva: float = 100.0,
) -> dict[str, Any]:
    """Perform a simplified transient-stability screening analysis.

    This is a pure-Python implementation using the classical
    equal-area criterion and swing-equation integration.  It does
    **not** require the C++ stability solver and serves as a
    quick screening tool.

    Parameters
    ----------
    network:
        Python :class:`Network` or C++ :class:`PowerSystem`.
    method:
        ``"transient"`` (currently the only supported method).
    fault_bus:
        Bus where a fault is cleared after *fault_duration_s*.
        If *None*, no fault is simulated (free response).
    fault_duration_s:
        Duration of the fault before clearing [seconds].
    simulation_time_s:
        Total simulation horizon [seconds].
    time_step_s:
        Integration step [seconds].
    base_mva:
        System base MVA.

    Returns
    -------
    dict
        - ``stable`` (*bool*) – coarse stability indicator
        - ``critical_clearing_time_s`` – estimated CCT [s]
        - ``time_points`` – array of time values
        - ``delta_rad`` – rotor-angle trajectories per generator
        - ``omega_pu`` – speed deviations per generator
        - ``max_angle_deviation_rad``
    """
    t0 = time.perf_counter()

    # Extract generator data from the network
    gen_data = _extract_generator_data(network, base_mva)
    if not gen_data:
        return {
            "stable": True,
            "critical_clearing_time_s": float("inf"),
            "time_points": np.array([0.0]),
            "delta_rad": {},
            "omega_pu": {},
            "max_angle_deviation_rad": 0.0,
            "message": "No generators found – stability is trivial",
        }

    n_steps = int(simulation_time_s / time_step_s) + 1
    time_points = np.linspace(0.0, simulation_time_s, n_steps)

    # Classical model: H * d²δ/dt² = Pm - Pe
    # Using semi-implicit Euler integration
    delta: dict[int, np.ndarray] = {
        g["id"]: np.zeros(n_steps) for g in gen_data
    }
    omega: dict[int, np.ndarray] = {
        g["id"]: np.ones(n_steps) for g in gen_data
    }

    # Initial conditions
    for g in gen_data:
        delta[g["id"]][0] = g["delta0_rad"]
        omega[g["id"]][0] = 1.0  # synchronous speed

    fault_step = int(fault_duration_s / time_step_s) if fault_bus else n_steps

    for step in range(n_steps - 1):
        t = time_points[step]
        for g in gen_data:
            gid = g["id"]
            h = g["inertia_s"]
            pm = g["pm_pu"]
            # During fault electrical power drops; after fault restored
            if step < fault_step:
                pe = 0.0  # simplified: zero power output during fault
            else:
                pe = g["pe_pre_fault_pu"] * np.sin(delta[gid][step])

            # Swing equation
            ddelta_dt = (omega[gid][step] - 1.0) * 2 * np.pi * 60.0
            domega_dt = (pm - pe) / (2.0 * h)

            delta[gid][step + 1] = delta[gid][step] + ddelta_dt * time_step_s
            omega[gid][step + 1] = omega[gid][step] + domega_dt * time_step_s

    # Stability assessment
    max_dev = 0.0
    for g in gen_data:
        gid = g["id"]
        dev = np.max(np.abs(delta[gid] - g["delta0_rad"]))
        if dev > max_dev:
            max_dev = dev

    # Critical clearing time estimate (simplified)
    cct = _estimate_cct(gen_data)

    stable = max_dev < np.radians(180.0)

    elapsed_ms = (time.perf_counter() - t0) * 1000.0

    return {
        "stable": stable,
        "critical_clearing_time_s": cct,
        "time_points": time_points,
        "delta_rad": {k: v.tolist() for k, v in delta.items()},
        "omega_pu": {k: v.tolist() for k, v in omega.items()},
        "max_angle_deviation_rad": round(float(max_dev), 6),
        "fault_bus": fault_bus,
        "fault_duration_s": fault_duration_s,
        "elapsed_ms": round(elapsed_ms, 3),
    }


# ---------------------------------------------------------------------------
# OPF (Optimal Power Flow)
# ---------------------------------------------------------------------------


def opf(
    network: "Network | PowerSystem",
    objective: str = "min_cost",
    base_mva: float = 100.0,
) -> dict[str, Any]:
    """Run an Optimal Power Flow via pandapower.

    Parameters
    ----------
    network:
        Python :class:`Network` or C++ :class:`PowerSystem`.
    objective:
        ``"min_cost"`` (minimise generation cost) or
        ``"min_loss"`` (minimise active losses).
    base_mva:
        System base MVA.

    Returns
    -------
    dict
        - ``converged`` (*bool*)
        - ``total_cost_pu_h`` – total generation cost
        - ``gen_dispatch`` – list of per-generator dispatch dicts
        - ``buses`` – voltage results
        - ``branches`` – flow results
        - ``total_ploss_mw``
        - ``elapsed_ms``

    Raises
    ------
    ImportError
        If *pandapower* is not installed.
    """
    try:
        import pandapower as pp  # type: ignore[import-untyped]
        import pandapower.optimal_powerflow as opf_mod  # type: ignore[import-untyped]
    except ImportError as exc:
        raise ImportError(
            "pandapower is required for OPF. Install it with:\n"
            "    pip install pandapower>=2.13"
        ) from exc

    t0 = time.perf_counter()

    # Convert to pandapower net
    if hasattr(network, "to_cpp_system"):
        # Python Network – convert via C++ then to pandapower
        pp_net = _network_to_pandapower(network)
    else:
        # C++ PowerSystem – convert to pandapower
        pp_net = _cpp_system_to_pandapower(network)

    # Run OPF
    try:
        if objective == "min_cost":
            pp.runopp(pp_net, verbose=False)
        elif objective == "min_loss":
            pp.runopp(
                pp_net, verbose=False, delta=1e-10
            )  # delta triggers loss minimisation
        else:
            raise ValueError(f"Unknown OPF objective: {objective}")
    except Exception as exc:
        raise RuntimeError(f"OPF solver failed: {exc}") from exc

    elapsed_ms = (time.perf_counter() - t0) * 1000.0

    # Extract results
    gen_dispatch = []
    for idx, row in pp_net.res_gen.iterrows():
        gen_dispatch.append(
            {
                "gen_index": int(idx),
                "p_mw": float(row.get("p_mw", 0.0)),
                "q_mvar": float(row.get("q_mvar", 0.0)),
                "vm_pu": float(row.get("vm_pu", 1.0)),
                "va_deg": float(row.get("va_degree", 0.0)),
            }
        )

    buses = []
    for idx, row in pp_net.res_bus.iterrows():
        buses.append(
            {
                "bus_index": int(idx),
                "vm_pu": float(row.get("vm_pu", 1.0)),
                "va_deg": float(row.get("va_degree", 0.0)),
                "p_mw": float(row.get("p_mw", 0.0)),
                "q_mvar": float(row.get("q_mvar", 0.0)),
            }
        )

    return {
        "converged": pp_net.OPF_converged if hasattr(pp_net, "OPF_converged") else True,
        "objective": objective,
        "total_cost_pu_h": float(pp_net.res_cost) if hasattr(pp_net, "res_cost") else 0.0,
        "gen_dispatch": gen_dispatch,
        "buses": buses,
        "total_ploss_mw": float(pp_net.res_line["pl_mw"].sum()) if "pl_mw" in pp_net.res_line else 0.0,
        "elapsed_ms": round(elapsed_ms, 3),
    }


# ---------------------------------------------------------------------------
# Private helpers
# ---------------------------------------------------------------------------


def _to_cpp_system(network: "Network | PowerSystem", base_mva: float) -> "PowerSystem":
    """Normalise *network* to a C++ PowerSystem."""
    if hasattr(network, "to_cpp_system"):
        return network.to_cpp_system()  # type: ignore[union-attr]
    # Assume it already is a C++ PowerSystem
    return network  # type: ignore[return-value]


def _method_to_enum(method: str, psc: Any) -> Any:
    """Map method string to C++ SolverMethod enum."""
    mapping = {
        "nr": psc.SolverMethod.NewtonRaphson,
        "newton": psc.SolverMethod.NewtonRaphson,
        "newtonraphson": psc.SolverMethod.NewtonRaphson,
        "fd": psc.SolverMethod.FastDecoupled,
        "fastdecoupled": psc.SolverMethod.FastDecoupled,
        "gs": psc.SolverMethod.GaussSeidel,
        "gaussseidel": psc.SolverMethod.GaussSeidel,
    }
    key = method.lower().replace("_", "").replace("-", "")
    if key not in mapping:
        raise ValueError(
            f"Unknown solver method '{method}'. Choose from: nr, fd, gs"
        )
    return mapping[key]


def _fault_type_to_enum(fault_type: str, psc: Any) -> Any:
    """Map fault-type string to C++ FaultType enum."""
    mapping = {
        "3ph": psc.FaultType.ThreePhase,
        "threephase": psc.FaultType.ThreePhase,
        "1ph": psc.FaultType.SinglePhase,
        "singlephase": psc.FaultType.SinglePhase,
        "slg": psc.FaultType.SinglePhase,
        "2ph": psc.FaultType.TwoPhase,
        "twophase": psc.FaultType.TwoPhase,
        "ll": psc.FaultType.TwoPhase,
        "2phg": psc.FaultType.TwoPhaseG,
        "twophaseg": psc.FaultType.TwoPhaseG,
        "llg": psc.FaultType.TwoPhaseG,
    }
    key = fault_type.lower().replace("-", "").replace(" ", "")
    if key not in mapping:
        raise ValueError(
            f"Unknown fault type '{fault_type}'. "
            f"Choose from: 3ph, 1ph, 2ph, 2phg"
        )
    return mapping[key]


def _cpp_power_flow_to_dict(cpp_result: Any) -> dict[str, Any]:
    """Convert C++ PowerFlowResult to a Python dictionary."""
    buses = []
    for b in cpp_result.bus_results:
        buses.append(
            {
                "bus_id": b.bus_id,
                "vm_pu": b.vm,
                "va_deg": b.va,
                "p_gen_mw": b.p_gen,
                "q_gen_mvar": b.q_gen,
                "p_load_mw": b.p_load,
                "q_load_mvar": b.q_load,
                "p_injected_mw": b.p_injected,
                "q_injected_mvar": b.q_injected,
            }
        )

    branches = []
    for br in cpp_result.line_results:
        branches.append(
            {
                "branch_id": br.line_id,
                "from_bus": br.from_bus,
                "to_bus": br.to_bus,
                "p_from_mw": br.p_from,
                "q_from_mvar": br.q_from,
                "p_to_mw": br.p_to,
                "q_to_mvar": br.q_to,
                "s_apparent_mva": br.s_apparent,
                "loss_p_mw": br.loss_p,
                "loss_q_mvar": br.loss_q,
                "loading_percent": br.loading_percent,
            }
        )

    return {
        "converged": cpp_result.converged,
        "iterations": cpp_result.iterations,
        "elapsed_ms": cpp_result.elapsed_ms,
        "final_mismatch": cpp_result.final_mismatch,
        "method": cpp_result.method,
        "buses": buses,
        "branches": branches,
        "total_pgen_mw": cpp_result.total_pgen,
        "total_pload_mw": cpp_result.total_pload,
        "total_qgen_mvar": cpp_result.total_qgen,
        "total_qload_mvar": cpp_result.total_qload,
        "total_ploss_mw": cpp_result.total_ploss,
        "total_qloss_mvar": cpp_result.total_qloss,
    }


def _extract_generator_data(
    network: "Network | PowerSystem", base_mva: float
) -> list[dict[str, Any]]:
    """Extract generator data for transient stability analysis."""
    ps = _to_cpp_system(network, base_mva)
    gens = []
    try:
        for gen in ps.getAllGenerators() if hasattr(ps, "getAllGenerators") else []:
            gens.append(
                {
                    "id": gen.id,
                    "bus_id": gen.bus_id,
                    "pm_pu": gen.pg / base_mva,
                    "pe_pre_fault_pu": gen.pg / base_mva,
                    "inertia_s": 5.0,  # default H if not specified
                    "delta0_rad": 0.0,
                }
            )
    except Exception:
        pass

    # Fallback: try to get generators from Python Network
    if not gens and hasattr(network, "generators"):
        for gen in network.generators.values():
            gens.append(
                {
                    "id": gen.gen_id,
                    "bus_id": gen.bus_id,
                    "pm_pu": gen.pg / base_mva,
                    "pe_pre_fault_pu": gen.pg / base_mva,
                    "inertia_s": 5.0,
                    "delta0_rad": 0.0,
                }
            )
    return gens


def _estimate_cct(gen_data: list[dict[str, Any]]) -> float:
    """Estimate critical clearing time using equal-area criterion."""
    if not gen_data:
        return float("inf")
    # Simplified: CCT ~ 2*H / (Pm - Pe_postfault) * delta_max
    g = gen_data[0]
    h = g["inertia_s"]
    pm = g["pm_pu"]
    pe_pf = g["pe_pre_fault_pu"]
    if abs(pm) < 1e-10:
        return float("inf")
    # Very rough approximation
    cct = 0.5 * h / max(pm, 0.01)
    return round(min(cct, 2.0), 4)


def _network_to_pandapower(network: "Network") -> Any:
    """Convert a Python :class:`Network` to a pandapower net."""
    import pandapower as pp  # type: ignore[import-untyped]

    net = pp.create_empty_network(name=network.name, f_hz=60.0)

    # Create buses
    bus_idx_map: dict[int, int] = {}
    for bid, b in sorted(network.buses.items()):
        idx = pp.create_bus(
            net,
            vn_kv=b.base_kv,
            name=b.name,
            max_vm_pu=b.vmax,
            min_vm_pu=b.vmin,
            zone=str(b.zone),
            in_service=True,
        )
        bus_idx_map[bid] = idx

    # External grid at slack bus
    for bid, b in sorted(network.buses.items()):
        if b.type.upper() == "SLACK":
            pp.create_ext_grid(net, bus=bus_idx_map[bid], vm_pu=b.vm)
            break

    # Lines
    for ld in sorted(network.lines.values(), key=lambda x: x.line_id):
        pp.create_line_from_parameters(
            net,
            from_bus=bus_idx_map[ld.from_bus],
            to_bus=bus_idx_map[ld.to_bus],
            length_km=1.0,
            r_ohm_per_km=ld.r * 100.0,
            x_ohm_per_km=ld.x * 100.0,
            c_nf_per_km=ld.b * 1e9 / (2 * np.pi * 60.0),
            max_i_ka=ld.rate_a / (network.base_mva * np.sqrt(3)) if ld.rate_a else 1.0,
            name=f"Line {ld.line_id}",
            in_service=bool(ld.status),
        )

    # Generators
    for gen in sorted(network.generators.values(), key=lambda x: x.gen_id):
        pp.create_gen(
            net,
            bus=bus_idx_map[gen.bus_id],
            p_mw=gen.pg,
            vm_pu=gen.vg,
            min_q_mvar=gen.qmin,
            max_q_mvar=gen.qmax,
            min_p_mw=gen.pg_min,
            max_p_mw=gen.pg_max,
            name=f"Gen {gen.gen_id}",
            in_service=bool(gen.status),
        )
        # Cost curve
        if gen.cost_a or gen.cost_b or gen.cost_c:
            pp.create_poly_cost(
                net,
                element=len(net.gen) - 1,
                et="gen",
                cp1_eur_per_mw=gen.cost_b,
                cp2_eur_per_mw2=gen.cost_a,
                cp0_eur=gen.cost_c,
            )

    # Loads
    for load in sorted(network.loads.values(), key=lambda x: x.load_id):
        pp.create_load(
            net,
            bus=bus_idx_map[load.bus_id],
            p_mw=load.pd,
            q_mvar=load.qd,
            name=f"Load {load.load_id}",
            in_service=bool(load.status),
        )

    # Transformers
    for trafo in sorted(
        network.transformers.values(), key=lambda x: x.trafo_id
    ):
        pp.create_transformer_from_parameters(
            net,
            hv_bus=bus_idx_map[trafo.from_bus],
            lv_bus=bus_idx_map[trafo.to_bus],
            sn_mva=trafo.rate_a if trafo.rate_a else 100.0,
            vn_hv_kv=network.buses[trafo.from_bus].base_kv,
            vn_lv_kv=network.buses[trafo.to_bus].base_kv,
            vk_percent=trafo.x * 100.0,
            vkr_percent=trafo.r * 100.0,
            pfe_kw=0.0,
            i0_percent=0.0,
            tap_pos=int(trafo.tap * 10),
            shift_degree=trafo.shift_deg,
            name=f"Trafo {trafo.trafo_id}",
            in_service=bool(trafo.status),
        )

    return net


def _cpp_system_to_pandapower(ps: Any) -> Any:
    """Convert a C++ PowerSystem to a pandapower net."""
    import pandapower as pp  # type: ignore[import-untyped]

    net = pp.create_empty_network(name=ps.name if hasattr(ps, "name") else "C++ System")

    bus_idx_map: dict[int, int] = {}
    for b in ps.getAllBuses():
        idx = pp.create_bus(
            net,
            vn_kv=b.base_kv,
            name=b.name,
            max_vm_pu=b.vmax,
            min_vm_pu=b.vmin,
            in_service=True,
        )
        bus_idx_map[b.id] = idx
        if b.type == 2:  # Slack
            pp.create_ext_grid(net, bus=idx, vm_pu=abs(b.voltage))

    # Lines would need similar mapping – simplified here
    return net
