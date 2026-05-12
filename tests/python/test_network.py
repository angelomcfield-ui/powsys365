#!/usr/bin/env python3
"""
tests/python/test_network.py - Python-side Network class unit tests.

Covers:
- Creating empty networks
- Adding buses with validation
- Adding lines with bus references
- IEEE 14-bus network construction
- JSON round-trip serialization
- Data validation (errors raised for bad input)
"""

from __future__ import annotations

import json
from typing import Any

import pytest

from powsy365.network import BusData, GeneratorData, LineData, LoadData, Network, TransformerData


# ============================================================================
# Empty network tests
# ============================================================================


def test_create_empty_network(empty_network: Network) -> None:
    """An empty network should be created with default attributes."""
    net = empty_network
    assert net.name == "empty_test"
    assert net.base_mva == 100.0
    assert len(net.buses) == 0
    assert len(net.lines) == 0
    assert len(net.transformers) == 0
    assert len(net.generators) == 0
    assert len(net.loads) == 0
    assert repr(net).startswith("<Network 'empty_test'")


def test_empty_network_validation_errors(empty_network: Network) -> None:
    """An empty network should fail validation (no buses, no slack)."""
    errors = empty_network.validate()
    assert len(errors) > 0
    assert any("at least one bus" in e.lower() for e in errors)


# ============================================================================
# Bus tests
# ============================================================================


def test_add_bus() -> None:
    """Buses should be added with correct attributes."""
    net = Network(name="bus_test")
    bus = net.add_bus(
        bus_id=1,
        name="Bus 1",
        bus_type="Slack",
        base_kv=138.0,
        vmin=0.9,
        vmax=1.1,
        vm=1.02,
        va_deg=0.0,
    )
    assert bus.bus_id == 1
    assert bus.name == "Bus 1"
    assert bus.type == "Slack"
    assert bus.base_kv == 138.0
    assert bus.vmin == 0.9
    assert bus.vmax == 1.1
    assert bus.vm == 1.02
    assert bus.va_deg == 0.0
    assert 1 in net.buses
    assert net.buses[1] is bus


def test_add_bus_auto_id() -> None:
    """Bus IDs should be auto-assigned when not provided."""
    net = Network(name="auto_id_test")
    b1 = net.add_bus(name="Bus 1", bus_type="Slack")
    b2 = net.add_bus(name="Bus 2", bus_type="PQ")
    assert b1.bus_id == 1
    assert b2.bus_id == 2


def test_add_bus_duplicate_id_raises() -> None:
    """Adding a bus with an existing ID should raise ValueError."""
    net = Network(name="dup_test")
    net.add_bus(1, "Bus 1", "Slack")
    with pytest.raises(ValueError, match="already exists"):
        net.add_bus(1, "Bus 1 again", "PQ")


def test_add_bus_invalid_type_raises() -> None:
    """Adding a bus with an invalid type should raise ValueError."""
    net = Network(name="invalid_type_test")
    with pytest.raises(ValueError, match="Invalid bus type"):
        net.add_bus(1, "Bus 1", "INVALID_TYPE")


def test_add_bus_negative_base_kv_raises() -> None:
    """Adding a bus with non-positive base_kv should raise ValueError."""
    net = Network(name="neg_kv_test")
    with pytest.raises(ValueError, match="base_kv"):
        net.add_bus(1, "Bus 1", "Slack", base_kv=-10.0)


def test_bus_validation() -> None:
    """BusData.validate() should return appropriate error lists."""
    valid_bus = BusData(bus_id=1, name="B1", type="PQ", base_kv=138.0)
    assert valid_bus.validate() == []

    invalid_bus = BusData(bus_id=-1, name="B2", type="PQ", base_kv=138.0)
    assert len(invalid_bus.validate()) > 0

    bad_type_bus = BusData(bus_id=1, name="B3", type="INVALID", base_kv=138.0)
    assert len(bad_type_bus.validate()) > 0


# ============================================================================
# Line tests
# ============================================================================


