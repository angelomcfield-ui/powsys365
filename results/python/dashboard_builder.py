"""
POWSYS365 - Dashboard Builder Module
Creates interactive dashboards for power system analysis visualization.
"""

import json
from typing import Dict, List, Any, Optional, Tuple, Callable
from dataclasses import dataclass, field, asdict
from datetime import datetime, timedelta
import math


# ---------------------------------------------------------------------------
# Data structures for dashboard building
# ---------------------------------------------------------------------------
@dataclass
class BusVoltageData:
    """Bus voltage profile data point."""
    bus_id: str
    bus_name: str = ""
    base_kv: float = 0.0
    voltage_kv: float = 0.0
    voltage_pu: float = 1.0
    angle_deg: float = 0.0


@dataclass
class LineLoadingData:
    """Line loading data point."""
    line_id: str
    from_bus: str = ""
    to_bus: str = ""
    loading_percent: float = 0.0
    current_a: float = 0.0
    rating_a: float = 0.0
    p_mw: float = 0.0
    q_mvar: float = 0.0


@dataclass
class GenerationData:
    """Generator output data point."""
    gen_id: str
    gen_name: str = ""
    bus_id: str = ""
    p_mw: float = 0.0
    p_max_mw: float = 0.0
    fuel_type: str = ""
    fuel_cost_per_mwh: float = 0.0
    status: int = 1


@dataclass
class TimeSeriesPoint:
    """Time series data point."""
    timestamp: str  # ISO format
    value: float = 0.0
    series_name: str = ""


@dataclass
class ChartConfig:
    """Chart configuration for dashboard."""
    chart_type: str = "line"  # line, bar, pie, gauge, color_bar
    title: str = ""
    x_axis_label: str = ""
    y_axis_label: str = ""
    width: int = 400
    height: int = 300
    colors: List[str] = field(default_factory=list)
    min_y: Optional[float] = None
    max_y: Optional[float] = None
    thresholds: Dict[str, float] = field(default_factory=dict)
    animated: bool = True
    show_legend: bool = True


@dataclass
class PanelConfig:
    """Dashboard panel configuration."""
    panel_id: str = ""
    title: str = ""
    panel_type: str = "chart"  # chart, gauge, stat_card, table, color_bar
    chart_config: Optional[ChartConfig] = None
    row: int = 0
    col: int = 0
    row_span: int = 1
    col_span: int = 1
    refresh_interval_ms: int = 1000
    data_source: str = ""  # identifier for data binding


@dataclass
class DashboardLayout:
    """Complete dashboard layout."""
    name: str = "POWSYS365 Dashboard"
    columns: int = 3
    theme: str = "dark"
    background_color: str = "#1a1a2e"
    panels: List[PanelConfig] = field(default_factory=list)


# ---------------------------------------------------------------------------
# Dashboard builder functions
# ---------------------------------------------------------------------------

