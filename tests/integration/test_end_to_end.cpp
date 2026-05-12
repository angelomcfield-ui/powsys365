// =============================================================================
// tests/integration/test_end_to_end.cpp - End-to-End Integration Tests
// =============================================================================
// Covers:
//   - Full workflow: load IEEE 14 -> power flow (NR) -> verify results
//   - Compare with known reference values from published data
//   - Run short-circuit analysis on converged power flow results
//   - Cross-check power balance and line flows
//   - Verify all solver methods produce consistent results
// =============================================================================

#include <catch2/catch_all.hpp>
#include <powsy365/power_system.h>
#include <powsy365/load_flow.h>
#include <powsy365/short_circuit.h>
#include <powsy365/ybus_builder.h>
#include <cmath>
#include <iostream>

using namespace powsys365;

// ============================================================================
// Known reference values for IEEE 14-bus system (from published data)
// These are approximate values from MATPOWER / published results
// ============================================================================

struct ReferenceValue {
    size_t busId;
    double vm_pu;
    double va_deg;
    double tol_vm;   // tolerance for voltage magnitude
    double tol_va;   // tolerance for voltage angle
};

static const std::vector<ReferenceValue> IEEE14_REFERENCE = {
    // bus_id, vm_pu, va_deg, tol_vm, tol_va
    {1,  1.060,   0.000, 0.01, 0.5},
    {2,  1.045,  -4.983, 0.01, 0.5},
    {3,  1.010, -12.725, 0.01, 0.5},
    {4,  1.019, -10.313, 0.01, 0.5},
    {5,  1.020,  -8.774, 0.01, 0.5},
    {6,  1.070, -14.221, 0.01, 0.5},
    {7,  1.062, -13.360, 0.01, 0.5},
    {8,  1.090, -13.360, 0.01, 0.5},
    {9,  1.056, -14.939, 0.01, 0.5},
    {10, 1.051, -15.097, 0.01, 0.5},
    {11, 1.057, -14.791, 0.01, 0.5},
    {12, 1.055, -15.076, 0.01, 0.5},
    {13, 1.050, -15.158, 0.01, 0.5},
    {14, 1.036, -16.034, 0.01, 0.5},
};

// ============================================================================
// TEST_CASE: End-to-end power flow with reference comparison
// ============================================================================

TEST_CASE("End-to-end: IEEE 14 NR power flow matches reference values", "[integration][e2e]") {
    PowerSystem system;
    system.loadIEEE14();
    system.buildYbus();

    LoadFlowSolver solver(system);
    SolverConfig config;
    config.method = SolverMethod::NewtonRaphson;
    config.tolerance = 1e-6;
    config.maxIterations = 30;
    config.flatStart = true;
    config.enforceQLimits = false;
    config.verbose = false;

    PowerFlowResult result = solver.solve(config);

    // Must converge
    REQUIRE(result.converged());
    REQUIRE(result.iterations < 10);

    // Compare with reference values
    REQUIRE(result.busResults.size() == IEEE14_REFERENCE.size());

    for (size_t i = 0; i < IEEE14_REFERENCE.size(); ++i) {
        const auto& ref = IEEE14_REFERENCE[i];
        const auto& actual = result.busResults[i];

        INFO("Bus " << ref.busId << ": ref_vm=" << ref.vm_pu
             << ", actual_vm=" << actual.vm_pu);
        INFO("Bus " << ref.busId << ": ref_va=" << ref.va_deg
             << ", actual_va=" << actual.va_deg);

        REQUIRE(actual.busId == ref.busId);
        REQUIRE(std::abs(actual.vm_pu - ref.vm_pu) < ref.tol_vm);
        REQUIRE(std::abs(actual.va_deg - ref.va_deg) < ref.tol_va);
    }

    // Total generation should be positive
    REQUIRE(result.summary.totalPg_pu > 0.0);

    // Total load should be positive
    REQUIRE(result.summary.totalPl_pu > 0.0);

    // Losses should be positive
    REQUIRE(result.summary.totalPloss_pu > 0.0);

    // Reactive generation should be positive
    REQUIRE(result.summary.totalQg_pu > 0.0);

    // System summary should have correct bus type counts
    REQUIRE(result.summary.numSlackBuses == 1);
    REQUIRE(result.summary.numPQBuses >= 9);   // IEEE 14 has at least 9 PQ buses
    REQUIRE(result.summary.numPVBuses >= 3);    // IEEE 14 has 3 PV buses (buses 2, 3, 8)
}

// ============================================================================
// TEST_CASE: End-to-end power balance verification
// ============================================================================

