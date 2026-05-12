# POWSYS365 - Architecture Documentation

## Overview

POWSYS365 is a modular, layered power system analysis platform designed for
multi-platform deployment (macOS Universal Binary, Linux, Windows). The
architecture follows clean separation of concerns with well-defined interfaces
between layers.

---

## Architectural Layers

```
+------------------------------------------------------------------+
|                         PRESENTATION LAYER                        |
|  +------------------+  +------------------+  +------------------+ |
|  | Qt6 GUI (QML)    |  | Python Bindings  |  | REST/Web API     | |
|  | powsys365_gui    |  | pybind11 module  |  | (future)         | |
|  +------------------+  +------------------+  +------------------+ |
+------------------------------------------------------------------+
|                         APPLICATION LAYER                         |
|  +------------------+  +------------------+  +------------------+ |
|  | IDE Components   |  | AI/LLM Module    |  | Results Engine   | |
|  | powsys365_ide    |  | powsys365_ai     |  | powsys365_results| |
|  | - Monaco Editor  |  | - NL Queries     |  | - PDF Export     | |
|  | - Terminal       |  | - Anomaly Detect |  | - Excel Export   | |
|  | - Script Editor  |  | - Predict Maint. |  | - HTML Dashboards| |
|  +------------------+  +------------------+  +------------------+ |
+------------------------------------------------------------------+
|                         DOMAIN LAYER                              |
|  +------------------+  +------------------+  +------------------+ |
|  | Simulation       |  | SCADA Protocols  |  | Core Engine      | |
|  | powsys365_sim    |  | powsys365_scada  |  | powsys365_core   | |
|  | - Transient      |  | - IEC 61850      |  | - Power System   | |
|  | - Dynamic Models |  | - DNP3           |  | - Load Flow      | |
|  | - FMI 2.0        |  | - Modbus         |  | - Topology       | |
|  | - SUNDIALS       |  | - OPC-UA         |  | - State Estim.   | |
|  +------------------+  +------------------+  +------------------+ |
+------------------------------------------------------------------+
|                         INFRASTRUCTURE LAYER                      |
|  +------------------+  +------------------+  +------------------+ |
|  | Math & Algebra   |  | Persistence      |  | Utilities        | |
|  | - Eigen3         |  | - PostgreSQL     |  | - nlohmann/json  | |
|  | - Sparse Matrices|  | - File I/O       |  | - tl::expected   | |
|  | - OpenMP         |  | - CIM/XML/RAW    |  | - Logging        | |
|  +------------------+  +------------------+  +------------------+ |
+------------------------------------------------------------------+
```

---

## Component Diagram

```
                            +------------------+
                            |   powsys365_gui  |
                            |   (Qt6/QML App)  |
                            +--------+---------+
                                     |
            +------------------------+------------------------+
            |                        |                        |
    +-------v-------+      +---------v---------+    +--------v--------+
    | powsys365_ide |      | powsys365_results |    |  powsys365_ai   |
    |               |      |                   |    |                 |
    | Monaco Bridge |      | PDF (libharu)     |    | Python Embed    |
    | Terminal      |      | Excel (xlsxwriter)|    | LLM Scripts     |
    | Syntax Highl. |      | HTML Dashboards   |    | Predict Maint.  |
    +-------+-------+      +---------+---------+    +--------+--------+
            |                        |                        |
            +------------------------+------------------------+
                                     |
                            +--------v---------+
                            | powsys365_core   |
                            |                  |
                            | PowerSystem      |
                            | LoadFlowSolver   |
                            | NetworkTopology  |
                            | SparseMatrix     |
                            +--------+---------+
                                     |
              +----------------------+----------------------+
              |                      |                      |
    +---------v---------+  +---------v---------+  +--------v---------+
    | powsys365_sim     |  | powsys365_scada   |  |   Infrastructure  |
    |                   |  |                   |  |                   |
    | SUNDIALS (CVODE)  |  | IEC 61850         |  | Eigen3            |
    | SUNDIALS (IDA)    |  | DNP3              |  | PostgreSQL        |
    | FMI 2.0 Library   |  | Modbus            |  | OpenMP            |
    | Dynamic Models    |  | OPC-UA            |  | JSON Parser       |
    +-------------------+  +-------------------+  +-------------------+
```

---

## Module Dependencies

