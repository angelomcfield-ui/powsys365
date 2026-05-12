"""
POWSYS365 - Report Templates Module
Generates structured report dictionaries for different analysis types.
"""

from typing import Dict, List, Any, Optional
from dataclasses import dataclass, field
from datetime import datetime


# ---------------------------------------------------------------------------
# Data structures
# ---------------------------------------------------------------------------
@dataclass
class BusResult:
    """Bus analysis result data."""
    bus_id: str
    name: str
    base_kv: float = 0.0
    voltage_kv: float = 0.0
    voltage_pu: float = 1.0
    angle_deg: float = 0.0
    p_mw: float = 0.0
    q_mvar: float = 0.0
    p_gen_mw: float = 0.0
    q_gen_mvar: float = 0.0
    p_load_mw: float = 0.0
    q_load_mvar: float = 0.0


@dataclass
class LineResult:
    """Line analysis result data."""
    line_id: str
    from_bus: str
    to_bus: str
    p_from_mw: float = 0.0
    q_from_mvar: float = 0.0
    p_to_mw: float = 0.0
    q_to_mvar: float = 0.0
    loading_percent: float = 0.0
    current_a: float = 0.0
    losses_mw: float = 0.0
    losses_mvar: float = 0.0


@dataclass
class GeneratorResult:
    """Generator analysis result data."""
    gen_id: str
    bus_id: str
    name: str
    p_mw: float = 0.0
    q_mvar: float = 0.0
    p_max_mw: float = 0.0
    p_min_mw: float = 0.0
    q_max_mvar: float = 0.0
    q_min_mvar: float = 0.0
    status: int = 1
    fuel_cost: float = 0.0


@dataclass
class LoadFlowResult:
    """Load flow analysis complete result."""
    converged: bool = False
    iterations: int = 0
    tolerance: float = 1e-6
    total_p_gen_mw: float = 0.0
    total_q_gen_mvar: float = 0.0
    total_p_load_mw: float = 0.0
    total_q_load_mvar: float = 0.0
    total_losses_mw: float = 0.0
    total_losses_mvar: float = 0.0
    buses: List[BusResult] = field(default_factory=list)
    lines: List[LineResult] = field(default_factory=list)
    generators: List[GeneratorResult] = field(default_factory=list)
    computation_time_sec: float = 0.0


@dataclass
class ShortCircuitResult:
    """Short circuit analysis result."""
    fault_bus: str = ""
    fault_type: str = ""
    fault_current_ka: float = 0.0
    fault_mva: float = 0.0
    r_x_ratio: float = 0.0
    peak_current_ka: float = 0.0
    breaking_current_ka: float = 0.0
    steady_state_current_ka: float = 0.0
    fault_contributions: Dict[str, float] = field(default_factory=dict)
    bus_voltages_during_fault: Dict[str, float] = field(default_factory=dict)
    computation_time_sec: float = 0.0


@dataclass
class StabilityResult:
    """Transient stability analysis result."""
    stable: bool = False
    critical_clearing_time_sec: float = 0.0
    max_rotor_angle_deviation_deg: float = 0.0
    damping_ratio: float = 0.0
    oscillation_frequency_hz: float = 0.0
    time_series: List[Dict[str, Any]] = field(default_factory=list)
    generator_angles: Dict[str, List[float]] = field(default_factory=dict)
    bus_voltages: Dict[str, List[float]] = field(default_factory=dict)
    frequencies: List[float] = field(default_factory=list)
    time_points: List[float] = field(default_factory=list)
    fault_applied_at_sec: float = 0.0
    fault_cleared_at_sec: float = 0.0
    computation_time_sec: float = 0.0


# ---------------------------------------------------------------------------
# Report template functions
# ---------------------------------------------------------------------------