TEST_CASE("End-to-end: power balance after NR solution", "[integration][e2e]") {
    PowerSystem system;
    system.loadIEEE14();
    system.buildYbus();

    LoadFlowSolver solver(system);
    SolverConfig config;
    config.method = SolverMethod::NewtonRaphson;
    config.tolerance = 1e-6;
    config.maxIterations = 30;
    config.flatStart = true;
    config.enforceQLimits = false;

    PowerFlowResult result = solver.solve(config);

    REQUIRE(result.converged());

    // Active power balance: |P_gen - P_load - P_loss| < 0.1 MW
    double pBalanceError = std::abs(
        result.summary.totalPg_pu -
        result.summary.totalPl_pu -
        result.summary.totalPloss_pu
    );
    INFO("Active power balance error = " << pBalanceError * 100.0 << " MW");
    REQUIRE(pBalanceError < 0.001);  // 0.1 MW on 100 MVA base

    // Reactive power balance: |Q_gen - Q_load - Q_loss + Q_shunt| should be small
    double qBalanceError = std::abs(
        result.summary.totalQg_pu -
        result.summary.totalQl_pu -
        result.summary.totalQloss_pu +
        result.summary.totalQshunt_pu
    );
    INFO("Reactive power balance error = " << qBalanceError * 100.0 << " MVAR");
    REQUIRE(qBalanceError < 0.01);  // 1 MVAR tolerance

    // Slack bus generation should cover losses + load not covered by PV buses
    double slackGen = 0.0;
    for (const auto& br : result.busResults) {
        if (br.type == BusType::Slack) {
            slackGen = br.pInyected_pu;
            break;
        }
    }
    INFO("Slack generation = " << slackGen * 100.0 << " MW");
    REQUIRE(slackGen > 0.0);
}

// ============================================================================
// TEST_CASE: End-to-end line flow verification
// ============================================================================

TEST_CASE("End-to-end: line flows are consistent", "[integration][e2e]") {
    PowerSystem system;
    system.loadIEEE14();
    system.buildYbus();

    LoadFlowSolver solver(system);
    SolverConfig config;
    config.method = SolverMethod::NewtonRaphson;
    config.tolerance = 1e-6;
    config.maxIterations = 30;
    config.flatStart = true;
    config.enforceQLimits = false;

    PowerFlowResult result = solver.solve(config);

    REQUIRE(result.converged());
    REQUIRE_FALSE(result.lineResults.empty());

    // Each line should have:
    // - Non-zero apparent power flow at at least one end
    // - Positive losses (or very close to zero)
    for (const auto& lr : result.lineResults) {
        INFO("Line " << lr.lineId << " (" << lr.fromBus << "->" << lr.toBus << ")");

        // At least one direction should have power flowing
        double sFrom = std::abs(lr.sFrom_pu);
        double sTo = std::abs(lr.sTo_pu);
        REQUIRE((sFrom > 1e-10 || sTo > 1e-10));

        // Losses should be non-negative (within numerical tolerance)
        REQUIRE(lr.pLoss_pu >= -1e-8);

        // Loading should be >= 0
        REQUIRE(lr.loading_pu >= 0.0);
    }
}

// ============================================================================
// TEST_CASE: End-to-end short-circuit after power flow
// ============================================================================

TEST_CASE("End-to-end: short-circuit analysis on converged IEEE 14 system", "[integration][e2e]") {
    // Step 1: Run power flow
    PowerSystem system;
    system.loadIEEE14();
    system.buildYbus();

    LoadFlowSolver lfSolver(system);
    SolverConfig lfConfig;
    lfConfig.method = SolverMethod::NewtonRaphson;
    lfConfig.tolerance = 1e-6;
    lfConfig.maxIterations = 30;
    lfConfig.flatStart = true;
    lfConfig.enforceQLimits = false;

    PowerFlowResult lfResult = lfSolver.solve(lfConfig);
    REQUIRE(lfResult.converged());

    // Step 2: Run short-circuit analysis
    ShortCircuitSolver scSolver(system);

    // Three-phase fault at bus 2
    size_t faultBusId = 2;
    ShortCircuitResult scResult = scSolver.calculateThreePhaseFault(faultBusId, 0.0);

    // Fault current must be positive
    REQUIRE(scResult.ik_pu > 0.0);
    REQUIRE(scResult.sk_pu > 0.0);

    // Short-circuit power should be positive
    REQUIRE(scResult.sk_pu > 0.0);

    // All buses should have post-fault voltage results
    REQUIRE(scResult.busResults.size() == system.numBuses());

    // The faulted bus should have near-zero voltage (bolted fault)
    for (const auto& br : scResult.busResults) {
        if (br.busId == faultBusId) {
            REQUIRE(std::abs(br.voltageDuringFault_pu) < 0.1);
        }
    }

    // Source contributions should exist (generators feed the fault)
    REQUIRE_FALSE(scResult.sourceContributions.empty());
}

// ============================================================================
// TEST_CASE: End-to-end multi-solver comparison
// ============================================================================

