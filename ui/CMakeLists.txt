cmake_minimum_required(VERSION 3.20)
project(POWSYS365 VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ── Find Qt6 ─────────────────────────────────────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Qml
    Quick
    QuickControls2
)

# ── QML Resource File ────────────────────────────────────────────────────
set(QML_RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/resources/qml.qrc)

# ── Source Files ─────────────────────────────────────────────────────────
set(SOURCES
    main.cpp
    cpp/main_window_controller.cpp
    cpp/sld_scene.cpp
    cpp/theme_manager.cpp
)

set(HEADERS
    cpp/main_window_controller.h
    cpp/sld_scene.h
    cpp/theme_manager.h
)

# ── Executable ───────────────────────────────────────────────────────────
add_executable(${PROJECT_NAME}
    ${SOURCES}
    ${HEADERS}
    ${QML_RESOURCES}
)

# ── Link Libraries ───────────────────────────────────────────────────────
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
)

# ── Include Directories ──────────────────────────────────────────────────
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ── macOS Specific ───────────────────────────────────────────────────────
if(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "dev.powsys365.app"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    )
endif()

# ── Windows Specific ─────────────────────────────────────────────────────
if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()

# ── Install ──────────────────────────────────────────────────────────────
install(TARGETS ${PROJECT_NAME}
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# ── Status ───────────────────────────────────────────────────────────────
message(STATUS "POWSYS365 build configured")
message(STATUS "  Qt version: ${Qt6Core_VERSION}")
message(STATUS "  Sources: ${SOURCES}")
message(STATUS "  Headers: ${HEADERS}")
