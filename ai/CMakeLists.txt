cmake_minimum_required(VERSION 3.25)

add_library(powsys365_ai
    src/ai_gateway.cpp
    src/rag_system.cpp
    src/predictive_analytics.cpp
)

target_link_libraries(powsys365_ai
    PRIVATE powsys365_core
)

target_include_directories(powsys365_ai PUBLIC include)