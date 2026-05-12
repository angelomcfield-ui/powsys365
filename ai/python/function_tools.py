"""
ai/python/function_tools.py
===========================

Function calling tools for LLM integration with POWSYS365.

Each tool exposes a power-system analysis capability via a JSON Schema
that is compatible with OpenAI Function Calling, Anthropic tool use,
and other LLM tool-calling protocols.

Tools provided:
- ``analyze_power_flow``: Run AC power flow analysis
- ``detect_faults``: Diagnose fault conditions
- ``optimize_generation``: Generation optimization recommendations
- ``check_voltage_stability``: Voltage stability screening
- "assess_line_loading": Evaluate branch loading

Usage::

    from powsy365.ai.function_tools import ToolRegistry
    from powsy365.ai.llm_providers import GPTProvider

    registry = ToolRegistry()
    provider = GPTProvider()

    # LLM will receive tool schemas and can call them
    resp = provider.chat(LLMRequest(
        messages=[Message("user", "Analyze the IEEE 14-bus system")],
        tools=registry.get_tool_schemas(),
        tool_choice="auto",
    ))
"""

from __future__ import annotations

import json
import logging
from dataclasses import asdict, dataclass, field
from typing import Any, Callable

logger = logging.getLogger(__name__)


# =========================================================================
# Tool definitions
# =========================================================================


@dataclass
class ToolParameter:
    """JSON Schema parameter definition for a tool."""

    name: str
    param_type: str  # "string", "number", "integer", "boolean", "object", "array"
    description: str
    required: bool = True
    enum: list[Any] | None = None
    default: Any = None

    def to_schema(self) -> dict[str, Any]:
        """Convert to JSON Schema property dict."""
        schema: dict[str, Any] = {
            "type": self.param_type,
            "description": self.description,
        }
        if self.enum:
            schema["enum"] = self.enum
        if self.default is not None:
            schema["default"] = self.default
        return schema


@dataclass
class ToolDefinition:
    """Definition of a callable tool with JSON Schema."""

    name: str
    description: str
    parameters: list[ToolParameter]
    func: Callable[..., dict[str, Any]] = field(repr=False)

    def to_openai_schema(self) -> dict[str, Any]:
        """Generate OpenAI-compatible function schema."""
        properties: dict[str, Any] = {}
        required: list[str] = []

        for param in self.parameters:
            properties[param.name] = param.to_schema()
            if param.required:
                required.append(param.name)

        return {
            "type": "function",
            "function": {
                "name": self.name,
                "description": self.description,
                "parameters": {
                    "type": "object",
                    "properties": properties,
                    "required": required,
                },
            },
        }

    def to_anthropic_schema(self) -> dict[str, Any]:
        """Generate Anthropic-compatible tool schema."""
        properties: dict[str, Any] = {}
        required: list[str] = []

        for param in self.parameters:
            properties[param.name] = param.to_schema()
            if param.required:
                required.append(param.name)

        return {
            "name": self.name,
            "description": self.description,
            "input_schema": {
                "type": "object",
                "properties": properties,
                "required": required,
            },
        }

    def to_deepseek_schema(self) -> dict[str, Any]:
        """DeepSeek uses OpenAI-compatible format."""
        return self.to_openai_schema()

    def execute(self, **kwargs: Any) -> dict[str, Any]:
        """Execute the tool with validated parameters."""
        try:
            return self.func(**kwargs)
        except Exception as exc:
            logger.error("Tool %s execution failed: %s", self.name, exc)
            return {
                "success": False,
                "error": str(exc),
                "tool": self.name,
            }


# =========================================================================
# Tool implementations
# =========================================================================


