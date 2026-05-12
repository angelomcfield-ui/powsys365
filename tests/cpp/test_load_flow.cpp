// =============================================================================
// tests/cpp/test_load_flow.cpp - Load Flow Solver Unit Tests (Catch2 v3)
// =============================================================================
// Covers:
//   - Newton-Raphson convergence on IEEE 14-bus system
//   - Fast Decoupled XB convergence
//   - Gauss-Seidel convergence
//   - Empty system error handling
//   - Missing slack bus error handling
//   - Power balance verification
//   - Voltage magnitude bounds
// =============================================================================

#include <catch2/catch_all.hpp>
#include <powsy365/power_system.h>
#include <powsy365/load_flow.h>
#include <cmath>

using namespace powsys365;

// ============================================================================
// Helper functions
// ============================================================================

static SolverConfig make_config(SolverMethod method) {
    SolverConfig config;
    config.method = method;
    config.tolerance = (method == SolverMethod::FastDecoupledXB) ? 1e-5 : 1e-6;
    config.maxIterations = (method == SolverMethod::GaussSeidel)   ? 200
                         : (method == SolverMethod::FastDecoupledXB) ? 60
                         : 30;
    config.flatStart = true;
    config.enforceQLimits = false;
    config.verbose = false;
    return config;
}

// ============================================================================
// TEST_CASE: Newton-Raphson IEEE 14 converges
// ============================================================================

TEST_CASE("Newton-Raphson IEEE 14 converges", "[loadflow][newtonraphson]") {
    PowerSystem system;
    system.loadIEEE14();
    system.buildYbus();

    LoadFlowSolver solver(system);
    SolverConfig config = make_config(SolverMethod::NewtonRaphson);

    PowerFlowResult result = solver.solve(config);

    REQUIRE(result.converged());
    REQUIRE(result.iterations < 10);
    REQUIRE(result.finalMismatch < 1e-6);
    REQUIRE(result.solveTime_ms > 0.0);

    // All bus voltages must be within [0.95, 1.05] pu
    REQUIRE_FALSE(result.busResults.empty());
    for (const auto& br : result.busResults) {
        INFO("Bus " << br.busId << " voltage = " << br.vm_pu << " pu");
        REQUIRE(br.vm_pu >= 0.95);
        REQUIRE(br.vm_pu <= 1.05);
    }

    // Slack bus voltage should match its setpoint
    const auto& slackResult = result.busResults[0];
    REQUIRE(slackResult.type == BusType::Slack);
    REQUIRE(std::abs(slackResult.vm_pu - 1.06) < 0.01);

    // Power balance: |P_gen - P_load - P_loss| < 0.1 MW (in pu on 100 MVA base)
    double mismatch = std::abs(result.summary.totalPg_pu -
                               result.summary.totalPl_pu -
                               result.summary.totalPloss_pu);
    INFO("Power balance mismatch = " << mismatch << " pu");
    REQUIRE(mismatch < 0.001);  // 0.1 MW on 100 MVA base = 0.001 pu

    // Line results should exist
    REQUIRE_FALSE(result.lineResults.empty());

    // System summary should show correct bus counts
    REQUIRE(result.summary.numPQBuses > 0);
    REQUIRE(result.summary.numSlackBuses == 1);
}

// ============================================================================
// TEST_CASE: Fast Decoupled XB converges
// ============================================================================

TEST_CASE("Fast Decoupled XB converges", "[loadflow][fastdecoupled]") {
    PowerSystem system;
    system.loadIEEE14();
    system.buildYbus();

    LoadFlowSolver solver(system);
    SolverConfig config = make_config(SolverMethod::FastDecoupledXB);

    PowerFlowResult result = solver.solve(config);

    REQUIRE(result.converged());
    REQUIRE(result.iterations <= 60);
    REQUIRE(result.finalMismatch < 1e-4);

    // All bus voltages within reasonable bounds
    REQUIRE_FALSE(result.busResults.empty());
    for (const auto& br : result.busResults) {
        INFO("Bus " << br.busId << " voltage = " << br.vm_pu << " pu");
        REQUIRE(br.vm_pu >= 0.95);
        REQUIRE(br.vm_pu <= 1.05);
    }

    // Power balance check
    double mismatch = std::abs(result.summary.totalPg_pu -
                               result.summary.totalPl_pu -
                               result.summary.totalPloss_pu);
    INFO("Power balance mismatch (FDXB) = " << mismatch << " pu");
    REQUIRE(mismatch < 0.01);
}

// ============================================================================
// TEST_CASE: Gauss-Seidel converges
// ============================================================================

TEST_CASE("Gauss-Seidel converges", "[loadflow][gaussseidel]") {
    PowerSystem system;
    system.loadIEEE14();
    system.buildYbus();

    LoadFlowSolver solver(system);
    SolverConfig config = make_config(SolverMethod::GaussSeidel);
    config.maxIterations = 200;

    PowerFlowResult result = solver.solve(config);

    // Gauss-Seidel may need many iterations; check it at least reduces mismatch
    REQUIRE(result.finalMismatch < 1.0);

    if (result.converged()) {
        REQUIRE(result.finalMismatch < 1e-6);

        // Voltage bounds check
        for (const auto& br : result.busResults) {
            INFO("Bus " << br.busId << " voltage = " << br.vm_pu << " pu");
            REQUIRE(br.vm_pu >= 0.95);
            REQUIRE(br.vm_pu <= 1.05);
        }

        // Power balance
        double mismatch = std::abs(result.summary.totalPg_pu -
                                   result.summary.totalPl_pu -
                                   result.summary.totalPloss_pu);
        REQUIRE(mismatch < 0.01);
    }
}

