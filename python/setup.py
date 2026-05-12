"""
setup.py
========

Build and install configuration for POWSYS365.

Usage::

    cd python
    pip install -e .

The C++ extension ``powsy365_core`` is compiled automatically via
pybind11 when running ``pip install``.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

from pybind11.setup_helpers import (  # type: ignore[import-untyped]
    Pybind11Extension,
    build_ext,
)
from setuptools import find_packages, setup

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------

HERE = Path(__file__).parent.resolve()
CORE_INCLUDE = (HERE.parent / "core" / "include").resolve()
AI_INCLUDE = (HERE.parent / "ai" / "include").resolve()

# Eigen3 – try common locations
EIGEN_PATHS = [
    "/usr/include/eigen3",
    "/usr/local/include/eigen3",
    os.environ.get("EIGEN3_INCLUDE_DIR", ""),
    "/opt/homebrew/include/eigen3",
]
eigen_include = None
for ep in EIGEN_PATHS:
    if ep and Path(ep).is_dir():
        eigen_include = ep
        break

if eigen_include is None:
    import warnings

    warnings.warn(
        "Eigen3 not found in standard locations. "
        "Set EIGEN3_INCLUDE_DIR environment variable or install libeigen3-dev.",
        stacklevel=2,
    )

include_dirs = [str(CORE_INCLUDE), str(AI_INCLUDE), str(HERE / "powsy365")]
if eigen_include:
    include_dirs.append(eigen_include)

# ---------------------------------------------------------------------------
# Extension module
# ---------------------------------------------------------------------------

ext_modules = [
    Pybind11Extension(
        "powsy365_core",
        sources=[
            str(HERE / "bindings.cpp"),
            # Core C++ sources
            str(HERE.parent / "core" / "src" / "bus.cpp"),
            str(HERE.parent / "core" / "src" / "line.cpp"),
            str(HERE.parent / "core" / "src" / "transformer.cpp"),
            str(HERE.parent / "core" / "src" / "generator.cpp"),
            str(HERE.parent / "core" / "src" / "load.cpp"),
            str(HERE.parent / "core" / "src" / "power_system.cpp"),
            str(HERE.parent / "core" / "src" / "ybus_builder.cpp"),
            str(HERE.parent / "core" / "src" / "load_flow_solver.cpp"),
            str(HERE.parent / "core" / "src" / "short_circuit_solver.cpp"),
            str(HERE.parent / "core" / "src" / "results.cpp"),
            str(HERE.parent / "core" / "src" / "solver_config.cpp"),
            # AI C++ sources
            str(HERE.parent / "ai" / "src" / "ai_gateway.cpp"),
            str(HERE.parent / "ai" / "src" / "rag_engine.cpp"),
            str(HERE.parent / "ai" / "src" / "report_generator_ai.cpp"),
        ],
        include_dirs=include_dirs,
        cxx_std=17,
        # Optimization flags for release builds
        extra_compile_args=[
            "-O3",
            "-march=native",
            "-ffast-math",
            "-fopenmp",
            "-DNDEBUG",
        ]
        if sys.platform != "win32"
        else ["/O2", "/openmp", "/DNDEBUG"],
        extra_link_args=["-fopenmp"] if sys.platform != "win32" else [],
        define_macros=[("VERSION_INFO", '"3.0.0"')],
    ),
]

# ---------------------------------------------------------------------------
# Package metadata
# ---------------------------------------------------------------------------

setup(
    name="powsy365",
    version="3.0.0",
    author="POWSYS365 Team",
    author_email="team@powsy365.dev",
    description="High-performance power system analysis with AI integration",
    long_description=(HERE / ".." / "README.md").read_text(encoding="utf-8")
    if (HERE / ".." / "README.md").exists()
    else "",
    long_description_content_type="text/markdown",
    url="https://github.com/powsy365/powsy365",
    license="MIT",
    classifiers=[
        "Development Status :: 4 - Beta",
        "Intended Audience :: Science/Research",
        "Intended Audience :: Developers",
        "Topic :: Scientific/Engineering :: Physics",
        "License :: OSI Approved :: MIT License",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.11",
        "Programming Language :: Python :: 3.12",
        "Programming Language :: Python :: 3.13",
        "Programming Language :: C++",
        "Operating System :: OS Independent",
    ],
    keywords="power-system load-flow newton-raphson short-circuit "
             "transient-stability opf energy",
    packages=find_packages(),
    package_data={
        "powsy365": ["py.typed"],
    },
    include_package_data=True,
    ext_modules=ext_modules,
    cmdclass={"build_ext": build_ext},
    zip_safe=False,
    python_requires=">=3.11",
    install_requires=[
        "numpy>=1.24",
        "scipy>=1.11",
        "pandas>=2.0",
        "matplotlib>=3.7",
        "plotly>=5.18",
    ],
    extras_require={
        "ai": [
            "langchain>=0.3",
            "llama-index>=0.12",
            "requests>=2.31",
            "openai>=1.0",
            "anthropic>=0.8",
        ],
        "power": [
            "pandapower>=2.13",
        ],
        "dev": [
            "pytest>=7.4",
            "pytest-cov>=4.1",
            "mypy>=1.7",
            "ruff>=0.1",
            "black>=23.0",
        ],
        "docs": [
            "sphinx>=7.0",
            "sphinx-rtd-theme>=1.3",
            "myst-parser>=2.0",
        ],
        "all": [
            "langchain>=0.3",
            "llama-index>=0.12",
            "requests>=2.31",
            "openai>=1.0",
            "anthropic>=0.8",
            "pandapower>=2.13",
            "pytest>=7.4",
            "pytest-cov>=4.1",
            "mypy>=1.7",
            "ruff>=0.1",
            "black>=23.0",
            "sphinx>=7.0",
            "sphinx-rtd-theme>=1.3",
            "myst-parser>=2.0",
        ],
    },
    entry_points={
        "console_scripts": [
            "powsy365=powsy365.cli:main",
        ],
    },
)