def _analyze_power_flow(
    bus_count: int = 14,
    base_mva: float = 100.0,
    solver_method: str = "newton_raphson",
    tolerance: float = 1e-6,
    max_iterations: int = 30,
) -> dict[str, Any]:
    """Run AC power flow analysis on a power system model.

    This implementation uses the POWSYS365 C++ engine when available,
    falling back to a simplified Python implementation for demonstration.
    """
    t0 = __import__("time").time()

    try:
        import powsy365 as ps

        # Create test system based on bus_count
        creators = {14: ps.create_ieee14, 30: ps.create_ieee30}
        if bus_count not in creators:
            return {
                "success": False,
                "error": f"No pre-built case for {bus_count} buses. "
                f"Available: {list(creators.keys())}",
            }

        network = creators[bus_count](base_mva)
        result = ps.power_flow(
            network,
            method=solver_method,
            tol=tolerance,
            max_iter=max_iterations,
        )

        # Format summary
        bus_summary = []
        for b in result.get("buses", [])[:20]:  # Limit output
            bus_summary.append(
                {
                    "bus_id": b.get("bus_id"),
                    "vm_pu": round(b.get("vm_pu", 0), 4),
                    "va_deg": round(b.get("va_deg", 0), 4),
                }
            )

        return {
            "success": True,
            "converged": result.get("converged", False),
            "iterations": result.get("iterations", 0),
            "elapsed_ms": result.get("elapsed_ms", 0),
            "total_pgen_mw": round(result.get("total_pgen_mw", 0), 2),
            "total_pload_mw": round(result.get("total_pload_mw", 0), 2),
            "total_ploss_mw": round(result.get("total_ploss_mw", 0), 2),
            "bus_count": len(result.get("buses", [])),
            "branch_count": len(result.get("branches", [])),
            "bus_summary": bus_summary,
            "method_used": result.get("method", solver_method),
        }

    except ImportError:
        # Fallback: return a simulated result
        logger.warning("powsy365 C++ engine not available; returning simulated result")
        return {
            "success": True,
            "converged": True,
            "iterations": 4,
            "elapsed_ms": 15.2,
            "total_pgen_mw": 272.4,
            "total_pload_mw": 259.0,
            "total_ploss_mw": 13.4,
            "bus_count": bus_count,
            "branch_count": bus_count + 5,
            "bus_summary": [
                {"bus_id": i, "vm_pu": round(1.0 + 0.05 * (i % 3 - 1), 4), "va_deg": round(-2.0 * i, 4)}
                for i in range(1, min(bus_count + 1, 6))
            ],
            "method_used": solver_method,
            "note": "Simulated result - C++ engine not available",
        }
    except Exception as exc:
        return {"success": False, "error": str(exc)}


def _detect_faults(
    fault_type: str = "three_phase",
    fault_location: str = "bus_1",
    system_description: str = "",
    base_mva: float = 100.0,
) -> dict[str, Any]:
    """Detect and diagnose fault conditions in a power system.

    Analyzes fault scenarios including three-phase, single-line-to-ground,
    line-to-line, and double-line-to-ground faults.
    """
    fault_analysis = {
        "three_phase": {
            "name": "Three-Phase Balanced Fault (L-L-L)",
            "severity": "Critical",
            "typical_fault_current_pu": 5.0,
            "description": "All three phases shorted together. Maximum fault current.",
        },
        "single_phase": {
            "name": "Single Line-to-Ground Fault (L-G)",
            "severity": "High",
            "typical_fault_current_pu": 3.0,
            "description": "One phase connected to ground. Most common fault type (70-80% of faults).",
        },
        "two_phase": {
            "name": "Line-to-Line Fault (L-L)",
            "severity": "High",
            "typical_fault_current_pu": 4.33,
            "description": "Two phases shorted together without ground.",
        },
        "two_phase_ground": {
            "name": "Double Line-to-Ground Fault (L-L-G)",
            "severity": "Critical",
            "typical_fault_current_pu": 4.0,
            "description": "Two phases connected to ground. Second most severe fault.",
        },
    }

    fault_key = fault_type.lower().replace(" ", "_").replace("-", "_")
    if fault_key not in fault_analysis:
        return {
            "success": False,
            "error": f"Unknown fault type: {fault_type}. "
            f"Available: {list(fault_analysis.keys())}",
        }

    info = fault_analysis[fault_key]

    # Generate diagnostic recommendations
    recommendations = [
        f"Verify relay coordination for {info['name']} at {fault_location}",
        "Check circuit breaker interrupting capacity",
        f"Expected fault current: {info['typical_fault_current_pu']:.1f} pu (approximate)",
        "Review protection scheme settings",
        "Assess equipment thermal and mechanical withstand ratings",
    ]

    if info["severity"] == "Critical":
        recommendations.insert(0, "IMMEDIATE: Isolate faulted section")
        recommendations.insert(1, "Activate emergency response procedures")

    return {
        "success": True,
        "fault_type": info["name"],
        "severity": info["severity"],
        "location": fault_location,
        "description": info["description"],
        "typical_fault_current_pu": info["typical_fault_current_pu"],
        "recommendations": recommendations,
        "protection_considerations": [
            "Overcurrent relays must operate within 3-5 cycles",
            "Distance protection Zone 1 covers 80-85% of line",
            "Differential protection for buses and transformers",
            "Ground fault protection sensitivity: 10-20% of rated current",
        ],
    }


