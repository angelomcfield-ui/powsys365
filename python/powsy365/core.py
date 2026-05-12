"""
powsy365.core
=============

Pythonic wrappers around the C++ ``powsy365_core`` extension module.

This module provides:
* Factory functions for IEEE test cases
* Safe construction patterns with validation
* Exception translation from C++ errors to Python exceptions
* Context-manager support for resource cleanup
"""

from __future__ import annotations

import contextlib
import logging
import time
from dataclasses import dataclass, field
from typing import TYPE_CHECKING, Any, Self

logger = logging.getLogger(__name__)

if TYPE_CHECKING:
    from powsy365_core import (  # type: ignore[import-untyped]
        Bus,
        Generator,
        Line,
        Load,
        PowerSystem,
        PowerFlowResult,
        SolverConfig,
        Transformer,
        BusType,
        FaultType,
        SolverMethod,
    )


# ---------------------------------------------------------------------------
# Exceptions
# ---------------------------------------------------------------------------


class PowerSystemError(Exception):
    """Base exception for POWSYS365 power-system operations."""

    def __init__(self, message: str, details: dict[str, Any] | None = None) -> None:
        super().__init__(message)
        self.message = message
        self.details = details or {}


class ValidationError(PowerSystemError):
    """Raised when power-system data fails consistency checks."""


class SolverError(PowerSystemError):
    """Raised when a numerical solver fails to converge or crashes."""


class NetworkNotBuiltError(PowerSystemError):
    """Raised when an operation requires a built network that is missing."""


# ---------------------------------------------------------------------------
# Safe wrapper for PowerSystem
# ---------------------------------------------------------------------------


@dataclass
class SystemContext:
    """Holds a :class:`PowerSystem` together with solver configuration.

    This dataclass keeps the Python-side configuration in sync with the
    C++ engine and provides a unified interface for running studies.
    """

    system: "PowerSystem"
    config: "SolverConfig" = field(default_factory=lambda: _make_default_config())
    _built: bool = field(default=False, repr=False)

    @classmethod
    def from_ieee14(cls, base_mva: float = 100.0) -> Self:
        """Create context populated with the IEEE 14-bus test case."""
        from powsy365_core import PowerSystem  # type: ignore[import-untyped]

        ps = PowerSystem(base_mva)
        ps.loadIEEE14()
        ctx = cls(system=ps)
        ctx._validate("IEEE14")
        return ctx

    @classmethod
    def from_ieee30(cls, base_mva: float = 100.0) -> Self:
        """Create context populated with the IEEE 30-bus test case."""
        from powsy365_core import PowerSystem  # type: ignore[import-untyped]

        ps = PowerSystem(base_mva)
        ps.loadIEEE30()
        ctx = cls(system=ps)
        ctx._validate("IEEE30")
        return ctx

    @classmethod
    def from_ieee57(cls, base_mva: float = 100.0) -> Self:
        """Create context populated with the IEEE 57-bus test case."""
        from powsy365_core import PowerSystem  # type: ignore[import-untyped]

        ps = PowerSystem(base_mva)
        ps.loadIEEE57()
        ctx = cls(system=ps)
        ctx._validate("IEEE57")
        return ctx

    @classmethod
    def from_ieee118(cls, base_mva: float = 100.0) -> Self:
        """Create context populated with the IEEE 118-bus test case."""
        from powsy365_core import PowerSystem  # type: ignore[import-untyped]

        ps = PowerSystem(base_mva)
        ps.loadIEEE118()
        ctx = cls(system=ps)
        ctx._validate("IEEE118")
        return ctx

    # -- internal helpers -------------------------------------------------

    def _validate(self, case_name: str) -> None:
        """Run post-load validation with detailed error reporting."""
        if not self.system.isValid():
            bad_buses = self.system.checkVoltageLimits()
            details = {
                "case": case_name,
                "bus_count": self.system.getBusCount(),
                "line_count": self.system.getLineCount(),
                "gen_count": self.system.getGeneratorCount(),
                "voltage_violations": [b.id for b in bad_buses],
            }
            raise ValidationError(
                f"{case_name} model failed validation", details=details
            )
        logger.info(
            "%s loaded: %d buses, %d lines, %d generators",
            case_name,
            self.system.getBusCount(),
            self.system.getLineCount(),
            self.system.getGeneratorCount(),
        )

    def build(self) -> None:
        """Build Ybus and initialize voltages (idempotent)."""
        if self._built:
            return
        try:
            self.system.buildYbus()
            self.system.initializeVoltages(self.config.flat_start)
        except Exception as exc:
            raise NetworkNotBuiltError(f"Failed to build network: {exc}") from exc
        self._built = True
        logger.debug("Network built successfully (Ybus + initial voltages)")

    # -- context manager --------------------------------------------------

    def __enter__(self) -> Self:
        self.build()
        return self

    def __exit__(self, *exc_info: object) -> None:
        """Cleanup resources if needed (currently a no-op)."""
        return None

    # -- solver dispatch --------------------------------------------------

    def solve_power_flow(
        self,
        method: str = "nr",
        tol: float = 1e-6,
        max_iter: int = 30,
        enforce_q_limits: bool = True,
    ) -> dict[str, Any]:
        """Run power flow and return a Python-friendly result dictionary.

        Parameters
        ----------
        method:
            ``"nr"`` (Newton-Raphson), ``"fd"`` (Fast-Decoupled),
            or ``"gs"`` (Gauss-Seidel).
        tol:
            Convergence tolerance for mismatch.
        max_iter:
            Maximum number of iterations.
        enforce_q_limits:
            Enforce generator reactive-power limits.

        Returns
        -------
        dict
            Dictionary with keys: ``converged``, ``iterations``,
            ``elapsed_ms``, ``final_mismatch``, ``buses``, ``branches``,
            ``total_pgen``, ``total_pload``, ``total_ploss``, etc.
        """
        from powsy365_core import (  # type: ignore[import-untyped]
            LoadFlowSolver,
            SolverMethod,
        )

        self.config.tolerance = tol
        self.config.max_iterations = max_iter
        self.config.enforce_q_limits = enforce_q_limits
        self.config.method = _parse_method(method)

        solver = LoadFlowSolver(self.system)
        solver.setConfig(self.config)

        t0 = time.perf_counter()
        try:
            cpp_result = solver.solve(self.config.method)
        except Exception as exc:
            raise SolverError(f"Power flow solver failed: {exc}") from exc
        elapsed_ms = (time.perf_counter() - t0) * 1000.0

        return _cpp_result_to_dict(cpp_result, elapsed_ms)

    def solve_short_circuit(
        self,
        fault_bus_id: int,
        fault_type: str = "3ph",
        fault_impedance: complex = 0j,
    ) -> dict[str, Any]:
        """Run short-circuit analysis.

        Parameters
        ----------
        fault_bus_id:
            ID of the bus where the fault is applied.
        fault_type:
            ``"3ph"``, ``"1ph"``, ``"2ph"``, or ``"2phg"``.
        fault_impedance:
            Fault impedance in Ohms.

        Returns
        -------
        dict
            Fault currents, post-fault voltages, and branch currents.
        """
        from powsy365_core import (  # type: ignore[import-untyped]
            ShortCircuitSolver,
            FaultType,
        )

        sc_solver = ShortCircuitSolver(self.system)
        ft = _parse_fault_type(fault_type)

        t0 = time.perf_counter()
        try:
            if ft == FaultType.ThreePhase:
                sc_solver.solveSymmetrical(fault_bus_id, fault_impedance)
            else:
                sc_solver.solveUnsymmetrical(
                    fault_bus_id, ft, fault_impedance
                )
        except Exception as exc:
            raise SolverError(f"Short-circuit solver failed: {exc}") from exc
        elapsed_ms = (time.perf_counter() - t0) * 1000.0

        return {
            "fault_bus_id": fault_bus_id,
            "fault_type": fault_type,
            "fault_current_ka": sc_solver.getFaultCurrent(),
            "bus_voltages": sc_solver.getBusVoltagesDuringFault(),
            "branch_currents": sc_solver.getBranchCurrentsDuringFault(),
            "elapsed_ms": round(elapsed_ms, 3),
        }

    # -- properties -------------------------------------------------------

    @property
    def bus_count(self) -> int:
        """Number of buses."""
        return self.system.getBusCount()

    @property
    def line_count(self) -> int:
        """Number of lines."""
        return self.system.getLineCount()

    @property
    def generator_count(self) -> int:
        """Number of generators."""
        return self.system.getGeneratorCount()

    @property
    def total_pgen(self) -> float:
        """Total active generation [MW]."""
        return self.system.getTotalPGen()

    @property
    def total_pload(self) -> float:
        """Total active load [MW]."""
        return self.system.getTotalPLoad()


