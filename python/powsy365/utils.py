"""
powsy365.utils
==============

Utility functions for POWSYS365:

* Unit conversions (kV <-> V, MW <-> W, etc.)
* Input validation helpers
* Logging and timing decorators
* Basic visualization with matplotlib
"""

from __future__ import annotations

import functools
import logging
import time
from typing import Any, Callable, TypeVar

import matplotlib
import matplotlib.pyplot as plt
import numpy as np

matplotlib.use("Agg")  # headless-safe backend

logger = logging.getLogger(__name__)

F = TypeVar("F", bound=Callable[..., Any])


# =========================================================================
# Unit conversions
# =========================================================================


def kv_to_v(kv: float) -> float:
    """Convert kilovolts to volts.

    Parameters
    ----------
    kv: Value in kilovolts [kV].

    Returns
    -------
    float: Value in volts [V].
    """
    return kv * 1e3


def v_to_kv(v: float) -> float:
    """Convert volts to kilovolts.

    Parameters
    ----------
    v: Value in volts [V].

    Returns
    -------
    float: Value in kilovolts [kV].
    """
    return v / 1e3


def mw_to_w(mw: float) -> float:
    """Convert megawatts to watts.

    Parameters
    ----------
    mw: Value in megawatts [MW].

    Returns
    -------
    float: Value in watts [W].
    """
    return mw * 1e6


def w_to_mw(w: float) -> float:
    """Convert watts to megawatts.

    Parameters
    ----------
    w: Value in watts [W].

    Returns
    -------
    float: Value in megawatts [MW].
    """
    return w / 1e6


def mva_to_va(mva: float) -> float:
    """Convert megavolt-amperes to volt-amperes."""
    return mva * 1e6


def va_to_mva(va: float) -> float:
    """Convert volt-amperes to megavolt-amperes."""
    return va / 1e6


def deg_to_rad(deg: float) -> float:
    """Convert degrees to radians."""
    return np.radians(deg)


def rad_to_deg(rad: float) -> float:
    """Convert radians to degrees."""
    return np.degrees(rad)


def db_to_ratio(db: float) -> float:
    """Convert decibels to a linear ratio.

    Parameters
    ----------
    db: Decibel value.

    Returns
    -------
    float: Linear ratio (power).
    """
    return 10.0 ** (db / 10.0)


def ratio_to_db(ratio: float) -> float:
    """Convert a linear ratio to decibels (power).

    Parameters
    ----------
    ratio: Linear power ratio.

    Returns
    -------
    float: Decibel value.
    """
    return 10.0 * np.log10(max(ratio, 1e-20))


def pu_to_actual(pu_value: float, base_value: float) -> float:
    """Convert a per-unit value to its actual value.

    Parameters
    ----------
    pu_value: Per-unit quantity.
    base_value: Base value of the same dimension.

    Returns
    -------
    float: Actual value.
    """
    return pu_value * base_value


def actual_to_pu(actual_value: float, base_value: float) -> float:
    """Convert an actual value to per-unit.

    Parameters
    ----------
    actual_value: Actual (physical) value.
    base_value: Base value of the same dimension.

    Returns
    -------
    float: Per-unit value.

    Raises
    ------
    ZeroDivisionError: If *base_value* is zero.
    """
    if base_value == 0.0:
        raise ZeroDivisionError("Base value cannot be zero for per-unit conversion")
    return actual_value / base_value


# Convenience mapping for batch conversions
UNIT_CONVERSIONS: dict[str, dict[str, Callable[[float], float]]] = {
    "voltage": {
        "kV_to_V": kv_to_v,
        "V_to_kV": v_to_kv,
        "pu_to_actual": pu_to_actual,
        "actual_to_pu": actual_to_pu,
    },
    "power": {
        "MW_to_W": mw_to_w,
        "W_to_MW": w_to_mw,
        "MVA_to_VA": mva_to_va,
        "VA_to_MVA": va_to_mva,
    },
    "angle": {
        "deg_to_rad": deg_to_rad,
        "rad_to_deg": rad_to_deg,
    },
}