def create_dashboard(data: Dict[str, Any],
                     layout: str = "default") -> DashboardLayout:
    """
    Create a dashboard layout from analysis data.

    Args:
        data: Dictionary with analysis results
        layout: Layout preset name ("default", "transmission", "generation", "distribution")

    Returns:
        DashboardLayout with configured panels
    """
    dashboard = DashboardLayout(
        name=f"POWSYS365 - {layout.title()} Dashboard",
        columns=3 if layout == "default" else 2
    )

    if layout == "default":
        # Row 0: System overview (2 cols) + Frequency gauge (1 col)
        dashboard.panels.append(PanelConfig(
            panel_id="voltage_profile",
            title="Voltage Profile",
            panel_type="chart",
            chart_config=ChartConfig(
                chart_type="bar",
                title="Bus Voltages (pu)",
                y_axis_label="Voltage (pu)",
                width=600, height=300,
                colors=["#2E86C1", "#E74C3C"],
                min_y=0.9, max_y=1.1,
                thresholds={"warning_low": 0.95, "warning_high": 1.05,
                           "alarm_low": 0.9, "alarm_high": 1.1}
            ),
            row=0, col=0, col_span=2
        ))

        dashboard.panels.append(PanelConfig(
            panel_id="frequency_gauge",
            title="System Frequency",
            panel_type="gauge",
            chart_config=ChartConfig(
                chart_type="gauge",
                title="Frequency (Hz)",
                width=300, height=300,
                min_y=59.0, max_y=61.0,
                thresholds={"warning_low": 59.5, "warning_high": 60.5,
                           "alarm_low": 59.0, "alarm_high": 61.0}
            ),
            row=0, col=2
        ))

        # Row 1: Line loading + Generation pie
        dashboard.panels.append(PanelConfig(
            panel_id="line_loading",
            title="Line Loading",
            panel_type="color_bar",
            chart_config=ChartConfig(
                chart_type="color_bar",
                title="Line Loading (%)",
                width=400, height=300,
                thresholds={"warning": 80.0, "alarm": 100.0}
            ),
            row=1, col=0, col_span=2
        ))

        dashboard.panels.append(PanelConfig(
            panel_id="generation_pie",
            title="Generation Mix",
            panel_type="chart",
            chart_config=ChartConfig(
                chart_type="pie",
                title="Generation by Source",
                width=400, height=300
            ),
            row=1, col=2
        ))

        # Row 2: Stat cards
        dashboard.panels.append(PanelConfig(
            panel_id="total_gen",
            title="Total Generation",
            panel_type="stat_card",
            row=2, col=0
        ))
        dashboard.panels.append(PanelConfig(
            panel_id="total_load",
            title="Total Load",
            panel_type="stat_card",
            row=2, col=1
        ))
        dashboard.panels.append(PanelConfig(
            panel_id="losses",
            title="System Losses",
            panel_type="stat_card",
            row=2, col=2
        ))

    elif layout == "transmission":
        dashboard.columns = 2
        dashboard.panels.append(PanelConfig(
            panel_id="voltage_map",
            title="Voltage Profile",
            panel_type="chart",
            chart_config=ChartConfig(
                chart_type="bar",
                title="Bus Voltages",
                y_axis_label="V (pu)",
                width=600, height=350
            ),
            row=0, col=0
        ))
        dashboard.panels.append(PanelConfig(
            panel_id="loading_map",
            title="Line Loading",
            panel_type="color_bar",
            chart_config=ChartConfig(
                chart_type="color_bar",
                title="Loading %",
                width=600, height=350,
                thresholds={"warning": 80.0, "alarm": 100.0}
            ),
            row=0, col=1
        ))
        dashboard.panels.append(PanelConfig(
            panel_id="power_flow",
            title="Power Flow",
            panel_type="chart",
            chart_config=ChartConfig(
                chart_type="line",
                title="Real-time Power Flow",
                width=1200, height=300
            ),
            row=1, col=0, col_span=2
        ))

    return dashboard


def plot_voltage_profile(buses: List[BusVoltageData],
                          title: str = "Voltage Profile",
                          vnom: float = 1.0,
                          vmin: float = 0.95,
                          vmax: float = 1.05) -> Dict[str, Any]:
    """
    Create voltage profile chart data.

    Args:
        buses: List of bus voltage data
        title: Chart title
        vnom: Nominal voltage in pu
        vmin: Minimum acceptable voltage
        vmax: Maximum acceptable voltage

    Returns:
        Chart configuration dictionary
    """
    categories = [b.bus_id for b in buses]
    voltage_values = [b.voltage_pu for b in buses]

    # Color each bar based on voltage status
    colors = []
    for v in voltage_values:
        if v < vmin or v > vmax:
            colors.append("#E74C3C")  # Red for violation
        elif v < vmin + 0.02 or v > vmax - 0.02:
            colors.append("#F39C12")  # Orange for warning
        else:
            colors.append("#27AE60")  # Green for OK

    return {
        "chart_type": "bar",
        "title": title,
        "x_axis_label": "Bus",
        "y_axis_label": "Voltage (pu)",
        "categories": categories,
        "series_names": ["Voltage (pu)"],
        "series_data": [voltage_values],
        "colors": colors,
        "thresholds": {
            "nominal": vnom,
            "vmin": vmin,
            "vmax": vmax
        },
        "annotations": [
            {"y": vmin, "label": f"Vmin = {vmin}", "color": "#E74C3C"},
            {"y": vmax, "label": f"Vmax = {vmax}", "color": "#E74C3C"},
            {"y": vnom, "label": f"Vnom = {vnom}", "color": "#2E86C1", "dashed": True}
        ],
        "width": max(600, len(categories) * 40),
        "height": 400
    }