def load_flow_report(result: LoadFlowResult) -> Dict[str, Any]:
    """
    Generate a load flow report dictionary from analysis results.

    Args:
        result: LoadFlowResult with bus, line, and generator data

    Returns:
        Dict with structured report sections
    """
    report = {
        "report_type": "Load Flow Analysis",
        "generated_at": datetime.now().isoformat(),
        "summary": {},
        "sections": []
    }

    # Executive Summary
    report["summary"] = {
        "convergence": "CONVERGED" if result.converged else "NOT CONVERGED",
        "iterations": result.iterations,
        "total_generation_mw": round(result.total_p_gen_mw, 2),
        "total_load_mw": round(result.total_p_load_mw, 2),
        "total_losses_mw": round(result.total_losses_mw, 4),
        "losses_percent": round(
            (result.total_losses_mw / result.total_p_gen_mw * 100), 4
        ) if result.total_p_gen_mw > 0 else 0.0,
        "computation_time_sec": round(result.computation_time_sec, 4),
    }

    # Section 1: System Summary
    sections = []

    sections.append({
        "type": "executive_summary",
        "title": "Executive Summary",
        "content": (
            f"Load flow analysis {'converged' if result.converged else 'did not converge'} "
            f"in {result.iterations} iterations. "
            f"Total system generation is {result.total_p_gen_mw:.2f} MW "
            f"supplying {result.total_p_load_mw:.2f} MW of load "
            f"with {result.total_losses_mw:.4f} MW ({report['summary']['losses_percent']:.4f}%) "
            f"of transmission losses. Computation time: {result.computation_time_sec:.4f} seconds."
        )
    })

    # Section 2: Bus Results Table
    bus_rows = []
    for bus in result.buses:
        bus_rows.append([
            bus.bus_id,
            bus.name,
            f"{bus.base_kv:.1f}",
            f"{bus.voltage_kv:.3f}",
            f"{bus.voltage_pu:.4f}",
            f"{bus.angle_deg:.2f}",
            f"{bus.p_mw:.2f}",
            f"{bus.q_mvar:.2f}",
            "OK" if 0.95 <= bus.voltage_pu <= 1.05 else "VIOLATION"
        ])

    sections.append({
        "type": "tabular_data",
        "title": "Bus Voltage Results",
        "headers": ["Bus ID", "Name", "Base kV", "Voltage kV", "V (pu)",
                     "Angle (deg)", "P (MW)", "Q (Mvar)", "Status"],
        "rows": bus_rows,
        "highlight_condition": lambda row: row[-1] == "VIOLATION"
    })

    # Section 3: Line Loading
    line_rows = []
    for line in result.lines:
        line_rows.append([
            line.line_id,
            line.from_bus,
            line.to_bus,
            f"{line.p_from_mw:.2f}",
            f"{line.q_from_mvar:.2f}",
            f"{line.loading_percent:.1f}",
            f"{line.losses_mw:.4f}",
            "OK" if line.loading_percent < 100.0 else "OVERLOAD"
        ])

    sections.append({
        "type": "tabular_data",
        "title": "Line Loading Results",
        "headers": ["Line ID", "From", "To", "P (MW)", "Q (Mvar)",
                     "Loading %", "Losses (MW)", "Status"],
        "rows": line_rows,
        "highlight_condition": lambda row: row[-1] == "OVERLOAD"
    })

    # Section 4: Generator Dispatch
    gen_rows = []
    for gen in result.generators:
        loading = (gen.p_mw / gen.p_max_mw * 100) if gen.p_max_mw > 0 else 0.0
        gen_rows.append([
            gen.gen_id,
            gen.bus_id,
            gen.name,
            f"{gen.p_mw:.2f}",
            f"{gen.q_mvar:.2f}",
            f"{gen.p_max_mw:.2f}",
            f"{loading:.1f}",
            "Online" if gen.status == 1 else "Offline",
            f"${gen.fuel_cost:.2f}/MWh" if gen.fuel_cost > 0 else "N/A"
        ])

    sections.append({
        "type": "tabular_data",
        "title": "Generator Dispatch",
        "headers": ["Gen ID", "Bus", "Name", "P (MW)", "Q (Mvar)",
                     "P Max", "Loading %", "Status", "Cost"],
        "rows": gen_rows
    })

    # Section 5: Voltage violations summary
    voltage_violations = [b for b in result.buses if b.voltage_pu < 0.95 or b.voltage_pu > 1.05]
    if voltage_violations:
        violation_rows = []
        for bus in voltage_violations:
            violation_rows.append([
                bus.bus_id,
                bus.name,
                f"{bus.voltage_pu:.4f}",
                "Undervoltage" if bus.voltage_pu < 0.95 else "Overvoltage",
                f"{abs(1.0 - bus.voltage_pu) * 100:.2f}%"
            ])

        sections.append({
            "type": "tabular_data",
            "title": "Voltage Violations",
            "headers": ["Bus ID", "Name", "V (pu)", "Type", "Deviation"],
            "rows": violation_rows,
            "warning": True
        })

    # Section 6: Conclusions
    conclusions = [
        f"System converged in {result.iterations} iterations with tolerance {result.tolerance:.0e}.",
        f"Total losses are {report['summary']['losses_percent']:.4f}% of generation.",
    ]
    if voltage_violations:
        conclusions.append(
            f"WARNING: {len(voltage_violations)} buses have voltage violations. "
            "Consider tap changer adjustments or reactive power compensation."
        )
    overloads = [l for l in result.lines if l.loading_percent >= 100.0]
    if overloads:
        conclusions.append(
            f"WARNING: {len(overloads)} lines are overloaded. "
            "Consider load shedding or network reinforcement."
        )

    sections.append({
        "type": "conclusions",
        "title": "Conclusions and Recommendations",
        "content": conclusions
    })

    report["sections"] = sections
    return report


