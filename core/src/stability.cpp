#include "powsy365/stability.h"
#include <cmath>
#include <stdexcept>
#include <iostream>

namespace powsys365 {

// ============================================================================
// CONSTRUCTOR
// ============================================================================

StabilitySolver::StabilitySolver(const PowerSystem& system)
    : system_(system), baseMVA_(system.getBaseMVA()),
      nBuses_(system.numBuses()),
      nGen_(system.numGenerators()) {}

// ============================================================================
// REDUCE NETWORK TO INTERNAL BUSES
// ============================================================================

SpMatrixC StabilitySolver::reduceToInternalBuses(
    const SpMatrixC& ybus,
    const std::vector<Generator>& generators
) {
    // Classical model: add subtransient reactance in series with each generator
    // The internal bus admittance is: Y_ii += 1/(j*Xd'')
    std::vector<TripletC> triplets;

    // Copy existing Ybus
    for (int k = 0; k < ybus.outerSize(); ++k) {
        for (SpMatrixC::InnerIterator it(ybus, k); it; ++it) {
            triplets.emplace_back(it.row(), it.col(), it.value());
        }
    }

    // Add generator internal reactance
    for (const auto& gen : generators) {
        if (gen.status != 1 || gen.busId == 0 || gen.busId > nBuses_) continue;
        int busIdx = static_cast<int>(gen.busId - 1);
        double xdpp = gen.xdDoublePrime_pu;
        if (xdpp < ZERO_IMPEDANCE_THRESHOLD) continue;

        // Y_added = 1 / (j*Xd'') = -j / Xd''
        Complex yInternal(0.0, -1.0 / xdpp);
        triplets.emplace_back(busIdx, busIdx, yInternal);
    }

    const int n = static_cast<int>(nBuses_);
    SpMatrixC ybusInt(n, n);
    ybusInt.setFromTriplets(triplets.begin(), triplets.end());
    ybusInt.makeCompressed();
    return ybusInt;
}

// ============================================================================
// COMPUTE ELECTRICAL POWER
// ============================================================================

DenseVector StabilitySolver::computeElectricalPower(
    const DenseVector& rotorAngles,
    const std::vector<size_t>& genBusIndices,
    const SpMatrixC& ybusReduced
) {
    const size_t ng = genBusIndices.size();
    DenseVector pe(ng);

    // Build voltage vector from rotor angles (|E'| = 1.0 pu classical model)
    const int n = static_cast<int>(nBuses_);
    DenseVectorC v(n);
    v.setZero();
    for (size_t g = 0; g < ng; ++g) {
        int busIdx = static_cast<int>(genBusIndices[g]);
        v(busIdx) = Complex(std::cos(rotorAngles(g)), std::sin(rotorAngles(g)));
    }

    // I = Ybus * V
    DenseVectorC i = ybusReduced * v;

    // Pe = Re(E' * conj(I)) for each generator
    for (size_t g = 0; g < ng; ++g) {
        int busIdx = static_cast<int>(genBusIndices[g]);
        Complex eg = v(busIdx);
        Complex ig = i(busIdx);
        pe(g) = (eg * std::conj(ig)).real();
    }

    return pe;
}

// ============================================================================
// TRANSIENT STABILITY
// ============================================================================

std::vector<TransientResult> StabilitySolver::transientStability(
    size_t faultBusId,
    double faultClearingTime_s,
    double totalSimTime_s,
    double timeStep_s
) {
    if (timeStep_s <= 0.0) {
        throw std::invalid_argument("Time step must be positive");
    }
    if (faultClearingTime_s < 0.0 || faultClearingTime_s > totalSimTime_s) {
        throw std::invalid_argument("Invalid fault clearing time");
    }

    const auto& generators = system_.getGenerators();
    const auto& buses = system_.getBuses();

    // Filter active generators
    std::vector<Generator> activeGens;
    std::vector<size_t> genBusIndices;
    for (const auto& gen : generators) {
        if (gen.status == 1 && gen.busId > 0 && gen.busId <= nBuses_) {
            activeGens.push_back(gen);
            genBusIndices.push_back(gen.busId - 1);
        }
    }
    const size_t ng = activeGens.size();
    if (ng == 0) {
        throw std::runtime_error("No active generators for transient stability");
    }

    // Build reduced Ybus (normal and during fault)
    if (!system_.hasYbus()) {
        throw std::runtime_error("Ybus not built");
    }

    const SpMatrixC& ybusNormal = system_.getYbus();

    // Create fault Ybus by removing the faulted bus (grounding it)
    SpMatrixC ybusFault = ybusNormal;
    {
        const int fIdx = static_cast<int>(faultBusId - 1);
        // Set fault bus row/col to large admittance (grounded)
        std::vector<TripletC> faultTriplets;
        for (int k = 0; k < ybusNormal.outerSize(); ++k) {
            for (SpMatrixC::InnerIterator it(ybusNormal, k); it; ++it) {
                if (it.row() == fIdx || it.col() == fIdx) {
                    faultTriplets.emplace_back(it.row(), it.col(), Complex(0.0, 0.0));
                } else {
                    faultTriplets.emplace_back(it.row(), it.col(), it.value());
                }
            }
        }
        // Add large grounding admittance at fault bus
        faultTriplets.emplace_back(fIdx, fIdx, Complex(1e6, 0.0));
        ybusFault.setFromTriplets(faultTriplets.begin(), faultTriplets.end());
        ybusFault.makeCompressed();
    }

    // Initial state: pre-fault equilibrium
    // delta0 from power flow results, omega0 = 0
    DenseVector delta(ng);
    DenseVector omega(ng);
    DenseVector pm(ng);  // Mechanical power = initial electrical power

    for (size_t g = 0; g < ng; ++g) {
        delta(g) = buses[genBusIndices[g]].va_rad;
        omega(g) = 0.0; // Speed deviation
        pm(g) = activeGens[g].pg_pu;
    }

    // Store results
    std::vector<TransientResult> results;
    int nSteps = static_cast<int>(totalSimTime_s / timeStep_s);

    bool transientStable = true;
    double criticalAngleThreshold = PI; // Rough stability limit (180 degrees)

    for (int step = 0; step <= nSteps; ++step) {
        double t = step * timeStep_s;

        TransientResult tr;
        tr.time_s = t;

        // Choose Ybus based on fault status
        const SpMatrixC& ybus = (t < faultClearingTime_s) ? ybusFault : ybusNormal;

        // Compute electrical power outputs
        DenseVector pe = computeElectricalPower(delta, genBusIndices, ybus);

        // Store state
        for (size_t g = 0; g < ng; ++g) {
            tr.rotorAngles_deg[activeGens[g].id] = delta(g) * RAD_TO_DEG;
            tr.rotorSpeeds_pu[activeGens[g].id] = 1.0 + omega(g) / TWO_PI / FREQ_DEFAULT;
        }

        // Store voltages at all buses
        DenseVectorC vFull = system_.getComplexVoltages();
        for (size_t i = 0; i < nBuses_; ++i) {
            tr.voltages_pu[buses[i].id] = std::abs(vFull(static_cast<Eigen::Index>(i)));
        }

        // Check stability: relative angle separation
        if (ng >= 2) {
            double maxDelta = 0.0;
            for (size_t i = 0; i < ng; ++i) {
                for (size_t j = i + 1; j < ng; ++j) {
                    double d = std::abs(delta(i) - delta(j));
                    if (d > maxDelta) maxDelta = d;
                }
            }
            if (maxDelta > criticalAngleThreshold) {
                transientStable = false;
                tr.stable = false;
                results.push_back(tr);
                break;
            }
        }

        tr.stable = transientStable;
        results.push_back(tr);

        // Integrate using modified Euler (predictor-corrector)
        // Swing equation:
        // ddelta/dt = omega
        // domega/dt = (Pm - Pe - D*omega) / M  where M = 2H/ws

        // Step 1: Predictor
        DenseVector k1_delta(ng), k1_omega(ng);
        for (size_t g = 0; g < ng; ++g) {
            double m = 2.0 * activeGens[g].h_inertia_s / TWO_PI / FREQ_DEFAULT;
            double d = activeGens[g].d_damping;

            k1_delta(g) = omega(g);
            k1_omega(g) = (pm(g) - pe(g) - d * omega(g)) / m;
        }

        DenseVector deltaPred = delta + timeStep_s * k1_delta;
        DenseVector omegaPred = omega + timeStep_s * k1_omega;

        // Step 2: Corrector
        DenseVector pePred = computeElectricalPower(deltaPred, genBusIndices, ybus);
        DenseVector k2_delta(ng), k2_omega(ng);
        for (size_t g = 0; g < ng; ++g) {
            double m = 2.0 * activeGens[g].h_inertia_s / TWO_PI / FREQ_DEFAULT;
            double d = activeGens[g].d_damping;

            k2_delta(g) = omegaPred(g);
            k2_omega(g) = (pm(g) - pePred(g) - d * omegaPred(g)) / m;
        }

        // Update state
        delta += 0.5 * timeStep_s * (k1_delta + k2_delta);
        omega += 0.5 * timeStep_s * (k1_omega + k2_omega);
    }

    // Update final stability status
    for (auto& tr : results) {
        tr.stable = transientStable;
    }

    return results;
}

// ============================================================================
// SMALL-SIGNAL STABILITY
// ============================================================================

StabilityResult StabilitySolver::smallSignalStability() {
    StabilityResult result;

    try {
        DenseMatrix stateMatrix = buildStateMatrix();
        result.eigenvalues = calculateEigenvalues(stateMatrix);

        result.smallSignalStable = true;
        for (const auto& ev : result.eigenvalues) {
            if (!ev.stable) {
                result.smallSignalStable = false;
                break;
            }
        }

        result.transientStable = true; // Not computed here
        result.message = result.smallSignalStable
            ? "Small-signal stable: all eigenvalues have negative real parts"
            : "Small-signal unstable: at least one eigenvalue has positive real part";

    } catch (const std::exception& e) {
        result.smallSignalStable = false;
        result.message = std::string("Small-signal analysis failed: ") + e.what();
    }

    return result;
}

// ============================================================================
// STATE MATRIX
// ============================================================================

DenseMatrix StabilitySolver::buildStateMatrix() {
    const auto& generators = system_.getGenerators();

    // Filter active generators
    std::vector<Generator> activeGens;
    for (const auto& gen : generators) {
        if (gen.status == 1) activeGens.push_back(gen);
    }
    const size_t ng = activeGens.size();

    if (ng == 0) {
        throw std::runtime_error("No active generators for state matrix");
    }

    // State vector: [delta_1, ..., delta_ng, omega_1, ..., omega_ng]^T
    const int nStates = static_cast<int>(2 * ng);
    DenseMatrix A(nStates, nStates);
    A.setZero();

    // Inertia coefficients M_i = 2*H_i / ws where ws = 2*pi*f
    const double ws = TWO_PI * FREQ_DEFAULT;

    // Build synchronizing torque coefficient matrix K
    // For classical model: K_ij = |Ei|*|Ej|*|Yij|*cos(delta_i - delta_j - theta_ij)
    // Simplified: K_ii = -sum_j K_ij, K_ij = |Ei*Ej*Yij| for i != j

    DenseMatrix kSync(ng, ng);
    kSync.setZero();

    if (!system_.hasYbus()) {
        throw std::runtime_error("Ybus not built for state matrix");
    }

    const SpMatrixC& ybus = system_.getYbus();
    const auto& buses = system_.getBuses();

    for (size_t i = 0; i < ng; ++i) {
        for (size_t j = 0; j < ng; ++j) {
            if (i == j) continue;
            size_t bi = activeGens[i].busId - 1;
            size_t bj = activeGens[j].busId - 1;
            if (bi >= nBuses_ || bj >= nBuses_) continue;

            Complex yij = ybus.coeff(static_cast<Eigen::Index>(bi), static_cast<Eigen::Index>(bj));
            double eMag = activeGens[i].vmSet_pu * activeGens[j].vmSet_pu;
            double di = buses[bi].va_rad;
            double dj = buses[bj].va_rad;
            double dd = di - dj;

            kSync(i, j) = eMag * std::abs(yij) * std::cos(dd - std::arg(yij));
        }
    }

    // Diagonal elements
    for (size_t i = 0; i < ng; ++i) {
        double kDiag = 0.0;
        for (size_t j = 0; j < ng; ++j) {
            if (i != j) kDiag -= kSync(i, j);
        }
        kSync(i, i) = kDiag;
    }

    // Build state matrix A = [0, I; -M^-1*K, -M^-1*D]
    // Top-left: 0
    // Top-right: Identity
    for (size_t i = 0; i < ng; ++i) {
        A(static_cast<Eigen::Index>(i), static_cast<Eigen::Index>(ng + i)) = 1.0;
    }

    // Bottom-left: -M^-1 * K
    // Bottom-right: -M^-1 * D
    for (size_t i = 0; i < ng; ++i) {
        double m = 2.0 * activeGens[i].h_inertia_s / ws;
        double d = activeGens[i].d_damping;
        if (std::abs(m) < ZERO_IMPEDANCE_THRESHOLD) m = 1.0;

        for (size_t j = 0; j < ng; ++j) {
            A(static_cast<Eigen::Index>(ng + i), static_cast<Eigen::Index>(j)) =
                -kSync(i, j) / m;
        }
        A(static_cast<Eigen::Index>(ng + i), static_cast<Eigen::Index>(ng + i)) =
            -d / m;
    }

    return A;
}

// ============================================================================
// EIGENVALUE ANALYSIS
// ============================================================================

std::vector<EigenvalueResult> StabilitySolver::calculateEigenvalues(
    const DenseMatrix& stateMatrix
) {
    std::vector<EigenvalueResult> results;

    Eigen::ComplexEigenSolver<DenseMatrix> solver(stateMatrix);
    if (solver.info() != Eigen::Success) {
        throw std::runtime_error("Eigenvalue computation failed");
    }

    auto eigenvalues = solver.eigenvalues();
    const int n = eigenvalues.size();

    for (int i = 0; i < n; ++i) {
        EigenvalueResult evr;
        evr.value = eigenvalues(i);

        double sigma = evr.value.real();   // Real part
        double wd = evr.value.imag();      // Damping frequency [rad/s]

        // Damping ratio: zeta = -sigma / sqrt(sigma^2 + wd^2)
        double magnitude = std::abs(evr.value);
        if (magnitude > ZERO_IMPEDANCE_THRESHOLD) {
            evr.dampingRatio = -sigma / magnitude;
        } else {
            evr.dampingRatio = 1.0;
        }

        // Oscillation frequency in Hz
        evr.frequency_Hz = std::abs(wd) / TWO_PI;

        // Stable if real part < 0
        evr.stable = (sigma < -1e-8);

        // Classify mode
        if (evr.frequency_Hz > 0.1 && evr.frequency_Hz < 2.5) {
            evr.modeDescription = "Local mode";
        } else if (evr.frequency_Hz >= 0.01 && evr.frequency_Hz <= 0.1) {
            evr.modeDescription = "Interarea mode";
        } else if (std::abs(wd) < 0.01) {
            evr.modeDescription = "Aperiodic mode";
        } else {
            evr.modeDescription = "High frequency mode";
        }

        results.push_back(evr);
    }

    // Sort by damping ratio (least damped first)
    std::sort(results.begin(), results.end(),
        [](const EigenvalueResult& a, const EigenvalueResult& b) {
            return a.dampingRatio < b.dampingRatio;
        });

    return results;
}

} // namespace powsys365
