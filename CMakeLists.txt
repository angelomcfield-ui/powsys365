# =============================================================================
# POWSYS365 - Power System Analysis Platform
# =============================================================================
# CMake 3.31+ required for modern target-based workflows, FetchContent improvements,
# and enhanced cross-platform support including macOS Universal Binaries.
# =============================================================================
cmake_minimum_required(VERSION 3.31)

# =============================================================================
# Project Definition
# =============================================================================
project(POWSYS365
    VERSION 3.0.0
    DESCRIPTION "POWSYS365 - Power System Analysis Platform v3.0.0"
    HOMEPAGE_URL "https://github.com/angelomcfield-ui/powsys365"
    LANGUAGES CXX C
)

# =============================================================================
# C++ Standard Configuration
# =============================================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for IDEs and static analyzers
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# =============================================================================
# Platform-Specific Configuration
# =============================================================================
# macOS Universal Binary (x86_64 + ARM64) support
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "Build architectures for macOS")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS deployment version")
endif()

# Windows-specific settings
if(WIN32)
    # Enable Unicode support on Windows
    add_compile_definitions(UNICODE _UNICODE)
    # Disable annoying min/max macros from windows.h
    add_compile_definitions(NOMINMAX)
endif()

# =============================================================================
# Build Type Defaults
# =============================================================================
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        "Debug" "Release" "MinSizeRel" "RelWithDebInfo"
    )
endif()

# Position-independent code for static libraries (required for shared objects)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# =============================================================================
# Build Options
# =============================================================================
option(BUILD_UI           "Build Qt6-based GUI application"          ON)
option(BUILD_PYTHON       "Build Python bindings (pybind11)"        ON)
option(BUILD_TESTS        "Build unit and integration tests"         ON)
option(BUILD_SCADA        "Build SCADA protocol libraries"           ON)
option(BUILD_SIMULATION   "Build transient simulation engine"        ON)
option(BUILD_IDE          "Build embedded IDE components"            ON)
option(BUILD_AI           "Build AI/LLM integration module"          ON)
option(BUILD_MODELS       "Build advanced equipment models"          ON)
option(BUILD_HARMONICS    "Build harmonic analysis module"           ON)
option(BUILD_MARKETS      "Build electricity markets module"         ON)
option(BUILD_RELIABILITY  "Build reliability analysis module"        ON)
option(BUILD_LICENSING    "Build license management module"          ON)
option(BUILD_INTEGRATION  "Build integration connectors"             ON)
option(BUILD_ICON_ENGINE  "Build icon rendering engine"              ON)
option(BUILD_I18N         "Build internationalization"               ON)
option(BUILD_IO           "Build import/export module (85+ formats)" ON)
option(BUILD_GIS          "Build GIS/map module (12 CRS)"            ON)
option(BUILD_XTALK        "Build xTalk communication module"         ON)
option(BUILD_LEGAL        "Build legal/GDPR module"                  ON)
option(BUILD_AUDIO        "Build audio engine (OpenAL)"              ON)
option(BUILD_HELP         "Build help/manual module"                 ON)
option(BUILD_LINE_DESIGN  "Build line design module"                 ON)
option(BUILD_CONFIG       "Build configuration module"               ON)
option(BUILD_SHARED_LIBS  "Build shared libraries instead of static"  OFF)
option(ENABLE_OPENMP      "Enable OpenMP parallelization"            ON)
option(ENABLE_SANITIZERS  "Enable Address/UB sanitizers (Debug)"     OFF)
option(BUILD_PACKAGING    "Enable CPack packaging targets"           ON)

