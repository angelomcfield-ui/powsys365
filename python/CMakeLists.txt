# =============================================================================
# python/CMakeLists.txt - Python Bindings (pybind11)
# =============================================================================
# Builds: powsys365 Python module (pybind11)
# Exposes: PowerSystem, LoadFlowSolver, NetworkTopology, Bus, Branch,
#          Generator, Load, and other core classes.
# =============================================================================

if(NOT BUILD_PYTHON OR NOT TARGET pybind11::module)
    message(STATUS "Python bindings disabled or pybind11 not available.")
    return()
endif()

# ---------------------------------------------------------------------------
# Python module: _powsys365_native
# ---------------------------------------------------------------------------
pybind11_add_module(_powsys365_native
    bindings.cpp
)

# ---------------------------------------------------------------------------
# Link dependencies
# ---------------------------------------------------------------------------
target_link_libraries(_powsys365_native
    PRIVATE
        powsys365_core
        powsys365_warnings
        Eigen3::Eigen
)

if(BUILD_SIMULATION AND TARGET powsys365_simulation)
    target_link_libraries(_powsys365_native
        PRIVATE
            powsys365_simulation
    )
    target_compile_definitions(_powsys365_native PRIVATE POWSYS365_HAS_SIMULATION=1)
endif()

if(BUILD_SCADA AND TARGET powsys365_scada)
    target_link_libraries(_powsys365_native
        PRIVATE
            powsys365_scada
    )
    target_compile_definitions(_powsys365_native PRIVATE POWSYS365_HAS_SCADA=1)
endif()

# ---------------------------------------------------------------------------
# Include directories
# ---------------------------------------------------------------------------
target_include_directories(_powsys365_native
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ---------------------------------------------------------------------------
# Properties
# ---------------------------------------------------------------------------
set_target_properties(_powsys365_native PROPERTIES
    CXX_VISIBILITY_PRESET "hidden"
    VISIBILITY_INLINES_HIDDEN ON
)

# ---------------------------------------------------------------------------
# Installation
# ---------------------------------------------------------------------------
install(TARGETS _powsys365_native
    EXPORT POWSYS365Targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/python/powsys365
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}/python/powsys365
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/python/powsys365
)

# Install Python package files
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/powsys365/
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/python/powsys365
    FILES_MATCHING PATTERN "*.py"
)
