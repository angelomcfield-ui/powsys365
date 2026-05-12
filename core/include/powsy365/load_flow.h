#pragma once
#include "../../commons/types.h"
#include "../../commons/math_utils.h"
#include "../../commons/constants.h"
#include "power_system.h"
#include <Eigen/Sparse>
#include <Eigen/SparseLU>
#include <functional>
#include <string>
#include <vector>

namespace powsys365 {

/**
 * LoadFlowSolver - Power flow analysis using numerical methods.
 *
 * Implements three industry-standard algorithms:
 * 1. Newton-Raphson: Full Jacobian, quadratic convergence (default)
 * 2. Fast Decoupled (XB variant): Constant B' and B'' matrices
 * 3. Gauss-Seidel: Iterative, robust for simple systems
 *
 * All methods support PQ, PV, and Slack bus types with automatic
 * Q-limit enforcement for PV buses.
 */
class LoadFlowSolver {
public:
    explicit LoadFlowSolver(PowerSystem& system);

    // ========================================================================
    // MAIN SOLVE ENTRY POINT
    // ========================================================================

    /**
     * Solve power flow using the configured method.
     * @param config Solver configuration (method, tolerance, max iterations)
     * @return PowerFlowResult with voltages, flows, and convergence info
     */
    PowerFlowResult solve(const SolverConfig& config);

    // ========================================================================
    // INDIVIDUAL METHODS (public for direct access)
    // ========================================================================

    /** Newton-Raphson method with full Jacobian. */
    PowerFlowResult newtonRaphson(const SolverConfig& config);

    /** Fast Decoupled Load Flow (XB version - B' and B''). */
    PowerFlowResult fastDecoupledFDXB(const SolverConfig& config);

    /** Fast Decoupled Load Flow (BX version - experimental). */
    PowerFlowResult fastDecoupledFDBX(const SolverConfig& config);

    /** Gauss-Seidel iterative method. */
    PowerFlowResult gaussSeidel(const SolverConfig& config);

    // ========================================================================
    // POST-PROCESSING
    // ========================================================================

    /** Calculate power flows on all lines and transformers. */
    std::vector<PowerFlowLineResult> calculateLineFlows(
        const DenseVector& vm,
        const DenseVector& va_rad
    );

    /** Calculate system summary statistics. */
    SystemSummary calculateSystemSummary(
        const std::vector<PowerFlowBusResult>& busResults,
        const std::vector<PowerFlowLineResult>& lineResults
    );

    /** Build bus result structures from final state. */
    std::vector<PowerFlowBusResult> buildBusResults(
        const DenseVector& vm,
        const DenseVector& va_rad,
        const DenseVector& pCalc,
        const DenseVector& qCalc,
        const DenseVector& pSpec,
        const DenseVector& qSpec
    );

private:
    PowerSystem& system_;

    // Ybus decomposed matrices
    SpMatrix gMatrix_;
    SpMatrix bMatrix_;

    // Bus classification
    std::vector<bool> isPQ_;
    std::vector<bool> isPV_;
    std::vector<bool> isSlack_;
    size_t numPQ_ = 0;
    size_t numPV_ = 0;
    size_t nBuses_ = 0;

    // Bus reordering: PQ buses first, then PV, slack last
    std::vector<size_t> busOrder_;       // Ordered bus indices
    std::vector<size_t> pqBusIndices_;   // Indices of PQ buses
    std::vector<size_t> pvBusIndices_;   // Indices of PV buses
    size_t slackIndex_ = 0;              // Index of slack bus

    // ========================================================================
    // SETUP
    // ========================================================================

    /** Classify buses and build ordering for efficient Jacobian assembly. */
    void classifyBuses();

    /** Decompose Ybus into G and B matrices. */
    void buildGBMatrices();

    /** Build specification vectors P_sch and Q_sch from bus data. */
    void buildSpecificationVectors(
        DenseVector& pSpec,
        DenseVector& qSpec
    );

    // ========================================================================
    // JACOBIAN
    // ========================================================================

    /** Build full Newton-Raphson Jacobian matrix J = [H N; J L]. */
    SpMatrix buildJacobian(
        const DenseVector& vm,
        const DenseVector& va_rad,
        const DenseVector& pCalc,
        const DenseVector& qCalc
    );

    // ========================================================================
    // FAST DECOUPLED MATRICES
    // ========================================================================

    /** Build B' matrix for fast decoupled P-theta update.
     *  B' = -B with: slack row/col removed, line charging ignored,
     *  transformer ratios ignored (set to 1), shunts ignored.
     */
    SpMatrix buildBPrime();

    /** Build B'' matrix for fast decoupled Q-V update.
     *  B'' = -B with: slack and PV rows/cols removed, line charging included.
     */
    SpMatrix buildBDoublePrime();

    // ========================================================================
    // Q LIMIT ENFORCEMENT
    // ========================================================================

    /** Enforce generator Q limits. Converts PV buses to PQ if violated.
     * @return Number of buses converted
     */
    int enforceQLimits(
        DenseVector& qGen,
        std::vector<bool>& pvToPqConverted
    );

    // ========================================================================
    // PROGRESS CALLBACK
    // ========================================================================

    void reportProgress(
        const SolverConfig& config,
        int iteration,
        double mismatch,
        const std::string& message
    );
};

} // namespace powsys365