def test_add_line() -> None:
    """Lines should be added with correct from/to bus references."""
    net = Network(name="line_test")
    net.add_bus(1, "Bus 1", "Slack")
    net.add_bus(2, "Bus 2", "PQ")
    line = net.add_line(
        line_id=1,
        from_bus=1,
        to_bus=2,
        r=0.01,
        x=0.1,
        b=0.0,
        rate_a=100.0,
    )
    assert line.line_id == 1
    assert line.from_bus == 1
    assert line.to_bus == 2
    assert line.r == 0.01
    assert line.x == 0.1
    assert 1 in net.lines


def test_add_line_zero_reactance_raises() -> None:
    """Adding a line with zero reactance should raise ValueError."""
    net = Network(name="line_x_test")
    with pytest.raises(ValueError, match="Reactance"):
        net.add_line(1, 1, 2, r=0.01, x=0.0)


def test_add_line_same_bus_raises() -> None:
    """Adding a line from a bus to itself should raise ValueError."""
    net = Network(name="loop_test")
    with pytest.raises(ValueError, match="must differ"):
        net.add_line(1, 1, 1, r=0.01, x=0.1)


def test_line_validation() -> None:
    """LineData.validate() should catch common errors."""
    valid_line = LineData(line_id=1, from_bus=1, to_bus=2, r=0.01, x=0.1)
    assert valid_line.validate() == []

    zero_x_line = LineData(line_id=1, from_bus=1, to_bus=2, r=0.01, x=0.0)
    assert len(zero_x_line.validate()) > 0

    loop_line = LineData(line_id=1, from_bus=1, to_bus=1, r=0.01, x=0.1)
    assert len(loop_line.validate()) > 0


# ============================================================================
# Generator tests
# ============================================================================


def test_add_generator() -> None:
    """Generators should be added with correct attributes."""
    net = Network(name="gen_test")
    net.add_bus(1, "Bus 1", "Slack")
    gen = net.add_generator(
        gen_id=1,
        bus_id=1,
        pg=100.0,
        qg=20.0,
        vg=1.02,
        qmin=-50.0,
        qmax=50.0,
    )
    assert gen.gen_id == 1
    assert gen.bus_id == 1
    assert gen.pg == 100.0
    assert gen.qg == 20.0
    assert gen.vg == 1.02
    assert gen.qmin == -50.0
    assert gen.qmax == 50.0


def test_generator_validation() -> None:
    """GeneratorData.validate() should catch range errors."""
    valid_gen = GeneratorData(gen_id=1, bus_id=1, pg=50.0, vg=1.0)
    assert valid_gen.validate() == []

    bad_qrange = GeneratorData(
        gen_id=1, bus_id=1, pg=50.0, qmin=10.0, qmax=-10.0, vg=1.0
    )
    assert len(bad_qrange.validate()) > 0


# ============================================================================
# Load tests
# ============================================================================


def test_add_load() -> None:
    """Loads should be added with correct attributes."""
    net = Network(name="load_test")
    net.add_bus(1, "Bus 1", "PQ")
    load = net.add_load(load_id=1, bus_id=1, pd=50.0, qd=20.0)
    assert load.load_id == 1
    assert load.bus_id == 1
    assert load.pd == 50.0
    assert load.qd == 20.0


def test_load_validation() -> None:
    """LoadData.validate() should catch negative power values."""
    valid_load = LoadData(load_id=1, bus_id=1, pd=50.0, qd=20.0)
    assert valid_load.validate() == []

    negative_load = LoadData(load_id=1, bus_id=1, pd=-10.0)
    assert len(negative_load.validate()) > 0


# ============================================================================
# Transformer tests
# ============================================================================


def test_add_transformer() -> None:
    """Transformers should be added with correct attributes."""
    net = Network(name="trafo_test")
    net.add_bus(1, "Bus 1", "Slack")
    net.add_bus(2, "Bus 2", "PQ")
    trafo = net.add_transformer(
        trafo_id=1, from_bus=1, to_bus=2, r=0.001, x=0.05, tap=1.0
    )
    assert trafo.trafo_id == 1
    assert trafo.from_bus == 1
    assert trafo.to_bus == 2
    assert trafo.r == 0.001
    assert trafo.x == 0.05
    assert trafo.tap == 1.0