def plot_loading_lines(lines: List[LineLoadingData],
                        title: str = "Line Loading") -> Dict[str, Any]:
    """
    Create line loading bar chart data.

    Args:
        lines: List of line loading data
        title: Chart title

    Returns:
        Chart configuration dictionary
    """
    categories = [f"{l.from_bus}-{l.to_bus}" for l in lines]
    loading_values = [l.loading_percent for l in lines]

    # Color based on loading
    colors = []
    for load in loading_values:
        if load >= 100.0:
            colors.append("#E74C3C")
        elif load >= 80.0:
            colors.append("#F39C12")
        else:
            colors.append("#27AE60")

    return {
        "chart_type": "bar",
        "title": title,
        "x_axis_label": "Line",
        "y_axis_label": "Loading (%)",
        "categories": categories,
        "series_names": ["Loading %"],
        "series_data": [loading_values],
        "colors": colors,
        "thresholds": {
            "warning": 80.0,
            "alarm": 100.0
        },
        "annotations": [
            {"y": 80.0, "label": "Warning (80%)", "color": "#F39C12", "dashed": True},
            {"y": 100.0, "label": "Rating (100%)", "color": "#E74C3C", "dashed": True}
        ],
        "width": max(600, len(categories) * 50),
        "height": 400
    }


def plot_pie_generation(generators: List[GenerationData],
                         title: str = "Generation Mix") -> Dict[str, Any]:
    """
    Create generation mix pie chart data.

    Args:
        generators: List of generator data
        title: Chart title

    Returns:
        Chart configuration dictionary
    """
    # Group by fuel type
    fuel_totals: Dict[str, float] = {}
    for gen in generators:
        if gen.status == 1 and gen.p_mw > 0:
            fuel = gen.fuel_type if gen.fuel_type else "Unknown"
            fuel_totals[fuel] = fuel_totals.get(fuel, 0.0) + gen.p_mw

    # Default fuel type colors
    fuel_colors = {
        "coal": "#34495E",
        "gas": "#E74C3C",
        "oil": "#8E44AD",
        "nuclear": "#2ECC71",
        "hydro": "#3498DB",
        "wind": "#1ABC9C",
        "solar": "#F1C40F",
        "geothermal": "#E67E22",
        "biomass": "#27AE60",
        "unknown": "#95A5A6"
    }

    labels = list(fuel_totals.keys())
    values = list(fuel_totals.values())
    colors = [fuel_colors.get(f.lower(), "#95A5A6") for f in labels]

    total = sum(values)
    percentages = [v / total * 100 if total > 0 else 0 for v in values]

    return {
        "chart_type": "pie",
        "title": title,
        "categories": labels,
        "series_names": ["Generation (MW)"],
        "series_data": [values],
        "colors": colors,
        "annotations": [
            {"label": f"{labels[i]}: {values[i]:.1f} MW ({percentages[i]:.1f}%)",
             "index": i}
            for i in range(len(labels))
        ],
        "total_mw": total,
        "width": 500,
        "height": 400
    }


