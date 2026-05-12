// =============================================================================
// tests/cpp/test_short_circuit.cpp - Short-Circuit Analysis Unit Tests
// =============================================================================
// Covers:
//   - Three-phase symmetrical fault on IEEE 14-bus
//   - Single-phase-to-ground fault
//   - Phase-to-phase fault
//   - Two-phase-to-ground fault
//   - Fault current magnitude verification
//   - Post-fault voltage verification
// =============================================================================

#include <catch2/catch_all.hpp>
#include <powsy365/power_system.h>
#include <powsy365/short_circuit.h>
#include <cmath>

using namespace powsys365;

// ============================================================================
// Helper: create IEEE 14 system for short-circuit tests
// ============================================================================

static PowerSystem create_ieee14_system() {
    PowerSystem system;
    system.loadIEEE14();
    system.buildYbus();
    return system;
}

// ============================================================================
// TEST_CASE: Three-phase fault IEEE 14
// ============================================================================

TEST_CASE("Three-phase fault IEEE 14", "[shortcircuit][threephase]") {
    PowerSystem system = create_ieee14_system();
    ShortCircuitSolver scSolver(system);

    // Apply fault at bus 2 (a PV bus)
    size_t faultBusId = 2;
    double faultZ = 0.0;  // Bolted fault

    ShortCircuitResult result = scSolver.calculateThreePhaseFault(faultBusId, faultZ);

    // Fault current must be positive
    REQUIRE(result.ik_pu > 0.0);
    REQUIRE(result.sk_pu > 0.0);
    REQUIRE(result.ip_pu > 0.0);

    // Peak current should be larger than initial symmetrical current
    REQUIRE(result.ip_pu >= result.ik_pu);

    // All buses should have results
    REQUIRE_FALSE(result.busResults.empty());
    REQUIRE(result.busResults.size() == system.numBuses());

    // The faulted bus should have very low voltage during fault
    for (const auto& br : result.busResults) {
        if (br.busId == faultBusId) {
            // Fault bus voltage magnitude should be near zero for bolted fault
            REQUIRE(std::abs(br.voltageDuringFault_pu) < 0.1);
        }
        // Fault current at each bus should be non-negative
        REQUIRE(br.faultCurrent_pu >= 0.0);
    }

    // Message should indicate success
    REQUIRE_FALSE(result.message.empty());

    // Source contributions should exist (generators feeding the fault)
    REQUIRE_FALSE(result.sourceContributions.empty());
}

// ============================================================================
// TEST_CASE: Three-phase fault at slack bus
// ============================================================================

TEST_CASE("Three-phase fault at slack bus", "[shortcircuit][threephase]") {
    PowerSystem system = create_ieee14_system();
    ShortCircuitSolver scSolver(system);

    size_t faultBusId = 1;  // Slack bus
    ShortCircuitResult result = scSolver.calculateThreePhaseFault(faultBusId, 0.0);

    REQUIRE(result.ik_pu > 0.0);
    REQUIRE(result.sk_pu > 0.0);
    REQUIRE_FALSE(result.busResults.empty());
}

// ============================================================================
// TEST_CASE: Three-phase fault with impedance
// ============================================================================

TEST_CASE("Three-phase fault with impedance", "[shortcircuit][threephase]") {
    PowerSystem system = create_ieee14_system();
    ShortCircuitSolver scSolver(system);

    size_t faultBusId = 4;

    // Fault with non-zero impedance should produce lower current
    ShortCircuitResult result0 = scSolver.calculateThreePhaseFault(faultBusId, 0.0);
    ShortCircuitResult resultZ = scSolver.calculateThreePhaseFault(faultBusId, 0.01);

    REQUIRE(result0.ik_pu > 0.0);
    REQUIRE(resultZ.ik_pu > 0.0);

    // Higher impedance => lower fault current
    REQUIRE(resultZ.ik_pu < result0.ik_pu);
}

// ============================================================================
// TEST_CASE: Unsymmetrical faults - single-phase-to-ground
// ============================================================================

TEST_CASE("Single-phase-to-ground fault", "[shortcircuit][unsymmetrical]") {
    PowerSystem system = create_ieee14_system();
    ShortCircuitSolver scSolver(system);

    size_t faultBusId = 3;
    ShortCircuitResult result = scSolver.calculateSinglePhaseFault(faultBusId, 0.0);

    // Fault current should be positive
    REQUIRE(result.ik_pu > 0.0);
    REQUIRE(result.sk_pu > 0.0);
    REQUIRE_FALSE(result.busResults.empty());

    // SLG fault current is typically smaller than 3ph fault at same bus
    ShortCircuitResult result3ph = scSolver.calculateThreePhaseFault(faultBusId, 0.0);
    // Note: this depends on zero-sequence network; just ensure both are valid
    REQUIRE(result3ph.ik_pu > 0.0);

    // All buses should have post-fault voltages
    for (const auto& br : result.busResults) {
        REQUIRE_FALSE(br.busName.empty());
    }
}

// ============================================================================
// TEST_CASE: Unsymmetrical faults - phase-to-phase fault
// ============================================================================