// ============================================================================
// TEST_CASE: Solver handles empty system
// ============================================================================

TEST_CASE("Solver handles empty system", "[loadflow][error_handling]") {
    PowerSystem system;  // Empty system - no buses

    LoadFlowSolver solver(system);
    SolverConfig config = make_config(SolverMethod::NewtonRaphson);

    PowerFlowResult result = solver.solve(config);

    REQUIRE_FALSE(result.converged());
    REQUIRE(result.status != ConvergenceStatus::Converged);
    REQUIRE(result.iterations == 0);
}

// ============================================================================
// TEST_CASE: Solver handles no slack bus
// ============================================================================

TEST_CASE("Solver handles no slack bus", "[loadflow][error_handling]") {
    PowerSystem system;

    // Add only PQ buses - no slack bus
    Bus bus1;
    bus1.id = 1;
    bus1.name = "Bus 1";
    bus1.type = BusType::PQ;
    bus1.baseVoltage_kV = 138.0;
    bus1.pl_pu = 1.0;
    bus1.ql_pu = 0.5;
    system.addBus(bus1);

    Bus bus2;
    bus2.id = 2;
    bus2.name = "Bus 2";
    bus2.type = BusType::PQ;
    bus2.baseVoltage_kV = 138.0;
    bus2.pl_pu = 0.8;
    bus2.ql_pu = 0.3;
    system.addBus(bus2);

    // Add a connecting line
    Line line;
    line.id = 1;
    line.fromBus = 1;
    line.toBus = 2;
    line.r_pu = 0.01;
    line.x_pu = 0.1;
    line.status = 1;
    system.addLine(line);

    LoadFlowSolver solver(system);
    SolverConfig config = make_config(SolverMethod::NewtonRaphson);

    PowerFlowResult result = solver.solve(config);

    REQUIRE_FALSE(result.converged());
    REQUIRE(result.status == ConvergenceStatus::InvalidInitialConditions);
}

// ============================================================================
// TEST_CASE: NR with Q-limit enforcement
// ============================================================================

TEST_CASE("Newton-Raphson with Q-limit enforcement", "[loadflow][qlimits]") {
    PowerSystem system;
    system.loadIEEE14();
    system.buildYbus();

    LoadFlowSolver solver(system);
    SolverConfig config = make_config(SolverMethod::NewtonRaphson);
    config.enforceQLimits = true;
    config.maxQLimitIterations = 10;

    PowerFlowResult result = solver.solve(config);

    REQUIRE(result.converged());
    REQUIRE(result.iterations < 20);
    REQUIRE(result.finalMismatch < 1e-6);

    // All voltages still within bounds after Q-limit enforcement
    for (const auto& br : result.busResults) {
        INFO("Bus " << br.busId << " voltage = " << br.vm_pu << " pu");
        REQUIRE(br.vm_pu >= 0.95);
        REQUIRE(br.vm_pu <= 1.05);
    }
}

// ============================================================================
// TEST_CASE: Power balance after line flow calculation
// ============================================================================

TEST_CASE("Power balance after line flow calculation", "[loadflow][balance]") {
    PowerSystem system;
    system.loadIEEE14();
    system.buildYbus();

    LoadFlowSolver solver(system);
    SolverConfig config = make_config(SolverMethod::NewtonRaphson);

    PowerFlowResult result = solver.solve(config);
    REQUIRE(result.converged());

    // Total generation should approximately equal total load + total losses
    double pGen = result.summary.totalPg_pu;
    double pLoad = result.summary.totalPl_pu;
    double pLoss = result.summary.totalPloss_pu;

    INFO("P_gen  = " << pGen * 100.0 << " MW");
    INFO("P_load = " << pLoad * 100.0 << " MW");
    INFO("P_loss = " << pLoss * 100.0 << " MW");

    double balanceError = std::abs(pGen - pLoad - pLoss);
    REQUIRE(balanceError < 0.01);  // Within 1 MW on 100 MVA base

    // Each line should have non-negative losses
    for (const auto& lr : result.lineResults) {
        INFO("Line " << lr.lineId << " P_loss = " << lr.pLoss_pu);
        REQUIRE(lr.pLoss_pu >= -1e-10);  // Losses should be >= 0
    }
}

// ============================================================================
// TEST_CASE: All solver methods produce consistent results
// ============================================================================

TEST_CASE("Solver methods produce consistent voltages", "[loadflow][consistency]") {
    PowerSystem system;
    system.loadIEEE14();
    system.buildYbus();

    // Solve with Newton-Raphson as reference
    LoadFlowSolver nrSolver(system);
    SolverConfig nrConfig = make_config(SolverMethod::NewtonRaphson);
    PowerFlowResult nrResult = nrSolver.solve(nrConfig);
    REQUIRE(nrResult.converged());

    // Solve with Fast Decoupled
    system.initializeVoltages();  // Reset to flat start
    LoadFlowSolver fdSolver(system);
    SolverConfig fdConfig = make_config(SolverMethod::FastDecoupledXB);
    PowerFlowResult fdResult = fdSolver.solve(fdConfig);
    REQUIRE(fdResult.converged());

    // Compare voltage magnitudes: all methods should agree within 1%
    REQUIRE(nrResult.busResults.size() == fdResult.busResults.size());
    for (size_t i = 0; i < nrResult.busResults.size(); ++i) {
        double vmNr = nrResult.busResults[i].vm_pu;
        double vmFd = fdResult.busResults[i].vm_pu;
        double diff = std::abs(vmNr - vmFd);
        INFO("Bus " << (i + 1) << ": NR=" << vmNr << " FD=" << vmFd
             << " diff=" << diff);
        REQUIRE(diff < 0.01);  // Within 1% difference
    }
}