def time_series_plot(times: List[float],
                      values: List[float],
                      title: str = "Time Series",
                      y_label: str = "Value",
                      x_label: str = "Time (s)",
                      series_name: str = "Series 1",
                      color: str = "#2E86C1") -> Dict[str, Any]:
    """
    Create time series chart data.

    Args:
        times: Time points in seconds
        values: Values at each time point
        title: Chart title
        y_label: Y axis label
        x_label: X axis label
        series_name: Name of the data series
        color: Series color (hex)

    Returns:
        Chart configuration dictionary
    """
    if not times or not values:
        return {
            "chart_type": "line",
            "title": title,
            "categories": [],
            "series_names": [series_name],
            "series_data": [[]],
            "colors": [color],
            "width": 800,
            "height": 400
        }

    # Convert to string labels for categories
    time_labels = [f"{t:.3f}" for t in times]

    # Compute stats
    min_val = min(values) if values else 0
    max_val = max(values) if values else 1
    val_range = max_val - min_val if max_val != min_val else 1.0

    return {
        "chart_type": "line",
        "title": title,
        "x_axis_label": x_label,
        "y_axis_label": y_label,
        "categories": time_labels,
        "series_names": [series_name],
        "series_data": [values],
        "colors": [color],
        "min_y": min_val - val_range * 0.1,
        "max_y": max_val + val_range * 0.1,
        "width": 800,
        "height": 400,
        "stats": {
            "min": round(min_val, 6),
            "max": round(max_val, 6),
            "mean": round(sum(values) / len(values), 6) if values else 0,
            "final": round(values[-1], 6) if values else 0
        }
    }


def plot_multi_series(times: List[float],
                       series_dict: Dict[str, List[float]],
                       title: str = "Multi-Series Plot",
                       y_label: str = "Value",
                       x_label: str = "Time (s)") -> Dict[str, Any]:
    """
    Create a multi-series time series chart.

    Args:
        times: Common time points
        series_dict: Dictionary of {series_name: [values]}
        title: Chart title
        y_label: Y axis label
        x_label: X axis label

    Returns:
        Chart configuration dictionary
    """
    palette = ["#2E86C1", "#E74C3C", "#27AE60", "#F39C12",
               "#8E44AD", "#1ABC9C", "#E67E22", "#3498DB"]

    time_labels = [f"{t:.3f}" for t in times]
    all_values = []

    chart_data = {
        "chart_type": "line",
        "title": title,
        "x_axis_label": x_label,
        "y_axis_label": y_label,
        "categories": time_labels,
        "series_names": [],
        "series_data": [],
        "colors": [],
        "width": 800,
        "height": 400
    }

    for i, (name, values) in enumerate(series_dict.items()):
        chart_data["series_names"].append(name)
        chart_data["series_data"].append(values)
        chart_data["colors"].append(palette[i % len(palette)])
        all_values.extend(values)

    if all_values:
        min_val = min(all_values)
        max_val = max(all_values)
        val_range = max_val - min_val if max_val != min_val else 1.0
        chart_data["min_y"] = min_val - val_range * 0.1
        chart_data["max_y"] = max_val + val_range * 0.1

    return chart_data


def create_gauge_data(title: str,
                       value: float,
                       unit: str,
                       min_val: float,
                       max_val: float,
                       warning_low: Optional[float] = None,
                       warning_high: Optional[float] = None,
                       alarm_low: Optional[float] = None,
                       alarm_high: Optional[float] = None) -> Dict[str, Any]:
    """
    Create gauge chart data.

    Args:
        title: Gauge title
        value: Current value
        unit: Unit string
        min_val: Minimum scale value
        max_val: Maximum scale value
        warning_low: Warning threshold (low)
        warning_high: Warning threshold (high)
        alarm_low: Alarm threshold (low)
        alarm_high: Alarm threshold (high)

    Returns:
        Gauge configuration dictionary
    """
    ratio = (value - min_val) / (max_val - min_val) if max_val > min_val else 0.5
    ratio = max(0.0, min(1.0, ratio))

    # Determine color
    color = "#27AE60"  # Green (normal)
    if alarm_high is not None and value >= alarm_high:
        color = "#E74C3C"  # Red
    elif alarm_low is not None and value <= alarm_low:
        color = "#E74C3C"
    elif warning_high is not None and value >= warning_high:
        color = "#F39C12"  # Orange
    elif warning_low is not None and value <= warning_low:
        color = "#F39C12"

    return {
        "chart_type": "gauge",
        "title": title,
        "value": round(value, 2),
        "unit": unit,
        "min": min_val,
        "max": max_val,
        "ratio": ratio,
        "color": color,
        "warning_low": warning_low,
        "warning_high": warning_high,
        "alarm_low": alarm_low,
        "alarm_high": alarm_high,
        "width": 300,
        "height": 300
    }


