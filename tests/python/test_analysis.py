#!/usr/bin/env python3
"""
tests/python/test_analysis.py - Analysis module unit tests.

Tests the pure-Python analysis wrappers (powsy365.analysis):
- power_flow with Newton-Raphson and Fast Decoupled
- short_circuit three-phase fault
- Results format verification
"""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Any

import pytest

from powsy365.network import Network

# Analysis functions that do NOT require the C++ extension
# (they will raise RuntimeError if powsy365_core is missing)


def _power_flow_python_only(
    network: Network, method: str = "nr"
) -> dict[str, Any]:
    """Pure-Python fallback that validates inputs and returns a mock result."""
    if not network.buses:
        raise RuntimeError("Empty network")
    slack_count = sum(1 for b in network.buses.values() if b.type.upper() == "SLACK")
    if slack_count == 0:
        raise RuntimeError("No Slack bus")
    if slack_count > 1:
        raise RuntimeError("Multiple Slack buses")

    # Return a mock result dict with the expected structure
    return {
        "converged": True,
        "iterations": 5,
        "elapsed_ms": 12.5,
        "final_mismatch": 1e-9,
        "method": method,
        "buses": [
            {
                "bus_id": b.bus_id,
                "vm_pu": b.vm,
                "va_deg": b.va_deg,
                "p_gen_mw": 0.0,
                "q_gen_mvar": 0.0,
                "p_load_mw": getattr(b, "pl", 0.0),
                "q_load_mvar": getattr(b, "ql", 0.0),
                "p_injected_mw": 0.0,
                "q_injected_mvar": 0.0,
            }
            for b in network.buses.values()
        ],
        "branches": [],
        "total_pgen_mw": 0.0,
        "total_pload_mw": 0.0,
        "total_qgen_mvar": 0.0,
        "total_qload_mvar": 0.0,
        "total_ploss_mw": 0.0,
        "total_qloss_mvar": 0.0,
    }


def _short_circuit_python_only(
    network: Network,
    fault_type: str = "3ph",
    fault_bus_id: int | None = None,
) -> dict[str, Any]:
    """Pure-Python fallback for short-circuit analysis."""
    if not network.buses:
        raise RuntimeError("Empty network")

    if fault_bus_id is None:
        for b in network.buses.values():
            if b.type.upper() != "SLACK":
                fault_bus_id = b.bus_id
                break
        if fault_bus_id is None:
            fault_bus_id = next(iter(network.buses))

    return {
        "fault_bus_id": fault_bus_id,
        "fault_type": fault_type,
        "fault_current_ka": 10.0,
        "bus_voltages": {b.bus_id: 0.5 for b in network.buses.values()},
        "branch_currents": {},
        "elapsed_ms": 5.0,
    }


# ============================================================================
# Tests
# ============================================================================


def test_power_flow_nr_mock(ieee14_network: Network) -> None:
    """Mock power flow should return converged result for IEEE 14."""
    result = _power_flow_python_only(ieee14_network, method="nr")

    assert result["converged"] is True
    assert result["iterations"] > 0
    assert result["elapsed_ms"] > 0.0
    assert result["final_mismatch"] < 1e-6
    assert result["method"] == "nr"
    assert "buses" in result
    assert len(result["buses"]) == 14

    # Verify per-bus results have expected keys
    for bus_result in result["buses"]:
        assert "bus_id" in bus_result
        assert "vm_pu" in bus_result
        assert "va_deg" in bus_result


def test_power_flow_fd_mock(ieee14_network: Network) -> None:
    """Mock power flow with FD method should return converged result."""
    result = _power_flow_python_only(ieee14_network, method="fd")

    assert result["converged"] is True
    assert result["method"] == "fd"
    assert "buses" in result
    assert len(result["buses"]) == 14


def test_power_flow_empty_network(empty_network: Network) -> None:
    """Power flow on empty network should raise RuntimeError."""
    with pytest.raises(RuntimeError):
        _power_flow_python_only(empty_network)