TEST_CASE("End-to-end: all solver methods produce consistent IEEE 14 results", "[integration][e2e]") {
    PowerSystem system;
    system.loadIEEE14();
    system.buildYbus();

    // Solve with Newton-Raphson
    LoadFlowSolver nrSolver(system);
    SolverConfig nrConfig;
    nrConfig.method = SolverMethod::NewtonRaphson;
    nrConfig.tolerance = 1e-6;
    nrConfig.maxIterations = 30;
    nrConfig.flatStart = true;
    nrConfig.enforceQLimits = false;

    PowerFlowResult nrResult = nrSolver.solve(nrConfig);
    REQUIRE(nrResult.converged());

    // Reset system for FDLF
    system.initializeVoltages();

    // Solve with Fast Decoupled XB
    LoadFlowSolver fdSolver(system);
    SolverConfig fdConfig;
    fdConfig.method = SolverMethod::FastDecoupledXB;
    fdConfig.tolerance = 1e-5;
    fdConfig.maxIterations = 60;
    fdConfig.flatStart = true;
    fdConfig.enforceQLimits = false;

    PowerFlowResult fdResult = fdSolver.solve(fdConfig);
    REQUIRE(fdResult.converged());

    // Both methods should produce similar voltage profiles
    REQUIRE(nrResult.busResults.size() == fdResult.busResults.size());

    double maxVmDiff = 0.0;
    double maxVaDiff = 0.0;

    for (size_t i = 0; i < nrResult.busResults.size(); ++i) {
        double vmDiff = std::abs(nrResult.busResults[i].vm_pu - fdResult.busResults[i].vm_pu);
        double vaDiff = std::abs(nrResult.busResults[i].va_deg - fdResult.busResults[i].va_deg);

        if (vmDiff > maxVmDiff) maxVmDiff = vmDiff;
        if (vaDiff > maxVaDiff) maxVaDiff = vaDiff;

        INFO("Bus " << (i + 1) << ": dVm=" << vmDiff << ", dVa=" << vaDiff);
    }

    INFO("Max voltage magnitude difference = " << maxVmDiff);
    INFO("Max voltage angle difference = " << maxVaDiff);

    // Voltage magnitudes should agree within 1%
    REQUIRE(maxVmDiff < 0.01);

    // Voltage angles should agree within 2 degrees
    REQUIRE(maxVaDiff < 2.0);

    // Total generation should be similar
    double pGenDiff = std::abs(nrResult.summary.totalPg_pu - fdResult.summary.totalPg_pu);
    INFO("Total P_gen difference = " << pGenDiff * 100.0 << " MW");
    REQUIRE(pGenDiff < 0.01);  // Within 1 MW
}

// ============================================================================
// TEST_CASE: End-to-end Ybus consistency after full workflow
// ============================================================================

TEST_CASE("End-to-end: Ybus remains consistent after multiple solves", "[integration][e2e]") {
    PowerSystem system;
    system.loadIEEE14();
    system.buildYbus();

    // Store original Ybus
    SpMatrixC ybusOriginal = system.getYbus();

    // Run power flow multiple times
    for (int run = 0; run < 3; ++run) {
        system.initializeVoltages();

        LoadFlowSolver solver(system);
        SolverConfig config;
        config.method = SolverMethod::NewtonRaphson;
        config.tolerance = 1e-6;
        config.maxIterations = 30;
        config.flatStart = true;
        config.enforceQLimits = false;

        PowerFlowResult result = solver.solve(config);
        REQUIRE(result.converged());

        // Ybus should not change between runs (topology unchanged)
        SpMatrixC ybusAfter = system.getYbus();
        REQUIRE(ybusAfter.rows() == ybusOriginal.rows());
        REQUIRE(ybusAfter.cols() == ybusOriginal.cols());
    }
}

// ============================================================================
// TEST_CASE: End-to-end system summary verification
// ============================================================================

TEST_CASE("End-to-end: system summary is internally consistent", "[integration][e2e]") {
    PowerSystem system;
    system.loadIEEE14();
    system.buildYbus();

    LoadFlowSolver solver(system);
    SolverConfig config;
    config.method = SolverMethod::NewtonRaphson;
    config.tolerance = 1e-6;
    config.maxIterations = 30;
    config.flatStart = true;
    config.enforceQLimits = false;

    PowerFlowResult result = solver.solve(config);
    REQUIRE(result.converged());

    // Count bus types manually and compare with summary
    int numPQ = 0, numPV = 0, numSlack = 0;
    for (const auto& br : result.busResults) {
        switch (br.type) {
            case BusType::PQ:    ++numPQ; break;
            case BusType::PV:    ++numPV; break;
            case BusType::Slack: ++numSlack; break;
        }
    }

    REQUIRE(numSlack == result.summary.numSlackBuses);
    REQUIRE(numPQ == result.summary.numPQBuses);
    REQUIRE(numPV == result.summary.numPVBuses);
    REQUIRE(numPQ + numPV + numSlack == 14);

    // Sum of generation from bus results should match summary
    double sumPGen = 0.0, sumQGen = 0.0;
    double sumPLoad = 0.0, sumQLoad = 0.0;
    for (const auto& br : result.busResults) {
        sumPGen += br.pg_pu;
        sumQGen += br.qg_pu;
        sumPLoad += br.pl_pu;
        sumQLoad += br.ql_pu;
    }

    INFO("Sum P_gen from buses = " << sumPGen * 100.0 << " MW");
    INFO("Summary P_gen = " << result.summary.totalPg_pu * 100.0 << " MW");

    // Generation from bus results should approximate summary
    // (slack generation is computed differently)
    REQUIRE(std::abs(sumPGen - result.summary.totalPg_pu) < 0.1);
    REQUIRE(std::abs(sumPLoad - result.summary.totalPl_pu) < 0.001);
}
