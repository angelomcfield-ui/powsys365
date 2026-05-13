# ============================================================================
# POWSYS365 - xTalk Module CMakeLists.txt
# ============================================================================

cmake_minimum_required(VERSION 3.14)

project(POWSYS365_XTALK VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================================
# Libreria xTalk
# ============================================================================

add_library(powsys365_xtalk STATIC
    messaging_engine.cpp
    messaging_engine.h
    webrtc_manager.cpp
    webrtc_manager.h
    mqtt_client.cpp
    mqtt_client.h
    calendar_sync.cpp
    calendar_sync.h
)

target_include_directories(powsys365_xtalk
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

# ============================================================================
# Dependencias
# ============================================================================

# libcurl para HTTP requests (Slack, WhatsApp, Google Calendar, Outlook)
find_package(CURL REQUIRED)
target_link_libraries(powsys365_xtalk PUBLIC CURL::libcurl)

# Threads
find_package(Threads REQUIRED)
target_link_libraries(powsys365_xtalk PUBLIC Threads::Threads)

# ============================================================================
# Opciones de compilacion
# ============================================================================

if(MSVC)
    target_compile_options(powsys365_xtalk PRIVATE /W4 /permissive-)
else()
    target_compile_options(powsys365_xtalk PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow
        -fno-omit-frame-pointer
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(powsys365_xtalk PRIVATE -g -O0)
    else()
        target_compile_options(powsys365_xtalk PRIVATE -O3 -march=native)
    endif()
endif()

# ============================================================================
# Tests
# ============================================================================

option(POWSYS365_XTALK_BUILD_TESTS "Build xTalk tests" ON)
if(POWSYS365_XTALK_BUILD_TESTS)
    enable_testing()
    add_executable(xtalk_test
        tests/test_messaging_engine.cpp
        tests/test_webrtc_manager.cpp
        tests/test_mqtt_client.cpp
        tests/test_calendar_sync.cpp
    )
    target_link_libraries(xtalk_test PRIVATE powsys365_xtalk)

    add_test(NAME MessagingEngineTest COMMAND xtalk_test --test messaging)
    add_test(NAME WebRTCManagerTest COMMAND xtalk_test --test webrtc)
    add_test(NAME MqttClientTest COMMAND xtalk_test --test mqtt)
    add_test(NAME CalendarSyncTest COMMAND xtalk_test --test calendar)
endif()

# ============================================================================
# Instalacion
# ============================================================================

install(TARGETS powsys365_xtalk
    EXPORT powsys365_xtalk-targets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES
    messaging_engine.h
    webrtc_manager.h
    mqtt_client.h
    calendar_sync.h
    DESTINATION include/powsys365/xtalk
)
