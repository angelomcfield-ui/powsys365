#include <iostream>
#include "../core/include/powsys365/power_system.h"
#include "../core/include/powsys365/load_flow.h"

int main() {
    std::cout << "POWSYS365 - Power System Analysis Platform\n";

    // Crear un sistema simple de prueba
    powsys365::PowerSystem system;

    // Agregar barras
    powsys365::Bus bus1{1, "Bus 1", 69.0, 1.06, 0.0, 3}; // Slack
    powsys365::Bus bus2{2, "Bus 2", 69.0, 1.045, -4.98, 2}; // PV
    powsys365::Bus bus3{3, "Bus 3", 69.0, 1.01, -12.72, 1}; // PQ

    system.addBus(bus1);
    system.addBus(bus2);
    system.addBus(bus3);

    // Agregar linea
    powsys365::Line line{1, 2, powsys365::Complex(0.01, 0.03), 0.02};
    system.addLine(line);

    // Agregar generador
    powsys365::Generator gen{1, 232.4, -16.9, 332.4, 0.0, 10.0, -10.0};
    system.addGenerator(gen);

    // Agregar carga
    powsys365::Load load{3, 94.2, 19.0};
    system.addLoad(load);

    // Resolver flujo de carga
    powsys365::LoadFlowSolver solver(system);
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
