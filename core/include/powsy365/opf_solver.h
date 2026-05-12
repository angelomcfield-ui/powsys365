#pragma once
#include "../../commons/types.h"
#include "power_system.h"
#include <vector>
#include <Eigen/Sparse>

namespace powsys365 {

/** OPF objective function types */
enum class ObjectiveType : int {
    MinCost = 1,       // Minimize generation cost
    MinLosses = 2,     // Minimize active power losses
    MinEmissions = 3   // Minimize emissions
};

/**
 * OptimalPowerFlow - DC and AC Optimal Power Flow solver.
 *
 * DC-OPF: Linear programming formulation minimizing generation cost
 * subject to power balance and line flow constraints.
 *
 * AC-OPF: Nonlinear programming formulation with full AC power flow
 * constraints. Uses penalty method / successive linearization.
 */
class OptimalPowerFlow {
public:
    explicit OptimalPowerFlow(const PowerSystem& system);

    // ========================================================================
    // DC OPF (LINEAR)
    // ========================================================================

    /**
     * Solve DC Optimal Power Flow.
     *
     * Formulation:
     *   min sum_i (c0_i + c1_i*Pg_i + c2_i*Pg_i^2)
     *   s.t. B * theta = Pg - Pd  (power balance)
     *        Pmin <= Pg <= Pmax    (generation limits)
     *        |PTDF * (Pg - Pd)| <= Pmax_line  (line limits)
     *
     * Uses a direct least-squares approach with active set for line limits.
     *
     * @param objective Objective function type
     * @return OPF result with dispatch and costs
     */
    OPFResult solveDCOPF(ObjectiveType objective = ObjectiveType::MinCost);

    // ========================================================================
    // AC OPF (NONLINEAR)
    // ========================================================================

    /**
     * Solve AC Optimal Power Flow (successive linearization).
     *
     * Uses iterative linearization around the current operating point,
     * solving a sequence of LP/QP problems until convergence.
     *
     * @param objective Objective function type
     * @param maxIterations Maximum number of linearization iterations
     * @return OPF result with dispatch and AC feasibility
     */
    OPFResult solveACOPF(
        ObjectiveType objective = ObjectiveType::MinCost,
        int maxIterations = 30
    );

    // ========================================================================
    // OBJECTIVE FUNCTIONS
    // ========================================================================

    /** Compute generation cost: sum_i (c0_i + c1_i*Pg_i + c2_i*Pg_i^2) */
    double objectiveMinCost(const std::vector<double>& pg) const;

    /** Compute total active power losses [pu] */
    double objectiveMinLosses(
        const std::vector<double>& pg,
        const std::vector<double>& va
    ) const;

    // ========================================================================
    // CONSTRAINTS
    // ========================================================================

    /** Check generation power limits. */
    std::vector<Violation> checkGenerationLimits(
        const std::vector<double>& pg
    ) const;

    /** Check line flow limits using DC approximation. */
    std::vector<Violation> checkLineFlowLimitsDC(
        const std::vector<double>& va
    ) const;

    /** Check voltage magnitude limits. */
    std::vector<Violation> checkVoltageLimits(
        const std::vector<double>& vm
    ) const;

    /** Build power transfer distribution factors (PTDF) matrix. */
    DenseMatrix buildPTDF();

    /** Build DC B' matrix (susceptance matrix for DC approximation). */
    SpMatrix buildDCBMatrix();

    /** Build bus-generator incidence matrix. */
    DenseMatrix buildGenBusMatrix();

private:
    const PowerSystem& system_;
    double baseMVA_;
    size_t nBuses_;
    size_t nGen_;
    size_t nLines_;

    // Bus indices for slack (reference), PV, and PQ buses
    size_t slackBusIdx_ = 0;
    std::vector<size_t> pvBusIndices_;
    std::vector<size_t> pqBusIndices_;

    void classifyBuses();
};

} // namespace powsys365
