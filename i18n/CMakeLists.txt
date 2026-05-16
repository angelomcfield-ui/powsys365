cmake_minimum_required(VERSION 3.20)

set(I18N_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/TranslationManager.cpp
)

file(GLOB I18N_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/*.h")

add_library(powsy365_i18n STATIC ${I18N_SOURCES} ${I18N_HEADERS})
target_include_directories(powsy365_i18n PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(powsy365_i18n PUBLIC powsys365_core)
target_link_libraries(powsy365_i18n PRIVATE powsys365_warnings)
