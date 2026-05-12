"""
POWSYS365 - Power System Analysis Framework
============================================

High-performance power system analysis toolkit combining a C++ compute
engine (via pybind11) with a Pythonic API, AI-powered analytics, and
modern data-science integrations.

Version: 3.0.0

Quick-start Example
-------------------
    >>> import powsy365 as ps
    >>> net = ps.create_ieee14()
    >>> result = ps.power_flow(net, method="nr")
    >>> print(f"Converged: {result['converged']} in {result['iterations']} iters")
    >>> print(f"Total losses: {result['total_ploss']:.2f} MW")
    >>>
    >>> # AI-powered analysis
    >>> from powsy365.ai.llm_providers import DeepSeekProvider
    >>> llm = DeepSeekProvider(api_key="sk-...")
    >>> insight = llm.chat("Analyze voltage stability for this system")

Submodules
----------
- ``core``       : Low-level C++ engine wrappers
- ``network``    : Pythonic network construction and I/O
- ``analysis``   : Power flow, short-circuit, OPF, stability
- ``utils``      : Unit conversion, validation, plotting
- ``ai``         : LLM providers, RAG, function calling, prompts

Classes from C++ Engine
-----------------------
- :class:`Bus`            - Electrical bus (PQ, PV, Slack)
- :class:`Line`           - Transmission line
- :class:`Transformer`    - Two-winding transformer
- :class:`Generator`      - Synchronous generator
- :class:`Load`           - Power load/demand
- :class:`PowerSystem`    - Network container
- :class:`SolverConfig`   - Solver settings
- :class:`LoadFlowSolver` - Newton-Raphson, FD, GS solvers
- :class:`ShortCircuitSolver` - Fault analysis
- :class:`YbusBuilder`    - Admittance matrix builder
- :class:`PowerFlowResult` - Aggregated results

Enums
-----
- :class:`BusType`       - PQ, PV, Slack
- :class:`FaultType`     - ThreePhase, SinglePhase, TwoPhase, TwoPhaseG
- :class:`SolverMethod`  - NewtonRaphson, FastDecoupled, GaussSeidel
"""

from __future__ import annotations

__version__: str = "3.0.0"
__author__: str = "POWSYS365 Team"
__license__: str = "MIT"
__all__: list[str] = [
    # Version
    "__version__",
    # C++ core classes (imported from powsy365_core)
    "Bus",
    "Line",
    "Transformer",
    "Generator",
    "Load",
    "PowerSystem",
    "SolverConfig",
    "LoadFlowSolver",
    "ShortCircuitSolver",
    "YbusBuilder",
    "PowerFlowResult",
    "PowerFlowBusResult",
    "PowerFlowLineResult",
    # Enums
    "BusType",
    "FaultType",
    "SolverMethod",
    # Pythonic wrappers
    "create_ieee14",
    "create_ieee30",
    "create_ieee57",
    "create_ieee118",
    "Network",
    "power_flow",
    "short_circuit",
    "stability_analysis",
    "opf",
    # Utilities
    "convert_units",
    "validate_network_data",
    "plot_voltages",
    "plot_loading",
]

# ---------------------------------------------------------------------------
# Attempt to import the C++ extension module.
# If it is not compiled yet, we provide a clear error message.
# ---------------------------------------------------------------------------

try:
    import powsy365_core as _core  # type: ignore[import-untyped]

    Bus = _core.Bus
    Line = _core.Line
    Transformer = _core.Transformer
    Generator = _core.Generator
    Load = _core.Load
    PowerSystem = _core.PowerSystem
    SolverConfig = _core.SolverConfig
    LoadFlowSolver = _core.LoadFlowSolver
    ShortCircuitSolver = _core.ShortCircuitSolver
    YbusBuilder = _core.YbusBuilder
    PowerFlowResult = _core.PowerFlowResult
    PowerFlowBusResult = _core.PowerFlowBusResult
    PowerFlowLineResult = _core.PowerFlowLineResult
    BusType = _core.BusType
    FaultType = _core.FaultType
    SolverMethod = _core.SolverMethod
except ImportError as _imp_err:  # pragma: no cover
    import warnings as _warnings

    _msg = (
        "powsy365_core C++ extension is not compiled. "
        "Build it first:  cd python && pip install -e ."
    )
    _warnings.warn(_msg, RuntimeWarning, stacklevel=2)

    # Placeholder classes so imports do not crash during doc builds, etc.
    class _Stub:  # type: ignore[no-redef]
        def __init__(self, *args, **kwargs) -> None:  # noqa: ANN002, ANN003
            raise RuntimeError(_msg)

    Bus = Line = Transformer = Generator = Load = _Stub  # type: ignore[misc]
    PowerSystem = SolverConfig = LoadFlowSolver = _Stub  # type: ignore[misc]
    ShortCircuitSolver = YbusBuilder = _Stub  # type: ignore[misc]
    PowerFlowResult = PowerFlowBusResult = PowerFlowLineResult = _Stub  # type: ignore[misc]

    class BusType:  # type: ignore[no-redef]
        PQ = PV = Slack = 0

    class FaultType:  # type: ignore[no-redef]
        ThreePhase = SinglePhase = TwoPhase = TwoPhaseG = 0

    class SolverMethod:  # type: ignore[no-redef]
        NewtonRaphson = FastDecoupled = GaussSeidel = 0


# ---------------------------------------------------------------------------
# Pythonic convenience functions
# ---------------------------------------------------------------------------

def create_ieee14(base_mva: float = 100.0) -> PowerSystem:
    """Create and return the IEEE 14-bus test system.

    Parameters
    ----------
    base_mva:
        System base MVA (default 100).

    Returns
    -------
    PowerSystem
        Populated 14-bus power system model.
    """
    ps = PowerSystem(base_mva)
    ps.name = "IEEE 14-Bus Test Case"
    ps.loadIEEE14()
    if not ps.isValid():
        raise RuntimeError("IEEE14 model failed validation")
    return ps


def create_ieee30(base_mva: float = 100.0) -> PowerSystem:
    """Create and return the IEEE 30-bus test system."""
    ps = PowerSystem(base_mva)
    ps.name = "IEEE 30-Bus Test Case"
    ps.loadIEEE30()
    if not ps.isValid():
        raise RuntimeError("IEEE30 model failed validation")
    return ps


def create_ieee57(base_mva: float = 100.0) -> PowerSystem:
    """Create and return the IEEE 57-bus test system."""
    ps = PowerSystem(base_mva)
    ps.name = "IEEE 57-Bus Test Case"
    ps.loadIEEE57()
    if not ps.isValid():
        raise RuntimeError("IEEE57 model failed validation")
    return ps


def create_ieee118(base_mva: float = 100.0) -> PowerSystem:
    """Create and return the IEEE 118-bus test system."""
    ps = PowerSystem(base_mva)
    ps.name = "IEEE 118-Bus Test Case"
    ps.loadIEEE118()
    if not ps.isValid():
        raise RuntimeError("IEEE118 model failed validation")
    return ps


# ---------------------------------------------------------------------------
# Lazy submodule imports (avoid heavy imports at package load time)
# ---------------------------------------------------------------------------

from powsy365.network import Network  # noqa: E402
from powsy365.analysis import power_flow, short_circuit, stability_analysis, opf  # noqa: E402
from powsy365.utils import (  # noqa: E402
    convert_units,
    validate_network_data,
    plot_voltages,
    plot_loading,
)
