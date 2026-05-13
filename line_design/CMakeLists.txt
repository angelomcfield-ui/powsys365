# ============================================================
# POWSYS365 – Line Design Module
# Transmission line engineering: catenary, ampacity, vibration,
# FEM, tower design, conductor database.
# ============================================================

cmake_minimum_required(VERSION 3.14)

project(powsys365_linedesign VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Source files
set(LINE_DESIGN_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/catenary_calculator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ampacity_calculator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vibration_analyzer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/fem_analyzer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tower_designer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/conductor_database.cpp
)

set(LINE_DESIGN_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/catenary_calculator.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ampacity_calculator.h
    ${CMAKE_CURRENT_SOURCE_DIR}/vibration_analyzer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/fem_analyzer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/tower_designer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/conductor_database.h
)

# Static library
add_library(powsys365_linedesign STATIC
    ${LINE_DESIGN_SOURCES}
    ${LINE_DESIGN_HEADERS}
)

target_include_directories(powsys365_linedesign
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include/line_design>
)

# Compiler flags
target_compile_options(powsys365_linedesign PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
        -Wnon-virtual-dtor -Wold-style-cast
    >
    $<$<CXX_COMPILER_ID:MSVC>:
        /W4 /permissive- /Zc:__cplusplus
    >
)

# Enable math definitions on Windows
target_compile_definitions(powsys365_linedesign PRIVATE
    $<$<PLATFORM_ID:Windows>:_USE_MATH_DEFINES>
    $<$<PLATFORM_ID:Windows>:NOMINMAX>
)

# Optional: build tests
option(LINE_DESIGN_BUILD_TESTS "Build line_design unit tests" OFF)
if(LINE_DESIGN_BUILD_TESTS)
    enable_testing()
    add_executable(test_linedesign
        tests/test_catenary.cpp
        tests/test_ampacity.cpp
        tests/test_vibration.cpp
        tests/test_fem.cpp
        tests/test_tower.cpp
        tests/test_database.cpp
    )
    target_link_libraries(test_linedesign PRIVATE powsys365_linedesign)
    add_test(NAME LineDesignTests COMMAND test_linedesign)
endif()

# Install
install(TARGETS powsys365_linedesign
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)
install(FILES ${LINE_DESIGN_HEADERS}
    DESTINATION include/line_design
)
