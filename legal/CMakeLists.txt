# ============================================================================
# POWSYS365 - Legal/GDPR Module CMakeLists.txt
# ============================================================================

cmake_minimum_required(VERSION 3.14)

project(POWSYS365_LEGAL VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================================
# Libreria Legal
# ============================================================================

add_library(powsys365_legal STATIC
    gdpr_engine.cpp
    gdpr_engine.h
    cookie_manager.cpp
    cookie_manager.h
    terms_manager.cpp
    terms_manager.h
)

target_include_directories(powsys365_legal
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

# ============================================================================
# Dependencias
# ============================================================================

# Threads
find_package(Threads REQUIRED)
target_link_libraries(powsys365_legal PUBLIC Threads::Threads)

# ============================================================================
# Opciones de compilacion
# ============================================================================

if(MSVC)
    target_compile_options(powsys365_legal PRIVATE /W4 /permissive-)
else()
    target_compile_options(powsys365_legal PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow
        -fno-omit-frame-pointer
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(powsys365_legal PRIVATE -g -O0)
    else()
        target_compile_options(powsys365_legal PRIVATE -O3 -march=native)
    endif()
endif()

# ============================================================================
# Tests
# ============================================================================

option(POWSYS365_LEGAL_BUILD_TESTS "Build Legal tests" ON)
if(POWSYS365_LEGAL_BUILD_TESTS)
    enable_testing()
    add_executable(legal_test
        tests/test_gdpr_engine.cpp
        tests/test_cookie_manager.cpp
        tests/test_terms_manager.cpp
    )
    target_link_libraries(legal_test PRIVATE powsys365_legal)

    add_test(NAME GDPREngineTest COMMAND legal_test --test gdpr)
    add_test(NAME CookieManagerTest COMMAND legal_test --test cookies)
    add_test(NAME TermsManagerTest COMMAND legal_test --test terms)
endif()

# ============================================================================
# Instalacion
# ============================================================================

install(TARGETS powsys365_legal
    EXPORT powsys365_legal-targets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES
    gdpr_engine.h
    cookie_manager.h
    terms_manager.h
    DESTINATION include/powsys365/legal
)
