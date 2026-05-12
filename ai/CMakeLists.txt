# =============================================================================
# ai/CMakeLists.txt - AI/LLM Integration Module
# =============================================================================
# Library: powsys365_ai
# Provides: Python integration for LLM-based power system analysis,
#           natural language query processing, anomaly detection,
#           predictive maintenance models, and training data pipelines.
# =============================================================================

if(NOT BUILD_AI)
    message(STATUS "AI/LLM integration module disabled (BUILD_AI=OFF).")
    return()
endif()

# ---------------------------------------------------------------------------
# Check Python availability for LLM integration
# ---------------------------------------------------------------------------
find_package(Python3 COMPONENTS Interpreter Development QUIET)
if(NOT Python3_FOUND)
    message(WARNING "Python3 not found. AI module will be built without Python embedding.")
endif()

# ---------------------------------------------------------------------------
# Source files
# ---------------------------------------------------------------------------
file(GLOB_RECURSE AI_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

file(GLOB_RECURSE AI_HEADERS
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/powsy365/ai/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/powsy365/ai/*.hpp"
)

# ---------------------------------------------------------------------------
# Library target
# ---------------------------------------------------------------------------
add_library(powsys365_ai STATIC)

target_sources(powsys365_ai
    PRIVATE
        ${AI_SOURCES}
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include"
        FILES ${AI_HEADERS}
)

target_include_directories(powsys365_ai
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ---------------------------------------------------------------------------
# Link dependencies
# ---------------------------------------------------------------------------
target_link_libraries(powsys365_ai
    PUBLIC
        powsys365_core
        powsys365_warnings
        nlohmann_json::nlohmann_json
)

# --- Python embedding for LLM integration ---
if(Python3_FOUND)
    target_link_libraries(powsys365_ai PUBLIC Python3::Python)
    target_include_directories(powsys365_ai PUBLIC ${Python3_INCLUDE_DIRS})
    target_compile_definitions(powsys365_ai PUBLIC POWSYS365_HAS_PYTHON_EMBED=1)
    
    # On Linux, Python often needs explicit linking to dl and util
    if(UNIX AND NOT APPLE)
        target_link_libraries(powsys365_ai PUBLIC ${CMAKE_DL_LIBS} util)
    endif()
else()
    target_compile_definitions(powsys365_ai PUBLIC POWSYS365_HAS_PYTHON_EMBED=0)
endif()

# --- pybind11 for Python/C++ interop in AI module ---
if(BUILD_PYTHON AND TARGET pybind11::embed)
    target_link_libraries(powsys365_ai PUBLIC pybind11::embed)
    target_compile_definitions(powsys365_ai PUBLIC POWSYS365_HAS_PYBIND11=1)
else()
    target_compile_definitions(powsys365_ai PUBLIC POWSYS365_HAS_PYBIND11=0)
endif()

# ---------------------------------------------------------------------------
# LLM Python scripts installation
# ---------------------------------------------------------------------------
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/llm/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/powsys365/ai/llm
    FILES_MATCHING PATTERN "*.py" PATTERN "*.json" PATTERN "*.yaml" PATTERN "*.yml"
)

# Install model configuration files
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/models/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/powsys365/ai/models
    FILES_MATCHING PATTERN "*.json" PATTERN "*.yaml" PATTERN "*.yml"
)

# ---------------------------------------------------------------------------
# Installation
# ---------------------------------------------------------------------------
install(TARGETS powsys365_ai
    EXPORT POWSYS365Targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    FILE_SET HEADERS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/powsys365/ai
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(BUILD_TESTS AND TARGET Catch2::Catch2WithMain)
    file(GLOB AI_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
    if(AI_TEST_SOURCES)
        add_executable(powsys365_ai_tests ${AI_TEST_SOURCES})
        target_link_libraries(powsys365_ai_tests
            PRIVATE
                powsys365_ai
                powsys365_core
                Catch2::Catch2WithMain
        )
        catch_discover_tests(powsys365_ai_tests)
    endif()
endif()
