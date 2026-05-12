# =============================================================================
# third_party/CMakeLists.txt - Third-party dependencies management
# =============================================================================
# Manages bundled/fetched third-party libraries:
#   - eigen: Header-only linear algebra (via find_package or FetchContent)
#   - pybind11: Python C++ bindings (via FetchContent in root CMakeLists.txt)
#   - catch2: Testing framework (via FetchContent in root CMakeLists.txt)
#   - nlohmann/json: JSON library (via FetchContent in root CMakeLists.txt)
#   - tl::expected: Expected monad type (via FetchContent in root CMakeLists.txt)
# =============================================================================

# All third-party dependencies are managed at the root level via FetchContent
# and find_package calls. This file serves as an anchor for any additional
# bundled libraries placed in this directory.

# If third-party libraries are vendored (placed directly in third_party/),
# add_subdirectory() calls for them should be placed here.

# Example:
# if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/some_vendored_lib/CMakeLists.txt")
#     add_subdirectory(some_vendored_lib)
# endif()

message(STATUS "Third-party dependencies configured via FetchContent/find_package at root level.")
