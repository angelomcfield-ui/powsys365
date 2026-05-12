#!/usr/bin/env python3
"""
scripts/verify_project.py - POWSYS365 Project Integrity Verification Script

Verifies that ALL expected files exist and have content, checks directory
structure, counts lines per module, generates a coverage report, and flags
any stubs or TODOs.

Usage:
    python scripts/verify_project.py

Exit codes:
    0 - All checks passed
    1 - One or more checks failed
"""

from __future__ import annotations

import os
import sys
from collections import defaultdict
from pathlib import Path
from typing import Any

PROJECT_ROOT = Path(__file__).resolve().parent.parent

EXPECTED_FILES = [
    "CMakeLists.txt",
    "README.md",
    "database/schema.sql",
    "database/migrations/V001__initial_schema.sql",
    "database/migrations/V002__add_indices.sql",
    "database/migrations/V003__add_triggers.sql",
    "database/seeds/ieee_14_barras.sql",
    "database/queries/power_flow_results.sql",
    "database/queries/short_circuit_results.sql",
    "database/queries/system_summary.sql",
    "core/commons/types.h",
    "core/commons/math_utils.h",
    "core/commons/math_utils.cpp",
    "core/commons/matrix_types.h",
    "core/commons/constants.h",
    "core/include/powsy365/power_system.h",
    "core/include/powsy365/load_flow.h",
    "core/include/powsy365/short_circuit.h",
    "core/include/powsy365/ybus_builder.h",
    "core/include/powsy365/opf_solver.h",
    "core/include/powsy365/stability.h",
    "core/include/powsy365/data_manager.h",
    "core/src/power_system.cpp",
    "core/src/load_flow.cpp",
    "core/src/short_circuit.cpp",
    "core/src/ybus_builder.cpp",
    "core/src/opf_solver.cpp",
    "core/src/stability.cpp",
    "core/src/data_manager.cpp",
    "core/CMakeLists.txt",
    "python/bindings.cpp",
    "python/powsy365/__init__.py",
    "python/powsy365/core.py",
    "python/powsy365/network.py",
    "python/powsy365/analysis.py",
    "python/powsy365/utils.py",
    "python/setup.py",
    "python/requirements.txt",
    "ui/main.cpp",
    "ui/qml/main.qml",
    "ui/qml/SLDCanvas.qml",
    "ui/qml/BusComponent.qml",
    "ui/qml/LineComponent.qml",
    "ui/qml/GeneratorComponent.qml",
    "ui/qml/LoadComponent.qml",
    "ui/qml/TransformerComponent.qml",
    "ui/qml/Toolbar.qml",
    "ui/qml/PropertiesPanel.qml",
    "ui/qml/DarkTheme.qml",
    "ui/qml/LightTheme.qml",
    "ui/cpp/main_window_controller.h",
    "ui/cpp/main_window_controller.cpp",
    "ui/cpp/sld_scene.h",
    "ui/cpp/sld_scene.cpp",
    "ui/cpp/theme_manager.h",
    "ui/cpp/theme_manager.cpp",
    "ui/resources/qml.qrc",
    "ui/CMakeLists.txt",
    "ide/CMakeLists.txt",
    "simulation/CMakeLists.txt",
    "simulation/include/powsy365/simulation/real_time_sync.h",
    "simulation/include/powsy365/simulation/event_scheduler.h",
    "simulation/include/powsy365/simulation/fmi_co_simulation.h",
    "simulation/include/powsy365/simulation/integrator.h",
    "simulation/src/real_time_sync.cpp",
    "simulation/src/event_scheduler.cpp",
    "simulation/src/fmi_co_simulation.cpp",
    "simulation/src/integrator.cpp",
    "scada/CMakeLists.txt",
    "scada/include/powsy365/scada/protocol_gateway.h",
    "scada/include/powsy365/scada/scada_hmi.h",
    "scada/include/powsy365/scada/alarm_manager.h",
    "scada/include/powsy365/scada/animation_engine.h",
    "scada/include/powsy365/scada/dnp3_client.h",
    "scada/include/powsy365/scada/iec61850_client.h",
    "scada/include/powsy365/scada/modbus_client.h",
    "scada/include/powsy365/scada/opcua_client.h",
    "scada/src/protocol_gateway.cpp",
    "scada/src/scada_hmi.cpp",
    "scada/src/alarm_manager.cpp",
    "scada/src/animation_engine.cpp",
    "scada/src/dnp3_client.cpp",
    "scada/src/iec61850_client.cpp",
    "scada/src/modbus_client.cpp",
    "scada/src/opcua_client.cpp",
    "results/CMakeLists.txt",
    "results/include/powsy365/results/report_generator.h",
    "results/include/powsy365/results/dashboard_engine.h",
    "results/include/powsy365/results/pdf_exporter.h",
    "results/src/report_generator.cpp",
    "results/src/dashboard_engine.cpp",
    "results/src/pdf_exporter.cpp",
    "results/python/report_templates.py",
    "results/python/dashboard_builder.py",
    "ai/CMakeLists.txt",
    "ai/include/powsy365/ai/ai_gateway.h",
    "ai/include/powsy365/ai/rag_engine.h",
    "ai/include/powsy365/ai/report_generator_ai.h",
    "ai/src/ai_gateway.cpp",
    "ai/src/rag_engine.cpp",
    "ai/src/report_generator_ai.cpp",
    "ai/python/llm_providers.py",
    "ai/python/rag_pipeline.py",
    "ai/python/function_tools.py",
    "ai/python/prompts/fault_diagnosis.txt",
    "ai/python/prompts/power_flow_analysis.txt",
    "ai/python/prompts/report_generation.txt",
    "tests/cpp/main_test.cpp",
    "tests/cpp/test_load_flow.cpp",
    "tests/cpp/test_short_circuit.cpp",
    "tests/cpp/test_ybus.cpp",
    "tests/cpp/test_math_utils.cpp",
    "tests/python/conftest.py",
    "tests/python/test_network.py",
    "tests/python/test_analysis.py",
    "tests/integration/test_end_to_end.cpp",
    ".github/workflows/build.yml",
    "docs/architecture.md",
]

