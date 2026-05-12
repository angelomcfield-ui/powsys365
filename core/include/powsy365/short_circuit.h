#pragma once
#include "../../commons/types.h"
#include "../../commons/math_utils.h"
#include "power_system.h"
#include <Eigen/Sparse>
#include <vector>

namespace powsys365 {

/**
 * ShortCircuitSolver - Short-circuit analysis per IEC 60909 methodology.
 *
 * Calculates fault currents for symmetrical and unsymmetrical faults
 * using sequence networks (positive, negative, zero sequence).
 */
class ShortCircuitSolver {
public:
    explicit ShortCircuitSolver(const PowerSystem& system);

    // ========================================================================
    // THREE-PHASE SYMMETRICAL FAULT
    // ========================================================================

    /**
     * Calculate three-phase short-circuit at specified bus.
     * @param faultBusId Bus where fault is applied
     * @param faultImpedance_pu Fault impedance in per-unit (0 = solid fault)
     * @return ShortCircuitResult with currents and MVA
     */
    ShortCircuitResult calculateThreePhaseFault(
        size_t faultBusId,
        double faultImpedance_pu = 0.0
    );

    // ========================================================================
    // UNSYMMETRICAL FAULTS
    // ========================================================================

    /** Calculate single-phase-to-ground fault. */
    ShortCircuitResult calculateSinglePhaseFault(
        size_t faultBusId,
        double faultImpedance_pu = 0.0
    );

    /** Calculate phase-to-phase fault. */
    ShortCircuitResult calculateTwoPhaseFault(
        size_t faultBusId,
        double faultImpedance_pu = 0.0
    );

    /** Calculate two-phase-to-ground fault. */
    ShortCircuitResult calculateTwoPhaseGroundFault(
        size_t faultBusId,
        double faultImpedance_pu = 0.0
    );

    /** Generic unsymmetrical fault dispatcher. */
    ShortCircuitResult calculateUnsymmetricalFault(
        FaultType type,
        size_t faultBusId,
        double faultImpedance_pu = 0.0
    );

    // ========================================================================
    // SEQUENCE NETWORKS
    // ========================================================================

    /** Build positive sequence network (uses standard Ybus with subtransient reactance). */
    SpMatrixC buildPositiveSequenceNetwork();

    /** Build negative sequence network. */
    SpMatrixC buildNegativeSequenceNetwork();

    /** Build zero sequence network. */
    SpMatrixC buildZeroSequenceNetwork();

    // ========================================================================
    // FAULT CURRENT CALCULATIONS
    // ========================================================================

    /**
     * Calculate fault currents from sequence networks.
     * @param y1 Positive sequence Ybus
     * @param y2 Negative sequence Ybus
     * @param y0 Zero sequence Ybus
     * @param faultBusId Fault location
     * @param type Fault type
     * @param zf Fault impedance
     */
    ShortCircuitResult calculateFaultCurrents(
        const SpMatrixC& y1,
        const SpMatrixC& y2,
        const SpMatrixC& y0,
        size_t faultBusId,
        FaultType type,
        double zf
    );

    /**
     * Calculate contributions from each source to the fault current.
     */
    std::vector<std::pair<size_t, double>> calculateContributions(
        const SpMatrixC& y1,
        size_t faultBusId
    );

    // ========================================================================
    // IEC 60909 QUANTITIES
    // ========================================================================

    /** Calculate initial symmetrical short-circuit current Ik'' */
    double calculateInitialCurrent(double zth_pu) const;

    /** Calculate peak short-circuit current ip = kappa * sqrt(2) * Ik'' */
    double calculatePeakCurrent(double ik_pu, double r_x_ratio) const;

    /** Calculate breaking current Ib */
    double calculateBreakingCurrent(double ik_pu, double mu) const;

    /** Calculate short-circuit power Sk = sqrt(3) * Un * Ik'' */
    double calculateShortCircuitPower(double ik_pu) const;

    /** Calculate Thevenin impedance at a bus */
    Complex calculateTheveninImpedance(
        const SpMatrixC& ybus,
        size_t busId
    );

private:
    const PowerSystem& system_;
    double baseMVA_;
    size_t nBuses_;

    // Modified Ybus with subtransient reactances for generators
    SpMatrixC buildFaultYbus();
};

} // namespace powsys365
