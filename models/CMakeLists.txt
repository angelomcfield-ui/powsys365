# ============================================================================
# CMakeLists.txt - Modulo models de POWSYS365
# ============================================================================
cmake_minimum_required(VERSION 3.16)

# ============================================================================
# Libreria models
# ============================================================================
set(MODELS_SOURCES
    wind_turbine.cpp
    solar_pv.cpp
    bess.cpp
    hvdc.cpp
    facts.cpp
    v2g.cpp
    electrolyzer.cpp
    induction_generator.cpp
)

set(MODELS_HEADERS
    wind_turbine.h
    solar_pv.h
    bess.h
    hvdc.h
    facts.h
    v2g.h
    electrolyzer.h
    induction_generator.h
)

# Libreria estatica
add_library(powsys365_models STATIC ${MODELS_SOURCES})

# Headers
target_include_directories(powsys365_models
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

# C++17
target_compile_features(powsys365_models PUBLIC cxx_std_17)

# Eigen3 (opcional para algebra lineal en modelos avanzados)
find_package(Eigen3 3.3 QUIET)
if(Eigen3_FOUND)
    target_link_libraries(powsys365_models PUBLIC Eigen3::Eigen)
    target_compile_definitions(powsys365_models PUBLIC POWSYS365_HAS_EIGEN)
    message(STATUS "POWSYS365 models: Eigen3 found and linked")
else()
    message(WARNING "POWSYS365 models: Eigen3 not found. Some advanced features may be limited.")
endif()

# OpenMP (opcional para paralelizacion)
find_package(OpenMP QUIET)
if(OpenMP_CXX_FOUND)
    target_link_libraries(powsys365_models PUBLIC OpenMP::OpenMP_CXX)
    target_compile_definitions(powsys365_models PUBLIC POWSYS365_HAS_OPENMP)
    message(STATUS "POWSYS365 models: OpenMP found and linked")
endif()

# ============================================================================
# Opciones de compilacion
# ============================================================================
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(powsys365_models PRIVATE
        -Wall -Wextra -Wpedantic
        -O3
        -march=native
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(powsys365_models PRIVATE
            -g -O0 -fno-omit-frame-pointer
        )
    endif()
elseif(MSVC)
    target_compile_options(powsys365_models PRIVATE
        /W4 /O2 /arch:AVX2
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(powsys365_models PRIVATE /Od /Zi)
    endif()
endif()

# ============================================================================
# Definiciones del proyecto
# ============================================================================
target_compile_definitions(powsys365_models PRIVATE
    POWSYS365_NAMESPACE=powsys365
)

# ============================================================================
# Instalacion
# ============================================================================
include(GNUInstallDirs)

install(TARGETS powsys365_models
    EXPORT powsys365-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES ${MODELS_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/powsys365
)

# ============================================================================
# Test de compilacion (opcional)
# ============================================================================
option(POWSYS365_BUILD_MODELS_TEST "Build models compilation test" OFF)
if(POWSYS365_BUILD_MODELS_TEST)
    add_executable(test_models_compile test_models_compile.cpp)
    target_link_libraries(test_models_compile PRIVATE powsys365_models)
    target_compile_features(test_models_compile PRIVATE cxx_std_17)
endif()