```
core (no internal deps)
  |
  +-- simulation (depends on: core, SUNDIALS, FMI)
  |
  +-- scada (depends on: core, IEC61850, DNP3, Modbus, OPC-UA)
  |
  +-- results (depends on: core, libharu, libxlsxwriter)
  |
  +-- ai (depends on: core, Python3, pybind11)
  |
  +-- ide (depends on: core, Qt6)
  |
  +-- python (depends on: core, pybind11)
  |
  +-- ui (depends on: core, results, ide, Qt6)
  |     (optionally: simulation, scada, ai)
  |
  +-- tests (depends on: all available modules)
```

---

## Data Flow

### Load Flow Analysis Flow

```
[Case File (.raw/.xml)]
         |
         v
+--------+---------+
| File I/O Parser  | --> CIM/XML or PSS/E RAW format
+--------+---------+
         |
         v
+--------+---------+
| PowerSystem      | --> Bus, Branch, Gen, Load objects
| Model Builder    |
+--------+---------+
         |
         v
+--------+---------+
| Network Topology | --> Connectivity analysis, island detection
| Processor        |
+--------+---------+
         |
         v
+--------+---------+
| LoadFlowSolver   | --> NR/DC/FDLF algorithms
| (NR/DC/FDLF)     |     Sparse matrix (Eigen3 + OpenMP)
+--------+---------+
         |
         v
+--------+---------+
| Results Export   | --> PDF / Excel / HTML / JSON
| & Visualization  |     (powsys365_results + Qt6 Charts)
+------------------+
```

### SCADA Real-time Data Flow

```
+------------+    +------------+    +------------+    +------------+
| IEC 61850  |    |   DNP3     |    |  Modbus    |    |  OPC-UA    |
|  Server    |    |  Master    |    |  Client    |    |  Client    |
+-----+------+    +-----+------+    +-----+------+    +-----+------+
      |                 |                 |                 |
      +-----------------+-----------------+-----------------+
                        |
                        v
               +--------+---------+
               | SCADA Adapter    | --> Protocol abstraction
               | (powsys365_scada)|
               +--------+---------+
                        |
                        v
               +--------+---------+
               | Data Acquisition | --> Real-time measurements
               | Engine           | --> Time series storage
               +--------+---------+
                        |
                        v
               +--------+---------+
               | Core PowerSystem | --> State estimation update
               | Model Update     |
               +------------------+
```

### Transient Simulation Flow

```
[Power System Steady State]
            |
            v
+-----------+-----------+
| Dynamic Model Loader  | --> Machine, Exciter, Governor models
| (.dyr / FMU files)    |
+-----------+-----------+
            |
            v
+-----------+-----------+
| ODE Formulation       | --> DAE system construction
| (Sparse Matrices)     |
+-----------+-----------+
            |
            v
+-----------+-----------+
| SUNDIALS Solver       | --> CVODE (ODE) or IDA (DAE)
| (CVODE / IDA)         |     Adaptive timestepping
+-----------+-----------+
            |
            v
+-----------+-----------+
| Results Storage       | --> Time series data
| & Animation           |
+-----------------------+
```

### AI/LLM Query Flow

```
[User Natural Language Query]
            |
            v
+-----------+-----------+
| Query Parser          | --> Intent classification
| (powsys365_ai)        |
+-----------+-----------+
            |
            v
+-----------+-----------+
| LLM Orchestrator      | --> Model selection (local/cloud)
| (Python/PyBind11)     |
+-----------+-----------+
            |
            v
+-----------+-----------+
| Core Engine Bridge    | --> Execute power system operations
| (C++ via pybind11)    |
+-----------+-----------+
            |
            v
+-----------+-----------+
| Response Formatter    | --> Natural language + data
| & Visualization       |
+-----------------------+
```

---

## Build System Architecture

### CMake Target Graph

```
 powsys365_warnings (INTERFACE)
         |
         |  PUBLIC
         v
+--------+---------+
| powsys365_core   | <------+------+------+------+------+------+------+
| (STATIC)         |        |      |      |      |      |      |      |
+------------------+        |      |      |      |      |      |      |
         |                  |      |      |      |      |      |      |
    +----+----+-------------+      |      |      |      |      |      |
    |         |                    |      |      |      |      |      |
    v         v                    v      v      v      v      v      v
+---v--+  +---v----------+  +-----v-----+  +--v----+  +--v----+  +---v-----+
|sim   |  | scada        |  | results   |  | ai    |  | ide   |  | python  |
|      |  |              |  |           |  |       |  |       |  | module  |
+---+--+  +---+----------+  +-----+-----+  +---+---+  +---+---+  +---+-----+
    |         |                    |            |          |          |
    |         |                    |            |          |          |
    |         |                    v            |          |          |
    |         |            +-------v-------+    |          |          |
    |         |            | powsys365_gui |<---+----------+----------+
    |         |            | (EXECUTABLE)  |
    |         |            +---------------+
    |         |
    v         v
+---v---------v--+
| tests/         |
| (Catch2/CTest) |
+----------------+
```

