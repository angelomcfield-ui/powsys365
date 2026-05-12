cmake_minimum_required(VERSION 3.25)

add_library(powsys365_core
    commons/math_utils.cpp
    src/power_system.cpp
    src/load_flow.cpp
    # Agregar otros archivos fuente aqui
)

target_include_directories(powsys365_core PUBLIC
    include
    commons
)

target_link_libraries(powsys365_core
    # Agregar dependencias aqui
)