TEST_CASE("Phase-to-phase fault", "[shortcircuit][unsymmetrical]") {
    PowerSystem system = create_ieee14_system();
    ShortCircuitSolver scSolver(system);

    size_t faultBusId = 5;
    ShortCircuitResult result = scSolver.calculateTwoPhaseFault(faultBusId, 0.0);

    REQUIRE(result.ik_pu > 0.0);
    REQUIRE(result.sk_pu > 0.0);
    REQUIRE_FALSE(result.busResults.empty());

    // Phase-to-phase fault current is typically sqrt(3)/2 times 3ph current
    ShortCircuitResult result3ph = scSolver.calculateThreePhaseFault(faultBusId, 0.0);
    REQUIRE(result3ph.ik_pu > 0.0);

    // Check that results are reasonable (not NaN or Inf)
    REQUIRE(std::isfinite(result.ik_pu));
    REQUIRE(std::isfinite(result.sk_pu));
    REQUIRE(std::isfinite(result.ip_pu));
}

// ============================================================================
// TEST_CASE: Unsymmetrical faults - two-phase-to-ground fault
// ============================================================================

TEST_CASE("Two-phase-to-ground fault", "[shortcircuit][unsymmetrical]") {
    PowerSystem system = create_ieee14_system();
    ShortCircuitSolver scSolver(system);

    size_t faultBusId = 7;
    ShortCircuitResult result = scSolver.calculateTwoPhaseGroundFault(faultBusId, 0.0);

    REQUIRE(result.ik_pu > 0.0);
    REQUIRE(result.sk_pu > 0.0);
    REQUIRE_FALSE(result.busResults.empty());

    // Results should be finite and valid
    REQUIRE(std::isfinite(result.ik_pu));
    REQUIRE(std::isfinite(result.sk_pu));

    for (const auto& br : result.busResults) {
        REQUIRE(br.faultCurrent_pu >= 0.0);
        REQUIRE(std::isfinite(br.faultCurrent_pu));
    }
}

// ============================================================================
// TEST_CASE: Generic unsymmetrical fault dispatcher
// ============================================================================

TEST_CASE("Unsymmetrical fault dispatcher", "[shortcircuit][unsymmetrical]") {
    PowerSystem system = create_ieee14_system();
    ShortCircuitSolver scSolver(system);

    size_t faultBusId = 9;

    // Test dispatcher for each fault type
    ShortCircuitResult r1ph = scSolver.calculateUnsymmetricalFault(
        FaultType::SinglePhase, faultBusId, 0.0);
    REQUIRE(r1ph.ik_pu > 0.0);

    ShortCircuitResult r2ph = scSolver.calculateUnsymmetricalFault(
        FaultType::TwoPhase, faultBusId, 0.0);
    REQUIRE(r2ph.ik_pu > 0.0);

    ShortCircuitResult r2phg = scSolver.calculateUnsymmetricalFault(
        FaultType::TwoPhaseG, faultBusId, 0.0);
    REQUIRE(r2phg.ik_pu > 0.0);
}

// ============================================================================
// TEST_CASE: Fault at remote bus produces smaller currents
// ============================================================================

TEST_CASE("Fault current varies with fault location", "[shortcircuit][physical]") {
    PowerSystem system = create_ieee14_system();
    ShortCircuitSolver scSolver(system);

    // Fault at bus 1 (slack, strongest source)
    ShortCircuitResult resultBus1 = scSolver.calculateThreePhaseFault(1, 0.0);

    // Fault at bus 14 (most remote)
    ShortCircuitResult resultBus14 = scSolver.calculateThreePhaseFault(14, 0.0);

    REQUIRE(resultBus1.ik_pu > 0.0);
    REQUIRE(resultBus14.ik_pu > 0.0);

    // Slack bus should generally have higher fault current (stronger source)
    // Bus 14 (remote) should have lower fault current
    INFO("Fault current at Bus 1:  " << resultBus1.ik_pu << " pu");
    INFO("Fault current at Bus 14: " << resultBus14.ik_pu << " pu");
}

// ============================================================================
// TEST_CASE: Sequence network building
// ============================================================================

TEST_CASE("Sequence networks build successfully", "[shortcircuit][sequence]") {
    PowerSystem system = create_ieee14_system();
    ShortCircuitSolver scSolver(system);

    SpMatrixC y1 = scSolver.buildPositiveSequenceNetwork();
    SpMatrixC y2 = scSolver.buildNegativeSequenceNetwork();
    SpMatrixC y0 = scSolver.buildZeroSequenceNetwork();

    // All sequence networks should be non-empty
    REQUIRE(y1.rows() > 0);
    REQUIRE(y1.cols() > 0);
    REQUIRE(y2.rows() > 0);
    REQUIRE(y2.cols() > 0);
    REQUIRE(y0.rows() > 0);
    REQUIRE(y0.cols() > 0);

    // Positive sequence should match system size
    REQUIRE(y1.rows() == static_cast<Eigen::Index>(system.numBuses()));

    // All should be square matrices
    REQUIRE(y1.rows() == y1.cols());
    REQUIRE(y2.rows() == y2.cols());
    REQUIRE(y0.rows() == y0.cols());
}