def convert_units(value: float, from_unit: str, to_unit: str) -> float:
    """Convert a value between common power-system units.

    Supported unit pairs:
    * ``"kV"`` <-> ``"V"``
    * ``"MW"`` <-> ``"W"``
    * ``"MVA"`` <-> ``"VA"``
    * ``"deg"`` <-> ``"rad"``
    * ``"pu"`` -> ``"actual"`` (requires *value* to be a 2-tuple
      ``(pu, base)``)

    Parameters
    ----------
    value: Numeric value to convert.
    from_unit: Source unit string.
    to_unit: Target unit string.

    Returns
    -------
    float: Converted value.

    Raises
    ------
    ValueError: If the unit pair is not supported.
    """
    pair = (from_unit.strip().lower(), to_unit.strip().lower())

    mapping: dict[tuple[str, str], Callable[[float], float]] = {
        ("kv", "v"): kv_to_v,
        ("v", "kv"): v_to_kv,
        ("mw", "w"): mw_to_w,
        ("w", "mw"): w_to_mw,
        ("mva", "va"): mva_to_va,
        ("va", "mva"): va_to_mva,
        ("mvar", "var"): lambda x: x * 1e6,
        ("var", "mvar"): lambda x: x / 1e6,
        ("deg", "rad"): deg_to_rad,
        ("rad", "deg"): rad_to_deg,
        ("db", "ratio"): db_to_ratio,
        ("ratio", "db"): ratio_to_db,
    }

    if pair not in mapping:
        raise ValueError(
            f"Unsupported unit conversion: {from_unit} -> {to_unit}. "
            f"Supported: {list(mapping.keys())}"
        )
    return mapping[pair](value)


# =========================================================================
# Validation helpers
# =========================================================================


def validate_positive(value: float, name: str = "value") -> float:
    """Ensure *value* is strictly positive.

    Returns the value on success; raises :class:`ValueError` otherwise.
    """
    if value <= 0:
        raise ValueError(f"{name} must be > 0, got {value}")
    return value


def validate_non_negative(value: float, name: str = "value") -> float:
    """Ensure *value* is non-negative."""
    if value < 0:
        raise ValueError(f"{name} must be >= 0, got {value}")
    return value


def validate_range(
    value: float, low: float, high: float, name: str = "value", low_inclusive: bool = True, high_inclusive: bool = True
) -> float:
    """Ensure *value* lies within [*low*, *high*].

    Parameters
    ----------
    low_inclusive: If *False*, enforce *value* > *low*.
    high_inclusive: If *False*, enforce *value* < *high*.
    """
    if low_inclusive:
        if value < low:
            raise ValueError(
                f"{name} must be >= {low}, got {value}"
            )
    else:
        if value <= low:
            raise ValueError(
                f"{name} must be > {low}, got {value}"
            )
    if high_inclusive:
        if value > high:
            raise ValueError(
                f"{name} must be <= {high}, got {value}"
            )
    else:
        if value >= high:
            raise ValueError(
                f"{name} must be < {high}, got {value}"
            )
    return value


def validate_bus_type(bus_type: str) -> str:
    """Normalise and validate a bus-type string.

    Returns the upper-case canonical form (``"PQ"``, ``"PV"``,
    or ``"SLACK"``).
    """
    canonical = bus_type.strip().upper()
    if canonical not in {"PQ", "PV", "SLACK"}:
        raise ValueError(
            f"Invalid bus type '{bus_type}'. "
            f"Must be one of: PQ, PV, Slack"
        )
    return canonical