def create_color_bar_data(title: str,
                           items: List[Tuple[str, float]],
                           min_val: float = 0.0,
                           max_val: float = 100.0,
                           warning_threshold: float = 80.0,
                           alarm_threshold: float = 100.0,
                           unit: str = "%") -> Dict[str, Any]:
    """
    Create color bar visualization data.

    Args:
        title: Bar title
        items: List of (name, value) tuples
        min_val: Minimum scale value
        max_val: Maximum scale value
        warning_threshold: Warning threshold
        alarm_threshold: Alarm threshold
        unit: Unit string

    Returns:
        Color bar configuration dictionary
    """
    bar_items = []
    for name, value in items:
        ratio = (value - min_val) / (max_val - min_val) if max_val > min_val else 0.5
        ratio = max(0.0, min(1.0, ratio))

        if value >= alarm_threshold:
            color = "#E74C3C"
            status = "ALARM"
        elif value >= warning_threshold:
            color = "#F39C12"
            status = "WARNING"
        else:
            color = "#27AE60"
            status = "OK"

        bar_items.append({
            "name": name,
            "value": round(value, 2),
            "ratio": ratio,
            "color": color,
            "status": status
        })

    return {
        "chart_type": "color_bar",
        "title": title,
        "unit": unit,
        "items": bar_items,
        "min": min_val,
        "max": max_val,
        "warning_threshold": warning_threshold,
        "alarm_threshold": alarm_threshold,
        "width": 500,
        "height": max(200, len(items) * 25 + 50)
    }


def create_stat_card(title: str,
                      value: str,
                      subtitle: str = "",
                      trend: float = 0.0,
                      color: str = "#2E86C1") -> Dict[str, Any]:
    """
    Create stat card data.

    Args:
        title: Card title
        value: Main value string
        subtitle: Subtitle text
        trend: Trend indicator (positive=up, negative=down, 0=neutral)
        color: Value color

    Returns:
        Stat card configuration dictionary
    """
    trend_icon = "+" if trend > 0 else "" if trend < 0 else ""
    trend_str = f"{trend_icon}{trend:.1f}%" if trend != 0 else ""
    trend_color = "#27AE60" if trend > 0 else "#E74C3C" if trend < 0 else "#95A5A6"

    return {
        "panel_type": "stat_card",
        "title": title,
        "value": value,
        "subtitle": subtitle,
        "trend": trend_str,
        "trend_color": trend_color,
        "color": color,
        "width": 250,
        "height": 120
    }


# ---------------------------------------------------------------------------
# HTML/JSON export
# ---------------------------------------------------------------------------

def dashboard_to_json(dashboard: DashboardLayout) -> str:
    """Convert dashboard to JSON string."""
    data = asdict(dashboard)
    return json.dumps(data, indent=2)


