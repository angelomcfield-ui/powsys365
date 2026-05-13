cmake_minimum_required(VERSION 3.14)

# =============================================================================
# POWSYS365 :: IO Module – 85+ Format Import/Export Engine
# =============================================================================
project(powsys365_io VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Source files -----------------------------------------------------------
set(IO_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/import_types.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/base_importer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/base_exporter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/format_registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/import_export_engine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/post_import_validator.cpp
    # Parsers
    ${CMAKE_CURRENT_SOURCE_DIR}/parsers/psse_raw_parser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/parsers/cim_parser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/parsers/geo_parser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/parsers/tabular_parser.cpp
    # Exporters
    ${CMAKE_CURRENT_SOURCE_DIR}/exporters/psse_raw_exporter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/exporters/cim_exporter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/exporters/geo_exporter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/exporters/tabular_exporter.cpp
)

set(IO_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/import_types.h
    ${CMAKE_CURRENT_SOURCE_DIR}/base_importer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/base_exporter.h
    ${CMAKE_CURRENT_SOURCE_DIR}/format_registry.h
    ${CMAKE_CURRENT_SOURCE_DIR}/import_export_engine.h
    ${CMAKE_CURRENT_SOURCE_DIR}/post_import_validator.h
    # Parsers
    ${CMAKE_CURRENT_SOURCE_DIR}/parsers/psse_raw_parser.h
    ${CMAKE_CURRENT_SOURCE_DIR}/parsers/cim_parser.h
    ${CMAKE_CURRENT_SOURCE_DIR}/parsers/geo_parser.h
    ${CMAKE_CURRENT_SOURCE_DIR}/parsers/tabular_parser.h
    # Exporters
    ${CMAKE_CURRENT_SOURCE_DIR}/exporters/psse_raw_exporter.h
    ${CMAKE_CURRENT_SOURCE_DIR}/exporters/cim_exporter.h
    ${CMAKE_CURRENT_SOURCE_DIR}/exporters/geo_exporter.h
    ${CMAKE_CURRENT_SOURCE_DIR}/exporters/tabular_exporter.h
)

# ---- Library target ---------------------------------------------------------
add_library(powsys365_io STATIC ${IO_SOURCES} ${IO_HEADERS})

# Note: base_importer.cpp, base_exporter.cpp, and import_types.cpp are header-only
# convenience stubs. The real implementations are in the headers. We include them
# for build system consistency.

# If header-only, create empty .cpp files or comment out the above entries
# For this build, we keep the header-only design with minimal .cpp files

# Re-define sources excluding header-only .cpp entries
set(IO_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/format_registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/import_export_engine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/post_import_validator.cpp
    # Parsers
    ${CMAKE_CURRENT_SOURCE_DIR}/parsers/psse_raw_parser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/parsers/cim_parser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/parsers/geo_parser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/parsers/tabular_parser.cpp
    # Exporters
    ${CMAKE_CURRENT_SOURCE_DIR}/exporters/psse_raw_exporter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/exporters/cim_exporter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/exporters/geo_exporter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/exporters/tabular_exporter.cpp
)

# Create stub .cpp files for header-only modules so the build doesn't break
file(WRITE ${CMAKE_CURRENT_SOURCE_DIR}/base_importer.cpp
     "// Stub: base_importer.h is header-only with inline implementations\n")
file(WRITE ${CMAKE_CURRENT_SOURCE_DIR}/base_exporter.cpp
     "// Stub: base_exporter.h is header-only with inline implementations\n")
file(WRITE ${CMAKE_CURRENT_SOURCE_DIR}/import_types.cpp
     "// Stub: import_types.h is header-only with inline implementations\n")

set(IO_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/import_types.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/base_importer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/base_exporter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/format_registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/import_export_engine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/post_import_validator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/parsers/psse_raw_parser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/parsers/cim_parser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/parsers/geo_parser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/parsers/tabular_parser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/exporters/psse_raw_exporter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/exporters/cim_exporter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/exporters/geo_exporter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/exporters/tabular_exporter.cpp
)

add_library(powsys365_io STATIC ${IO_SOURCES} ${IO_HEADERS})

target_include_directories(powsys365_io
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/parsers>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/exporters>
        $<INSTALL_INTERFACE:include>
)

# Compiler options
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(powsys365_io PRIVATE
        -Wall -Wextra -Wpedantic
        -Wshadow -Wconversion -Wsign-conversion
    )
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    target_compile_options(powsys365_io PRIVATE
        /W4 /permissive-
    )
endif()

# Threading support
target_link_libraries(powsys365_io PUBLIC Threads::Threads)
find_package(Threads REQUIRED)

# ---- Tests ------------------------------------------------------------------
option(POWSYS365_BUILD_TESTS "Build IO module tests" ON)
if(POWSYS365_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---- Installation -----------------------------------------------------------
install(TARGETS powsys365_io
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)
install(FILES ${IO_HEADERS}
    DESTINATION include/powsys365/io
)

# ---- Summary ----------------------------------------------------------------
message(STATUS "POWSYS365 IO Module configured:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Sources: ${IO_SOURCES}")
message(STATUS "  Tests: ${POWSYS365_BUILD_TESTS}")
