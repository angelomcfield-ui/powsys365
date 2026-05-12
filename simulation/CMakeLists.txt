# =============================================================================
# simulation/CMakeLists.txt - Transient Simulation Engine
# =============================================================================
# Library: powsys365_simulation
# Dependencies: core, SUNDIALS (CVODE, IDA), FMI library
# Provides: Transient stability simulation, dynamic models for synchronous
#           machines, exciters, governors, loads, and FMI 2.0 co-simulation.
# =============================================================================

if(NOT BUILD_SIMULATION)
    message(STATUS "Transient simulation engine disabled (BUILD_SIMULATION=OFF).")
    return()
endif()

# ---------------------------------------------------------------------------
# Find SUNDIALS (CVODE + IDA solvers)
# ---------------------------------------------------------------------------
find_package(SUNDIALS QUIET)
if(NOT SUNDIALS_FOUND)
    message(WARNING "SUNDIALS not found via find_package. Trying FetchContent fallback...")
    include(FetchContent)
    FetchContent_Declare(sundials
        GIT_REPOSITORY https://github.com/LLNL/sundials.git
        GIT_TAG        v6.7.0
        GIT_SHALLOW    TRUE
        CMAKE_ARGS
            -DEXAMPLES_ENABLE_C=OFF
            -DEXAMPLES_ENABLE_CXX=OFF
            -DBUILD_SHARED_LIBS=OFF
            -DENABLE_OPENMP=${ENABLE_OPENMP}
            -DENABLE_MPI=OFF
    )
    set(EXAMPLES_ENABLE_C OFF CACHE BOOL "" FORCE)
    set(EXAMPLES_ENABLE_CXX OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS_SAVED ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(sundials)
    set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_SAVED} CACHE BOOL "" FORCE)
    set(SUNDIALS_FOUND TRUE)
endif()

# ---------------------------------------------------------------------------
# Find FMI Library (FMILibrary - FMI 2.0 support)
# ---------------------------------------------------------------------------
find_package(FMILibrary QUIET)
if(NOT FMILibrary_FOUND)
    message(STATUS "FMILibrary not found via find_package. Using stub/FetchContent fallback.")
    set(FMILibrary_FOUND FALSE)
endif()

# ---------------------------------------------------------------------------
# Source files
# ---------------------------------------------------------------------------
file(GLOB_RECURSE SIMULATION_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

file(GLOB_RECURSE SIMULATION_HEADERS
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/powsy365/simulation/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/powsy365/simulation/*.hpp"
)

# ---------------------------------------------------------------------------
# Library target
# ---------------------------------------------------------------------------
add_library(powsys365_simulation STATIC)

target_sources(powsys365_simulation
    PRIVATE
        ${SIMULATION_SOURCES}
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include"
        FILES ${SIMULATION_HEADERS}
)

# ---------------------------------------------------------------------------
# Include directories
# ---------------------------------------------------------------------------
target_include_directories(powsys365_simulation
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ---------------------------------------------------------------------------
# Link dependencies
# ---------------------------------------------------------------------------
target_link_libraries(powsys365_simulation
    PUBLIC
        powsys365_core
        powsys365_warnings
        Eigen3::Eigen
        nlohmann_json::nlohmann_json
)

# --- SUNDIALS (CVODE + IDA) ---
if(SUNDIALS_FOUND AND TARGET SUNDIALS::cvode)
    target_link_libraries(powsys365_simulation PUBLIC SUNDIALS::cvode SUNDIALS::ida)
elseif(TARGET sundials_cvode_static)
    target_link_libraries(powsys365_simulation PUBLIC sundials_cvode_static sundials_ida_static)
elseif(TARGET sundials_cvode)
    target_link_libraries(powsys365_simulation PUBLIC sundials_cvode sundials_ida)
else()
    message(WARNING "SUNDIALS CVODE/IDA targets not found. Simulation may have limited ODE support.")
endif()

if(SUNDIALS_FOUND AND TARGET SUNDIALS::nvecserial)
    target_link_libraries(powsys365_simulation PUBLIC SUNDIALS::nvecserial)
elseif(TARGET sundials_nvecserial_static)
    target_link_libraries(powsys365_simulation PUBLIC sundials_nvecserial_static)
elseif(TARGET sundials_nvecserial)
    target_link_libraries(powsys365_simulation PUBLIC sundials_nvecserial)
endif()

if(SUNDIALS_FOUND AND TARGET SUNDIALS::sunlinsoldense)
    target_link_libraries(powsys365_simulation PUBLIC SUNDIALS::sunlinsoldense)
elseif(TARGET sundials_sunlinsoldense_static)
    target_link_libraries(powsys365_simulation PUBLIC sundials_sunlinsoldense_static)
endif()

if(SUNDIALS_FOUND AND TARGET SUNDIALS::sunmatrixdense)
    target_link_libraries(powsys365_simulation PUBLIC SUNDIALS::sunmatrixdense)
elseif(TARGET sundials_sunmatrixdense_static)
    target_link_libraries(powsys365_simulation PUBLIC sundials_sunmatrixdense_static)
endif()

if(ENABLE_OPENMP AND OpenMP_CXX_FOUND AND TARGET SUNDIALS::nvecopenmp)
    target_link_libraries(powsys365_simulation PUBLIC SUNDIALS::nvecopenmp)
elseif(ENABLE_OPENMP AND TARGET sundials_nvecopenmp_static)
    target_link_libraries(powsys365_simulation PUBLIC sundials_nvecopenmp_static)
endif()

# --- FMI Library ---
if(FMILibrary_FOUND)
    target_compile_definitions(powsys365_simulation PUBLIC POWSYS365_HAS_FMI=1)
    if(TARGET fmilibrary)
        target_link_libraries(powsys365_simulation PUBLIC fmilibrary)
    elseif(TARGET fmilib)
        target_link_libraries(powsys365_simulation PUBLIC fmilib)
    else()
        target_link_libraries(powsys365_simulation PUBLIC ${FMILibrary_LIBRARIES})
        target_include_directories(powsys365_simulation PUBLIC ${FMILibrary_INCLUDE_DIRS})
    endif()
else()
    target_compile_definitions(powsys365_simulation PUBLIC POWSYS365_HAS_FMI=0)
    message(WARNING "FMILibrary not found. FMI 2.0 co-simulation will be disabled.")
endif()

# --- OpenMP ---
if(OpenMP_CXX_FOUND AND ENABLE_OPENMP)
    target_link_libraries(powsys365_simulation PUBLIC OpenMP::OpenMP_CXX)
endif()

# ---------------------------------------------------------------------------
# Installation
# ---------------------------------------------------------------------------
install(TARGETS powsys365_simulation
    EXPORT POWSYS365Targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    FILE_SET HEADERS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/powsys365/simulation
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(BUILD_TESTS AND TARGET Catch2::Catch2WithMain)
    file(GLOB SIMULATION_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
    if(SIMULATION_TEST_SOURCES)
        add_executable(powsys365_simulation_tests ${SIMULATION_TEST_SOURCES})
        target_link_libraries(powsys365_simulation_tests
            PRIVATE
                powsys365_simulation
                powsys365_core
                Catch2::Catch2WithMain
        )
        catch_discover_tests(powsys365_simulation_tests)
    endif()
endif()