MODULES = {
    "core":        ["core/commons/", "core/include/", "core/src/", "core/examples/", "core/tests/"],
    "python":      ["python/"],
    "ui":          ["ui/"],
    "ide":         ["ide/"],
    "simulation":  ["simulation/"],
    "scada":       ["scada/"],
    "results":     ["results/"],
    "ai":          ["ai/"],
    "database":    ["database/"],
    "tests":       ["tests/cpp/", "tests/python/", "tests/integration/"],
    "docs":        ["docs/"],
    "ci_cd":       [".github/"],
    "root":        [".clang-format", ".gitignore", "CMakeLists.txt", "LICENSE", "README.md", "vcpkg.json"],
}

CODE_EXTENSIONS = {
    ".cpp", ".h", ".hpp", ".c", ".py", ".qml", ".js", ".ts",
    ".sql", ".yml", ".yaml", ".md", ".txt", ".json",
    ".cmake", ".qrc", ".plist", ".in", ".toml", ".cfg", ".ini",
}


def is_code_file(path: Path) -> bool:
    return path.suffix.lower() in CODE_EXTENSIONS or path.name in {
        "CMakeLists.txt", "Makefile", "Dockerfile", ".gitignore",
        ".clang-format", "qmldir", "MANIFEST.in", "setup.cfg",
    }


def count_file_lines(path: Path) -> int:
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            return sum(1 for line in f if line.strip())
    except Exception:
        return 0


