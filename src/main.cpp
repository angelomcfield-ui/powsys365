#include <iostream>
#include "powsys365/power_system.h"
#include "powsys365/load_flow.h"

int main() {
    std::cout << "POWSYS365 - Power System Analysis Platform\n";

    // Crear un sistema simple de 2 barras
    powsys365::PowerSystem system;

    // Barra slack
    powsys365::Bus bus1{1, "Slack", 69.0, 1.0, 0.0, 3};
    // Barra PQ
    powsys365::Bus bus2{2, "Load", 69.0, 1.0, 0.0, 1};

    system.addBus(bus1);
    system.addBus(bus2);

    // Linea
    powsys365::Line line{1, 2, powsys365::Complex(0.01, 0.03), 0.02};
    system.addLine(line);

    // Generador en barra 1
    powsys365::Generator gen{1, 100.0, 0.0, 100.0, 0.0, 50.0, -50.0};
    system.addGenerator(gen);

    // Carga en barra 2
    powsys365::Load load{2, 50.0, 10.0};
    system.addLoad(load);

    // Resolver flujo de carga
    powsys365::LoadFlowSolver solver(system, 1e-6, 100, "GS");
    auto result = solver.solve();

    if (result.converged) {
        std::cout << "Load flow converged in " << result.iterations << " iterations\n";
        for (size_t i = 0; i < result.vm_pu.size(); ++i) {
            std::cout << "Bus " << system.getBuses()[i].number << ": V=" << result.vm_pu[i] << " pu, " << result.va_deg[i] << " deg\n";
        }
    } else {
        std::cout << "Load flow did not converge\n";
    }

    return 0;
}
