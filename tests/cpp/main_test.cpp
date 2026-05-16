// =============================================================================
// tests/cpp/main_test.cpp - Catch2 v3 Test Utilities
// =============================================================================
// Provides shared test utilities and global fixtures for POWSYS365 test suite.
// NOTE: Do NOT define CATCH_CONFIG_MAIN here; Catch2::Catch2WithMain is
//       linked by CMake, which already provides the main() function.
//       This file should only contain shared helpers and fixtures.
// =============================================================================

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
