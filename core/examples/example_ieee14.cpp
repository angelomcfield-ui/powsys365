#include <powsy365/power_system.h>
#include <powsy365/load_flow.h>
#include <powsy365/short_circuit.h>
#include <powsy365/stability.h>
#include <iostream>
#include <iomanip>

using namespace powsys365;

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  POWSYS365 - IEEE 14 Bus Test System" << std::endl;
    std::cout << "========================================" << std::endl;

    // Create system and load IEEE 14
    PowerSystem system;
    system.loadIEEE14();

    std::cout << "\nSystem loaded:" << std::endl;
    std::cout << "  Buses:      " << system.numBuses() << std::endl;
    std::cout << "  Lines:      " << system.numLines() << std::endl;
    std::cout << "  Generators: " << system.numGenerators() << std::endl;
    std::cout << "  Base MVA:   " << system.getBaseMVA() << std::endl;

    // Validate
    if (!system.isValid()) {
        std::cerr << "System validation failed!" << std::endl;
        return 1;
    }
    std::cout << "  Valid:      YES" << std::endl;

    // Build Ybus
    system.buildYbus();
    std::cout << "  Ybus built: YES" << std::endl;

    // Power Flow - Newton-Raphson
    std::cout << "\n--- Newton-Raphson Power Flow ---" << std::endl;
    LoadFlowSolver solver(system);
    SolverConfig nrConfig;
    nrConfig.method = SolverMethod::NewtonRaphson;
    nrConfig.tolerance = 1e-6;
    nrConfig.maxIterations = 30;
    nrConfig.enforceQLimits = true;
    nrConfig.flatStart = true;
    nrConfig.verbose = true;

    PowerFlowResult nrResult = solver.newtonRaphson(nrConfig);
    std::cout << "Status:   " << nrResult.message << std::endl;
    std::cout << "Mismatch: " << std::scientific << nrResult.finalMismatch << std::endl;
    std::cout << "Time:     " << std::fixed << nrResult.solveTime_ms << " ms" << std::endl;

    // Print bus voltages
    std::cout << "\n--- Bus Voltages (Newton-Raphson) ---" << std::endl;
    std::cout << std::setw(6) << "Bus" << std::setw(12) << "|V| (pu)"
              << std::setw(12) << "Angle (deg)" << std::setw(8) << "Type" << std::endl;
    std::cout << std::string(40, '-') << std::endl;
    for (const auto& br : nrResult.busResults) {
        std::string typeStr;
        switch (br.type) {
            case BusType::Slack: typeStr = "Slack"; break;
            case BusType::PV:    typeStr = "PV"; break;
            case BusType::PQ:    typeStr = "PQ"; break;
        }
        std::cout << std::setw(6) << br.busId
                  << std::setw(12) << std::fixed << std::setprecision(4) << br.vm_pu
                  << std::setw(12) << std::setprecision(2) << br.va_deg
                  << std::setw(8) << typeStr << std::endl;
    }

    // System summary
    std::cout << "\n--- System Summary ---" << std::endl;
    std::cout << "Total P Gen:  " << std::setw(10) << nrResult.summary.totalPg_pu << " pu" << std::endl;
    std::cout << "Total P Load: " << std::setw(10) << nrResult.summary.totalPl_pu << " pu" << std::endl;
    std::cout << "Total P Loss: " << std::setw(10) << nrResult.summary.totalPloss_pu << " pu" << std::endl;
    std::cout << "Total Q Gen:  " << std::setw(10) << nrResult.summary.totalQg_pu << " pu" << std::endl;

    // Line flows
    std::cout << "\n--- Line Flows ---" << std::endl;
    std::cout << std::setw(8) << "Line" << std::setw(8) << "From"
              << std::setw(8) << "To" << std::setw(12) << "P_from"
              << std::setw(12) << "P_to" << std::setw(12) << "P_loss"
              << std::setw(10) << "Loading" << std::endl;
    std::cout << std::string(70, '-') << std::endl;
    for (const auto& lr : nrResult.lineResults) {
        std::cout << std::setw(8) << lr.lineName
                  << std::setw(8) << lr.fromBus
                  << std::setw(8) << lr.toBus
                  << std::setw(12) << std::setprecision(4) << lr.pFrom_pu
                  << std::setw(12) << lr.pTo_pu
                  << std::setw(12) << lr.pLoss_pu
                  << std::setw(9) << std::setprecision(1) << lr.loading_pu * 100 << "%"
                  << std::endl;
    }

    // Short circuit analysis
    std::cout << "\n--- Short Circuit Analysis ---" << std::endl;
    ShortCircuitSolver scSolver(system);
    auto scResult = scSolver.calculateThreePhaseFault(2, 0.0);
    std::cout << "Fault at Bus 2:" << std::endl;
    std::cout << "  Ik'' = " << scResult.ik_pu << " pu" << std::endl;
    std::cout << "  ip   = " << scResult.ip_pu << " pu" << std::endl;
    std::cout << "  Sk   = " << scResult.sk_pu << " pu" << std::endl;

    // Small-signal stability
    std::cout << "\n--- Small-Signal Stability ---" << std::endl;
    StabilitySolver stabSolver(system);
    auto stabResult = stabSolver.smallSignalStability();
    std::cout << "Small-signal stable: " << (stabResult.smallSignalStable ? "YES" : "NO") << std::endl;
    std::cout << "Eigenvalues found: " << stabResult.eigenvalues.size() << std::endl;
    if (!stabResult.eigenvalues.empty()) {
        std::cout << "Most critical mode:" << std::endl;
        std::cout << "  Real part:     " << stabResult.eigenvalues[0].value.real() << std::endl;
        std::cout << "  Freq (Hz):     " << stabResult.eigenvalues[0].frequency_Hz << std::endl;
        std::cout << "  Damping ratio: " << stabResult.eigenvalues[0].dampingRatio << std::endl;
    }

    std::cout << "\n========================================" << std::endl;
    std::cout << "  All analyses completed successfully" << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}
