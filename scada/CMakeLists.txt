# =============================================================================
# scada/CMakeLists.txt - SCADA Protocol Libraries
# =============================================================================
# Library: powsys365_scada
# Protocols: IEC 61850 (MMS/GOOSE/SV), DNP3, Modbus (TCP/RTU), OPC-UA
# Provides: Protocol adapters, data acquisition, command execution,
#           time synchronization (IEEE C37.118), secure authentication.
# =============================================================================

if(NOT BUILD_SCADA)
    message(STATUS "SCADA protocol libraries disabled (BUILD_SCADA=OFF).")
    return()
endif()

# ---------------------------------------------------------------------------
# External Protocol Libraries (via FetchContent or find_package)
# ---------------------------------------------------------------------------

# --- libIEC61850 (IEC 61850 protocol stack) ---
find_package(libIEC61850 QUIET)
if(NOT libIEC61850_FOUND)
    include(FetchContent)
    FetchContent_Declare(libiec61850
        GIT_REPOSITORY https://github.com/mz-automation/libiec61850.git
        GIT_TAG        v1.6.0
        GIT_SHALLOW    TRUE
    )
    set(BUILD_SHARED_LIBS_SAVED ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(libiec61850)
    set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_SAVED} CACHE BOOL "" FORCE)
    add_library(libIEC61850::iec61850 ALIAS iec61850)
    set(libIEC61850_FOUND TRUE)
endif()

# --- opendnp3 (DNP3 protocol stack) ---
find_package(opendnp3 QUIET)
if(NOT opendnp3_FOUND)
    include(FetchContent)
    FetchContent_Declare(opendnp3
        GIT_REPOSITORY https://github.com/dnp3/opendnp3.git
        GIT_TAG        3.1.2
        GIT_SHALLOW    TRUE
    )
    set(DNP3_STATIC_LIBS ON CACHE BOOL "" FORCE)
    set(DNP3_TESTS OFF CACHE BOOL "" FORCE)
    set(DNP3_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(opendnp3)
    set(opendnp3_FOUND TRUE)
endif()

# --- libmodbus (Modbus protocol stack) ---
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LIBMODBUS QUIET libmodbus>=3.1)
endif()
if(NOT LIBMODBUS_FOUND)
    include(FetchContent)
    FetchContent_Declare(libmodbus
        GIT_REPOSITORY https://github.com/stephane/libmodbus.git
        GIT_TAG        v3.1.10
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(libmodbus)
    set(LIBMODBUS_FOUND TRUE)
endif()

# --- open62541 (OPC-UA implementation) ---
find_package(open62541 QUIET)
if(NOT open62541_FOUND)
    include(FetchContent)
    FetchContent_Declare(open62541
        GIT_REPOSITORY https://github.com/open62541/open62541.git
        GIT_TAG        v1.3.9
        GIT_SHALLOW    TRUE
    )
    set(UA_ENABLE_ENCRYPTION_MBEDTLS ON CACHE BOOL "" FORCE)
    set(UA_ENABLE_SUBSCRIPTIONS ON CACHE BOOL "" FORCE)
    set(UA_ENABLE_METHODCALLS ON CACHE BOOL "" FORCE)
    set(UA_NAMESPACE_ZERO "REDUCED" CACHE STRING "" FORCE)
    set(BUILD_SHARED_LIBS_SAVED ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(open62541)
    set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_SAVED} CACHE BOOL "" FORCE)
    set(open62541_FOUND TRUE)
endif()