def validate_network_data(data: dict[str, Any]) -> list[str]:
    """Validate a raw network data dictionary and return all errors.

    The dictionary is expected to follow the same schema as
    :meth:`Network.to_json`.

    Returns
    -------
    list[str]
        Empty list if valid; otherwise human-readable error strings.
    """
    errors: list[str] = []

    if not isinstance(data, dict):
        return ["Network data must be a dictionary"]

    if "buses" not in data or not data["buses"]:
        errors.append("'buses' list is missing or empty")
        return errors

    bus_ids: set[int] = set()
    for i, bus in enumerate(data["buses"]):
        prefix = f"buses[{i}]"
        if "bus_id" not in bus:
            errors.append(f"{prefix}: missing 'bus_id'")
        else:
            bid = bus["bus_id"]
            if bid in bus_ids:
                errors.append(f"{prefix}: duplicate bus_id {bid}")
            bus_ids.add(bid)
        if bus.get("base_kv", 0) <= 0:
            errors.append(f"{prefix}: base_kv must be > 0")

    # Check for slack bus
    slack_types = {"slack", "SLACK", "Slack"}
    has_slack = any(
        b.get("type", "") in slack_types for b in data["buses"]
    )
    if not has_slack:
        errors.append("Network must have at least one Slack bus")

    # Validate branch references
    for key, elem_name in [("lines", "line"), ("transformers", "transformer")]:
        for i, elem in enumerate(data.get(key, [])):
            prefix = f"{key}[{i}]"
            fb = elem.get("from_bus")
            tb = elem.get("to_bus")
            if fb is not None and fb not in bus_ids:
                errors.append(
                    f"{prefix}: from_bus {fb} not found in buses"
                )
            if tb is not None and tb not in bus_ids:
                errors.append(
                    f"{prefix}: to_bus {tb} not found in buses"
                )
            if fb == tb:
                errors.append(f"{prefix}: from_bus equals to_bus")

    return errors


# =========================================================================
# Timing and logging decorators
# =========================================================================


def timed(func: F) -> F:
    """Decorator that logs the execution time of *func*.

    Usage::

        @timed
        def my_analysis(...):
            ...
    """
    @functools.wraps(func)
    def wrapper(*args: Any, **kwargs: Any) -> Any:
        t0 = time.perf_counter()
        result = func(*args, **kwargs)
        elapsed = (time.perf_counter() - t0) * 1000.0
        logger.info(
            "[%s] completed in %.2f ms", func.__name__, elapsed
        )
        return result
    return wrapper  # type: ignore[return-value]


def retry_on_error(
    exceptions: tuple[type[BaseException], ...] = (Exception,),
    max_retries: int = 3,
    backoff_s: float = 1.0,
) -> Callable[[F], F]:
    """Decorator that retries *func* on specified exceptions.

    Parameters
    ----------
    exceptions: Tuple of exception types to catch.
    max_retries: Maximum number of retry attempts.
    backoff_s: Initial backoff in seconds (doubles each retry).
    """
    def decorator(func: F) -> F:
        @functools.wraps(func)
        def wrapper(*args: Any, **kwargs: Any) -> Any:
            delay = backoff_s
            for attempt in range(1, max_retries + 1):
                try:
                    return func(*args, **kwargs)
                except exceptions as exc:
                    if attempt == max_retries:
                        raise
                    logger.warning(
                        "[%s] attempt %d/%d failed: %s. "
                        "Retrying in %.1f s...",
                        func.__name__, attempt, max_retries, exc, delay,
                    )
                    time.sleep(delay)
                    delay *= 2.0
            return None  # unreachable
        return wrapper  # type: ignore[return-value]
    return decorator


def log_call(level: int = logging.DEBUG) -> Callable[[F], F]:
    """Decorator that logs function calls with arguments.

    Parameters
    ----------
    level: Logging level (default DEBUG).
    """
    def decorator(func: F) -> F:
        @functools.wraps(func)
        def wrapper(*args: Any, **kwargs: Any) -> Any:
            arg_repr = ", ".join(
                [repr(a) for a in args]
                + [f"{k}={v!r}" for k, v in kwargs.items()]
            )
            logger.log(level, "CALL %s(%s)", func.__name__, arg_repr)
            result = func(*args, **kwargs)
            logger.log(level, "RETURN %s -> %r", func.__name__, result)
            return result
        return wrapper  # type: ignore[return-value]
    return decorator


# =========================================================================
# Visualization
# =========================================================================


