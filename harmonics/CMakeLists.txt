# ============================================================================
# CMakeLists.txt - Modulo de Armonicos/Calidad de Potencia para POWSYS365
# ============================================================================
# Compila los siguientes modulos:
#   - Flujo de carga armonico (HarmonicLoadFlow)
#   - Frequency scan para resonancia (FrequencyScanner)
#   - Analizador de resonancia (ResonanceAnalyzer)
#   - Calculador de THD (THDCalculator)
#   - Analizador de flicker (FlickerAnalyzer)
#   - Disenador de filtros (FilterDesigner)
# ============================================================================

cmake_minimum_required(VERSION 3.16)

# --- Configuracion del modulo ---
set(POWSYS365_HARMONICS_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/harmonic_load_flow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/frequency_scan.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/resonance_analyzer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/thd_calculator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/flicker_analyzer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/filter_designer.cpp
)

set(POWSYS365_HARMONICS_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/harmonic_load_flow.h
    ${CMAKE_CURRENT_SOURCE_DIR}/frequency_scan.h
    ${CMAKE_CURRENT_SOURCE_DIR}/resonance_analyzer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/thd_calculator.h
    ${CMAKE_CURRENT_SOURCE_DIR}/flicker_analyzer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/filter_designer.h
)

# --- Dependencias ---
find_package(Eigen3 3.3 REQUIRED NO_MODULE)

# --- Libreria del modulo ---
add_library(powsys365_harmonics STATIC
    ${POWSYS365_HARMONICS_SOURCES}
    ${POWSYS365_HARMONICS_HEADERS}
)

target_include_directories(powsys365_harmonics
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(powsys365_harmonics
    PUBLIC
        Eigen3::Eigen
)

target_compile_features(powsys365_harmonics PUBLIC cxx_std_17)

target_compile_options(powsys365_harmonics
    PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:
            -Wall -Wextra -Wpedantic -O3 -march=native
        >
        $<$<CXX_COMPILER_ID:MSVC>:
            /W4 /O2 /arch:AVX2
        >
)

# --- Opciones de compilacion condicionales ---
option(POWSYS365_HARMONICS_DEBUG "Habilitar modo debug para modulos de armonicos" OFF)

if(POWSYS365_HARMONICS_DEBUG)
    target_compile_definitions(powsys365_harmonics PRIVATE HARMONICS_DEBUG=1)
    target_compile_options(powsys365_harmonics PRIVATE -g -O0)
endif()

# --- Instalacion ---
install(TARGETS powsys365_harmonics
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES ${POWSYS365_HARMONICS_HEADERS}
    DESTINATION include/powsys365/harmonics
)
