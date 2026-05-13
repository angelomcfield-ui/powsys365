# ============================================================
# POWSYS365 – Configuration System Module
# Multi-panel settings with JSON persistence, Qt6-compatible.
# ============================================================

cmake_minimum_required(VERSION 3.14)

project(powsys365_config VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Source files
set(CONFIG_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/config_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/panels/ai_config.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/panels/ui_config.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/panels/payment_config.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/panels/ide_config.cpp
)

set(CONFIG_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/config_manager.h
    ${CMAKE_CURRENT_SOURCE_DIR}/panels/ai_config.h
    ${CMAKE_CURRENT_SOURCE_DIR}/panels/ui_config.h
    ${CMAKE_CURRENT_SOURCE_DIR}/panels/payment_config.h
    ${CMAKE_CURRENT_SOURCE_DIR}/panels/ide_config.h
)

# Static library
add_library(powsys365_config STATIC
    ${CONFIG_SOURCES}
    ${CONFIG_HEADERS}
)

target_include_directories(powsys365_config
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include/config>
)

# Compiler flags
target_compile_options(powsys365_config PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
        -Wnon-virtual-dtor -Wold-style-cast
    >
    $<$<CXX_COMPILER_ID:MSVC>:
        /W4 /permissive- /Zc:__cplusplus
    >
)

# Qt6 integration (optional)
option(CONFIG_USE_QT6 "Enable Qt6 integration" OFF)
if(CONFIG_USE_QT6)
    find_package(Qt6 COMPONENTS Core REQUIRED)
    target_link_libraries(powsys365_config PUBLIC Qt6::Core)
    target_compile_definitions(powsys365_config PRIVATE POWSYS365_QT6=1)
endif()

# Optional: build tests
option(CONFIG_BUILD_TESTS "Build config module unit tests" OFF)
if(CONFIG_BUILD_TESTS)
    enable_testing()
    add_executable(test_config
        tests/test_config_manager.cpp
        tests/test_ai_config.cpp
        tests/test_ui_config.cpp
        tests/test_payment_config.cpp
        tests/test_ide_config.cpp
    )
    target_link_libraries(test_config PRIVATE powsys365_config)
    add_test(NAME ConfigTests COMMAND test_config)
endif()

# Install
install(TARGETS powsys365_config
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)
install(FILES ${CONFIG_HEADERS}
    DESTINATION include/config
)
install(FILES ${CONFIG_HEADERS}
    DESTINATION include/config/panels
)