def short_circuit_report(result: ShortCircuitResult) -> Dict[str, Any]:
    """
    Generate a short circuit report dictionary from analysis results.

    Args:
        result: ShortCircuitResult with fault analysis data

    Returns:
        Dict with structured report sections
    """
    report = {
        "report_type": "Short Circuit Analysis",
        "generated_at": datetime.now().isoformat(),
        "summary": {},
        "sections": []
    }

    report["summary"] = {
        "fault_bus": result.fault_bus,
        "fault_type": result.fault_type,
        "fault_current_ka": round(result.fault_current_ka, 3),
        "fault_mva": round(result.fault_mva, 2),
        "peak_current_ka": round(result.peak_current_ka, 3),
        "breaking_current_ka": round(result.breaking_current_ka, 3),
        "computation_time_sec": round(result.computation_time_sec, 4),
    }

    sections = []

    sections.append({
        "type": "executive_summary",
        "title": "Executive Summary",
        "content": (
            f"Short circuit analysis at bus {result.fault_bus} "
            f"({result.fault_type}). Fault current: {result.fault_current_ka:.3f} kA "
            f"({result.fault_mva:.2f} MVA). R/X ratio: {result.r_x_ratio:.3f}. "
            f"Peak current: {result.peak_current_ka:.3f} kA. "
            f"Breaking current: {result.breaking_current_ka:.3f} kA."
        )
    })

    # Fault contributions
    contrib_rows = []
    for source_id, current_ka in sorted(
        result.fault_contributions.items(),
        key=lambda x: x[1],
        reverse=True
    ):
        pct = (current_ka / result.fault_current_ka * 100) if result.fault_current_ka > 0 else 0.0
        contrib_rows.append([
            source_id,
            f"{current_ka:.3f}",
            f"{pct:.1f}%"
        ])

    sections.append({
        "type": "tabular_data",
        "title": "Fault Current Contributions",
        "headers": ["Source", "Current (kA)", "Contribution"],
        "rows": contrib_rows
    })

    # Bus voltages during fault
    voltage_rows = []
    for bus_id, voltage_pu in sorted(result.bus_voltages_during_fault.items()):
        voltage_rows.append([
            bus_id,
            f"{voltage_pu:.4f}",
            "Collapsed" if voltage_pu < 0.7 else "Low" if voltage_pu < 0.9 else "Normal"
        ])

    sections.append({
        "type": "tabular_data",
        "title": "Bus Voltages During Fault",
        "headers": ["Bus ID", "Voltage (pu)", "Status"],
        "rows": voltage_rows
    })

    sections.append({
        "type": "conclusions",
        "title": "Conclusions",
        "content": [
            f"Fault at bus {result.fault_bus} produces {result.fault_current_ka:.3f} kA "
            f"fault current ({result.fault_mva:.2f} MVA).",
            f"R/X ratio of {result.r_x_ratio:.3f} indicates "
            f"{'high DC offset' if result.r_x_ratio < 0.3 else 'moderate DC offset' if result.r_x_ratio < 0.7 else 'low DC offset'}.",
            f"Breaker rating should exceed {result.breaking_current_ka:.3f} kA breaking current.",
        ]
    })

    report["sections"] = sections
    return report


