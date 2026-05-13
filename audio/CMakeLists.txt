# Audio Engine CMakeLists.txt for POWSYS365
# OpenAL-based audio system with 3D positional audio support

cmake_minimum_required(VERSION 3.16)

project(powsys365_audio VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(Qt6 REQUIRED COMPONENTS Core)

# Find OpenAL
find_package(OpenAL QUIET)
if(NOT OpenAL_FOUND)
    # Try pkg-config fallback
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(OPENAL openal)
    endif()
    
    if(NOT OPENAL_FOUND)
        # Manual search for OpenAL
        find_path(OPENAL_INCLUDE_DIR 
            NAMES AL/al.h al.h
            PATHS 
                /usr/include
                /usr/local/include
                /opt/local/include
                ${OPENAL_ROOT}/include
            DOC "OpenAL include directory"
        )
        
        find_library(OPENAL_LIBRARY 
            NAMES openal OpenAL32
            PATHS
                /usr/lib
                /usr/local/lib
                /opt/local/lib
                ${OPENAL_ROOT}/lib
            DOC "OpenAL library"
        )
        
        if(OPENAL_INCLUDE_DIR AND OPENAL_LIBRARY)
            set(OpenAL_FOUND TRUE)
            set(OpenAL_INCLUDE_DIRS ${OPENAL_INCLUDE_DIR})
            set(OpenAL_LIBRARIES ${OPENAL_LIBRARY})
            message(STATUS "Found OpenAL manually:")
            message(STATUS "  Include: ${OPENAL_INCLUDE_DIR}")
            message(STATUS "  Library: ${OPENAL_LIBRARY}")
        else()
            message(FATAL_ERROR "OpenAL not found. Please install OpenAL or set OPENAL_ROOT.")
        endif()
    endif()
else()
    set(OPENAL_INCLUDE_DIRS ${OpenAL_INCLUDE_DIRS})
    set(OPENAL_LIBRARIES ${OpenAL_LIBRARIES})
endif()

# Source files
set(AUDIO_SOURCES
    audio_engine.cpp
    sound_library.cpp
)

set(AUDIO_HEADERS
    audio_engine.h
    sound_library.h
)

# Create library
add_library(powsys365_audio STATIC ${AUDIO_SOURCES} ${AUDIO_HEADERS})

# Include directories
target_include_directories(powsys365_audio
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${OPENAL_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(powsys365_audio
    PUBLIC
        Qt6::Core
    PRIVATE
        ${OPENAL_LIBRARIES}
)

# Compiler warnings
if(MSVC)
    target_compile_options(powsys365_audio PRIVATE /W4 /permissive-)
else()
    target_compile_options(powsys365_audio PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Install
install(TARGETS powsys365_audio
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES ${AUDIO_HEADERS}
    DESTINATION include/powsys365/audio
)

# Optional: Build tests
option(BUILD_AUDIO_TESTS "Build audio tests" OFF)
if(BUILD_AUDIO_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
