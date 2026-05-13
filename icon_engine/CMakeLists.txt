cmake_minimum_required(VERSION 3.20)

ICON_ENGINE_SOURCES
set(icon_engine_sources)

add_library(powsy365_icon_engine INTERFACE)
target_include_directories(powsy365_icon_engine INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
