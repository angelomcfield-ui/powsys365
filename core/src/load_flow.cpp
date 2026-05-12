// core/src/load_flow.cpp

#include "../include/powsys365/load_flow.h"
#include "../../commons/math_utils.h"
#include "../../commons/constants.h"
#include <cmath>
#include <iostream>
#include <algorithm>

namespace powsys365 {

LoadFlowSolver::LoadFlowSolver(const PowerSystem& system, double tolerance, int max_iter, const std::string& method)
    : system_(system), tolerance_(tolerance), max_iterations_(max_iter), method_(method) {}

LoadFlowResult LoadFlowSolver::solve() {
    if (method_ == "NR") {
        return solveNewtonRaphson();
    } else if (method_ == "FDBX") {
        return solveFastDecoupled();
    } else if (method_ == "GS") {
        return solveGaussSeidel();
    } else {
        throw std::invalid_argument("Unknown load flow method");
    }
}

LoadFlowResult LoadFlowSolver::solveNewtonRaphson() {
    size_t n = system_.getNumBuses();
    Vector vm(n), va(n);
    // Inicializar voltajes
    for (size_t i = 0; i < n; ++i) {
        const auto& bus = system_.getBuses()[i];
        vm[i] = bus.vm_pu;
        va[i] = bus.va_deg;
    }

    LoadFlowResult result;
    result.converged = false;
    result.iterations = 0;

    for (int iter = 0; iter < max_iterations_; ++iter) {
        Vector p_mismatch(n), q_mismatch(n);
        calculatePowerMismatch(vm, va, p_mismatch, q_mismatch);

        // Verificar convergencia
        double max_mismatch = 0.0;
        for (size_t i = 0; i < n; ++i) {
            if (system_.getBuses()[i].type == 1) { // PQ bus
                max_mismatch = std::max(max_mismatch, std::abs(p_mismatch[i]));
                max_mismatch = std::max(max_mismatch, std::abs(q_mismatch[i]));
            } else if (system_.getBuses()[i].type == 2) { // PV bus
                max_mismatch = std::max(max_mismatch, std::abs(p_mismatch[i]));
            }
        }

        if (max_mismatch < tolerance_) {
            result.converged = true;
            result.iterations = iter + 1;
            break;
        }

        // Construir Jacobiano
        Matrix jacobian(2 * n, Vector(2 * n, 0.0));
        buildJacobian(vm, va, jacobian);

        // Construir vector de mismatch
        Vector mismatch(2 * n);
        for (size_t i = 0; i < n; ++i) {
            mismatch[2 * i] = p_mismatch[i];
            mismatch[2 * i + 1] = q_mismatch[i];
        }

        // Resolver sistema
        Vector delta = MathUtils::solveLinearSystem(jacobian, mismatch);

        // Actualizar voltajes
        for (size_t i = 0; i < n; ++i) {
            va[i] += delta[2 * i] * 180.0 / PI; // convertir a grados
            vm[i] += delta[2 * i + 1];
        }

        result.iterations = iter + 1;
    }

    // Llenar resultado
    result.vm_pu = vm;
    result.va_deg = va;
    // Calcular potencias (simplificado)
    result.p_gen.assign(n, 0.0);
    result.q_gen.assign(n, 0.0);
    result.p_load.assign(n, 0.0);
    result.q_load.assign(n, 0.0);
    result.total_ploss = 0.0;
    result.total_qloss = 0.0;

    return result;
}

void LoadFlowSolver::buildYbus(ComplexMatrix& ybus) const {
    size_t n = system_.getNumBuses();
    ybus.assign(n, std::vector<Complex>(n, Complex(0.0, 0.0)));

    // Adicionar admitancias de linea
    for (const auto& line : system_.getLines()) {
        size_t i = system_.getBusIndex(line.from_bus);
        size_t j = system_.getBusIndex(line.to_bus);
        Complex y = 1.0 / line.z_pu;
        ybus[i][i] += y + Complex(0.0, line.b_pu / 2.0);
        ybus[j][j] += y + Complex(0.0, line.b_pu / 2.0);
        ybus[i][j] -= y;
        ybus[j][i] -= y;
    }
}

void LoadFlowSolver::calculatePowerMismatch(const Vector& vm, const Vector& va, Vector& p_mismatch, Vector& q_mismatch) const {
    size_t n = system_.getNumBuses();
    ComplexMatrix ybus(n, std::vector<Complex>(n, Complex(0.0, 0.0)));
    buildYbus(ybus);

    for (size_t i = 0; i < n; ++i) {
        Complex v_i = MathUtils::polarToRect(vm[i], va[i]);
        Complex s_calc(0.0, 0.0);
        for (size_t j = 0; j < n; ++j) {
            Complex v_j = MathUtils::polarToRect(vm[j], va[j]);
            s_calc += v_i * MathUtils::conj(ybus[i][j] * v_j);
        }

        // Potencia calculada
        double p_calc = s_calc.real();
        double q_calc = s_calc.imag();

        // Potencia especificada
        double p_spec = 0.0;
        double q_spec = 0.0;

        // Adicionar cargas
        for (const auto& load : system_.getLoads()) {
            if (load.bus == system_.getBuses()[i].number) {
                p_spec -= load.p_mw / system_.getBaseMVA();
                q_spec -= load.q_mvar / system_.getBaseMVA();
            }
        }

        // Adicionar generadores
        for (const auto& gen : system_.getGenerators()) {
            if (gen.bus == system_.getBuses()[i].number) {
                p_spec += gen.p_mw / system_.getBaseMVA();
                q_spec += gen.q_mvar / system_.getBaseMVA();
            }
        }

        p_mismatch[i] = p_spec - p_calc;
        q_mismatch[i] = q_spec - q_calc;
    }
}

void LoadFlowSolver::buildJacobian(const Vector& vm, const Vector& va, Matrix& jacobian) const {
    // Implementacion simplificada del Jacobiano
    // Para una implementacion completa, ver literatura de flujo de carga
    size_t n = system_.getNumBuses();
    // Este es un placeholder - la implementacion real es compleja
    for (size_t i = 0; i < 2 * n; ++i) {
        jacobian[i][i] = 1.0; // Diagonal dominante
    }
}

LoadFlowResult LoadFlowSolver::solveFastDecoupled() {
    // Placeholder - implementar Fast Decoupled
    LoadFlowResult result;
    result.converged = false;
    return result;
}

LoadFlowResult LoadFlowSolver::solveGaussSeidel() {
    // Placeholder - implementar Gauss-Seidel
    LoadFlowResult result;
    result.converged = false;
    return result;
}

} // namespace powsys365