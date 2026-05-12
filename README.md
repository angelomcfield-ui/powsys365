# POWSYS365 - Power System Analysis Platform

[![POWSYS365 CI/CD](https://github.com/powsybl/powsys365/actions/workflows/build.yml/badge.svg)](https://github.com/powsybl/powsys365/actions/workflows/build.yml)
[![Version](https://img.shields.io/badge/version-1.0.0-blue.svg)](https://github.com/powsybl/powsys365/releases)
[![CMake](https://img.shields.io/badge/CMake-3.31+-064F8C.svg)](https://cmake.org)
[![C++17](https://img.shields.io/badge/C++-17-00599C.svg)](https://isocpp.org)
[![License](https://img.shields.io/badge/license-MPL--2.0-green.svg)](LICENSE)

> A modern, multi-platform power system analysis engine with integrated GUI,
> Python bindings, SCADA protocols, transient simulation, AI/LLM integration,
> and an embedded IDE.

---

## Table of Contents

- [Features](#features)
- [Technology Stack](#technology-stack)
- [Supported Platforms](#supported-platforms)
- [Directory Structure](#directory-structure)
- [Building from Source](#building-from-source)
  - [Prerequisites](#prerequisites)
  - [macOS](#macos)
  - [Linux (Ubuntu/Debian)](#linux-ubuntudebian)
  - [Windows](#windows)
- [Build Options](#build-options)
- [Running Tests](#running-tests)
- [Installation](#installation)
- [Packaging](#packaging)
- [Usage](#usage)
- [Architecture](#architecture)
- [Contributing](#contributing)
- [License](#license)

---

## Features

- **Core Power System Engine**: Bus-branch and node-breaker topology, load flow
  solvers (NR, DC, FDLF), state estimation, and contingency analysis.
- **Transient Simulation**: Synchronous machine dynamic models, excitation systems,
  turbine governors, loads. Integrates SUNDIALS (CVODE/IDA) and FMI 2.0.
- **SCADA Protocols**: IEC 61850 (MMS/GOOSE/SV), DNP3, Modbus (TCP/RTU), OPC-UA.
- **Results & Reporting**: Export to PDF, Excel (xlsx), HTML dashboards with
  charts and tables.
- **Qt6 GUI**: Modern QML-based interface with embedded Monaco editor, charts,
  network visualization, and WebEngine for documentation.
- **Python Bindings**: Full pybind11 integration for scripting and Jupyter
  notebook workflows.
- **AI/LLM Integration**: Python-embedded LLM analysis for natural language
  queries and predictive maintenance.
- **Embedded IDE**: Script editor with syntax highlighting, terminal widget,
  and debugging support.

---

## Technology Stack

| Component         | Technology                                     |
|-------------------|------------------------------------------------|
| Build System      | CMake 3.31+                                    |
| C++ Standard      | C++17                                          |
| GUI Framework     | Qt 6.5+ (Widgets, QML, Charts, WebEngine)      |
| Math & Algebra    | Eigen3 (header-only)                           |
| ODE Solvers       | SUNDIALS (CVODE, IDA)                          |
| Co-Simulation     | FMI 2.0 (FMILibrary)                           |
| SCADA Protocols   | libiec61850, opendnp3, libmodbus, open62541    |
| Python Bindings   | pybind11 2.13+                                 |
| Testing           | Catch2 v3                                      |
| JSON              | nlohmann/json                                  |
| PDF Export        | libharu                                        |
| Excel Export      | libxlsxwriter                                  |
| Database          | PostgreSQL (libpq)                             |
| Parallelization   | OpenMP (optional)                              |

---

## Supported Platforms

| Platform | Architecture | Status |
|----------|-------------|--------|
| macOS 12+ | x86_64, ARM64 (Universal Binary) | Supported |
| Ubuntu 22.04+ / Debian 12+ | x86_64, aarch64 | Supported |
| Windows 11 | x64 | Supported |

---

## Directory Structure

```
POWSYS365/
├── CMakeLists.txt              # Root CMake configuration
├── cmake/                      # CMake modules and config templates
│   └── POWSYS365Config.cmake.in
├── .github/
│   └── workflows/
│       └── build.yml           # CI/CD pipeline (GitHub Actions)
├── core/                       # Core power system engine
│   ├── commons/                # Common headers and utilities
│   ├── include/powsy365/       # Public API headers
│   ├── src/                    # Implementation files
│   └── tests/                  # Core unit tests
├── python/                     # Python bindings (pybind11)
│   ├── src/                    # Binding implementations
│   ├── include/                # Binding headers
│   └── powsys365/              # Python package files
├── ui/                         # Qt6 GUI application
│   ├── cpp/                    # C++ backend for QML
│   ├── qml/                    # QML frontend files
│   ├── resources/              # Icons, images, translations
│   └── main.cpp                # Application entry point
├── simulation/                 # Transient simulation engine
│   ├── include/                # Public headers
│   └── src/                    # Implementation
├── scada/                      # SCADA protocol adapters
│   ├── include/                # Public headers
│   └── src/                    # Implementation
├── results/                    # Results & reporting
│   ├── include/                # Public headers
│   ├── src/                    # Implementation
│   └── templates/              # HTML report templates
├── ai/                         # AI/LLM integration
│   ├── include/                # Public headers
│   ├── src/                    # Implementation
│   ├── llm/                    # LLM Python scripts
│   └── models/                 # Model configurations
├── ide/                        # Embedded IDE components
│   ├── include/                # Public headers
│   ├── src/                    # Implementation
│   └── qml/                    # IDE QML files
├── tests/                      # Integration & regression tests
│   ├── core/                   # Core integration tests
│   ├── math/                   # Math test suites
│   ├── loadflow/               # Load flow solver tests
│   ├── simulation/             # Simulation tests
│   ├── scada/                  # SCADA protocol tests
│   ├── results/                # Results export tests
│   ├── ai/                     # AI module tests
│   ├── integration/            # Cross-module integration tests
│   ├── regression/             # Regression/benchmark tests
│   ├── utils/                  # Shared test utilities
│   └── data/                   # Test data files
├── third_party/                # Vendored third-party libraries
└── docs/
    └── architecture.md         # Architecture documentation
```

---

## Building from Source

### Prerequisites

#### All Platforms

- CMake 3.31 or later
- Ninja (recommended) or Make
- C++17 compiler:
  - macOS: Xcode 14+ (Clang)
  - Linux: GCC 11+ or Clang 15+
  - Windows: Visual Studio 2022 (MSVC v193+)
- Git

#### macOS

```bash
brew install cmake ninja eigen postgresql@15 qt@6 open-mpi
```

#### Linux (Ubuntu/Debian)

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build \
    libeigen3-dev libpq-dev qt6-base-dev qt6-tools-dev \
    qt6-declarative-dev qt6-quickcontrols2-dev qt6-charts-dev \
    qt6-webengine-dev libopenmpi-dev pybind11-dev catch2 \
    nlohmann-json3-dev libssl-dev pkg-config
```

#### Windows

Install via vcpkg or manually:
- Visual Studio 2022 with C++ workload
- Qt 6.5+ (via Qt Online Installer or vcpkg)
- PostgreSQL 13+

### Build Instructions

#### Quick Start

```bash
git clone https://github.com/powsybl/powsys365.git
cd powsys365

# Configure
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --parallel

# Test
ctest --test-dir build --output-on-failure --parallel

# Install
sudo cmake --install build
```

#### macOS Universal Binary

```bash
cmake -B build -S . -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
    -DBUILD_UI=ON

cmake --build build --parallel
```

#### Linux

```bash
cmake -B build -S . -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=gcc \
    -DCMAKE_CXX_COMPILER=g++ \
    -DBUILD_UI=ON \
    -DBUILD_PYTHON=ON

cmake --build build --parallel
```

#### Windows (Visual Studio)

```cmd
cmake -B build -S . -G "Visual Studio 17 2022" -A x64 ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DBUILD_UI=ON ^
    -DBUILD_PYTHON=OFF

cmake --build build --config Release --parallel
```

---

## Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_UI` | `ON` | Build Qt6-based GUI application |
| `BUILD_PYTHON` | `ON` | Build Python bindings (pybind11) |
| `BUILD_TESTS` | `ON` | Build unit and integration tests |
| `BUILD_SCADA` | `ON` | Build SCADA protocol libraries |
| `BUILD_SIMULATION` | `ON` | Build transient simulation engine |
| `BUILD_IDE` | `ON` | Build embedded IDE components |
| `BUILD_AI` | `ON` | Build AI/LLM integration module |
| `BUILD_SHARED_LIBS` | `OFF` | Build shared libraries instead of static |
| `ENABLE_OPENMP` | `ON` | Enable OpenMP parallelization |
| `ENABLE_SANITIZERS` | `OFF` | Enable Address/UB sanitizers (Debug only) |
| `BUILD_PACKAGING` | `ON` | Enable CPack packaging targets |

---

## Running Tests

### All Tests

```bash
cd build
ctest --output-on-failure --parallel
```

### Specific Test Suites

```bash
# Core unit tests
ctest -R test_core --output-on-failure

# Load flow solver tests
ctest -R test_loadflow --output-on-failure

# Simulation tests
ctest -R test_simulation --output-on-failure

# SCADA protocol tests
ctest -R test_scada --output-on-failure

# Integration tests
ctest -R test_integration --output-on-failure

# Regression tests (may take longer)
ctest -R test_regression --output-on-failure
```

### Test Categories

| Label | Description |
|-------|-------------|
| `unit` | Fast unit tests for individual modules |
| `integration` | Cross-module integration tests |
| `slow` | Regression and benchmark tests |

---

## Installation

### System-wide Installation

```bash
sudo cmake --install build --prefix /usr/local
```

### User-local Installation

```bash
cmake --install build --prefix "$HOME/.local"
```

---

## Packaging

### macOS (.dmg)

```bash
cd build
cpack -G DragNDrop
```

### Linux (.deb / .rpm / .tar.gz)

```bash
cd build
cpack -G DEB    # Debian/Ubuntu
cpack -G RPM    # Fedora/RHEL
cpack -G TGZ    # Generic tarball
```

### Windows (.exe / .zip)

```bash
cd build
cpack -G NSIS   # Installer executable
cpack -G ZIP    # ZIP archive
```

---

## Usage

### GUI Application

```bash
powsys365_gui
```

### Python API

```python
import powsys365

# Load a power system model
net = powsys365.PowerSystem.load_case("case14.raw")

# Run load flow
solver = powsys365.LoadFlowSolver()
result = solver.solve(net)
print(f"Converged: {result.converged}, iterations: {result.iterations}")

# Access bus voltages
for bus in net.buses:
    print(f"Bus {bus.id}: V = {bus.vmagnitude:.4f} pu")
```

### C++ API

```cpp
#include <powsy365/power_system.h>
#include <powsy365/load_flow.h>

int main() {
    auto net = powsys365::PowerSystem::load_case("case14.raw");
    
    powsys365::LoadFlowSolver solver;
    auto result = solver.solve(*net);
    
    std::cout << "Converged: " << result.converged
              << ", Iterations: " << result.iterations << "\n";
    return 0;
}
```

---

## Architecture

See [docs/architecture.md](docs/architecture.md) for detailed architecture
 documentation including component diagrams and data flow diagrams.

---

## Contributing

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

Please ensure:
- Code follows the project formatting style (`.clang-format`)
- All tests pass (`ctest --output-on-failure`)
- New features include corresponding tests
- Documentation is updated as needed

---

## License

This project is licensed under the Mozilla Public License 2.0 (MPL-2.0).
See [LICENSE](LICENSE) for details.

---

## Acknowledgments

- [powsybl](https://www.powsybl.org/) - Open source power system blocks
- [SUNDIALS](https://computing.llnl.gov/projects/sundials) - LLNL
- [Eigen](https://eigen.tuxfamily.org/) - Linear algebra library
- [Qt](https://www.qt.io/) - Cross-platform GUI framework
