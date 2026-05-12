# =============================================================================
# results/CMakeLists.txt - Results & Reporting Library
# =============================================================================
# Library: powsys365_results
# Provides: Export to PDF, Excel (xlsx), HTML dashboards, CSV, JSON reports.
#           Report templates, chart generation, and data formatting utilities.
# =============================================================================

# ---------------------------------------------------------------------------
# External dependencies
# ---------------------------------------------------------------------------

# --- libharu / HPDF (PDF generation) ---
find_package(libharu QUIET)
if(NOT libharu_FOUND)
    include(FetchContent)
    FetchContent_Declare(libharu
        GIT_REPOSITORY https://github.com/libharu/libharu.git
        GIT_TAG        v2.4.4
        GIT_SHALLOW    TRUE
    )
    set(BUILD_SHARED_LIBS_SAVED ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(LIBHPDF_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(libharu)
    set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_SAVED} CACHE BOOL "" FORCE)
    if(NOT TARGET hpdf AND TARGET libharu)
        add_library(hpdf ALIAS libharu)
    endif()
endif()

# --- libxlsxwriter (Excel .xlsx generation) ---
find_package(LibXlsxWriter QUIET)
if(NOT LibXlsxWriter_FOUND)
    include(FetchContent)
    FetchContent_Declare(libxlsxwriter
        GIT_REPOSITORY https://github.com/jmcnamara/libxlsxwriter.git
        GIT_TAG        RELEASE_1.1.6
        GIT_SHALLOW    TRUE
    )
    set(BUILD_SHARED_LIBS_SAVED ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(libxlsxwriter)
    set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_SAVED} CACHE BOOL "" FORCE)
    if(NOT TARGET xlsxwriter AND TARGET libxlsxwriter)
        add_library(xlsxwriter ALIAS libxlsxwriter)
    endif()
endif()

# ---------------------------------------------------------------------------
# Source files
# ---------------------------------------------------------------------------
file(GLOB_RECURSE RESULTS_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

file(GLOB_RECURSE RESULTS_HEADERS
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/powsy365/results/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/powsy365/results/*.hpp"
)

# ---------------------------------------------------------------------------
# Library target
# ---------------------------------------------------------------------------
add_library(powsys365_results STATIC)

target_sources(powsys365_results
    PRIVATE
        ${RESULTS_SOURCES}
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include"
        FILES ${RESULTS_HEADERS}
)

target_include_directories(powsys365_results
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(powsys365_results
    PUBLIC
        powsys365_core
        powsys365_warnings
        nlohmann_json::nlohmann_json
)

# --- PDF (libharu) ---
if(TARGET hpdf)
    target_link_libraries(powsys365_results PUBLIC hpdf)
    target_compile_definitions(powsys365_results PUBLIC POWSYS365_HAS_PDF=1)
elseif(TARGET libharu)
    target_link_libraries(powsys365_results PUBLIC libharu)
    target_compile_definitions(powsys365_results PUBLIC POWSYS365_HAS_PDF=1)
else()
    message(WARNING "libharu not found. PDF export will be disabled.")
    target_compile_definitions(powsys365_results PUBLIC POWSYS365_HAS_PDF=0)
endif()

# --- Excel (libxlsxwriter) ---
if(TARGET xlsxwriter)
    target_link_libraries(powsys365_results PUBLIC xlsxwriter)
    target_compile_definitions(powsys365_results PUBLIC POWSYS365_HAS_EXCEL=1)
elseif(TARGET libxlsxwriter)
    target_link_libraries(powsys365_results PUBLIC libxlsxwriter)
    target_compile_definitions(powsys365_results PUBLIC POWSYS365_HAS_EXCEL=1)
else()
    message(WARNING "libxlsxwriter not found. Excel export will be disabled.")
    target_compile_definitions(powsys365_results PUBLIC POWSYS365_HAS_EXCEL=0)
endif()

# --- Platform-specific: zlib required by libharu and libxlsxwriter ---
find_package(ZLIB QUIET)
if(ZLIB_FOUND)
    target_link_libraries(powsys365_results PUBLIC ZLIB::ZLIB)
endif()

# ---------------------------------------------------------------------------
# Installation
# ---------------------------------------------------------------------------
install(TARGETS powsys365_results
    EXPORT POWSYS365Targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    FILE_SET HEADERS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/powsys365/results
)

# Install HTML report templates
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/templates/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/powsys365/templates
    FILES_MATCHING PATTERN "*.html" PATTERN "*.css" PATTERN "*.js"
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(BUILD_TESTS AND TARGET Catch2::Catch2WithMain)
    file(GLOB RESULTS_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
    if(RESULTS_TEST_SOURCES)
        add_executable(powsys365_results_tests ${RESULTS_TEST_SOURCES})
        target_link_libraries(powsys365_results_tests
            PRIVATE
                powsys365_results
                powsys365_core
                Catch2::Catch2WithMain
        )
        catch_discover_tests(powsys365_results_tests)
    endif()
endif()