def dashboard_to_html(dashboard: DashboardLayout,
                       data_bindings: Optional[Dict[str, Any]] = None) -> str:
    """
    Generate HTML dashboard page.

    Args:
        dashboard: Dashboard layout
        data_bindings: Optional data to bind to panels

    Returns:
        HTML string
    """
    theme_bg = dashboard.background_color
    theme_fg = "#ffffff" if dashboard.theme == "dark" else "#333333"
    card_bg = "#16213e" if dashboard.theme == "dark" else "#ffffff"
    border_color = "#0f3460" if dashboard.theme == "dark" else "#e0e0e0"

    html = f"""<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<title>{dashboard.name}</title>
<style>
body {{ margin: 0; padding: 20px; background: {theme_bg}; color: {theme_fg};
       font-family: 'Segoe UI', Arial, sans-serif; }}
.dashboard {{ display: grid; grid-template-columns: repeat({dashboard.columns}, 1fr);
              gap: 15px; max-width: 1400px; margin: 0 auto; }}
.panel {{ background: {card_bg}; border: 1px solid {border_color};
         border-radius: 8px; padding: 15px; min-height: 200px; }}
.panel h3 {{ margin-top: 0; font-size: 14px; color: {'#a0c4e8' if dashboard.theme == 'dark' else '#1a1a2e'}; }}
.value {{ font-size: 32px; font-weight: bold; color: #2E86C1; }}
.trend {{ font-size: 14px; margin-left: 10px; }}
.trend-up {{ color: #27AE60; }}
.trend-down {{ color: #E74C3C; }}
.bar {{ height: 20px; background: #2E86C1; border-radius: 3px;
        transition: width 0.5s; margin: 3px 0; }}
.bar-warning {{ background: #F39C12; }}
.bar-alarm {{ background: #E74C3C; }}
table {{ width: 100%; border-collapse: collapse; font-size: 12px; }}
th {{ background: #1E3A5F; color: white; padding: 8px; text-align: left; }}
td {{ padding: 6px 8px; border-bottom: 1px solid {border_color}; }}
tr:hover {{ background: {'#0f3460' if dashboard.theme == 'dark' else '#f0f0f0'}; }}
.violation {{ color: #E74C3C; font-weight: bold; }}
.warning {{ color: #F39C12; }}
.ok {{ color: #27AE60; }}
.gauge {{ width: 200px; height: 150px; margin: 0 auto; }}
.header {{ text-align: center; margin-bottom: 20px; }}
.header h1 {{ margin: 0; color: {'#e94560' if dashboard.theme == 'dark' else '#1E3A5F'}; }}
.timestamp {{ color: {'#a0c4e8' if dashboard.theme == 'dark' else '#666'}; font-size: 12px; }}
</style>
<script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
</head>
<body>
<div class="header">
<h1>{dashboard.name}</h1>
<p class="timestamp">Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}</p>
</div>
<div class="dashboard">
"""

    for panel in dashboard.panels:
        html += f'<div class="panel" style="'
        if panel.col_span > 1:
            html += f'grid-column: span {panel.col_span};'
        if panel.row_span > 1:
            html += f'grid-row: span {panel.row_span};'
        html += f'">\n'
        html += f'<h3>{panel.title}</h3>\n'

        # Panel content based on type
        if panel.panel_type == "stat_card" and data_bindings:
            binding = data_bindings.get(panel.panel_id, {})
            value = binding.get("value", "--")
            trend = binding.get("trend", 0.0)
            trend_class = "trend-up" if trend > 0 else "trend-down" if trend < 0 else ""
            trend_sign = "+" if trend > 0 else ""
            html += f'<div><span class="value">{value}</span>'
            if trend != 0:
                html += f'<span class="trend {trend_class}">{trend_sign}{trend:.1f}%</span>'
            html += '</div>\n'
            if binding.get("subtitle"):
                html += f'<p style="color:#999;font-size:12px;margin-top:5px;">{binding["subtitle"]}</p>\n'
        else:
            html += f'<div style="color:#666;font-size:12px;">Panel: {panel.panel_id}</div>\n'

        html += '</div>\n'

    html += """
</div>
</body>
</html>
"""
    return html


# ---------------------------------------------------------------------------
# Convenience function for quick dashboard creation
# ---------------------------------------------------------------------------

def quick_dashboard(buses: Optional[List[BusVoltageData]] = None,
                     lines: Optional[List[LineLoadingData]] = None,
                     generators: Optional[List[GenerationData]] = None,
                     time_data: Optional[Dict[str, List[float]]] = None) -> Dict[str, Any]:
    """
    Quickly create a complete dashboard from system data.

    Args:
        buses: Bus voltage data
        lines: Line loading data
        generators: Generator data
        time_data: Time series data dict {series_name: [values]}

    Returns:
        Dictionary with all dashboard charts
    """
    charts = {}

    if buses:
        charts["voltage_profile"] = plot_voltage_profile(buses)

    if lines:
        charts["line_loading"] = plot_loading_lines(lines)

    if generators:
        charts["generation_mix"] = plot_pie_generation(generators)

    if time_data:
        times = list(range(len(next(iter(time_data.values())))))
        charts["time_series"] = plot_multi_series(
            [t * 0.001 for t in times], time_data,
            title="System Response", y_label="Value"
        )

    return {
        "dashboard": create_dashboard({}, layout="default"),
        "charts": charts,
        "generated_at": datetime.now().isoformat()
    }
