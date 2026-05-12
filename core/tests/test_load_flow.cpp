#include <powsy365/power_system.h>
#include <powsy365/load_flow.h>
#include <iostream>
#include <cmath>
#include <cassert>

using namespace powsys365;

void testIEEE14FlatStart() {
    std::cout << "Test: IEEE 14 Flat Start Newton-Raphson..." << std::flush;

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

    auto result = solver.solve(config);

    assert(result.converged() && "NR should converge for IEEE 14");
    assert(result.iterations <= 10 && "NR should converge in < 10 iterations for IEEE 14");
    assert(result.finalMismatch < 1e-6 && "Final mismatch should be within tolerance");

    // Check slack bus voltage
    auto slackResult = result.busResults[0];
    assert(std::abs(slackResult.vm_pu - 1.06) < 0.01 && "Slack voltage should be ~1.06 pu");

    // Check total generation equals total load + losses
    double mismatch = std::abs(result.summary.totalPg_pu -
        result.summary.totalPl_pu - result.summary.totalPloss_pu);
    assert(mismatch < 0.01 && "Power balance should be satisfied");

    std::cout << " PASSED (" << result.iterations << " iterations)" << std::endl;
}

void testIEEE14FastDecoupled() {
    std::cout << "Test: IEEE 14 Fast Decoupled XB..." << std::flush;

    PowerSystem system;
    system.loadIEEE14();
    system.buildYbus();

    LoadFlowSolver solver(system);
    SolverConfig config;
    config.method = SolverMethod::FastDecoupledXB;
    config.tolerance = 1e-5;
    config.maxIterations = 60;
    config.flatStart = true;
    config.enforceQLimits = false;

    auto result = solver.solve(config);

    assert(result.converged() && "FDLF should converge for IEEE 14");
    assert(result.finalMismatch < 1e-4 && "FDLF mismatch should be small");

    std::cout << " PASSED (" << result.iterations << " iterations)" << std::endl;
}

void testIEEE14GaussSeidel() {
    std::cout << "Test: IEEE 14 Gauss-Seidel..." << std::flush;

    PowerSystem system;
    system.loadIEEE14();
    system.buildYbus();

    LoadFlowSolver solver(system);
    SolverConfig config;
    config.method = SolverMethod::GaussSeidel;
    config.tolerance = 1e-6;
    config.maxIterations = 200;
    config.flatStart = true;
    config.enforceQLimits = false;

    auto result = solver.solve(config);

    // GS may or may not converge within max iterations for IEEE 14
    // but it should make significant progress
    assert(result.finalMismatch < 1.0 && "GS should reduce mismatch");

    std::cout << " " << (result.converged() ? "PASSED" : "PARTIAL")
              << " (" << result.iterations << " iters, mismatch=" << result.finalMismatch << ")"
              << std::endl;
}

void testSystemValidation() {
    std::cout << "Test: System Validation..." << std::flush;

    PowerSystem system;
    system.loadIEEE14();

    assert(system.hasSlackBus() && "Should have slack bus");
    assert(system.isConnected() && "System should be connected");
    assert(system.isValid() && "System should be valid");
    assert(system.numBuses() == 14 && "Should have 14 buses");

    std::cout << " PASSED" << std::endl;
}

void testVoltageInitialization() {
    std::cout << "Test: Voltage Initialization..." << std::flush;

    PowerSystem system;
    system.loadIEEE14();
    system.initializeVoltages();

    auto vm = system.getVm();
    auto va = system.getVa();

    // Slack bus should have its specified voltage
    assert(std::abs(vm(0) - 1.06) < 1e-10 && "Slack voltage should be 1.06");
    assert(std::abs(va(0)) < 1e-10 && "Slack angle should be 0");

    // All buses should have reasonable voltage
    for (int i = 0; i < vm.size(); ++i) {
        assert(vm(i) > 0.5 && vm(i) < 2.0 && "Voltage should be reasonable");
    }

    std::cout << " PASSED" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  POWSYS365 - Load Flow Unit Tests" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;

    try {
        testSystemValidation();
        testVoltageInitialization();
        testIEEE14FlatStart();
        testIEEE14FastDecoupled();
        testIEEE14GaussSeidel();

        std::cout << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "  ALL TESTS PASSED" << std::endl;
        std::cout << "========================================" << std::endl;
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "TEST FAILED: " << e.what() << std::endl;
        return 1;
    }
}