# ---------------------------------------------------------------------------
# Source files
# ---------------------------------------------------------------------------
file(GLOB_RECURSE SCADA_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

file(GLOB_RECURSE SCADA_HEADERS
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/powsy365/scada/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/powsy365/scada/*.hpp"
)

# ---------------------------------------------------------------------------
# Library target
# ---------------------------------------------------------------------------
add_library(powsys365_scada STATIC)

target_sources(powsys365_scada
    PRIVATE
        ${SCADA_SOURCES}
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include"
        FILES ${SCADA_HEADERS}
)

target_include_directories(powsys365_scada
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ---------------------------------------------------------------------------
# Link dependencies
# ---------------------------------------------------------------------------
target_link_libraries(powsys365_scada
    PUBLIC
        powsys365_core
        powsys365_warnings
        nlohmann_json::nlohmann_json
)

# --- IEC 61850 ---
if(libIEC61850_FOUND AND TARGET iec61850)
    target_link_libraries(powsys365_scada PUBLIC iec61850)
    target_compile_definitions(powsys365_scada PUBLIC POWSYS365_HAS_IEC61850=1)
elseif(libIEC61850_FOUND AND TARGET libIEC61850::iec61850)
    target_link_libraries(powsys365_scada PUBLIC libIEC61850::iec61850)
    target_compile_definitions(powsys365_scada PUBLIC POWSYS365_HAS_IEC61850=1)
else()
    message(WARNING "IEC 61850 library not found. IEC 61850 protocol support disabled.")
    target_compile_definitions(powsys365_scada PUBLIC POWSYS365_HAS_IEC61850=0)
endif()

# --- DNP3 ---
if(opendnp3_FOUND AND TARGET opendnp3)
    target_link_libraries(powsys365_scada PUBLIC opendnp3)
    target_compile_definitions(powsys365_scada PUBLIC POWSYS365_HAS_DNP3=1)
else()
    message(WARNING "DNP3 library not found. DNP3 protocol support disabled.")
    target_compile_definitions(powsys365_scada PUBLIC POWSYS365_HAS_DNP3=0)
endif()

# --- Modbus ---
if(LIBMODBUS_FOUND AND TARGET modbus)
    target_link_libraries(powsys365_scada PUBLIC modbus)
    target_compile_definitions(powsys365_scada PUBLIC POWSYS365_HAS_MODBUS=1)
elseif(LIBMODBUS_FOUND AND LIBMODBUS_LIBRARIES)
    target_link_libraries(powsys365_scada PUBLIC ${LIBMODBUS_LIBRARIES})
    target_include_directories(powsys365_scada PUBLIC ${LIBMODBUS_INCLUDE_DIRS})
    target_compile_definitions(powsys365_scada PUBLIC POWSYS365_HAS_MODBUS=1)
else()
    message(WARNING "Modbus library not found. Modbus protocol support disabled.")
    target_compile_definitions(powsys365_scada PUBLIC POWSYS365_HAS_MODBUS=0)
endif()

# --- OPC-UA ---
if(open62541_FOUND AND TARGET open62541)
    target_link_libraries(powsys365_scada PUBLIC open62541)
    target_compile_definitions(powsys365_scada PUBLIC POWSYS365_HAS_OPCUA=1)
elseif(open62541_FOUND AND TARGET open62541::open62541)
    target_link_libraries(powsys365_scada PUBLIC open62541::open62541)
    target_compile_definitions(powsys365_scada PUBLIC POWSYS365_HAS_OPCUA=1)
else()
    message(WARNING "OPC-UA library not found. OPC-UA protocol support disabled.")
    target_compile_definitions(powsys365_scada PUBLIC POWSYS365_HAS_OPCUA=0)
endif()

# ---------------------------------------------------------------------------
# Platform socket libraries
# ---------------------------------------------------------------------------
if(WIN32)
    target_link_libraries(powsys365_scada PUBLIC ws2_32 iphlpapi)
endif()

# ---------------------------------------------------------------------------
# Installation
# ---------------------------------------------------------------------------
install(TARGETS powsys365_scada
    EXPORT POWSYS365Targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    FILE_SET HEADERS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/powsys365/scada
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(BUILD_TESTS AND TARGET Catch2::Catch2WithMain)
    file(GLOB SCADA_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
    if(SCADA_TEST_SOURCES)
        add_executable(powsys365_scada_tests ${SCADA_TEST_SOURCES})
        target_link_libraries(powsys365_scada_tests
            PRIVATE
                powsys365_scada
                powsys365_core
                Catch2::Catch2WithMain
        )
        catch_discover_tests(powsys365_scada_tests)
    endif()
endif()