# =============================================================================
# CMake Modules Path
# =============================================================================
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# =============================================================================
# Compiler Warnings (GNU/Clang/MSVC)
# =============================================================================
add_library(powsys365_warnings INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(powsys365_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wsign-conversion
        -Wunused
        -Wnon-virtual-dtor
        -Woverloaded-virtual
        -Wnull-dereference
        -Wdouble-promotion
        -Wformat=2
        -Wno-unknown-pragmas
    )
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(powsys365_warnings INTERFACE
            -Wmisleading-indentation
            -Wduplicated-cond
            -Wduplicated-branches
            -Wlogical-op
        )
    endif()
elseif(MSVC)
    target_compile_options(powsys365_warnings INTERFACE
        /W4
        /permissive-
        /w14242
        /w14254
        /w14263
        /w14265
        /w14287
        /we4289
        /w14296
        /w14311
        /w14545
        /w14546
        /w14547
        /w14549
        /w14555
        /w14619
        /w14640
        /w14826
        /w14905
        /w14906
        /w14928
    )
endif()

# =============================================================================
# Sanitizers (Debug builds only)
# =============================================================================
if(ENABLE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_library(powsys365_sanitizers INTERFACE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(powsys365_sanitizers INTERFACE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        )
        target_link_options(powsys365_sanitizers INTERFACE
            -fsanitize=address,undefined
        )
    endif()
endif()

# =============================================================================
# External Dependencies - FetchContent
# =============================================================================
include(FetchContent)

# --- nlohmann/json (JSON library) ---
FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  cmake
)

# --- Catch2 (testing framework) ---
if(BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.7.0
        GIT_SHALLOW    TRUE
    )
endif()

# --- pybind11 (Python bindings) ---
if(BUILD_PYTHON)
    FetchContent_Declare(pybind11
        GIT_REPOSITORY https://github.com/pybind/pybind11.git
        GIT_TAG        v2.13.0
        GIT_SHALLOW    TRUE
    )
endif()

# --- tl::expected ---
FetchContent_Declare(expected
    GIT_REPOSITORY https://github.com/TartanLlama/expected.git
    GIT_TAG        v1.1.0
    GIT_SHALLOW    TRUE
)

# --- Eigen3 (header-only linear algebra) ---
find_package(Eigen3 3.4 QUIET)
if(NOT Eigen3_FOUND)
    FetchContent_Declare(eigen
        GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
        GIT_TAG        3.4.0
        GIT_SHALLOW    TRUE
    )
    set(EIGEN_BUILD_DOC OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(EIGEN_BUILD_PKGCONFIG OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(eigen)
else()
    add_library(Eigen3::Eigen ALIAS Eigen3::Eigen)
endif()

# Make FetchContent available
FetchContent_MakeAvailable(json expected)
if(BUILD_TESTS)
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()
if(BUILD_PYTHON)
    FetchContent_MakeAvailable(pybind11)
endif()

# =============================================================================
# System Dependencies (find_package)
# =============================================================================

# --- OpenMP (optional parallelization) ---
if(ENABLE_OPENMP)
    find_package(OpenMP COMPONENTS CXX)
endif()

# --- PostgreSQL (libpq) ---
find_package(PostgreSQL QUIET)

# --- Qt6 (for GUI) ---
if(BUILD_UI)
    find_package(Qt6 6.5 COMPONENTS
        Core
        Gui
        Widgets
        Quick
        QuickControls2
        Charts
        Network
        SQL
        WebEngineQuick
        QUIET
    )
    if(NOT Qt6_FOUND)
        message(WARNING "Qt6 not found. Disabling BUILD_UI.")
        set(BUILD_UI OFF)
    else()
        set(QT6_FOUND TRUE)
        qt6_standard_project_setup()
    endif()
endif()

# =============================================================================
# third_party/ Directory Setup
# =============================================================================
add_subdirectory(third_party)

# =============================================================================
# Subdirectory Build Order (dependency-aware)
# =============================================================================
# 1. core/     - Base library (no internal deps)
# 2. simulation/ - Depends on core
# 3. scada/    - Depends on core
# 4. results/  - Depends on core
# 5. ai/       - Depends on core
# 6. ide/      - Depends on core, ui components
# 7. python/   - Depends on core
# 8. ui/       - Depends on core, simulation, scada, results, ai, ide
# 9. tests/    - Depends on all libraries

add_subdirectory(core)

if(BUILD_SIMULATION)
    add_subdirectory(simulation)
endif()

if(BUILD_SCADA)
    add_subdirectory(scada)
endif()

add_subdirectory(results)

if(BUILD_AI)
    add_subdirectory(ai)
endif()

if(BUILD_IDE)
    add_subdirectory(ide)
endif()

if(BUILD_PYTHON)
    add_subdirectory(python)
endif()

if(BUILD_UI AND QT6_FOUND)
    add_subdirectory(ui)
endif()

if(BUILD_MODELS)
    add_subdirectory(models)
endif()

if(BUILD_HARMONICS)
    add_subdirectory(harmonics)
endif()

if(BUILD_MARKETS)
    add_subdirectory(markets)
endif()

if(BUILD_RELIABILITY)
    add_subdirectory(reliability)
endif()

if(BUILD_LICENSING)
    add_subdirectory(licensing)
endif()

if(BUILD_INTEGRATION)
    add_subdirectory(integration)
endif()

if(BUILD_ICON_ENGINE)
    add_subdirectory(icon_engine)
endif()

if(BUILD_I18N)
    add_subdirectory(i18n)
endif()

if(BUILD_IO)
    add_subdirectory(io)
endif()

if(BUILD_GIS)
    add_subdirectory(gis)
endif()

if(BUILD_XTALK)
    add_subdirectory(xtalk)
endif()

if(BUILD_LEGAL)
    add_subdirectory(legal)
endif()

if(BUILD_AUDIO)
    add_subdirectory(audio)
endif()

if(BUILD_HELP)
    add_subdirectory(help)
endif()

if(BUILD_LINE_DESIGN)
    add_subdirectory(line_design)
endif()

if(BUILD_CONFIG)
    add_subdirectory(config)
endif()

if(BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()

# =============================================================================
# Installation Configuration
# =============================================================================
include(GNUInstallDirs)

# Export targets for find_package support
install(EXPORT POWSYS365Targets
    FILE POWSYS365Targets.cmake
    NAMESPACE POWSYS365::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/POWSYS365
)

# Generate and install config file
include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/POWSYS365Config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/POWSYS365Config.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/POWSYS365
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/POWSYS365ConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/POWSYS365Config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/POWSYS365ConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/POWSYS365
)

# =============================================================================
# CPack Packaging
# =============================================================================
if(BUILD_PACKAGING)
    set(CPACK_PACKAGE_NAME "POWSYS365")
    set(CPACK_PACKAGE_VENDOR "POWSYS365 Team")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Power System Analysis Platform")
    set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
    set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
    set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "POWSYS365")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
    set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

    if(APPLE)
        set(CPACK_GENERATOR "DragNDrop;ZIP")
        set(CPACK_SYSTEM_NAME "macOS-Universal")
        set(CPACK_DMG_VOLUME_NAME "POWSYS365-${PROJECT_VERSION}")
        set(CPACK_DMG_FORMAT "UDZO")
    elseif(WIN32)
        set(CPACK_GENERATOR "NSIS;ZIP")
        set(CPACK_NSIS_DISPLAY_NAME "POWSYS365")
        set(CPACK_NSIS_HELP_LINK "https://github.com/powsybl/powsys365")
        set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/powsybl/powsys365")
        set(CPACK_NSIS_MODIFY_PATH ON)
    else()
        set(CPACK_GENERATOR "DEB;RPM;TGZ")
        set(CPACK_DEBIAN_PACKAGE_DEPENDS "libpq5, libqt6widgets6")
        set(CPACK_DEBIAN_PACKAGE_SECTION "science")
        set(CPACK_RPM_PACKAGE_REQUIRES "libpq >= 13, qt6-qtbase >= 6.5")
    endif()

    include(CPack)
endif()

# =============================================================================
# Summary
# =============================================================================
message(STATUS "")
message(STATUS "╔═══════════════════════════════════════════════════════════════╗")
message(STATUS "║              POWSYS365 Build Configuration                    ║")
message(STATUS "╠═══════════════════════════════════════════════════════════════╣")
message(STATUS "║ Version:        ${PROJECT_VERSION}                                    ║")
message(STATUS "║ Build Type:     ${CMAKE_BUILD_TYPE}                                  ║")
message(STATUS "║ C++ Standard:   ${CMAKE_CXX_STANDARD}                                 ║")
message(STATUS "║ Platform:       ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR}              ║")
if(APPLE)
message(STATUS "║ macOS Architectures: ${CMAKE_OSX_ARCHITECTURES}                    ║")
message(STATUS "║ macOS Deployment:    ${CMAKE_OSX_DEPLOYMENT_TARGET}                         ║")
endif()
message(STATUS "╠═══════════════════════════════════════════════════════════════╣")
message(STATUS "║ BUILD_UI:          ${BUILD_UI}          ║")
message(STATUS "║ BUILD_PYTHON:      ${BUILD_PYTHON}      ║")
message(STATUS "║ BUILD_TESTS:       ${BUILD_TESTS}       ║")
message(STATUS "║ BUILD_SCADA:       ${BUILD_SCADA}       ║")
message(STATUS "║ BUILD_SIMULATION:  ${BUILD_SIMULATION}  ║")
message(STATUS "║ BUILD_IDE:         ${BUILD_IDE}         ║")
message(STATUS "║ BUILD_AI:          ${BUILD_AI}          ║")
message(STATUS "║ BUILD_MODELS:      ${BUILD_MODELS}      ║")
message(STATUS "║ BUILD_HARMONICS:   ${BUILD_HARMONICS}   ║")
message(STATUS "║ BUILD_MARKETS:     ${BUILD_MARKETS}     ║")
message(STATUS "║ BUILD_RELIABILITY: ${BUILD_RELIABILITY} ║")
message(STATUS "║ BUILD_LICENSING:   ${BUILD_LICENSING}   ║")
message(STATUS "║ BUILD_INTEGRATION: ${BUILD_INTEGRATION} ║")
message(STATUS "║ BUILD_ICON_ENGINE: ${BUILD_ICON_ENGINE} ║")
message(STATUS "║ BUILD_I18N:        ${BUILD_I18N}        ║")
message(STATUS "╠═══════════════════════════════════════════════════════════════╣")
message(STATUS "║ ENABLE_OPENMP:      ${ENABLE_OPENMP}      ║")
message(STATUS "║ ENABLE_SANITIZERS:  ${ENABLE_SANITIZERS}  ║")
message(STATUS "║ BUILD_PACKAGING:    ${BUILD_PACKAGING}    ║")
message(STATUS "╠═══════════════════════════════════════════════════════════════╣")
message(STATUS "║ Eigen3:            ${Eigen3_FOUND}                             ║")
message(STATUS "║ PostgreSQL:        ${PostgreSQL_FOUND}                         ║")
message(STATUS "║ OpenMP:            ${OpenMP_CXX_FOUND}                         ║")
message(STATUS "║ Qt6:               ${Qt6_FOUND}                                ║")
message(STATUS "╚═══════════════════════════════════════════════════════════════╝")
message(STATUS "")
