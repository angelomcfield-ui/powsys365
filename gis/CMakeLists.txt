# ============================================================================
# POWSYS365 - GIS Module CMakeLists.txt
# ============================================================================

cmake_minimum_required(VERSION 3.14)

project(POWSYS365_GIS VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================================
# Libreria GIS
# ============================================================================

add_library(powsys365_gis STATIC
    coordinate_converter.cpp
    coordinate_converter.h
    map_renderer.cpp
    map_renderer.h
    kml_exporter.cpp
    kml_exporter.h
    geojson_parser.cpp
    geojson_parser.h
)

target_include_directories(powsys365_gis
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

# ============================================================================
# Dependencias
# ============================================================================

# libcurl para descarga de tiles
find_package(CURL REQUIRED)
target_link_libraries(powsys365_gis PUBLIC CURL::libcurl)

# libpng para procesamiento de tiles PNG (opcional pero recomendado)
find_package(PNG)
if(PNG_FOUND)
    target_link_libraries(powsys365_gis PUBLIC PNG::PNG)
    target_compile_definitions(powsys365_gis PUBLIC POWSYS365_HAS_LIBPNG)
endif()

# Threads
find_package(Threads REQUIRED)
target_link_libraries(powsys365_gis PUBLIC Threads::Threads)

# ============================================================================
# Opciones de compilacion
# ============================================================================

if(MSVC)
    target_compile_options(powsys365_gis PRIVATE /W4 /permissive-)
else()
    target_compile_options(powsys365_gis PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow
        -fno-omit-frame-pointer
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(powsys365_gis PRIVATE -g -O0)
    else()
        target_compile_options(powsys365_gis PRIVATE -O3 -march=native)
    endif()
endif()

# ============================================================================
# Tests
# ============================================================================

option(POWSYS365_GIS_BUILD_TESTS "Build GIS tests" ON)
if(POWSYS365_GIS_BUILD_TESTS)
    enable_testing()
    add_executable(gis_test
        tests/test_coordinate_converter.cpp
        tests/test_kml_exporter.cpp
        tests/test_geojson_parser.cpp
    )
    target_link_libraries(gis_test PRIVATE powsys365_gis)

    add_test(NAME CoordinateConverterTest COMMAND gis_test --test converter)
    add_test(NAME KmlExporterTest COMMAND gis_test --test kml)
    add_test(NAME GeoJsonParserTest COMMAND gis_test --test geojson)
endif()

# ============================================================================
# Instalacion
# ============================================================================

install(TARGETS powsys365_gis
    EXPORT powsys365_gis-targets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES
    coordinate_converter.h
    map_renderer.h
    kml_exporter.h
    geojson_parser.h
    DESTINATION include/powsys365/gis
)
