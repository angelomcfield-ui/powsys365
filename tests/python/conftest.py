#!/usr/bin/env python3
"""
pytest fixtures for POWSYS365 Python tests.

Provides shared test data through pytest fixtures:
- ieee14_network:  IEEE 14-bus test system as a Python Network object
- solver:          LoadFlowSolver configured for IEEE 14
- empty_network:   Empty network for edge-case testing
- minimal_network: Minimal 2-bus system with one slack and one PQ bus
"""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Any

import pytest

# Ensure project python/ is importable
PROJECT_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT_ROOT / "python"))

from powsy365.network import Network


# ============================================================================
# Fixtures
# ============================================================================


@pytest.fixture(scope="session")
def ieee14_network() -> Network:
    """Provide the IEEE 14-bus test system as a fully populated Network."""
    return Network.ieee14()


@pytest.fixture(scope="session")
def empty_network() -> Network:
    """Provide an empty, valid Network instance."""
    return Network(name="empty_test")


@pytest.fixture
def minimal_network() -> Network:
    """Provide a minimal 2-bus system with one Slack and one PQ bus."""
    net = Network(name="minimal_2bus", base_mva=100.0)
    net.add_bus(1, "Bus 1", "Slack", base_kv=138.0, vm=1.0, va_deg=0.0)
    net.add_bus(2, "Bus 2", "PQ", base_kv=138.0, vm=1.0, va_deg=0.0)
    net.add_line(1, 1, 2, r=0.01, x=0.1, rate_a=100.0)
    net.add_generator(1, 1, pg=50.0, vg=1.0)
    net.add_load(1, 2, pd=30.0, qd=10.0)
    return net


@pytest.fixture
def isolated_bus_network() -> Network:
    """Provide a network with an isolated bus (no connected branches)."""
    net = Network(name="isolated_test", base_mva=100.0)
    net.add_bus(1, "Bus 1", "Slack", base_kv=138.0)
    net.add_bus(2, "Bus 2", "PQ", base_kv=138.0)
    # No lines connecting bus 2 - it will be isolated
    return net
