#include <powsy365/power_system.h>
#include <powsy365/load_flow.h>
#include <iostream>
#include <iomanip>

using namespace powsys365;

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  POWSYS365 - Method Comparison" << std::endl;
    std::cout << "========================================" << std::endl;

    PowerSystem system;
    system.loadIEEE14();
    system.buildYbus();

    LoadFlowSolver solver(system);

    struct TestCase {
        SolverMethod method;
        const char* name;
        double tolerance;
    };

    std::vector<TestCase> tests = {
        {SolverMethod::NewtonRaphson,    "Newton-Raphson",    1e-6},
        {SolverMethod::FastDecoupledXB,  "Fast Decoupled XB", 1e-5},
        {SolverMethod::FastDecoupledBX,  "Fast Decoupled BX", 1e-5},
        {SolverMethod::GaussSeidel,      "Gauss-Seidel",      1e-6}
    };

    std::cout << std::setw(20) << "Method"
              << std::setw(10) << "Iters"
              << std::setw(18) << "Final Mismatch"
              << std::setw(12) << "Time (ms)"
              << std::setw(12) << "Converged"
              << std::endl;
    std::cout << std::string(72, '-') << std::endl;

    for (const auto& test : tests) {
        // Reload fresh system for each test
        system.loadIEEE14();
        system.buildYbus();
        LoadFlowSolver testSolver(system);

        SolverConfig config;
        config.method = test.method;
        config.tolerance = test.tolerance;
        config.maxIterations = 100;
        config.enforceQLimits = false;
        config.flatStart = true;
        config.verbose = false;

        auto result = testSolver.solve(config);

        std::cout << std::setw(20) << test.name
                  << std::setw(10) << result.iterations
                  << std::setw(18) << std::scientific << result.finalMismatch
                  << std::setw(12) << std::fixed << std::setprecision(2) << result.solveTime_ms
                  << std::setw(12) << (result.converged() ? "YES" : "NO")
                  << std::endl;
    }

    return 0;
}
