cmake_minimum_required(VERSION 3.25)
find_package(pybind11 REQUIRED)

pybind11_add_module(powsys365_core bindings.cpp)
target_link_libraries(powsys365_core PRIVATE powsys365_core)
target_include_directories(powsys365_core PRIVATE ../core/include ../core/commons)