cmake_minimum_required(VERSION 3.20)

# =============================================================================
# POWSYS365 :: Licensing Module
# =============================================================================
set(LICENSING_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/LicenseManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/LicenseValidator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PaymentGateway.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UserRegistration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/DeviceFingerprint.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/api/license_api.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/payments/payment_api.cpp
)

set(LICENSING_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/LicenseManager.h
    ${CMAKE_CURRENT_SOURCE_DIR}/LicenseValidator.h
    ${CMAKE_CURRENT_SOURCE_DIR}/PaymentGateway.h
    ${CMAKE_CURRENT_SOURCE_DIR}/UserRegistration.h
    ${CMAKE_CURRENT_SOURCE_DIR}/DeviceFingerprint.h
    ${CMAKE_CURRENT_SOURCE_DIR}/api/license_api.h
    ${CMAKE_CURRENT_SOURCE_DIR}/payments/payment_api.h
)

add_library(powsys365_licensing STATIC ${LICENSING_SOURCES} ${LICENSING_HEADERS})

target_include_directories(powsys365_licensing
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/api>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/payments>
        $<INSTALL_INTERFACE:include/powsys365/licensing>
)

target_link_libraries(powsys365_licensing
    PUBLIC
        powsys365_core
        nlohmann_json::nlohmann_json
    PRIVATE
        powsys365_warnings
)

# OpenSSL for crypto
find_package(OpenSSL QUIET)
if(OpenSSL_FOUND)
    target_link_libraries(powsys365_licensing PUBLIC OpenSSL::SSL OpenSSL::Crypto)
    target_compile_definitions(powsys365_licensing PRIVATE POWSYS365_HAS_OPENSSL=1)
else()
    message(WARNING "OpenSSL not found. Licensing crypto features disabled.")
endif()

install(TARGETS powsys365_licensing
    EXPORT POWSYS365Targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES ${LICENSING_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/powsys365/licensing
)