def plot_voltages(
    bus_results: list[dict[str, Any]],
    title: str = "Bus Voltage Magnitudes",
    vmin_limit: float = 0.9,
    vmax_limit: float = 1.1,
    figsize: tuple[float, float] = (12, 5),
    save_path: str | None = None,
    show_plot: bool = False,
) -> plt.Figure:
    """Plot voltage magnitudes for all buses.

    Parameters
    ----------
    bus_results: List of bus result dicts with keys ``bus_id`` and
        ``vm_pu`` (or ``vm``).
    title: Plot title.
    vmin_limit, vmax_limit: Acceptable voltage range shown as
        horizontal reference lines.
    figsize: Matplotlib figure size.
    save_path: If provided, save the figure to this file path.
    show_plot: If *True*, call ``plt.show()``.

    Returns
    -------
    matplotlib.figure.Figure
    """
    bus_ids = [b.get("bus_id", b.get("bus", i)) for i, b in enumerate(bus_results)]
    vms = [b.get("vm_pu", b.get("vm", 1.0)) for b in bus_results]

    fig, ax = plt.subplots(figsize=figsize)
    colors = ["#2ecc71" if vmin_limit <= v <= vmax_limit else "#e74c3c" for v in vms]
    ax.bar(range(len(bus_ids)), vms, color=colors, edgecolor="black", linewidth=0.5)
    ax.axhline(y=vmin_limit, color="red", linestyle="--", label=f"Vmin = {vmin_limit}")
    ax.axhline(y=1.0, color="gray", linestyle=":", label="Vnom = 1.0")
    ax.axhline(y=vmax_limit, color="red", linestyle="--", label=f"Vmax = {vmax_limit}")
    ax.set_xticks(range(len(bus_ids)))
    ax.set_xticklabels([str(b) for b in bus_ids], rotation=45, ha="right")
    ax.set_ylabel("Voltage magnitude [p.u.]")
    ax.set_xlabel("Bus ID")
    ax.set_title(title)
    ax.legend(loc="lower right")
    ax.set_ylim(min(vms + [vmin_limit - 0.05]) - 0.02,
                max(vms + [vmax_limit + 0.05]) + 0.02)
    ax.grid(axis="y", alpha=0.3)
    fig.tight_layout()

    if save_path:
        fig.savefig(save_path, dpi=150, bbox_inches="tight")
        logger.info("Voltage plot saved to %s", save_path)
    if show_plot:
        plt.show()
    return fig


def plot_loading(
    branch_results: list[dict[str, Any]],
    title: str = "Branch Loading",
    rating_mva: float | None = None,
    figsize: tuple[float, float] = (12, 5),
    save_path: str | None = None,
    show_plot: bool = False,
) -> plt.Figure:
    """Plot branch loading percentages.

    Parameters
    ----------
    branch_results: List of branch result dicts with keys
        ``branch_id`` (or ``line_id``) and ``loading_percent``.
    title: Plot title.
    rating_mva: If provided, show as a reference line.
    figsize: Matplotlib figure size.
    save_path: If provided, save the figure to this file path.
    show_plot: If *True*, call ``plt.show()``.

    Returns
    -------
    matplotlib.figure.Figure
    """
    labels = [
        f"{b.get('from_bus', '?')}-{b.get('to_bus', '?')}"
        for b in branch_results
    ]
    loadings = [b.get("loading_percent", 0.0) for b in branch_results]

    fig, ax = plt.subplots(figsize=figsize)
    colors = ["#2ecc71" if l <= 100.0 else "#e74c3c" if l <= 120.0 else "#c0392b"
              for l in loadings]
    ax.bar(range(len(labels)), loadings, color=colors, edgecolor="black", linewidth=0.5)
    ax.axhline(y=100.0, color="red", linestyle="--", label="100% (normal)")
    ax.axhline(y=120.0, color="orange", linestyle="--", label="120% (emergency)")
    ax.set_xticks(range(len(labels)))
    ax.set_xticklabels(labels, rotation=45, ha="right")
    ax.set_ylabel("Loading [%]")
    ax.set_xlabel("Branch (from-to)")
    ax.set_title(title)
    ax.legend(loc="upper right")
    ax.set_ylim(0, max(loadings + [130.0]))
    ax.grid(axis="y", alpha=0.3)
    fig.tight_layout()

    if save_path:
        fig.savefig(save_path, dpi=150, bbox_inches="tight")
        logger.info("Loading plot saved to %s", save_path)
    if show_plot:
        plt.show()
    return fig