---

## Key Design Decisions

### 1. Header-only vs Compiled Dependencies

| Dependency | Type | Reasoning |
|------------|------|-----------|
| Eigen3 | Header-only | Template-heavy, zero runtime overhead |
| nlohmann/json | Header-only | Convenience, minimal build impact |
| tl::expected | Header-only | C++17 standard-compatible |
| pybind11 | Header-only | Template metaprogramming for bindings |
| Catch2 | Header-only (v3) | Test framework, no runtime dep |
| SUNDIALS | Compiled | C library with complex build |
| Qt6 | Compiled | Large framework, shared libs |

### 2. Static vs Shared Libraries

- **Default**: Static libraries for all POWSYS365 components
- **Rationale**: Easier distribution, no DLL hell, self-contained binaries
- **Override**: Set `BUILD_SHARED_LIBS=ON` for shared library build

### 3. FetchContent vs find_package

| Dependency | Strategy | Fallback |
|------------|----------|----------|
| Eigen3 | `find_package` first | `FetchContent` |
| Qt6 | `find_package` only | N/A (required for UI) |
| PostgreSQL | `find_package` only | Disable feature |
| SUNDIALS | `find_package` first | `FetchContent` |
| Catch2 | `FetchContent` | N/A |
| pybind11 | `FetchContent` | N/A |
| nlohmann/json | `FetchContent` | N/A |
| tl::expected | `FetchContent` | N/A |

### 4. macOS Universal Binary

- Uses `CMAKE_OSX_ARCHITECTURES="x86_64;arm64"`
- Builds both architectures in a single pass
- Deployment target: macOS 12.0 (Monterey)
- Qt6 and all dependencies must be Universal Binaries

---

## Cross-Platform Considerations

### Platform Abstraction

```
+------------------------------------------+
|  Platform Abstraction Layer (core/)      |
+------------------------------------------+
|  macOS  |  Linux    |  Windows           |
|  (Unix) |  (Unix)   |  (Win32)           |
+------------------------------------------+
|  POSIX sockets     |  Winsock2           |
|  pthread           |  Windows threads    |
|  dlopen            |  LoadLibrary        |
|  syslog            |  Event Log          |
+------------------------------------------+
```

### Compiler Support

| Compiler | Minimum Version | Status |
|----------|----------------|--------|
| GCC | 11.0 | Supported |
| Clang | 15.0 | Supported |
| Apple Clang | 14.0 | Supported |
| MSVC | 193 (VS 2022) | Supported |

---

## Security Architecture

### Data Protection

- **AES-256**: Encryption for sensitive configuration and credentials
- **RSA-2048**: Key exchange for secure communications
- **bcrypt**: Password hashing for user authentication
- **TLS 1.3**: Encrypted SCADA communications (OPC-UA, IEC 61850)

### SCADA Security

- IEC 62351 compliant authentication
- Role-based access control (RBAC)
- Secure session management
- Audit logging for all control operations

---

## Performance Considerations

### Parallelization Strategy

```
+------------------------------------------+
| OpenMP Parallel Regions                  |
+------------------------------------------+
| - Sparse matrix-vector products          |
| - Jacobian construction (NR solver)      |
| - Contingency analysis (N-1)             |
| - Monte Carlo simulations                |
+------------------------------------------+
| Thread Pool (QtConcurrent)               |
+------------------------------------------+
| - File I/O operations                    |
| - Results export                         |
| - UI updates                             |
+------------------------------------------+
```

### Memory Management

- Custom allocators for matrix operations
- Memory pools for small object allocation
- Zero-copy data sharing between C++ and Python
- RAII patterns throughout the codebase

---

## Extension Points

### Adding a New SCADA Protocol

1. Create protocol adapter in `scada/src/protocols/`
2. Implement `IScadaProtocol` interface
3. Register in `ScadaProtocolFactory`
4. Add protocol-specific CMake find/FetchContent
5. Write tests in `scada/tests/`

### Adding a New Dynamic Model

1. Create model class in `simulation/src/models/`
2. Inherit from `DynamicModel` base class
3. Implement `DAEEquations()` and `Jacobian()` methods
4. Register in `DynamicModelFactory`
5. Write tests in `simulation/tests/`

### Adding a New Results Export Format

1. Create exporter class in `results/src/exporters/`
2. Implement `IResultsExporter` interface
3. Register in `ResultsExporterFactory`
4. Add format-specific library dependency
5. Write tests in `results/tests/`
