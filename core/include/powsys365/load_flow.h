// core/include/powsys365/load_flow.h

#ifndef POWSYS365_LOAD_FLOW_H
#define POWSYS365_LOAD_FLOW_H

#include <vector>
#include <string>
#include "power_system.h"
#include "../../commons/types.h"

namespace powsys365 {

struct LoadFlowResult {
    bool converged;
    int iterations;
    std::vector<double> vm_pu;
    std::vector<double> va_deg;
    std::vector<double> p_gen;
    std::vector<double> q_gen;
    std::vector<double> p_load;
    std::vector<double> q_load;
    double total_ploss;
    double total_qloss;
};

class LoadFlowSolver {
private:
    const PowerSystem& system_;
    double tolerance_;
    int max_iterations_;
    std::string method_; // "NR", "FDBX", "GS"

public:
    LoadFlowSolver(const PowerSystem& system, double tolerance = 1e-6, int max_iter = 100, const std::string& method = "NR");

    LoadFlowResult solve();

private:
    LoadFlowResult solveNewtonRaphson();
    LoadFlowResult solveFastDecoupled();
    LoadFlowResult solveGaussSeidel();

    void buildYbus(ComplexMatrix& ybus) const;
    void calculatePowerMismatch(const Vector& vm, const Vector& va, Vector& p_mismatch, Vector& q_mismatch) const;
    void buildJacobian(const Vector& vm, const Vector& va, Matrix& jacobian) const;
};

} // namespace powsys365

#endif // POWSYS365_LOAD_FLOW_H