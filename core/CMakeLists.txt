cmake_minimum_required(VERSION 3.14)
project(powsys365_core VERSION 1.0.0 LANGUAGES CXX)

# ============================================================================
# C++ STANDARD
# ============================================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================================
# OPTIONS
# ============================================================================
option(POWSYS365_BUILD_TESTS "Build unit tests" ON)
option(POWSYS365_BUILD_EXAMPLES "Build example programs" ON)
option(POWSYS365_ENABLE_OPENMP "Enable OpenMP parallelization" OFF)

# ============================================================================
# FIND DEPENDENCIES
# ============================================================================
find_package(Eigen3 3.3 REQUIRED NO_MODULE)

if(POWSYS365_ENABLE_OPENMP)
    find_package(OpenMP)
    if(OpenMP_CXX_FOUND)
        add_definitions(-DEIGEN_HAS_OPENMP)
    endif()
endif()

# ============================================================================
# SOURCE FILES
# ============================================================================
set(COMMON_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/commons/math_utils.cpp
)

set(CORE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ybus_builder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/power_system.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/load_flow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/short_circuit.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/stability.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/opf_solver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/data_manager.cpp
)

set(ALL_SOURCES
    ${COMMON_SOURCES}
    ${CORE_SOURCES}
)

# ============================================================================
# INCLUDE DIRECTORIES
# ============================================================================
set(POWSYS365_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/commons
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${EIGEN3_INCLUDE_DIR}
)

# ============================================================================
# STATIC LIBRARY
# ============================================================================
add_library(powsys365_core STATIC ${ALL_SOURCES})

target_include_directories(powsys365_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/commons
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(powsys365_core
    PUBLIC
        Eigen3::Eigen
)

if(POWSYS365_ENABLE_OPENMP AND OpenMP_CXX_FOUND)
    target_link_libraries(powsys365_core PUBLIC OpenMP::OpenMP_CXX)
endif()

# ============================================================================
# COMPILER FLAGS
# ============================================================================
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(powsys365_core PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wsign-conversion
        -Wno-unused-parameter
        -O3
        -march=native
        -ffast-math
    )
elseif(MSVC)
    target_compile_options(powsys365_core PRIVATE
        /W4
        /O2
        /arch:AVX2
        /EHsc
    )
endif()

# ============================================================================
# POSITION INDEPENDENT CODE (for shared library linking)
# ============================================================================
set_target_properties(powsys365_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

# ============================================================================
# TESTS
# ============================================================================
if(POWSYS365_BUILD_TESTS)
    enable_testing()
    add_executable(test_load_flow tests/test_load_flow.cpp)
    target_link_libraries(test_load_flow PRIVATE powsys365_core)
    add_test(NAME load_flow_test COMMAND test_load_flow)
endif()

# ============================================================================
# EXAMPLES
# ============================================================================
if(POWSYS365_BUILD_EXAMPLES)
    add_executable(example_ieee14 examples/example_ieee14.cpp)
    target_link_libraries(example_ieee14 PRIVATE powsys365_core)

    add_executable(example_comparison examples/example_comparison.cpp)
    target_link_libraries(example_comparison PRIVATE powsys365_core)
endif()

# ============================================================================
# INSTALL
# ============================================================================
install(TARGETS powsys365_core
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY commons/ DESTINATION include/powsys365/commons)

# ============================================================================
# SUMMARY
# ============================================================================
message(STATUS "")
message(STATUS "POWSYS365 Core Configuration:")
message(STATUS "  Version:        ${PROJECT_VERSION}")
message(STATUS "  C++ Standard:   ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type:     ${CMAKE_BUILD_TYPE}")
message(STATUS "  Eigen3:         ${EIGEN3_VERSION}")
message(STATUS "  OpenMP:         ${POWSYS365_ENABLE_OPENMP}")
message(STATUS "  Tests:          ${POWSYS365_BUILD_TESTS}")
message(STATUS "  Examples:       ${POWSYS365_BUILD_EXAMPLES}")
message(STATUS "")
