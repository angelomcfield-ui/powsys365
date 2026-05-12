cmake_minimum_required(VERSION 3.25)

add_library(powsys365_simulation
    src/event_scheduler.cpp
    src/integrator.cpp
    src/simulation_engine.cpp
)

target_link_libraries(powsys365_simulation
    PRIVATE powsys365_core
)

target_include_directories(powsys365_simulation PUBLIC include)