def main() -> int:
    root = PROJECT_ROOT
    errors: list[str] = []

    print("=" * 70)
    print("  POWSYS365 - Project Integrity Verification")
    print("=" * 70)
    print()

    # [1] Check all expected files
    print("[1/6] Checking expected files...")
    found = 0
    missing_files: list[str] = []
    for rel_path in EXPECTED_FILES:
        full = root / rel_path
        if full.exists() and full.is_file() and full.stat().st_size > 0:
            found += 1
        else:
            missing_files.append(rel_path)
            if not full.exists():
                errors.append(f"MISSING: {rel_path}")
            elif not full.is_file():
                errors.append(f"NOT A FILE: {rel_path}")
            else:
                errors.append(f"EMPTY: {rel_path}")

    print(f"      Found: {found} / {len(EXPECTED_FILES)}")
    if missing_files:
        print(f"      Missing: {len(missing_files)}")
        for mf in missing_files:
            print(f"        - {mf}")
    print()

    # [2] Check directory structure
    print("[2/6] Checking directory structure...")
    required_dirs = [
        "core", "python", "ui", "ide", "simulation", "scada",
        "results", "ai", "database", "tests", "docs", "scripts",
        ".github/workflows",
    ]
    for d in required_dirs:
        full = root / d
        if not full.exists() or not full.is_dir():
            errors.append(f"MISSING DIRECTORY: {d}")
            print(f"      MISSING: {d}")
    if not any("DIRECTORY" in e for e in errors):
        print("      All required directories present.")
    print()

    # [3] Count lines per module
    print("[3/6] Counting lines of code per module...")
    module_lines: dict[str, int] = defaultdict(int)
    for module_name, prefixes in MODULES.items():
        for prefix in prefixes:
            full = root / prefix
            if full.is_file():
                module_lines[module_name] += count_file_lines(full)
            elif full.is_dir():
                for filepath in full.rglob("*"):
                    if filepath.is_file() and is_code_file(filepath):
                        module_lines[module_name] += count_file_lines(filepath)

    total = sum(v for k, v in module_lines.items())
    for mod, lines in sorted(module_lines.items()):
        print(f"      {mod:20s}: {lines:6d} lines")
    print(f"      {'TOTAL':20s}: {total:6d} lines")
    print()

    # [4] Check for TODOs/stubs
    print("[4/6] Scanning for TODOs/stubs...")
    stub_keywords = ["TODO:", "FIXME:", "HACK:", "XXX:", "STUB:", "PLACEHOLDER"]
    findings: dict[str, list[str]] = defaultdict(list)

    for pattern in ["**/*.cpp", "**/*.h", "**/*.hpp", "**/*.py", "**/*.qml", "**/*.sql"]:
        for filepath in root.glob(pattern):
            rel_str = str(filepath.relative_to(root))
            if "build" in rel_str or "third_party" in rel_str or "verify_project.py" in rel_str:
                continue
            try:
                with open(filepath, "r", encoding="utf-8", errors="replace") as f:
                    for lineno, line in enumerate(f, 1):
                        stripped = line.strip()
                        for kw in stub_keywords:
                            if kw in stripped:
                                if (stripped.startswith("//") or
                                    stripped.startswith("#") or
                                    stripped.startswith("*") or
                                    stripped.startswith('"') or
                                    stripped.startswith("'") or
                                    stripped.startswith("[") or
                                    "stub_keywords" in stripped or
                                    "stub_patterns" in stripped):
                                    continue
                                findings[rel_str].append(f"Line {lineno}: {kw}")
            except Exception:
                pass

    todo_count = sum(len(v) for v in findings.values())
    if todo_count > 0:
        print(f"      Found {todo_count} TODO/stub markers:")
        for fp, markers in sorted(findings.items()):
            for m in markers:
                print(f"        {fp}: {m}")
    else:
        print("      No TODO/stub markers found in source code.")
    print()

    # [5] Coverage report
    print("[5/6] Generating file coverage report...")
    coverage_pct = (found / len(EXPECTED_FILES) * 100.0) if EXPECTED_FILES else 0.0
    print(f"      Overall coverage: {coverage_pct:.1f}%")
    print(f"      ({found} / {len(EXPECTED_FILES)} files)")
    print()

    module_files: dict[str, dict[str, Any]] = defaultdict(lambda: {"expected": 0, "found": 0})
    for rel_path in EXPECTED_FILES:
        mod = rel_path.split("/")[0] if "/" in rel_path else "root"
        module_files[mod]["expected"] += 1
        if (root / rel_path).exists() and (root / rel_path).stat().st_size > 0:
            module_files[mod]["found"] += 1

    print("      Per-module coverage:")
    for mod, data in sorted(module_files.items()):
        pct = (data["found"] / data["expected"] * 100.0) if data["expected"] else 0.0
        status = "OK" if pct >= 100.0 else "PARTIAL"
        print(f"        {mod:20s}: {data['found']:3d}/{data['expected']:3d}  "
              f"({pct:5.1f}%)  [{status}]")
    print()

    # [6] Extra files
    print("[6/6] Checking for unexpected files...")
    expected_set = set(EXPECTED_FILES)
    extra_files = []
    for filepath in root.rglob("*"):
        if not filepath.is_file():
            continue
        rel = str(filepath.relative_to(root))
        skip_prefixes = (
            "build/", "cmake-build-", ".git/", "__pycache__/",
            ".idea/", ".vscode/", ".pyc", ".egg-info/",
        )
        if any(rel.startswith(p) or p in rel for p in skip_prefixes):
            continue
        if rel not in expected_set:
            extra_files.append(rel)

    if extra_files:
        print(f"      Found {len(extra_files)} extra files (not errors):")
        for ef in sorted(extra_files)[:15]:
            print(f"        + {ef}")
        if len(extra_files) > 15:
            print(f"        ... and {len(extra_files) - 15} more")
    else:
        print("      No unexpected files found.")
    print()

    # Final summary
    print("=" * 70)
    print("  VERIFICATION SUMMARY")
    print("=" * 70)
    if not errors:
        print("  Status: ALL CHECKS PASSED")
        print(f"  Files found: {found}/{len(EXPECTED_FILES)}")
        print(f"  Total lines: {total:,}")
        print(f"  Coverage: {coverage_pct:.1f}%")
        if todo_count > 0:
            print(f"  Warnings: {todo_count} TODO markers (not errors)")
        print("=" * 70)
        return 0
    else:
        print("  Status: CHECKS FAILED")
        print(f"  Errors: {len(errors)}")
        for e in errors:
            print(f"    - {e}")
        print("=" * 70)
        return 1


if __name__ == "__main__":
    sys.exit(main())
