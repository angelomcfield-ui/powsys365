# ============================================================================
# CMakeLists.txt - Modulo de Mercados Electricos para POWSYS365
# ============================================================================
# Compila los siguientes modulos:
#   - Calculador de LMP (LMPricingCalculator)
#   - Calculador de ATC/TTC (ATCCalculator)
#   - Calculador de settlements (SettlementCalculator)
# ============================================================================

cmake_minimum_required(VERSION 3.16)

# --- Configuracion del modulo ---
set(POWSYS365_MARKETS_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/lmp_calculator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/atc_calculator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/settlement_calculator.cpp
)

set(POWSYS365_MARKETS_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/lmp_calculator.h
    ${CMAKE_CURRENT_SOURCE_DIR}/atc_calculator.h
    ${CMAKE_CURRENT_SOURCE_DIR}/settlement_calculator.h
)

# --- Dependencias ---
find_package(Eigen3 3.3 REQUIRED NO_MODULE)

# --- Libreria del modulo ---
add_library(powsys365_markets STATIC
    ${POWSYS365_MARKETS_SOURCES}
    ${POWSYS365_MARKETS_HEADERS}
)

target_include_directories(powsys365_markets
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(powsys365_markets
    PUBLIC
        Eigen3::Eigen
)

target_compile_features(powsys365_markets PUBLIC cxx_std_17)

target_compile_options(powsys365_markets
    PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:
            -Wall -Wextra -Wpedantic -O3 -march=native
        >
        $<$<CXX_COMPILER_ID:MSVC>:
            /W4 /O2 /arch:AVX2
        >
)

# --- Opciones de compilacion condicionales ---
option(POWSYS365_MARKETS_DEBUG "Habilitar modo debug para modulos de mercados" OFF)

if(POWSYS365_MARKETS_DEBUG)
    target_compile_definitions(powsys365_markets PRIVATE MARKETS_DEBUG=1)
    target_compile_options(powsys365_markets PRIVATE -g -O0)
endif()

# --- Instalacion ---
install(TARGETS powsys365_markets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES ${POWSYS365_MARKETS_HEADERS}
    DESTINATION include/powsys365/markets
)
