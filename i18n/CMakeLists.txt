cmake_minimum_required(VERSION 3.20)

I18N_SOURCES
set(i18n_sources)

add_library(powsy365_i18n INTERFACE)
target_include_directories(powsy365_i18n INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
