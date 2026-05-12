cmake_minimum_required(VERSION 3.25)

find_package(GTest REQUIRED)

add_executable(powsys365_tests
    test_power_system.cpp
    test_load_flow.cpp
    test_simulation.cpp
)

target_link_libraries(powsys365_tests
    PRIVATE GTest::gtest_main powsys365_core powsys365_simulation
)

include(GoogleTest)
gtest_discover_tests(powsys365_tests)