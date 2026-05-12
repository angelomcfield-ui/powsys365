"""
powsy365.network
================

Pure-Python network modeling with validation, serialization,
and conversion to/from the C++ :class:`PowerSystem` engine.

Supports:
* Interactive network construction
* JSON / CSV import and export
* IEEE CDF and MATPOWER-like case formats
* Data validation with detailed error messages
"""

from __future__ import annotations

import csv
import json
import logging
import os
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import Any, Self

import numpy as np

logger = logging.getLogger(__name__)


# ---------------------------------------------------------------------------
# Data classes for Python-side network elements
# ---------------------------------------------------------------------------


@dataclass
class BusData:
    """Python-side bus record with validation."""

    bus_id: int
    name: str = ""
    type: str = "PQ"  # "PQ", "PV", or "Slack"
    base_kv: float = 1.0
    vmin: float = 0.9
    vmax: float = 1.1
    vm: float = 1.0
    va_deg: float = 0.0
    gs: float = 0.0  # shunt conductance [p.u.]
    bs: float = 0.0  # shunt susceptance [p.u.]
    area: int = 1
    zone: int = 1

    def validate(self) -> list[str]:
        """Return list of validation error strings (empty if valid)."""
        errors: list[str] = []
        if self.bus_id <= 0:
            errors.append(f"Bus ID must be positive, got {self.bus_id}")
        if self.base_kv <= 0:
            errors.append(f"base_kv must be > 0, got {self.base_kv}")
        if self.vmin >= self.vmax:
            errors.append(
                f"vmin ({self.vmin}) must be < vmax ({self.vmax})"
            )
        if self.type not in {"PQ", "PV", "Slack", "SLACK", "slack"}:
            errors.append(f"Invalid bus type: {self.type}")
        return errors


@dataclass
class LineData:
    """Python-side transmission line record."""

    line_id: int
    from_bus: int
    to_bus: int
    r: float = 0.0
    x: float = 0.1
    b: float = 0.0
    rate_a: float = 0.0
    rate_b: float = 0.0
    rate_c: float = 0.0
    status: int = 1

    def validate(self) -> list[str]:
        errors: list[str] = []
        if self.line_id < 0:
            errors.append(f"Line ID must be >= 0, got {self.line_id}")
        if self.from_bus <= 0:
            errors.append(f"from_bus must be > 0, got {self.from_bus}")
        if self.to_bus <= 0:
            errors.append(f"to_bus must be > 0, got {self.to_bus}")
        if self.from_bus == self.to_bus:
            errors.append("from_bus and to_bus must differ")
        if self.x == 0.0:
            errors.append("Reactance x cannot be zero (would cause singularity)")
        return errors


@dataclass
class TransformerData:
    """Python-side transformer record."""

    trafo_id: int
    from_bus: int
    to_bus: int
    r: float = 0.0
    x: float = 0.1
    tap: float = 1.0
    shift_deg: float = 0.0
    rate_a: float = 0.0
    status: int = 1

    def validate(self) -> list[str]:
        errors: list[str] = []
        if self.trafo_id < 0:
            errors.append(f"Transformer ID must be >= 0")
        if self.from_bus <= 0 or self.to_bus <= 0:
            errors.append("Bus IDs must be > 0")
        if self.from_bus == self.to_bus:
            errors.append("from_bus and to_bus must differ")
        if self.x == 0.0:
            errors.append("Reactance x cannot be zero")
        if self.tap <= 0:
            errors.append(f"tap ratio must be > 0, got {self.tap}")
        return errors


@dataclass
class GeneratorData:
    """Python-side generator record."""

    gen_id: int
    bus_id: int
    pg: float = 0.0
    qg: float = 0.0
    qmin: float = -9999.0
    qmax: float = 9999.0
    vg: float = 1.0
    mbase: float = 100.0
    pg_min: float = 0.0
    pg_max: float = 9999.0
    status: int = 1
    cost_a: float = 0.0
    cost_b: float = 0.0
    cost_c: float = 0.0

    def validate(self) -> list[str]:
        errors: list[str] = []
        if self.gen_id < 0:
            errors.append(f"Generator ID must be >= 0")
        if self.bus_id <= 0:
            errors.append(f"bus_id must be > 0")
        if self.pg_min > self.pg_max:
            errors.append(f"pg_min ({self.pg_min}) > pg_max ({self.pg_max})")
        if self.qmin > self.qmax:
            errors.append(f"qmin ({self.qmin}) > qmax ({self.qmax})")
        if self.vg <= 0:
            errors.append(f"vg must be > 0, got {self.vg}")
        return errors


