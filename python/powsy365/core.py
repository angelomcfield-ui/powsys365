# powsy365/core.py

# This module provides access to the C++ core engine via pybind11 bindings

try:
    from . import _core as core
    PowerSystem = core.PowerSystem
    LoadFlowSolver = core.LoadFlowSolver
    Bus = core.Bus
    Line = core.Line
    Generator = core.Generator
    Load = core.Load
except ImportError:
    # Fallback if bindings not built
    class PowerSystem:
        pass
    class LoadFlowSolver:
        pass
    class Bus:
        pass
    class Line:
        pass
    class Generator:
        pass
    class Load:
        pass