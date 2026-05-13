# Help Engine CMakeLists.txt for POWSYS365
# Full-text search system with MeiliSearch + Lucene FTS5 integration

cmake_minimum_required(VERSION 3.16)

project(powsys365_help VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(Qt6 REQUIRED COMPONENTS Core Sql Network Widgets)

# Source files
set(HELP_SOURCES
    help_engine.cpp
    help_browser.cpp
    glossary_manager.cpp
)

set(HELP_HEADERS
    help_engine.h
    help_browser.h
    glossary_manager.h
)

# Create library
add_library(powsys365_help STATIC ${HELP_SOURCES} ${HELP_HEADERS})

# Include directories
target_include_directories(powsys365_help
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link libraries
target_link_libraries(powsys365_help
    PUBLIC
        Qt6::Core
        Qt6::Sql
        Qt6::Network
        Qt6::Widgets
)

# Compiler warnings
if(MSVC)
    target_compile_options(powsys365_help PRIVATE /W4 /permissive-)
else()
    target_compile_options(powsys365_help PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Install
install(TARGETS powsys365_help
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES ${HELP_HEADERS}
    DESTINATION include/powsys365/help
)

# Optional: Build tests
option(BUILD_HELP_TESTS "Build help tests" OFF)
if(BUILD_HELP_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
