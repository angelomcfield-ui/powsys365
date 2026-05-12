cmake_minimum_required(VERSION 3.25)

add_library(powsys365_scada
    src/protocol_handler.cpp
    src/hmi_controller.cpp
    src/data_acquisition.cpp
)

target_link_libraries(powsys365_scada
    PRIVATE powsys365_core
)

target_include_directories(powsys365_scada PUBLIC include)