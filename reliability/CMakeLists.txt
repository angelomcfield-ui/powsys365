cmake_minimum_required(VERSION 3.20)

RELIABILITY_SOURCES
set(reliability_sources)

add_library(powsy365_reliability INTERFACE)
target_include_directories(powsy365_reliability INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