def _optimize_generation(
    total_demand_mw: float = 200.0,
    generator_count: int = 5,
    objective: str = "min_cost",
    constraints: str = "",
) -> dict[str, Any]:
    """Provide generation optimization recommendations.

    Suggests optimal generator dispatch based on economic criteria,
    emission constraints, and system security requirements.
    """
    objectives = {
        "min_cost": {
            "name": "Minimize Generation Cost",
            "description": "Dispatch generators by merit order based on incremental cost",
        },
        "min_emissions": {
            "name": "Minimize Emissions",
            "description": "Prioritize low-emission generation sources",
        },
        "min_losses": {
            "name": "Minimize Transmission Losses",
            "description": "Dispatch considering transmission loss sensitivity factors",
        },
        "max_reliability": {
            "name": "Maximize Reliability",
            "description": "Maintain adequate spinning reserve and diversify generation",
        },
    }

    obj_key = objective.lower().replace(" ", "_")
    if obj_key not in objectives:
        return {
            "success": False,
            "error": f"Unknown objective: {objective}. "
            f"Available: {list(objectives.keys())}",
        }

    obj_info = objectives[obj_key]

    # Simple economic dispatch approximation
    gen_outputs = []
    remaining_load = total_demand_mw
    for i in range(generator_count):
        if remaining_load <= 0:
            break
        cap = min(remaining_load * 0.4, total_demand_mw / generator_count * 1.2)
        cap = min(cap, remaining_load)
        gen_outputs.append(
            {
                "generator_id": i + 1,
                "output_mw": round(cap, 2),
                "marginal_cost_approx": round(20.0 + 5.0 * i, 2),
            }
        )
        remaining_load -= cap

    reserve_mw = sum(g["output_mw"] for g in gen_outputs) * 0.15

    recommendations = [
        f"Objective: {obj_info['name']}",
        obj_info["description"],
        f"Total demand: {total_demand_mw} MW",
        f"Number of generators: {generator_count}",
        f"Required spinning reserve: ~{reserve_mw:.1f} MW (15% of load)",
        "",
        "General recommendations:",
        "1. Load most efficient generators to full capacity first",
        "2. Maintain minimum 10-15% spinning reserve",
        "3. Monitor transmission constraints during dispatch",
        "4. Consider startup/shutdown costs for unit commitment",
        "5. Implement ramp rate constraints (typically 3-5%/min for thermal units)",
    ]

    return {
        "success": True,
        "objective": obj_info["name"],
        "total_demand_mw": total_demand_mw,
        "generator_count": generator_count,
        "recommended_dispatch": gen_outputs,
        "spinning_reserve_mw": round(reserve_mw, 2),
        "recommendations": recommendations,
        "cost_factors": {
            "typical_thermal_cost_usd_mwh": 30.0,
            "typical_gas_cost_usd_mwh": 45.0,
            "typical_renewable_cost_usd_mwh": 0.0,
            "transmission_loss_factor": 0.03,
        },
    }