# ---------------------------------------------------------------------------
# Private helpers
# ---------------------------------------------------------------------------


def _make_default_config() -> "SolverConfig":
    from powsy365_core import SolverConfig  # type: ignore[import-untyped]

    return SolverConfig()


def _parse_method(method: str) -> "SolverMethod":
    from powsy365_core import SolverMethod  # type: ignore[import-untyped]

    mapping = {
        "nr": SolverMethod.NewtonRaphson,
        "newton": SolverMethod.NewtonRaphson,
        "newtonraphson": SolverMethod.NewtonRaphson,
        "fd": SolverMethod.FastDecoupled,
        "fastdecoupled": SolverMethod.FastDecoupled,
        "gs": SolverMethod.GaussSeidel,
        "gaussseidel": SolverMethod.GaussSeidel,
    }
    key = method.lower().replace("_", "").replace("-", "")
    if key not in mapping:
        raise ValueError(
            f"Unknown solver method '{method}'. "
            f"Choose from: nr, fd, gs"
        )
    return mapping[key]


def _parse_fault_type(ft: str) -> "FaultType":
    from powsy365_core import FaultType  # type: ignore[import-untyped]

    mapping = {
        "3ph": FaultType.ThreePhase,
        "threephase": FaultType.ThreePhase,
        "1ph": FaultType.SinglePhase,
        "singlephase": FaultType.SinglePhase,
        "slg": FaultType.SinglePhase,
        "2ph": FaultType.TwoPhase,
        "twophase": FaultType.TwoPhase,
        "ll": FaultType.TwoPhase,
        "2phg": FaultType.TwoPhaseG,
        "twophaseg": FaultType.TwoPhaseG,
        "llg": FaultType.TwoPhaseG,
    }
    key = ft.lower().replace("-", "").replace(" ", "")
    if key not in mapping:
        raise ValueError(
            f"Unknown fault type '{ft}'. "
            f"Choose from: 3ph, 1ph, 2ph, 2phg"
        )
    return mapping[key]


def _cpp_result_to_dict(
    cpp_result: "PowerFlowResult", elapsed_ms: float
) -> dict[str, Any]:
    """Convert a C++ PowerFlowResult to a plain Python dictionary."""
    buses = []
    for b in cpp_result.bus_results:
        buses.append(
            {
                "bus_id": b.bus_id,
                "vm": b.vm,
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
        "elapsed_ms": round(elapsed_ms, 3),
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
