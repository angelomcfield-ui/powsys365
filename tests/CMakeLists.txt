# =============================================================================
# tests/CMakeLists.txt - C++ Unit and Integration Tests (Catch2)
# =============================================================================
# Provides: Comprehensive test suite covering all POWSYS365 modules.
#           Uses Catch2 v3 with native CMake integration (CTest).
# =============================================================================

if(NOT BUILD_TESTS)
    message(STATUS "Tests disabled (BUILD_TESTS=OFF).")
    return()
endif()

if(NOT TARGET Catch2::Catch2WithMain)
    message(FATAL_ERROR "Catch2 not available but BUILD_TESTS=ON. Ensure Catch2 is fetched.")
endif()

include(Catch)

# ---------------------------------------------------------------------------
# Test utilities library (shared test helpers)
# ---------------------------------------------------------------------------
file(GLOB TEST_UTILS_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/utils/*.cpp")
if(TEST_UTILS_SOURCES)
    add_library(powsys365_test_utils STATIC ${TEST_UTILS_SOURCES})
    target_link_libraries(powsys365_test_utils
        PUBLIC
            powsys365_core
            Catch2::Catch2
    )
    target_include_directories(powsys365_test_utils
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/utils
    )
endif()

# ---------------------------------------------------------------------------
# Core Unit Tests
# ---------------------------------------------------------------------------
file(GLOB CORE_UNIT_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp")
if(CORE_UNIT_TESTS)
    add_executable(test_core ${CORE_UNIT_TESTS})
    target_link_libraries(test_core
        PRIVATE
            powsys365_core
            Catch2::Catch2WithMain
            $<TARGET_NAME_IF_EXISTS:powsys365_test_utils>
    )
    catch_discover_tests(test_core TEST_PREFIX "core." REPORTER xml OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/reports")
endif()

# ---------------------------------------------------------------------------
# Math & Linear Algebra Tests
# ---------------------------------------------------------------------------
file(GLOB MATH_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/math/*.cpp")
if(MATH_TESTS)
    add_executable(test_math ${MATH_TESTS})
    target_link_libraries(test_math
        PRIVATE
            powsys365_core
            Catch2::Catch2WithMain
            $<TARGET_NAME_IF_EXISTS:powsys365_test_utils>
    )
    catch_discover_tests(test_math TEST_PREFIX "math." REPORTER xml OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/reports")
endif()

# ---------------------------------------------------------------------------
# Load Flow Solver Tests
# ---------------------------------------------------------------------------
file(GLOB LOADFLOW_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/loadflow/*.cpp")
if(LOADFLOW_TESTS)
    add_executable(test_loadflow ${LOADFLOW_TESTS})
    target_link_libraries(test_loadflow
        PRIVATE
            powsys365_core
            Catch2::Catch2WithMain
            $<TARGET_NAME_IF_EXISTS:powsys365_test_utils>
    )
    catch_discover_tests(test_loadflow TEST_PREFIX "loadflow." REPORTER xml OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/reports")
endif()

# ---------------------------------------------------------------------------
# Simulation Tests
# ---------------------------------------------------------------------------
if(BUILD_SIMULATION AND TARGET powsys365_simulation)
    file(GLOB SIMULATION_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/simulation/*.cpp")
    if(SIMULATION_TESTS)
        add_executable(test_simulation ${SIMULATION_TESTS})
        target_link_libraries(test_simulation
            PRIVATE
                powsys365_simulation
                powsys365_core
                Catch2::Catch2WithMain
                $<TARGET_NAME_IF_EXISTS:powsys365_test_utils>
        )
        catch_discover_tests(test_simulation TEST_PREFIX "simulation." REPORTER xml OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/reports")
    endif()
endif()

# ---------------------------------------------------------------------------
# SCADA Protocol Tests
# ---------------------------------------------------------------------------
if(BUILD_SCADA AND TARGET powsys365_scada)
    file(GLOB SCADA_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/scada/*.cpp")
    if(SCADA_TESTS)
        add_executable(test_scada ${SCADA_TESTS})
        target_link_libraries(test_scada
            PRIVATE
                powsys365_scada
                powsys365_core
                Catch2::Catch2WithMain
                $<TARGET_NAME_IF_EXISTS:powsys365_test_utils>
        )
        catch_discover_tests(test_scada TEST_PREFIX "scada." REPORTER xml OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/reports")
    endif()
endif()

# ---------------------------------------------------------------------------
# Results & Export Tests
# ---------------------------------------------------------------------------
file(GLOB RESULTS_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/results/*.cpp")
if(RESULTS_TESTS)
    add_executable(test_results ${RESULTS_TESTS})
    target_link_libraries(test_results
        PRIVATE
            powsys365_results
            powsys365_core
            Catch2::Catch2WithMain
            $<TARGET_NAME_IF_EXISTS:powsys365_test_utils>
    )
    catch_discover_tests(test_results TEST_PREFIX "results." REPORTER xml OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/reports")
endif()

# ---------------------------------------------------------------------------
# AI Module Tests
# ---------------------------------------------------------------------------
if(BUILD_AI AND TARGET powsys365_ai)
    file(GLOB AI_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ai/*.cpp")
    if(AI_TESTS)
        add_executable(test_ai ${AI_TESTS})
        target_link_libraries(test_ai
            PRIVATE
                powsys365_ai
                powsys365_core
                Catch2::Catch2WithMain
                $<TARGET_NAME_IF_EXISTS:powsys365_test_utils>
        )
        catch_discover_tests(test_ai TEST_PREFIX "ai." REPORTER xml OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/reports")
    endif()
endif()

# ---------------------------------------------------------------------------
# Integration Tests (multi-module)
# ---------------------------------------------------------------------------
file(GLOB INTEGRATION_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/integration/*.cpp")
if(INTEGRATION_TESTS)
    add_executable(test_integration ${INTEGRATION_TESTS})
    target_link_libraries(test_integration
        PRIVATE
            powsys365_core
            powsys365_results
            $<TARGET_NAME_IF_EXISTS:powsys365_simulation>
            $<TARGET_NAME_IF_EXISTS:powsys365_scada>
            $<TARGET_NAME_IF_EXISTS:powsys365_ai>
            Catch2::Catch2WithMain
            $<TARGET_NAME_IF_EXISTS:powsys365_test_utils>
    )
    catch_discover_tests(test_integration TEST_PREFIX "integration." REPORTER xml OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/reports")
endif()

# ---------------------------------------------------------------------------
# Regression Tests (benchmark comparisons)
# ---------------------------------------------------------------------------
file(GLOB REGRESSION_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/regression/*.cpp")
if(REGRESSION_TESTS)
    add_executable(test_regression ${REGRESSION_TESTS})
    target_link_libraries(test_regression
        PRIVATE
            powsys365_core
            $<TARGET_NAME_IF_EXISTS:powsys365_simulation>
            Catch2::Catch2WithMain
            $<TARGET_NAME_IF_EXISTS:powsys365_test_utils>
    )
    # Regression tests may take longer; label them accordingly
    catch_discover_tests(test_regression
        TEST_PREFIX "regression."
        PROPERTIES LABELS "slow"
        REPORTER xml
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/reports"
    )
endif()

# ---------------------------------------------------------------------------
# Test data files
# ---------------------------------------------------------------------------
file(GLOB TEST_DATA_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/data/*")
if(TEST_DATA_FILES)
    # Copy test data to build directory
    add_custom_target(copy_test_data ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/data
            ${CMAKE_CURRENT_BINARY_DIR}/data
        COMMENT "Copying test data files"
    )
endif()

# ---------------------------------------------------------------------------
# CTest configuration
# ---------------------------------------------------------------------------
set(CTEST_PARALLEL_LEVEL "auto" CACHE STRING "Parallel test execution level")
set(CTEST_OUTPUT_ON_FAILURE ON CACHE BOOL "Print test output on failure" FORCE)

# Add labels for CTest filtering
set_tests_properties(test_core PROPERTIES LABELS "unit;core")
set_tests_properties(test_math PROPERTIES LABELS "unit;math")
set_tests_properties(test_loadflow PROPERTIES LABELS "unit;loadflow")
set_tests_properties(test_integration PROPERTIES LABELS "integration")
