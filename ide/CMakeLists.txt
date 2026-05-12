# =============================================================================
# ide/CMakeLists.txt - Embedded IDE Components
# =============================================================================
# Library: powsys365_ide
# Provides: Monaco Editor bridge (via Qt WebEngine), embedded terminal widget,
#           script editor with syntax highlighting, project explorer,
#           and debugging integration for power system scripts.
# =============================================================================

if(NOT BUILD_IDE)
    message(STATUS "Embedded IDE components disabled (BUILD_IDE=OFF).")
    return()
endif()

if(NOT BUILD_UI OR NOT QT6_FOUND)
    message(WARNING "IDE module requires Qt6 and BUILD_UI=ON. Disabling IDE module.")
    set(BUILD_IDE OFF)
    return()
endif()

# ---------------------------------------------------------------------------
# Source files
# ---------------------------------------------------------------------------
file(GLOB_RECURSE IDE_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

file(GLOB_RECURSE IDE_HEADERS
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/powsy365/ide/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/powsy365/ide/*.hpp"
)

# ---------------------------------------------------------------------------
# Library target
# ---------------------------------------------------------------------------
add_library(powsys365_ide STATIC)

target_sources(powsys365_ide
    PRIVATE
        ${IDE_SOURCES}
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include"
        FILES ${IDE_HEADERS}
)

target_include_directories(powsys365_ide
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ---------------------------------------------------------------------------
# Link dependencies
# ---------------------------------------------------------------------------
target_link_libraries(powsys365_ide
    PUBLIC
        powsys365_core
        powsys365_warnings
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::WebEngineQuick
        Qt6::Network
)

# ---------------------------------------------------------------------------
# QML files for IDE components
# ---------------------------------------------------------------------------
file(GLOB IDE_QML_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/qml/*.qml")
if(IDE_QML_FILES)
    qt6_add_qml_module(powsys365_ide
        URI POWSYS365.IDE
        VERSION 1.0
        QML_FILES ${IDE_QML_FILES}
        RESOURCE_PREFIX "/qt/qml"
        OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/POWSYS365/IDE
    )
endif()

# ---------------------------------------------------------------------------
# Monaco Editor static files
# ---------------------------------------------------------------------------
set(MONACO_EDITOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/monaco-editor")
if(EXISTS ${MONACO_EDITOR_DIR})
    set(IDE_RESOURCE_FILES "")
    file(GLOB_RECURSE MONACO_FILES CONFIGURE_DEPENDS "${MONACO_EDITOR_DIR}/*")
    foreach(_file ${MONACO_FILES})
        file(RELATIVE_PATH _rel_path "${MONACO_EDITOR_DIR}" "${_file}")
        set_source_files_properties("${_file}" PROPERTIES QT_RESOURCE_ALIAS "${_rel_path}")
        list(APPEND IDE_RESOURCE_FILES "${_file}")
    endforeach()
    
    if(IDE_RESOURCE_FILES)
        qt6_add_resources(powsys365_ide "ide_monaco_resources"
            PREFIX "/monaco-editor"
            FILES ${IDE_RESOURCE_FILES}
        )
    endif()
endif()

# ---------------------------------------------------------------------------
# Installation
# ---------------------------------------------------------------------------
install(TARGETS powsys365_ide
    EXPORT POWSYS365Targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    FILE_SET HEADERS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/powsys365/ide
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(BUILD_TESTS AND TARGET Catch2::Catch2WithMain)
    file(GLOB IDE_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
    if(IDE_TEST_SOURCES)
        add_executable(powsys365_ide_tests ${IDE_TEST_SOURCES})
        target_link_libraries(powsys365_ide_tests
            PRIVATE
                powsys365_ide
                powsys365_core
                Catch2::Catch2WithMain
        )
        catch_discover_tests(powsys365_ide_tests)
    endif()
endif()