def _check_voltage_stability(
    bus_voltages_pu: list[float] | None = None,
    base_mva: float = 100.0,
    stability_margin_required: float = 0.15,
) -> dict[str, Any]:
    """Assess voltage stability of a power system.

    Evaluates voltage profiles against acceptable limits and computes
    stability margins.
    """
    if bus_voltages_pu is None:
        # Default IEEE 14-bus voltages
        bus_voltages_pu = [
            1.06, 1.045, 1.01, 1.019, 1.02, 1.07, 1.062,
            1.09, 1.056, 1.051, 1.057, 1.055, 1.05, 1.036,
        ]

    vmin_limit = 0.95
    vmax_limit = 1.05

    bus_assessments = []
    violation_count = 0
    low_voltage_count = 0

    for i, v in enumerate(bus_voltages_pu, 1):
        status = "normal"
        margin = min(v - vmin_limit, vmax_limit - v)
        if v < vmin_limit:
            status = "undervoltage"
            violation_count += 1
            low_voltage_count += 1
        elif v > vmax_limit:
            status = "overvoltage"
            violation_count += 1

        bus_assessments.append(
            {
                "bus_id": i,
                "voltage_pu": round(v, 4),
                "status": status,
                "margin_pu": round(margin, 4),
                "margin_percent": round(margin * 100, 2),
            }
        )

    overall_stable = violation_count == 0

    recommendations = []
    if low_voltage_count > 0:
        recommendations.extend([
            f"CRITICAL: {low_voltage_count} buses below {vmin_limit} pu voltage limit",
            "Consider adding reactive power support (capacitors, SVCs)",
            "Verify transformer tap settings",
            "Check for excessive reactive power demand",
            "Evaluate possibility of network reconfiguration",
        ])
    elif not overall_stable:
        recommendations.append("Address overvoltage conditions at affected buses")
    else:
        recommendations.extend([
            "All bus voltages within acceptable limits",
            f"Minimum stability margin: {min(b['margin_percent'] for b in bus_assessments):.1f}%",
            "Continue routine monitoring",
        ])

    # PV curve proximity indicator
    pv_proximity = 1.0 - min(
        b["voltage_pu"] for b in bus_assessments
    ) / vmin_limit

    return {
        "success": True,
        "overall_stable": overall_stable,
        "bus_count": len(bus_voltages_pu),
        "violation_count": violation_count,
        "low_voltage_count": low_voltage_count,
        "v_min_pu": round(min(bus_voltages_pu), 4),
        "v_max_pu": round(max(bus_voltages_pu), 4),
        "v_avg_pu": round(sum(bus_voltages_pu) / len(bus_voltages_pu), 4),
        "pv_curve_proximity": round(pv_proximity, 4),
        "bus_assessments": bus_assessments,
        "recommendations": recommendations,
        "stability_criteria": {
            "voltage_limit_min_pu": vmin_limit,
            "voltage_limit_max_pu": vmax_limit,
            "minimum_margin_required": stability_margin_required,
        },
    }


def _assess_line_loading(
    branch_loadings_percent: list[float] | None = None,
    normal_rating_percent: float = 100.0,
    emergency_rating_percent: float = 120.0,
) -> dict[str, Any]:
    """Evaluate transmission line and transformer loading levels.

    Identifies overloaded branches and provides capacity upgrade
    recommendations.
    """
    if branch_loadings_percent is None:
        # Default IEEE 14-bus approximate loadings
        branch_loadings_percent = [
            45.2, 38.5, 52.1, 41.3, 35.7, 28.9, 33.4,
            55.6, 22.1, 18.5, 42.3, 15.2, 8.7, 12.4,
            25.6, 31.2, 19.8, 7.5, 11.3, 5.2,
        ]

    overloaded = []
    near_limit = []
    normal = []

    for i, loading in enumerate(branch_loadings_percent, 1):
        info = {
            "branch_id": i,
            "loading_percent": round(loading, 2),
            "remaining_capacity_percent": round(100.0 - loading, 2),
        }
        if loading > emergency_rating_percent:
            overloaded.append(info)
        elif loading > normal_rating_percent * 0.85:
            near_limit.append(info)
        else:
            normal.append(info)

    recommendations = []
    if overloaded:
        rec_lines = [f"Branch {b['branch_id']}: {b['loading_percent']}%" for b in overloaded]
        recommendations.extend([
            f"CRITICAL: {len(overloaded)} branches exceed emergency rating ({emergency_rating_percent}%):",
            *rec_lines,
            "Immediate load transfer or generation redispatch required",
            "Consider temporary network reconfiguration",
            "Verify relay protection settings for overload conditions",
        ])
    elif near_limit:
        recommendations.extend([
            f"WARNING: {len(near_limit)} branches approaching normal rating",
            "Monitor closely during peak load periods",
            "Evaluate N-1 contingency compliance",
        ])
    else:
        recommendations.append("All branches within normal operating limits")

    max_loading = max(branch_loadings_percent) if branch_loadings_percent else 0
    avg_loading = sum(branch_loadings_percent) / len(branch_loadings_percent) if branch_loadings_percent else 0

    return {
        "success": True,
        "branch_count": len(branch_loadings_percent),
        "max_loading_percent": round(max_loading, 2),
        "avg_loading_percent": round(avg_loading, 2),
        "overloaded_count": len(overloaded),
        "near_limit_count": len(near_limit),
        "normal_count": len(normal),
        "overloaded_branches": overloaded,
        "near_limit_branches": near_limit,
        "normal_branches": normal,
        "recommendations": recommendations,
        "ratings": {
            "normal_percent": normal_rating_percent,
            "emergency_percent": emergency_rating_percent,
        },
    }


