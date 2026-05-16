cmake_minimum_required(VERSION 3.20)

set(RELIABILITY_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/reliability_analyzer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/monte_carlo_reliability.cpp
)

file(GLOB RELIABILITY_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/*.h")

add_library(powsy365_reliability STATIC ${RELIABILITY_SOURCES} ${RELIABILITY_HEADERS})
target_include_directories(powsy365_reliability PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(powsy365_reliability PUBLIC powsys365_core Eigen3::Eigen)
target_link_libraries(powsy365_reliability PRIVATE powsys365_warnings)