def plot_convergence(
    mismatch_history: list[float],
    title: str = "Solver Convergence",
    tol: float = 1e-6,
    figsize: tuple[float, float] = (8, 5),
    save_path: str | None = None,
    show_plot: bool = False,
) -> plt.Figure:
    """Plot the convergence history (mismatch vs iteration).

    Parameters
    ----------
    mismatch_history: List of max mismatch values per iteration.
    title: Plot title.
    tol: Tolerance line to draw.
    figsize: Matplotlib figure size.
    save_path: If provided, save the figure to this file path.
    show_plot: If *True*, call ``plt.show()``.

    Returns
    -------
    matplotlib.figure.Figure
    """
    iterations = list(range(1, len(mismatch_history) + 1))

    fig, ax = plt.subplots(figsize=figsize)
    ax.semilogy(iterations, mismatch_history, "o-", color="#3498db", linewidth=2, markersize=6)
    ax.axhline(y=tol, color="red", linestyle="--", label=f"Tolerance = {tol:.0e}")
    ax.set_xlabel("Iteration")
    ax.set_ylabel("Max mismatch [p.u.] (log)")
    ax.set_title(title)
    ax.legend()
    ax.grid(True, which="both", alpha=0.3)
    fig.tight_layout()

    if save_path:
        fig.savefig(save_path, dpi=150, bbox_inches="tight")
        logger.info("Convergence plot saved to %s", save_path)
    if show_plot:
        plt.show()
    return fig


def plot_pie_losses(
    total_pgen: float,
    total_pload: float,
    total_ploss: float,
    title: str = "Power Balance",
    figsize: tuple[float, float] = (7, 7),
    save_path: str | None = None,
    show_plot: bool = False,
) -> plt.Figure:
    """Plot a pie chart showing power generation, load, and losses.

    Parameters
    ----------
    total_pgen: Total active generation [MW].
    total_pload: Total active load [MW].
    total_ploss: Total active losses [MW].
    title: Plot title.
    figsize: Matplotlib figure size.
    save_path: If provided, save the figure to this file path.
    show_plot: If *True*, call ``plt.show()``.

    Returns
    -------
    matplotlib.figure.Figure
    """
    fig, ax = plt.subplots(figsize=figsize)
    labels = [f"Generation\n{total_pgen:.1f} MW", f"Load\n{total_pload:.1f} MW", f"Losses\n{total_ploss:.1f} MW"]
    sizes = [total_pgen, total_pload, total_ploss]
    colors = ["#3498db", "#2ecc71", "#e74c3c"]
    explode = (0.0, 0.0, 0.05)
    wedges, texts, autotexts = ax.pie(
        sizes, explode=explode, labels=labels, colors=colors,
        autopct="%1.1f%%", shadow=False, startangle=90,
        textprops={"fontsize": 10},
    )
    ax.set_title(title, fontsize=13, fontweight="bold")
    fig.tight_layout()

    if save_path:
        fig.savefig(save_path, dpi=150, bbox_inches="tight")
        logger.info("Losses pie chart saved to %s", save_path)
    if show_plot:
        plt.show()
    return fig


# =========================================================================
# Progress / timing helpers
# =========================================================================


class Timer:
    """Context manager for timing code blocks.

    Usage::

        with Timer("matrix factorization") as t:
            lu = scipy.sparse.linalg.splu(A)
        print(f"Took {t.elapsed_ms:.1f} ms")
    """

    def __init__(self, name: str = "block") -> None:
        self.name = name
        self.elapsed_ms: float = 0.0

    def __enter__(self) -> "Timer":
        self._t0 = time.perf_counter()
        return self

    def __exit__(self, *exc: object) -> None:
        self.elapsed_ms = (time.perf_counter() - self._t0) * 1000.0
        logger.info("[%s] %.2f ms", self.name, self.elapsed_ms)


def format_elapsed(seconds: float) -> str:
    """Format a duration in a human-readable string.

    >>> format_elapsed(0.00123)
    '1.23 ms'
    >>> format_elapsed(12.5)
    '12.50 s'
    >>> format_elapsed(3661.5)
    '1h 1m 1.50s'
    """
    if seconds < 1e-3:
        return f"{seconds * 1e6:.1f} us"
    if seconds < 1.0:
        return f"{seconds * 1e3:.2f} ms"
    if seconds < 60.0:
        return f"{seconds:.2f} s"
    if seconds < 3600.0:
        m = int(seconds // 60)
        s = seconds % 60
        return f"{m}m {s:.1f}s"
    h = int(seconds // 3600)
    rem = seconds % 3600
    m = int(rem // 60)
    s = rem % 60
    return f"{h}h {m}m {s:.1f}s"
