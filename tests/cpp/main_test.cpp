// =============================================================================
// tests/cpp/main_test.cpp - Catch2 v3 Main Entry Point
// =============================================================================
// Provides the main() function for Catch2 v3 test runner.
// All test suites are discovered automatically by the Catch2 framework.
//
// Build:  compile with all other test_*.cpp files and link against
//         Catch2::Catch2WithMain (or define CATCH_CONFIG_MAIN here).
// =============================================================================

#define CATCH_CONFIG_MAIN
#include <catch2/catch_all.hpp>

// =============================================================================
// Global Test Setup / Teardown
// =============================================================================

// Custom Catch2 reporter tag for POWSYS365 test sessions
namespace Catch {
    // Session configuration is handled automatically by Catch2 v3
} // namespace Catch

// =============================================================================
// Global fixture: runs once before any test and once after all tests
// =============================================================================
TEST_CASE("Global sanity check", "[.global]") {
    // Verify that the test environment is functional
    REQUIRE(true);
}