def stability_report(result: StabilityResult) -> Dict[str, Any]:
    """
    Generate a transient stability report dictionary from analysis results.

    Args:
        result: StabilityResult with stability analysis data

    Returns:
        Dict with structured report sections
    """
    report = {
        "report_type": "Transient Stability Analysis",
        "generated_at": datetime.now().isoformat(),
        "summary": {},
        "sections": []
    }

    report["summary"] = {
        "stable": result.stable,
        "critical_clearing_time_sec": round(result.critical_clearing_time_sec, 4),
        "max_rotor_deviation_deg": round(result.max_rotor_angle_deviation_deg, 2),
        "damping_ratio": round(result.damping_ratio, 4),
        "oscillation_frequency_hz": round(result.oscillation_frequency_hz, 3),
        "computation_time_sec": round(result.computation_time_sec, 4),
        "fault_applied_at": result.fault_applied_at_sec,
        "fault_cleared_at": result.fault_cleared_at_sec,
    }

    sections = []

    stability_status = "STABLE" if result.stable else "UNSTABLE"
    sections.append({
        "type": "executive_summary",
        "title": "Executive Summary",
        "content": (
            f"System is {stability_status} following fault applied at t={result.fault_applied_at_sec:.3f}s "
            f"and cleared at t={result.fault_cleared_at_sec:.3f}s. "
            f"Critical clearing time: {result.critical_clearing_time_sec:.4f}s. "
            f"Maximum rotor angle deviation: {result.max_rotor_angle_deviation_deg:.2f} degrees. "
            f"Damping ratio: {result.damping_ratio:.4f}, "
            f"oscillation frequency: {result.oscillation_frequency_hz:.3f} Hz."
        )
    })

    # Generator angle summary
    angle_rows = []
    for gen_id, angles in result.generator_angles.items():
        if angles:
            max_angle = max(abs(a) for a in angles)
            final_angle = angles[-1]
            angle_rows.append([
                gen_id,
                f"{angles[0]:.2f}",
                f"{max_angle:.2f}",
                f"{final_angle:.2f}",
                "Stable" if result.stable else "Unstable"
            ])

    sections.append({
        "type": "tabular_data",
        "title": "Generator Rotor Angle Summary",
        "headers": ["Generator", "Initial (deg)", "Max (deg)", "Final (deg)", "Status"],
        "rows": angle_rows
    })

    # Frequency analysis
    if result.frequencies:
        min_freq = min(result.frequencies)
        max_freq = max(result.frequencies)
        nadir_freq = min_freq
        sections.append({
            "type": "tabular_data",
            "title": "Frequency Response Summary",
            "headers": ["Metric", "Value"],
            "rows": [
                ["Minimum Frequency", f"{min_freq:.3f} Hz"],
                ["Maximum Frequency", f"{max_freq:.3f} Hz"],
                ["Frequency Nadir", f"{nadir_freq:.3f} Hz"],
                ["Damping Ratio", f"{result.damping_ratio:.4f}"],
                ["Oscillation Frequency", f"{result.oscillation_frequency_hz:.3f} Hz"],
            ]
        })

    conclusions = [
        f"System stability: {stability_status}.",
        f"Critical clearing time: {result.critical_clearing_time_sec:.4f} seconds. "
        f"Actual clearing time was {result.fault_cleared_at_sec - result.fault_applied_at_sec:.4f} seconds.",
    ]
    if result.stable:
        conclusions.append(
            f"System remains stable with damping ratio of {result.damping_ratio:.4f}. "
            f"Oscillations decay with frequency {result.oscillation_frequency_hz:.3f} Hz."
        )
    else:
        conclusions.append(
            "WARNING: System is unstable. Consider faster protection schemes, "
            "additional damping, or generation rescheduling."
        )

    sections.append({
        "type": "conclusions",
        "title": "Conclusions and Recommendations",
        "content": conclusions
    })

    report["sections"] = sections
    return report