@dataclass
class LoadData:
    """Python-side load record."""

    load_id: int
    bus_id: int
    pd: float = 0.0
    qd: float = 0.0
    ip: float = 0.0
    iq: float = 0.0
    yp: float = 0.0
    yq: float = 0.0
    status: int = 1

    def validate(self) -> list[str]:
        errors: list[str] = []
        if self.load_id < 0:
            errors.append(f"Load ID must be >= 0")
        if self.bus_id <= 0:
            errors.append(f"bus_id must be > 0")
        if self.pd < 0:
            errors.append("pd must be >= 0")
        if self.qd < 0 and abs(self.qd) > 1000:
            errors.append(f"qd magnitude too large: {self.qd}")
        return errors


# ---------------------------------------------------------------------------
# Network container
# ---------------------------------------------------------------------------


class Network:
    """Pure-Python power-system network builder with validation.

    The :class:`Network` class lets users construct a power system
    interactively in Python, validates all data, and can export the
    model to the C++ engine or serialize it to JSON / CSV.

    Example
    -------
    >>> net = Network(name="MySystem", base_mva=100.0)
    >>> net.add_bus(1, "Bus 1", "Slack", base_kv=138.0)
    >>> net.add_bus(2, "Bus 2", "PQ", base_kv=138.0)
    >>> net.add_line(1, 1, 2, r=0.01, x=0.1, rate_a=100.0)
    >>> net.add_generator(1, 1, pg=50.0, vg=1.02)
    >>> net.add_load(1, 2, pd=30.0, qd=10.0)
    >>> net.validate()
    >>> json_path = net.to_json("/tmp/my_system.json")
    """

    def __init__(self, name: str = "", base_mva: float = 100.0) -> None:
        self.name: str = name
        self.base_mva: float = base_mva
        self.buses: dict[int, BusData] = {}
        self.lines: dict[int, LineData] = {}
        self.transformers: dict[int, TransformerData] = {}
        self.generators: dict[int, GeneratorData] = {}
        self.loads: dict[int, LoadData] = {}
        self._bus_id_counter: int = 1
        self._element_id_counter: int = 1

    # -- Bus methods ------------------------------------------------------

    def add_bus(
        self,
        bus_id: int | None = None,
        name: str = "",
        bus_type: str = "PQ",
        base_kv: float = 1.0,
        vmin: float = 0.9,
        vmax: float = 1.1,
        vm: float = 1.0,
        va_deg: float = 0.0,
        gs: float = 0.0,
        bs: float = 0.0,
        area: int = 1,
        zone: int = 1,
    ) -> BusData:
        """Add a bus to the network.

        Parameters
        ----------
        bus_id:
            Unique identifier (auto-assigned if *None*).
        name:
            Human-readable name.
        bus_type:
            ``"PQ"``, ``"PV"``, or ``"Slack"``.
        base_kv:
            Base voltage [kV].
        vmin, vmax:
            Voltage magnitude limits [p.u.].
        vm, va_deg:
            Initial voltage magnitude [p.u.] and angle [deg].
        gs, bs:
            Shunt conductance and susceptance [p.u.].
        area, zone:
            Control area and loss zone numbers.

        Returns
        -------
        BusData
            The created bus record.
        """
        if bus_id is None:
            bus_id = self._bus_id_counter
            while bus_id in self.buses:
                bus_id += 1
            self._bus_id_counter = bus_id + 1

        if bus_id in self.buses:
            raise ValueError(f"Bus ID {bus_id} already exists")

        bus = BusData(
            bus_id=bus_id,
            name=name or f"Bus {bus_id}",
            type=bus_type,
            base_kv=base_kv,
            vmin=vmin,
            vmax=vmax,
            vm=vm,
            va_deg=va_deg,
            gs=gs,
            bs=bs,
            area=area,
            zone=zone,
        )
        errors = bus.validate()
        if errors:
            raise ValueError(f"Bus {bus_id} validation failed: {errors}")

        self.buses[bus_id] = bus
        logger.debug("Added bus %d (%s)", bus_id, bus.name)
        return bus

    # -- Line methods -----------------------------------------------------

    def add_line(
        self,
        line_id: int | None = None,
        from_bus: int = 0,
        to_bus: int = 0,
        r: float = 0.0,
        x: float = 0.1,
        b: float = 0.0,
        rate_a: float = 0.0,
        rate_b: float = 0.0,
        rate_c: float = 0.0,
        status: int = 1,
    ) -> LineData:
        """Add a transmission line to the network."""
        if line_id is None:
            line_id = self._next_element_id()
        if line_id in self.lines:
            raise ValueError(f"Line ID {line_id} already exists")

        line = LineData(
            line_id=line_id,
            from_bus=from_bus,
            to_bus=to_bus,
            r=r,
            x=x,
            b=b,
            rate_a=rate_a,
            rate_b=rate_b,
            rate_c=rate_c,
            status=status,
        )
        errors = line.validate()
        if errors:
            raise ValueError(f"Line {line_id} validation failed: {errors}")

        self.lines[line_id] = line
        logger.debug("Added line %d (%d->%d)", line_id, from_bus, to_bus)
        return line

    # -- Transformer methods ----------------------------------------------

    def add_transformer(
        self,
        trafo_id: int | None = None,
        from_bus: int = 0,
        to_bus: int = 0,
        r: float = 0.0,
        x: float = 0.1,
        tap: float = 1.0,
        shift_deg: float = 0.0,
        rate_a: float = 0.0,
        status: int = 1,
    ) -> TransformerData:
        """Add a two-winding transformer to the network."""
        if trafo_id is None:
            trafo_id = self._next_element_id()
        if trafo_id in self.transformers:
            raise ValueError(f"Transformer ID {trafo_id} already exists")

        trafo = TransformerData(
            trafo_id=trafo_id,
            from_bus=from_bus,
            to_bus=to_bus,
            r=r,
            x=x,
            tap=tap,
            shift_deg=shift_deg,
            rate_a=rate_a,
            status=status,
        )
        errors = trafo.validate()
        if errors:
            raise ValueError(
                f"Transformer {trafo_id} validation failed: {errors}"
            )

        self.transformers[trafo_id] = trafo
        logger.debug("Added transformer %d (%d->%d)", trafo_id, from_bus, to_bus)
        return trafo

    # -- Generator methods ------------------------------------------------

    def add_generator(
        self,
        gen_id: int | None = None,
        bus_id: int = 0,
        pg: float = 0.0,
        qg: float = 0.0,
        qmin: float = -9999.0,
        qmax: float = 9999.0,
        vg: float = 1.0,
        mbase: float = 100.0,
        pg_min: float = 0.0,
        pg_max: float = 9999.0,
        status: int = 1,
        cost_a: float = 0.0,
        cost_b: float = 0.0,
        cost_c: float = 0.0,
    ) -> GeneratorData:
        """Add a synchronous generator to the network."""
        if gen_id is None:
            gen_id = self._next_element_id()
        if gen_id in self.generators:
            raise ValueError(f"Generator ID {gen_id} already exists")

        gen = GeneratorData(
            gen_id=gen_id,
            bus_id=bus_id,
            pg=pg,
            qg=qg,
            qmin=qmin,
            qmax=qmax,
            vg=vg,
            mbase=mbase,
            pg_min=pg_min,
            pg_max=pg_max,
            status=status,
            cost_a=cost_a,
            cost_b=cost_b,
            cost_c=cost_c,
        )
        errors = gen.validate()
        if errors:
            raise ValueError(
                f"Generator {gen_id} validation failed: {errors}"
            )

        self.generators[gen_id] = gen
        logger.debug("Added generator %d at bus %d", gen_id, bus_id)
        return gen

    # -- Load methods -----------------------------------------------------

    def add_load(
        self,
        load_id: int | None = None,
        bus_id: int = 0,
        pd: float = 0.0,
        qd: float = 0.0,
        ip: float = 0.0,
        iq: float = 0.0,
        yp: float = 0.0,
        yq: float = 0.0,
        status: int = 1,
    ) -> LoadData:
        """Add a power load/demand to the network."""
        if load_id is None:
            load_id = self._next_element_id()
        if load_id in self.loads:
            raise ValueError(f"Load ID {load_id} already exists")

        load = LoadData(
            load_id=load_id,
            bus_id=bus_id,
            pd=pd,
            qd=qd,
            ip=ip,
            iq=iq,
            yp=yp,
            yq=yq,
            status=status,
        )
        errors = load.validate()
        if errors:
            raise ValueError(f"Load {load_id} validation failed: {errors}")

        self.loads[load_id] = load
        logger.debug("Added load %d at bus %d", load_id, bus_id)
        return load

    # -- Validation -------------------------------------------------------

    def validate(self) -> list[str]:
        """Validate the entire network and return all error messages.

        Returns
        -------
        list[str]
            Empty list if the network is valid; otherwise contains
            human-readable error descriptions.
        """
        errors: list[str] = []

        # Check we have at least one bus
        if not self.buses:
            errors.append("Network must contain at least one bus")
            return errors

        # Check for exactly one slack bus
        slack_count = sum(
            1 for b in self.buses.values() if b.type.upper() == "SLACK"
        )
        if slack_count == 0:
            errors.append("Network must have exactly one Slack bus (found 0)")
        elif slack_count > 1:
            errors.append(
                f"Network must have exactly one Slack bus (found {slack_count})"
            )

        # Validate all element cross-references
        bus_ids = set(self.buses.keys())
        for line in self.lines.values():
            if line.from_bus not in bus_ids:
                errors.append(
                    f"Line {line.line_id}: from_bus {line.from_bus} not found"
                )
            if line.to_bus not in bus_ids:
                errors.append(
                    f"Line {line.line_id}: to_bus {line.to_bus} not found"
                )

        for trafo in self.transformers.values():
            if trafo.from_bus not in bus_ids:
                errors.append(
                    f"Transformer {trafo.trafo_id}: from_bus {trafo.from_bus} not found"
                )
            if trafo.to_bus not in bus_ids:
                errors.append(
                    f"Transformer {trafo.trafo_id}: to_bus {trafo.to_bus} not found"
                )

        for gen in self.generators.values():
            if gen.bus_id not in bus_ids:
                errors.append(
                    f"Generator {gen.gen_id}: bus {gen.bus_id} not found"
                )

        for load in self.loads.values():
            if load.bus_id not in bus_ids:
                errors.append(
                    f"Load {load.load_id}: bus {load.bus_id} not found"
                )

        # Check connectivity (simple: every bus must have at least one
        # incident branch)
        bus_branch_count: dict[int, int] = {bid: 0 for bid in bus_ids}
        for line in self.lines.values():
            if line.status:
                bus_branch_count[line.from_bus] = (
                    bus_branch_count.get(line.from_bus, 0) + 1
                )
                bus_branch_count[line.to_bus] = (
                    bus_branch_count.get(line.to_bus, 0) + 1
                )
        for trafo in self.transformers.values():
            if trafo.status:
                bus_branch_count[trafo.from_bus] = (
                    bus_branch_count.get(trafo.from_bus, 0) + 1
                )
                bus_branch_count[trafo.to_bus] = (
                    bus_branch_count.get(trafo.to_bus, 0) + 1
                )

        for bid, count in bus_branch_count.items():
            if count == 0:
                errors.append(
                    f"Bus {bid} is isolated (no connected branches)"
                )

        return errors

    # -- Conversion to C++ engine -----------------------------------------

    def to_cpp_system(self) -> Any:  # PowerSystem
        """Convert this Python network to a C++ :class:`PowerSystem`.

        Returns
        -------
        powsy365_core.PowerSystem
            Fully populated C++ engine object ready for analysis.
        """
        import powsy365_core as psc  # type: ignore[import-untyped]

        ps = psc.PowerSystem(self.base_mva)
        ps.name = self.name

        # Map bus type strings to enum
        type_map = {
            "PQ": psc.BusType.PQ,
            "PV": psc.BusType.PV,
            "SLACK": psc.BusType.Slack,
            "Slack": psc.BusType.Slack,
            "slack": psc.BusType.Slack,
        }

        for bd in self.buses.values():
            bus = psc.Bus(
                id=bd.bus_id,
                name=bd.name,
                type=type_map.get(bd.type, psc.BusType.PQ),
                base_kv=bd.base_kv,
                vmin=bd.vmin,
                vmax=bd.vmax,
                voltage=complex(bd.vm, np.radians(bd.va_deg)),
                generation=complex(0.0, 0.0),
            )
            bus.shunt_conductance = bd.gs
            bus.shunt_susceptance = bd.bs
            bus.area = bd.area
            bus.zone = bd.zone
            ps.addBus(bus)

        for ld in self.lines.values():
            line = psc.Line(
                id=ld.line_id,
                from_bus=ld.from_bus,
                to_bus=ld.to_bus,
                r=ld.r,
                x=ld.x,
                b=ld.b,
                rate_a=ld.rate_a,
            )
            line.rate_b = ld.rate_b
            line.rate_c = ld.rate_c
            line.status = ld.status
            ps.addLine(line)

        for td in self.transformers.values():
            trafo = psc.Transformer(
                id=td.trafo_id,
                from_bus=td.from_bus,
                to_bus=td.to_bus,
                r=td.r,
                x=td.x,
                tap=td.tap,
                shift=td.shift_deg,
            )
            trafo.rate_a = td.rate_a
            trafo.status = td.status
            ps.addTransformer(trafo)

        for gd in self.generators.values():
            gen = psc.Generator(
                id=gd.gen_id,
                bus_id=gd.bus_id,
                pg=gd.pg,
                qg=gd.qg,
                qmin=gd.qmin,
                qmax=gd.qmax,
                vg=gd.vg,
                mbase=gd.mbase,
                pg_min=gd.pg_min,
                pg_max=gd.pg_max,
            )
            gen.status = gd.status
            gen.cost_a = gd.cost_a
            gen.cost_b = gd.cost_b
            gen.cost_c = gd.cost_c
            ps.addGenerator(gen)

        for ldd in self.loads.values():
            load = psc.Load(
                id=ldd.load_id,
                bus_id=ldd.bus_id,
                pd=ldd.pd,
                qd=ldd.qd,
                ip=ldd.ip,
                iq=ldd.iq,
                yp=ldd.yp,
                yq=ldd.yq,
            )
            load.status = ldd.status
            ps.addLoad(load)

        return ps

    # -- Serialization: JSON ----------------------------------------------

    def to_json(self, filepath: str | Path | None = None) -> str:
        """Serialize the network to JSON.

        Parameters
        ----------
        filepath:
            If provided, write JSON to this path and return the path.
            Otherwise return the JSON string.

        Returns
        -------
        str
            JSON representation (or file path if *filepath* given).
        """
        data = {
            "name": self.name,
            "base_mva": self.base_mva,
            "buses": [asdict(b) for b in self.buses.values()],
            "lines": [asdict(ln) for ln in self.lines.values()],
            "transformers": [asdict(t) for t in self.transformers.values()],
            "generators": [asdict(g) for g in self.generators.values()],
            "loads": [asdict(ld) for ld in self.loads.values()],
        }
        json_str = json.dumps(data, indent=2, default=str)
        if filepath is not None:
            path = Path(filepath)
            path.write_text(json_str, encoding="utf-8")
            logger.info("Network saved to %s", path)
            return str(path)
        return json_str

    @classmethod
    def from_json(cls, source: str | Path) -> Self:
        """Load a network from a JSON file or string.

        Parameters
        ----------
        source:
            File path (string or Path) or raw JSON string.
        """
        path = Path(source)
        if path.is_file():
            text = path.read_text(encoding="utf-8")
        else:
            text = str(source)

        data = json.loads(text)
        net = cls(name=data.get("name", ""), base_mva=data.get("base_mva", 100.0))

        for bd in data.get("buses", []):
            # JSON uses 'type' (from asdict) but add_bus expects 'bus_type'
            bd_copy = dict(bd)
            if "type" in bd_copy and "bus_type" not in bd_copy:
                bd_copy["bus_type"] = bd_copy.pop("type")
            net.add_bus(**bd_copy)
        for ld in data.get("lines", []):
            net.add_line(**ld)
        for td in data.get("transformers", []):
            # Handle legacy key name
            kwargs = {k: v for k, v in td.items() if k != "shift_deg" or "shift_deg" in td}
            if "shift_deg" in td and "shift_deg" not in TransformerData.__dataclass_fields__:
                td["shift_deg"] = td.pop("shift_deg", 0.0)
            net.add_transformer(**td)
        for gd in data.get("generators", []):
            net.add_generator(**gd)
        for ldd in data.get("loads", []):
            net.add_load(**ldd)

        logger.info("Network loaded from %s", source)
        return net

    # -- Serialization: CSV -----------------------------------------------

    def to_csv(self, directory: str | Path) -> list[str]:
        """Write network elements to CSV files in *directory*.

        Returns
        -------
        list[str]
            Paths of the written files.
        """
        dir_path = Path(directory)
        dir_path.mkdir(parents=True, exist_ok=True)
        written: list[str] = []

        def write_csv(filename: str, rows: list[dict[str, Any]]) -> None:
            if not rows:
                return
            fpath = dir_path / filename
            with fpath.open("w", newline="", encoding="utf-8") as fh:
                writer = csv.DictWriter(fh, fieldnames=rows[0].keys())
                writer.writeheader()
                writer.writerows(rows)
            written.append(str(fpath))

        write_csv("buses.csv", [asdict(b) for b in self.buses.values()])
        write_csv("lines.csv", [asdict(ln) for ln in self.lines.values()])
        write_csv(
            "transformers.csv", [asdict(t) for t in self.transformers.values()]
        )
        write_csv(
            "generators.csv", [asdict(g) for g in self.generators.values()]
        )
        write_csv("loads.csv", [asdict(ld) for ld in self.loads.values()])

        logger.info("Network CSVs written to %s", dir_path)
        return written

    @classmethod
    def from_csv(cls, directory: str | Path) -> Self:
        """Load a network from CSV files in *directory*."""
        dir_path = Path(directory)
        net = cls()

        def read_csv(filename: str) -> list[dict[str, str]]:
            fpath = dir_path / filename
            if not fpath.exists():
                return []
            with fpath.open("r", encoding="utf-8") as fh:
                return list(csv.DictReader(fh))

        for row in read_csv("buses.csv"):
            net.add_bus(
                bus_id=int(row["bus_id"]),
                name=row.get("name", ""),
                bus_type=row.get("type", "PQ"),
                base_kv=float(row.get("base_kv", 1.0)),
                vmin=float(row.get("vmin", 0.9)),
                vmax=float(row.get("vmax", 1.1)),
                vm=float(row.get("vm", 1.0)),
                va_deg=float(row.get("va_deg", 0.0)),
                gs=float(row.get("gs", 0.0)),
                bs=float(row.get("bs", 0.0)),
                area=int(row.get("area", 1)),
                zone=int(row.get("zone", 1)),
            )

        for row in read_csv("lines.csv"):
            net.add_line(
                line_id=int(row["line_id"]),
                from_bus=int(row["from_bus"]),
                to_bus=int(row["to_bus"]),
                r=float(row.get("r", 0.0)),
                x=float(row.get("x", 0.1)),
                b=float(row.get("b", 0.0)),
                rate_a=float(row.get("rate_a", 0.0)),
                rate_b=float(row.get("rate_b", 0.0)),
                rate_c=float(row.get("rate_c", 0.0)),
                status=int(row.get("status", 1)),
            )

        logger.info("Network loaded from CSV directory %s", dir_path)
        return net

    # -- IEEE test cases --------------------------------------------------

    @classmethod
    def ieee14(cls) -> Self:
        """Return the IEEE 14-bus test system as a :class:`Network`."""
        net = cls(name="IEEE 14-Bus", base_mva=100.0)

        # Buses (id, name, type, base_kv, vmin, vmax, vm, va)
        buses = [
            (1, "Bus 1", "Slack", 138.0, 0.9, 1.1, 1.06, 0.0),
            (2, "Bus 2", "PV", 138.0, 0.9, 1.1, 1.045, -4.98),
            (3, "Bus 3", "PV", 138.0, 0.9, 1.1, 1.01, -12.72),
            (4, "Bus 4", "PQ", 138.0, 0.9, 1.1, 1.019, -10.33),
            (5, "Bus 5", "PQ", 138.0, 0.9, 1.1, 1.02, -8.78),
            (6, "Bus 6", "PQ", 138.0, 0.9, 1.1, 1.07, -14.22),
            (7, "Bus 7", "PQ", 138.0, 0.9, 1.1, 1.062, -13.37),
            (8, "Bus 8", "PV", 138.0, 0.9, 1.1, 1.09, -13.36),
            (9, "Bus 9", "PQ", 138.0, 0.9, 1.1, 1.056, -14.94),
            (10, "Bus 10", "PQ", 138.0, 0.9, 1.1, 1.051, -15.1),
            (11, "Bus 11", "PQ", 138.0, 0.9, 1.1, 1.057, -14.79),
            (12, "Bus 12", "PQ", 138.0, 0.9, 1.1, 1.055, -15.07),
            (13, "Bus 13", "PQ", 138.0, 0.9, 1.1, 1.05, -15.16),
            (14, "Bus 14", "PQ", 138.0, 0.9, 1.1, 1.036, -16.04),
        ]
        for b in buses:
            net.add_bus(*b)

        # Lines (id, from, to, r, x, b, rate_a)
        lines = [
            (1, 1, 2, 0.01938, 0.05917, 0.0528, 200.0),
            (2, 1, 5, 0.05403, 0.22304, 0.0492, 100.0),
            (3, 2, 3, 0.04699, 0.19797, 0.0438, 100.0),
            (4, 2, 4, 0.05811, 0.17632, 0.0340, 100.0),
            (5, 2, 5, 0.05695, 0.17388, 0.0346, 100.0),
            (6, 3, 4, 0.06701, 0.17103, 0.0128, 100.0),
            (7, 4, 5, 0.01335, 0.04211, 0.0, 100.0),
            (8, 4, 7, 0.0, 0.20912, 0.0, 100.0),
            (9, 4, 9, 0.0, 0.55618, 0.0, 100.0),
            (10, 5, 6, 0.0, 0.25202, 0.0, 100.0),
            (11, 6, 11, 0.09498, 0.19890, 0.0, 100.0),
            (12, 6, 12, 0.12291, 0.25581, 0.0, 100.0),
            (13, 6, 13, 0.06615, 0.13027, 0.0, 100.0),
            (14, 7, 8, 0.0, 0.17615, 0.0, 100.0),
            (15, 7, 9, 0.0, 0.11001, 0.0, 100.0),
            (16, 9, 10, 0.03181, 0.08450, 0.0, 100.0),
            (17, 9, 14, 0.12711, 0.27038, 0.0, 100.0),
            (18, 10, 11, 0.08205, 0.19207, 0.0, 100.0),
            (19, 12, 13, 0.22092, 0.19988, 0.0, 100.0),
            (20, 13, 14, 0.17093, 0.34802, 0.0, 100.0),
        ]
        for ln in lines:
            net.add_line(*ln)

        # Generators (id, bus, pg, qg, qmin, qmax, vg)
        gens = [
            (1, 1, 232.4, -16.9, -9999.0, 9999.0, 1.06),
            (2, 2, 40.0, 43.56, -40.0, 50.0, 1.045),
            (3, 3, 0.0, 25.08, 0.0, 40.0, 1.01),
            (4, 6, 0.0, 12.73, -6.0, 24.0, 1.07),
            (5, 8, 0.0, 17.62, -6.0, 24.0, 1.09),
        ]
        for g in gens:
            net.add_generator(*g)

        # Loads (id, bus, pd, qd)
        loads = [
            (1, 2, 21.7, 12.7),
            (2, 3, 94.2, 19.0),
            (3, 4, 47.8, -3.9),
            (4, 5, 7.6, 1.6),
            (5, 6, 11.2, 7.5),
            (6, 9, 29.5, 16.6),
            (7, 10, 9.0, 5.8),
            (8, 11, 3.5, 1.8),
            (9, 12, 6.1, 1.6),
            (10, 13, 13.5, 5.8),
            (11, 14, 14.9, 5.0),
        ]
        for ld in loads:
            net.add_load(*ld)

        errors = net.validate()
        if errors:
            raise ValidationError(f"IEEE14 build failed: {errors}")
        logger.info("IEEE 14-bus network created (%d buses, %d lines)",
                     len(net.buses), len(net.lines))
        return net

    # -- Internal helpers -------------------------------------------------

    def _next_element_id(self) -> int:
        """Return the next available element ID."""
        while self._element_id_counter in {
            *self.lines,
            *self.transformers,
            *self.generators,
            *self.loads,
        }:
            self._element_id_counter += 1
        rid = self._element_id_counter
        self._element_id_counter += 1
        return rid

    def __repr__(self) -> str:
        return (
            f"<Network '{self.name}' buses={len(self.buses)} "
            f"lines={len(self.lines)} transformers={len(self.transformers)} "
            f"gens={len(self.generators)} loads={len(self.loads)}>"
        )


class ValidationError(Exception):
    """Raised when network data fails validation."""
