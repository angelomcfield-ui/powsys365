#pragma once
#include "../../commons/types.h"
#include "power_system.h"
#include <Eigen/Dense>
#include <Eigen/Eigenvalues>
#include <vector>

namespace powsys365 {

/**
 * StabilitySolver - Power system stability analysis.
 *
 * Provides two main analysis modes:
 * 1. Transient stability: Time-domain simulation of rotor angle dynamics
 *    using the swing equation after a disturbance.
 * 2. Small-signal stability: Eigenvalue analysis of the linearized system
 *    state matrix to identify oscillatory modes.
 */
class StabilitySolver {
public:
    explicit StabilitySolver(const PowerSystem& system);

    // ========================================================================
    // TRANSIENT STABILITY
    // ========================================================================

    /**
     * Run transient stability simulation.
     *
     * Simulates the swing equation for each generator:
     *   M_i * d2delta_i/dt2 = Pmi - Pei - D_i * ddelta_i/dt
     *
     * where M = 2H/omega0 is the inertia constant,
     * Pm is mechanical power input,
     * Pe is electrical power output,
     * D is damping coefficient.
     *
     * @param faultBusId Bus where fault is applied
     * @param faultClearingTime_s Time to clear fault [seconds]
     * @param totalSimTime_s Total simulation time [seconds]
     * @param timeStep_s Integration time step [seconds]
     * @return Time series of rotor angles, speeds, and stability assessment
     */
    std::vector<TransientResult> transientStability(
        size_t faultBusId,
        double faultClearingTime_s,
        double totalSimTime_s,
        double timeStep_s = 0.01
    );

    // ========================================================================
    // SMALL-SIGNAL STABILITY
    // ========================================================================

    /**
     * Perform small-signal stability analysis.
     *
     * Linearizes the system around the operating point and computes
     * eigenvalues of the state matrix A. A mode is stable if its
     * real part is negative.
     *
     * @return Eigenvalue analysis results with damping ratios
     */
    StabilityResult smallSignalStability();

    // ========================================================================
    // STATE MATRIX
    // ========================================================================

    /**
     * Build the linearized state matrix A for the multi-machine system.
     *
     * State vector: x = [delta_1, ..., delta_n, omega_1, ..., omega_n]^T
     * A = [ 0           I       ]
     *     [ -M^-1*K    -M^-1*D  ]
     *
     * where K is the synchronizing torque coefficient matrix.
     *
     * @return Dense state matrix A
     */
    DenseMatrix buildStateMatrix();

    // ========================================================================
    // EIGENVALUE ANALYSIS
    // ========================================================================

    /**
     * Calculate eigenvalues of the state matrix and classify stability.
     * @param stateMatrix The A matrix
     * @return List of eigenvalue results with damping info
     */
    std::vector<EigenvalueResult> calculateEigenvalues(
        const DenseMatrix& stateMatrix
    );

private:
    const PowerSystem& system_;
    double baseMVA_;
    size_t nBuses_;
    size_t nGen_;

    // ========================================================================
    // INTERNAL HELPERS
    // ========================================================================

    /** Compute electrical power output for each generator. */
    DenseVector computeElectricalPower(
        const DenseVector& rotorAngles,
        const std::vector<size_t>& genBusIndices,
        const SpMatrixC& ybusReduced
    );

    /** Classical model: reduce network to generator internal buses. */
    SpMatrixC reduceToInternalBuses(
        const SpMatrixC& ybus,
        const std::vector<Generator>& generators
    );
};

} // namespace powsys365