def comparison_report(results_list: List[Dict[str, Any]]) -> Dict[str, Any]:
    """
    Generate a comparison report from multiple analysis results.

    Args:
        results_list: List of result dictionaries to compare

    Returns:
        Dict with structured comparison report
    """
    report = {
        "report_type": "Comparative Analysis",
        "generated_at": datetime.now().isoformat(),
        "summary": {
            "num_cases": len(results_list),
            "case_names": [r.get("case_name", f"Case {i+1}")
                           for i, r in enumerate(results_list)]
        },
        "sections": []
    }

    sections = []

    sections.append({
        "type": "executive_summary",
        "title": "Executive Summary",
        "content": f"Comparison of {len(results_list)} analysis cases: "
                    f"{', '.join(report['summary']['case_names'])}."
    })

    # Comparison table
    if results_list:
        headers = ["Metric"] + report["summary"]["case_names"]
        rows = []

        metrics = [
            ("Total Generation (MW)", "total_p_gen_mw"),
            ("Total Load (MW)", "total_p_load_mw"),
            ("Total Losses (MW)", "total_losses_mw"),
            ("Losses (%)", "losses_percent"),
            ("Iterations", "iterations"),
            ("Computation Time (s)", "computation_time_sec"),
        ]

        for metric_name, key in metrics:
            row = [metric_name]
            for r in results_list:
                summary = r.get("summary", {})
                val = summary.get(key, "N/A")
                if isinstance(val, float):
                    row.append(f"{val:.4f}")
                else:
                    row.append(str(val))
            rows.append(row)

        sections.append({
            "type": "tabular_data",
            "title": "Case Comparison",
            "headers": headers,
            "rows": rows
        })

    sections.append({
        "type": "conclusions",
        "title": "Conclusions",
        "content": [
            f"Compared {len(results_list)} cases with varying operating conditions.",
            "Refer to individual case reports for detailed analysis."
        ]
    })

    report["sections"] = sections
    return report


# ---------------------------------------------------------------------------
# Utility functions
# ---------------------------------------------------------------------------

def format_value(value: Any, precision: int = 4) -> str:
    """Format a numeric value with specified precision."""
    if isinstance(value, float):
        return f"{value:.{precision}f}"
    return str(value)


def check_voltage_violation(voltage_pu: float,
                             vmin: float = 0.95,
                             vmax: float = 1.05) -> str:
    """Check if voltage is within acceptable range."""
    if voltage_pu < vmin:
        return f"UNDERVOLTAGE ({(vmin - voltage_pu) * 100:.2f}%)"
    elif voltage_pu > vmax:
        return f"OVERVOLTAGE ({(voltage_pu - vmax) * 100:.2f}%)"
    return "OK"


def check_loading_violation(loading_percent: float,
                             max_loading: float = 100.0) -> str:
    """Check if line loading is within acceptable range."""
    if loading_percent > max_loading:
        return f"OVERLOAD ({loading_percent - max_loading:.1f}%)"
    elif loading_percent > max_loading * 0.9:
        return f"WARNING ({loading_percent:.1f}%)"
    return "OK"


def generate_csv_from_report(report: Dict[str, Any],
                               section_idx: int = 0) -> str:
    """
    Generate CSV string from a report tabular section.

    Args:
        report: Report dictionary
        section_idx: Index of the tabular section to export

    Returns:
        CSV formatted string
    """
    lines = []
    count = 0
    for section in report.get("sections", []):
        if section.get("type") == "tabular_data":
            if count == section_idx:
                lines.append(",".join(f'"{h}"' for h in section.get("headers", [])))
                for row in section.get("rows", []):
                    lines.append(",".join(f'"{str(c)}"' for c in row))
                break
            count += 1

    return "\n".join(lines)
