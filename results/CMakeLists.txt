cmake_minimum_required(VERSION 3.25)

add_library(powsys365_results
    src/report_generator.cpp
    src/dashboard.cpp
    src/data_visualizer.cpp
)

target_link_libraries(powsys365_results
    PRIVATE powsys365_core
)

target_include_directories(powsys365_results PUBLIC include)