# ============================================================================
# IEEE 14-bus test system
# ============================================================================


def test_ieee14_network(ieee14_network: Network) -> None:
    """The IEEE 14-bus network fixture should have exactly 14 buses."""
    net = ieee14_network
    assert len(net.buses) == 14
    assert len(net.lines) == 20
    assert len(net.generators) == 5
    # 11 loads in the IEEE 14-bus system
    assert len(net.loads) == 11


def test_ieee14_has_slack_bus(ieee14_network: Network) -> None:
    """The IEEE 14-bus system should have exactly one Slack bus."""
    slack_buses = [b for b in ieee14_network.buses.values() if b.type.upper() == "SLACK"]
    assert len(slack_buses) == 1
    assert slack_buses[0].bus_id == 1


def test_ieee14_bus_ids(ieee14_network: Network) -> None:
    """The IEEE 14-bus system should have buses with IDs 1..14."""
    expected_ids = set(range(1, 15))
    actual_ids = set(ieee14_network.buses.keys())
    assert actual_ids == expected_ids


def test_ieee14_network_validation(ieee14_network: Network) -> None:
    """The built-in IEEE 14 network should pass validation."""
    errors = ieee14_network.validate()
    assert errors == [], f"IEEE 14 validation failed: {errors}"


def test_ieee14_bus_voltages_in_range(ieee14_network: Network) -> None:
    """All IEEE 14 bus voltage setpoints should be within reasonable bounds."""
    for bus in ieee14_network.buses.values():
        assert 0.9 <= bus.vm <= 1.2, f"Bus {bus.bus_id} vm={bus.vm} out of range"
        assert 0.8 <= bus.vmin < bus.vmax <= 1.3


def test_ieee14_line_parameters_valid(ieee14_network: Network) -> None:
    """All IEEE 14 line parameters should be physically valid."""
    for line in ieee14_network.lines.values():
        assert line.from_bus in ieee14_network.buses
        assert line.to_bus in ieee14_network.buses
        assert line.from_bus != line.to_bus
        assert line.x != 0.0
        assert line.r >= 0.0


# ============================================================================
# Network validation tests
# ============================================================================


def test_network_no_slack(minimal_network: Network) -> None:
    """A network without a Slack bus should fail validation."""
    net = Network(name="no_slack")
    net.add_bus(1, "Bus 1", "PQ")
    net.add_bus(2, "Bus 2", "PQ")
    net.add_line(1, 1, 2, r=0.01, x=0.1)
    errors = net.validate()
    assert any("Slack" in e for e in errors)


def test_network_isolated_bus(isolated_bus_network: Network) -> None:
    """A network with an isolated bus should fail validation."""
    net = isolated_bus_network
    errors = net.validate()
    assert any("isolated" in e.lower() for e in errors)


# ============================================================================
# Serialization tests
# ============================================================================


def test_to_json_roundtrip(ieee14_network: Network, tmp_path: Path) -> None:
    """Network should survive a JSON round-trip unchanged."""
    json_path = tmp_path / "ieee14.json"
    ieee14_network.to_json(str(json_path))

    # Read back
    net2 = Network.from_json(str(json_path))

    assert net2.name == ieee14_network.name
    assert len(net2.buses) == len(ieee14_network.buses)
    assert len(net2.lines) == len(ieee14_network.lines)
    assert len(net2.generators) == len(ieee14_network.generators)

    # Check bus data preserved
    for bid in ieee14_network.buses:
        assert bid in net2.buses
        orig = ieee14_network.buses[bid]
        restored = net2.buses[bid]
        assert restored.type == orig.type
        assert restored.base_kv == orig.base_kv


# ============================================================================
# __repr__ test
# ============================================================================


def test_network_repr(ieee14_network: Network) -> None:
    """Network repr should be informative."""
    r = repr(ieee14_network)
    assert "IEEE 14-Bus" in r or "14" in r
    assert "buses=14" in r
