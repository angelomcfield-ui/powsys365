cmake_minimum_required(VERSION 3.20)

set(ICON_ENGINE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/IconProvider.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/IconRenderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ThemeManager.cpp
)

file(GLOB ICON_ENGINE_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/*.h")

add_library(powsy365_icon_engine STATIC ${ICON_ENGINE_SOURCES} ${ICON_ENGINE_HEADERS})
target_include_directories(powsy365_icon_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(powsy365_icon_engine PUBLIC powsys365_core)
target_link_libraries(powsy365_icon_engine PRIVATE powsys365_warnings)