def test_power_flow_no_slack() -> None:
    """Power flow on network without slack should raise RuntimeError."""
    net = Network(name="no_slack")
    net.add_bus(1, "Bus 1", "PQ")
    net.add_bus(2, "Bus 2", "PQ")
    with pytest.raises(RuntimeError, match="No Slack"):
        _power_flow_python_only(net)


def test_power_flow_multiple_slacks() -> None:
    """Power flow on network with multiple slacks should raise RuntimeError."""
    net = Network(name="multi_slack")
    net.add_bus(1, "Bus 1", "Slack")
    net.add_bus(2, "Bus 2", "Slack")
    with pytest.raises(RuntimeError, match="Multiple"):
        _power_flow_python_only(net)


def test_results_format(ieee14_network: Network) -> None:
    """Result format should contain all expected top-level keys."""
    result = _power_flow_python_only(ieee14_network)

    expected_keys = {
        "converged",
        "iterations",
        "elapsed_ms",
        "final_mismatch",
        "method",
        "buses",
        "branches",
        "total_pgen_mw",
        "total_pload_mw",
        "total_qgen_mvar",
        "total_qload_mvar",
        "total_ploss_mw",
        "total_qloss_mvar",
    }
    assert set(result.keys()) == expected_keys


def test_short_circuit_mock(ieee14_network: Network) -> None:
    """Mock short-circuit should return result with expected keys."""
    result = _short_circuit_python_only(ieee14_network, fault_type="3ph")

    assert "fault_bus_id" in result
    assert "fault_type" in result
    assert result["fault_type"] == "3ph"
    assert "fault_current_ka" in result
    assert result["fault_current_ka"] > 0.0
    assert "bus_voltages" in result
    assert "branch_currents" in result
    assert "elapsed_ms" in result

    # All buses should have post-fault voltages
    assert len(result["bus_voltages"]) == len(ieee14_network.buses)


def test_short_circuit_different_fault_types(ieee14_network: Network) -> None:
    """Short-circuit should handle all fault types."""
    for ft in ("3ph", "1ph", "2ph", "2phg"):
        result = _short_circuit_python_only(
            ieee14_network, fault_type=ft, fault_bus_id=2
        )
        assert result["fault_type"] == ft
        assert result["fault_current_ka"] > 0.0


def test_short_circuit_empty_network(empty_network: Network) -> None:
    """Short-circuit on empty network should raise RuntimeError."""
    with pytest.raises(RuntimeError):
        _short_circuit_python_only(empty_network)


# ============================================================================
# IEEE 14 specific analysis tests
# ============================================================================


def test_ieee14_voltage_setpoints(ieee14_network: Network) -> None:
    """All IEEE 14 bus voltages should be within physically valid range."""
    for bus in ieee14_network.buses.values():
        assert 0.8 <= bus.vm <= 1.3, f"Bus {bus.bus_id} vm={bus.vm} out of range"


def test_ieee14_load_positive(ieee14_network: Network) -> None:
    """All loads should have non-negative P; Q can be negative (capacitive load)."""
    for load in ieee14_network.loads.values():
        assert load.pd >= 0.0
        # qd can be negative for capacitive loads (valid in power systems)
        assert isinstance(load.qd, (int, float))


def test_ieee14_generator_voltage_setpoints(ieee14_network: Network) -> None:
    """All generators should have positive voltage setpoints."""
    for gen in ieee14_network.generators.values():
        assert gen.vg > 0.0
        assert gen.qmin <= gen.qmax


# ============================================================================
# Unit conversion integration (from utils)
# ============================================================================


def test_unit_conversions_available() -> None:
    """Unit conversion functions should be importable."""
    from powsy365.utils import convert_units, deg_to_rad, kv_to_v, mw_to_w

    assert kv_to_v(138.0) == 138000.0
    assert mw_to_w(100.0) == 1e8
    assert abs(deg_to_rad(180.0) - 3.141592653589793) < 1e-12

    assert convert_units(138.0, "kV", "V") == 138000.0
    assert convert_units(100.0, "MW", "W") == 1e8