# =========================================================================
# Tool Registry
# =========================================================================


class ToolRegistry:
    """Central registry for all function-calling tools.

    Manages tool definitions and provides schemas compatible with
    multiple LLM providers.
    """

    def __init__(self) -> None:
        self._tools: dict[str, ToolDefinition] = {}
        self._register_default_tools()

    def _register_default_tools(self) -> None:
        """Register all built-in power system analysis tools."""

        self.register(
            ToolDefinition(
                name="analyze_power_flow",
                description=(
                    "Run AC power flow analysis on an electrical power system. "
                    "Computes bus voltages, angles, generation dispatch, line flows, "
                    "and total losses. Supports Newton-Raphson, Fast-Decoupled, "
                    "and Gauss-Seidel methods."
                ),
                parameters=[
                    ToolParameter(
                        name="bus_count",
                        param_type="integer",
                        description="Number of buses in the system (14, 30, 57, 118)",
                        default=14,
                    ),
                    ToolParameter(
                        name="base_mva",
                        param_type="number",
                        description="System base MVA",
                        default=100.0,
                    ),
                    ToolParameter(
                        name="solver_method",
                        param_type="string",
                        description="Power flow solution method",
                        enum=["newton_raphson", "fast_decoupled", "gauss_seidel"],
                        default="newton_raphson",
                    ),
                    ToolParameter(
                        name="tolerance",
                        param_type="number",
                        description="Convergence tolerance in p.u.",
                        default=1e-6,
                    ),
                    ToolParameter(
                        name="max_iterations",
                        param_type="integer",
                        description="Maximum solver iterations",
                        default=30,
                    ),
                ],
                func=_analyze_power_flow,
            )
        )

        self.register(
            ToolDefinition(
                name="detect_faults",
                description=(
                    "Detect and diagnose electrical fault conditions in a power "
                    "system. Analyzes three-phase, single-line-to-ground, line-to-line, "
                    "and double-line-to-ground faults with severity assessment and "
                    "protection recommendations."
                ),
                parameters=[
                    ToolParameter(
                        name="fault_type",
                        param_type="string",
                        description="Type of electrical fault to analyze",
                        enum=[
                            "three_phase",
                            "single_phase",
                            "two_phase",
                            "two_phase_ground",
                        ],
                        default="three_phase",
                    ),
                    ToolParameter(
                        name="fault_location",
                        param_type="string",
                        description="Location of the fault (bus or line identifier)",
                        default="bus_1",
                    ),
                    ToolParameter(
                        name="system_description",
                        param_type="string",
                        description="Optional description of the power system",
                        default="",
                    ),
                    ToolParameter(
                        name="base_mva",
                        param_type="number",
                        description="System base MVA",
                        default=100.0,
                    ),
                ],
                func=_detect_faults,
            )
        )

        self.register(
            ToolDefinition(
                name="optimize_generation",
                description=(
                    "Optimize generator dispatch for economic operation, emission "
                    "reduction, or loss minimization. Provides merit-order dispatch "
                    "recommendations with spinning reserve requirements."
                ),
                parameters=[
                    ToolParameter(
                        name="total_demand_mw",
                        param_type="number",
                        description="Total system load demand in MW",
                        default=200.0,
                    ),
                    ToolParameter(
                        name="generator_count",
                        param_type="integer",
                        description="Number of generators available",
                        default=5,
                    ),
                    ToolParameter(
                        name="objective",
                        param_type="string",
                        description="Optimization objective",
                        enum=["min_cost", "min_emissions", "min_losses", "max_reliability"],
                        default="min_cost",
                    ),
                    ToolParameter(
                        name="constraints",
                        param_type="string",
                        description="Additional constraints description",
                        default="",
                    ),
                ],
                func=_optimize_generation,
            )
        )

        self.register(
            ToolDefinition(
                name="check_voltage_stability",
                description=(
                    "Assess voltage stability of a power system by evaluating "
                    "bus voltage profiles against standard limits (0.95-1.05 pu). "
                    "Computes stability margins and identifies at-risk buses."
                ),
                parameters=[
                    ToolParameter(
                        name="bus_voltages_pu",
                        param_type="array",
                        description="List of bus voltage magnitudes in per-unit",
                    ),
                    ToolParameter(
                        name="base_mva",
                        param_type="number",
                        description="System base MVA",
                        default=100.0,
                    ),
                    ToolParameter(
                        name="stability_margin_required",
                        param_type="number",
                        description="Required voltage stability margin (0-1)",
                        default=0.15,
                    ),
                ],
                func=_check_voltage_stability,
            )
        )

        self.register(
            ToolDefinition(
                name="assess_line_loading",
                description=(
                    "Evaluate transmission line and transformer loading levels. "
                    "Identifies overloaded branches and provides capacity upgrade "
                    "recommendations. Compares against normal (100%) and emergency "
                    "(120%) ratings."
                ),
                parameters=[
                    ToolParameter(
                        name="branch_loadings_percent",
                        param_type="array",
                        description="List of branch loading percentages",
                    ),
                    ToolParameter(
                        name="normal_rating_percent",
                        param_type="number",
                        description="Normal operating rating percentage",
                        default=100.0,
                    ),
                    ToolParameter(
                        name="emergency_rating_percent",
                        param_type="number",
                        description="Emergency operating rating percentage",
                        default=120.0,
                    ),
                ],
                func=_assess_line_loading,
            )
        )

    def register(self, tool: ToolDefinition) -> None:
        """Register a new tool."""
        self._tools[tool.name] = tool
        logger.debug("Registered tool: %s", tool.name)

    def get(self, name: str) -> ToolDefinition | None:
        """Get a tool by name."""
        return self._tools.get(name)

    def list_tools(self) -> list[str]:
        """Return a list of all registered tool names."""
        return sorted(self._tools.keys())

    def get_tool_schemas(self, provider: str = "openai") -> list[dict[str, Any]]:
        """Get tool schemas formatted for a specific LLM provider.

        Parameters
        ----------
        provider: One of ``"openai"``, ``"anthropic"``, ``"deepseek"``.

        Returns
        -------
        list[dict]
            List of tool schemas in the provider's native format.
        """
        schemas: list[dict[str, Any]] = []
        for tool in self._tools.values():
            if provider.lower() in ("openai", "gpt"):
                schemas.append(tool.to_openai_schema())
            elif provider.lower() in ("anthropic", "claude"):
                schemas.append(tool.to_anthropic_schema())
            elif provider.lower() == "deepseek":
                schemas.append(tool.to_deepseek_schema())
            else:
                schemas.append(tool.to_openai_schema())
        return schemas

    def execute(self, tool_name: str, **kwargs: Any) -> dict[str, Any]:
        """Execute a registered tool by name.

        Parameters
        ----------
        tool_name: Name of the tool to execute.
        **kwargs: Tool-specific parameters.

        Returns
        -------
        dict: Tool execution result.
        """
        tool = self._tools.get(tool_name)
        if tool is None:
            return {
                "success": False,
                "error": f"Tool '{tool_name}' not found. "
                f"Available: {self.list_tools()}",
            }
        return tool.execute(**kwargs)

    def execute_tool_call(self, tool_call: dict[str, Any]) -> dict[str, Any]:
        """Execute a tool from an LLM tool_call object.

        Handles both OpenAI-style and Anthropic-style tool calls.
        """
        # OpenAI format
        if "function" in tool_call:
            fn = tool_call["function"]
            name = fn.get("name", "")
            try:
                arguments = json.loads(fn.get("arguments", "{}"))
            except json.JSONDecodeError:
                arguments = {}
            return self.execute(name, **arguments)

        # Anthropic format
        name = tool_call.get("name", "")
        input_data = tool_call.get("input", {})
        return self.execute(name, **input_data)

    def __len__(self) -> int:
        return len(self._tools)

    def __contains__(self, name: str) -> bool:
        return name in self._tools

    def __repr__(self) -> str:
        return f"<ToolRegistry tools={self.list_tools()}>"
