#include "powsy365/short_circuit.h"
#include <cmath>
#include <stdexcept>

namespace powsys365 {

// ============================================================================
// CONSTRUCTOR
// ============================================================================

ShortCircuitSolver::ShortCircuitSolver(const PowerSystem& system)
    : system_(system), baseMVA_(system.getBaseMVA()), nBuses_(system.numBuses()) {}

// ============================================================================
// BUILD FAULT YBUS (with subtransient reactances)
// ============================================================================

SpMatrixC ShortCircuitSolver::buildFaultYbus() {
    // Start from operational Ybus but replace generator impedances
    // with subtransient reactances Xd''
    if (!system_.hasYbus()) {
        throw std::runtime_error("ShortCircuitSolver: Ybus not built");
    }

    const SpMatrixC& ybusOp = system_.getYbus();
    const auto& generators = system_.getGenerators();
    const int n = static_cast<int>(nBuses_);

    // Get triplets from operational Ybus
    std::vector<TripletC> triplets;
    for (int k = 0; k < ybusOp.outerSize(); ++k) {
        for (SpMatrixC::InnerIterator it(ybusOp, k); it; ++it) {
            triplets.emplace_back(it.row(), it.col(), it.value());
        }
    }

    // Modify generator self-admittances to use subtransient reactance
    for (const auto& gen : generators) {
        if (gen.status != 1 || gen.busId == 0 || gen.busId > nBuses_) continue;

        const int busIdx = static_cast<int>(gen.busId - 1);
        const double xdPrimePrime = gen.xdDoublePrime_pu;
        if (xdPrimePrime < ZERO_IMPEDANCE_THRESHOLD) continue;

        // Generator subtransient admittance: Y = 1 / (j*Xd'')
        // Z = R + j*Xd'' where R is typically very small (Rs)
        const double rs = gen.rs_pu > 0 ? gen.rs_pu : 0.001;
        const double z2 = rs * rs + xdPrimePrime * xdPrimePrime;
        const Complex yGen(rs / z2, -xdPrimePrime / z2);

        // Add generator subtransient admittance to diagonal
        // (network already has the operational admittance, we add the difference)
        triplets.emplace_back(busIdx, busIdx, yGen);
    }

    SpMatrixC ybusFault(n, n);
    ybusFault.setFromTriplets(triplets.begin(), triplets.end());
    ybusFault.makeCompressed();
    return ybusFault;
}

// ============================================================================
// SEQUENCE NETWORKS
// ============================================================================

SpMatrixC ShortCircuitSolver::buildPositiveSequenceNetwork() {
    return buildFaultYbus();
}

SpMatrixC ShortCircuitSolver::buildNegativeSequenceNetwork() {
    const int n = static_cast<int>(nBuses_);
    std::vector<TripletC> triplets;
    const auto& lines = system_.getLines();
    const auto& transformers = system_.getTransformers();
    const auto& generators = system_.getGenerators();

    // Lines: same as positive sequence
    for (const auto& line : lines) {
        if (line.status != 1) continue;
        int fi = static_cast<int>(line.fromBus - 1);
        int ti = static_cast<int>(line.toBus - 1);
        if (fi < 0 || ti < 0 || fi >= n || ti >= n) continue;

        double r = line.r_pu;
        double x = line.x_pu;
        double z2 = r * r + x * x;
        if (z2 < ZERO_IMPEDANCE_THRESHOLD) continue;

        Complex y(r / z2, -x / z2);
        triplets.emplace_back(fi, ti, -y);
        triplets.emplace_back(ti, fi, -y);
        triplets.emplace_back(fi, fi, y);
        triplets.emplace_back(ti, ti, y);
    }

    // Transformers: same as positive sequence
    for (const auto& tx : transformers) {
        if (tx.status != 1) continue;
        int fi = static_cast<int>(tx.fromBus - 1);
        int ti = static_cast<int>(tx.toBus - 1);
        if (fi < 0 || ti < 0 || fi >= n || ti >= n) continue;

        double r = tx.r_pu;
        double x = tx.x_pu;
        double z2 = r * r + x * x;
        if (z2 < ZERO_IMPEDANCE_THRESHOLD) continue;

        Complex y(r / z2, -x / z2);
        triplets.emplace_back(fi, ti, -y);
        triplets.emplace_back(ti, fi, -y);
        triplets.emplace_back(fi, fi, y);
        triplets.emplace_back(ti, ti, y);
    }

    // Generators: use negative sequence reactance X2 (≈ Xq'')
    for (const auto& gen : generators) {
        if (gen.status != 1 || gen.busId == 0 || gen.busId > nBuses_) continue;
        int busIdx = static_cast<int>(gen.busId - 1);
        double x2 = gen.xqDoublePrime_pu > ZERO_IMPEDANCE_THRESHOLD
                        ? gen.xqDoublePrime_pu
                        : gen.xdDoublePrime_pu;
        if (x2 < ZERO_IMPEDANCE_THRESHOLD) continue;

        double rs = gen.rs_pu > 0 ? gen.rs_pu : 0.001;
        double z2 = rs * rs + x2 * x2;
        Complex y2(rs / z2, -x2 / z2);
        triplets.emplace_back(busIdx, busIdx, y2);
    }

    SpMatrixC y2(n, n);
    y2.setFromTriplets(triplets.begin(), triplets.end());
    y2.makeCompressed();
    return y2;
}

SpMatrixC ShortCircuitSolver::buildZeroSequenceNetwork() {
    const int n = static_cast<int>(nBuses_);
    std::vector<TripletC> triplets;
    const auto& lines = system_.getLines();
    const auto& transformers = system_.getTransformers();
    const auto& generators = system_.getGenerators();

    // Lines: zero-sequence impedance ≈ 3x positive-sequence
    // (accounts for ground return path)
    for (const auto& line : lines) {
        if (line.status != 1) continue;
        int fi = static_cast<int>(line.fromBus - 1);
        int ti = static_cast<int>(line.toBus - 1);
        if (fi < 0 || ti < 0 || fi >= n || ti >= n) continue;

        double r0 = 3.0 * line.r_pu;
        double x0 = 3.0 * line.x_pu;
        double z02 = r0 * r0 + x0 * x0;
        if (z02 < ZERO_IMPEDANCE_THRESHOLD) continue;

        Complex y0(r0 / z02, -x0 / z02);
        triplets.emplace_back(fi, ti, -y0);
        triplets.emplace_back(ti, fi, -y0);
        triplets.emplace_back(fi, fi, y0);
        triplets.emplace_back(ti, ti, y0);

        // Zero-sequence charging (typically small)
        if (std::abs(line.bch_pu) > ZERO_IMPEDANCE_THRESHOLD) {
            Complex b0(0.0, line.bch_pu * 0.5);
            triplets.emplace_back(fi, fi, b0);
            triplets.emplace_back(ti, ti, b0);
        }
    }

    // Transformers: zero-sequence depends on winding connection
    // Simplified: treat same as positive sequence but with 3x impedance
    for (const auto& tx : transformers) {
        if (tx.status != 1) continue;
        int fi = static_cast<int>(tx.fromBus - 1);
        int ti = static_cast<int>(tx.toBus - 1);
        if (fi < 0 || ti < 0 || fi >= n || ti >= n) continue;

        double r0 = 3.0 * tx.r_pu;
        double x0 = 3.0 * tx.x_pu;
        double z02 = r0 * r0 + x0 * x0;
        if (z02 < ZERO_IMPEDANCE_THRESHOLD) continue;

        Complex y0(r0 / z02, -x0 / z02);
        triplets.emplace_back(fi, ti, -y0);
        triplets.emplace_back(ti, fi, -y0);
        triplets.emplace_back(fi, fi, y0);
        triplets.emplace_back(ti, ti, y0);
    }

    // Generators: zero-sequence reactance X0
    for (const auto& gen : generators) {
        if (gen.status != 1 || gen.busId == 0 || gen.busId > nBuses_) continue;
        int busIdx = static_cast<int>(gen.busId - 1);
        // X0 typically 0.1-0.7 * Xd for grounded generators
        double x0 = 0.15 * gen.xd_pu;
        if (x0 < ZERO_IMPEDANCE_THRESHOLD) continue;

        double rs = gen.rs_pu > 0 ? gen.rs_pu : 0.001;
        double z02 = rs * rs + x0 * x0;
        Complex y0(rs / z02, -x0 / z02);
        triplets.emplace_back(busIdx, busIdx, y0);
    }

    SpMatrixC y0(n, n);
    y0.setFromTriplets(triplets.begin(), triplets.end());
    y0.makeCompressed();
    return y0;
}

// ============================================================================
// THEVENIN IMPEDANCE
// ============================================================================

Complex ShortCircuitSolver::calculateTheveninImpedance(
    const SpMatrixC& ybus,
    size_t busId
) {
    const int n = static_cast<int>(nBuses_);
    const int faultIdx = static_cast<int>(busId - 1);

    if (faultIdx < 0 || faultIdx >= n) {
        throw std::invalid_argument("Invalid fault bus ID");
    }

    // Zth = V_fault when injecting 1A at fault bus (all other sources zeroed)
    // Solve Ybus * V = I where I(fault) = 1, I(other) = 0
    DenseVectorC rhs(n);
    rhs.setZero();
    rhs(faultIdx) = Complex(1.0, 0.0);

    Eigen::SparseLU<SpMatrixC, Eigen::COLAMDOrdering<int>> solver;
    solver.compute(ybus);
    if (solver.info() != Eigen::Success) {
        throw std::runtime_error("Failed to factorize Ybus for Thevenin impedance");
    }

    DenseVectorC v = solver.solve(rhs);
    if (solver.info() != Eigen::Success) {
        throw std::runtime_error("Failed to solve for Thevenin impedance");
    }

    return v(faultIdx);
}

// ============================================================================
// THREE-PHASE FAULT
// ============================================================================

ShortCircuitResult ShortCircuitSolver::calculateThreePhaseFault(
    size_t faultBusId,
    double faultImpedance_pu
) {
    ShortCircuitResult result;
    result.faultType = FaultType::ThreePhase;
    result.faultBusId = faultBusId;
    result.faultImpedance_pu = faultImpedance_pu;

    if (faultBusId == 0 || faultBusId > nBuses_) {
        result.message = "Invalid fault bus ID: " + std::to_string(faultBusId);
        return result;
    }

    try {
        SpMatrixC y1 = buildPositiveSequenceNetwork();
        Complex zth = calculateTheveninImpedance(y1, faultBusId);

        // Ik'' = Vprefault / (Zth + Zf)  (Vprefault ≈ 1.0 pu)
        const Complex zf(faultImpedance_pu, 0.0);
        const Complex zTotal = zth + zf;

        if (std::abs(zTotal) < ZERO_IMPEDANCE_THRESHOLD) {
            result.message = "Total impedance near zero";
            return result;
        }

        const double vPre = 1.0; // Pre-fault voltage
        result.ik_pu = vPre / std::abs(zTotal);

        // R/X ratio for peak current calculation
        double rxRatio = zTotal.real() / std::abs(zTotal.imag());
        rxRatio = std::max(rxRatio, MIN_X_R_RATIO);

        result.ip_pu = calculatePeakCurrent(result.ik_pu, rxRatio);
        result.ib_pu = result.ik_pu; // Simplified: Ib ≈ Ik'' for far-from-generator faults
        result.sk_pu = vPre * result.ik_pu; // Sk = Un * Ik'' (Un = 1 pu)

        // Calculate voltages during fault at all buses
        const int n = static_cast<int>(nBuses_);
        DenseVectorC rhs(n);
        rhs.setZero();
        rhs(static_cast<Eigen::Index>(faultBusId - 1)) = Complex(1.0, 0.0) / zTotal;

        Eigen::SparseLU<SpMatrixC, Eigen::COLAMDOrdering<int>> solver;
        solver.compute(y1);
        DenseVectorC vDuringFault = solver.solve(rhs);

        const auto& buses = system_.getBuses();
        for (int i = 0; i < n; ++i) {
            ShortCircuitBusResult br;
            br.busId = buses[static_cast<size_t>(i)].id;
            br.busName = buses[static_cast<size_t>(i)].name;
            br.voltageDuringFault_pu = vDuringFault(i);

            // Fault current at fault bus
            if (static_cast<size_t>(i) == faultBusId - 1) {
                br.faultCurrent_pu = result.ik_pu;
                br.faultMVA_pu = result.sk_pu;
                br.faultMVA_MVA = result.sk_pu * baseMVA_;
            }

            result.busResults.push_back(br);
        }

        // Source contributions
        result.sourceContributions = calculateContributions(y1, faultBusId);
        result.message = "Three-phase fault calculated successfully";

    } catch (const std::exception& e) {
        result.message = std::string("Three-phase fault calculation failed: ") + e.what();
    }

    return result;
}

// ============================================================================
// UNSYMMETRICAL FAULTS
// ============================================================================

ShortCircuitResult ShortCircuitSolver::calculateSinglePhaseFault(
    size_t faultBusId,
    double faultImpedance_pu
) {
    ShortCircuitResult result;
    result.faultType = FaultType::SinglePhase;
    result.faultBusId = faultBusId;
    result.faultImpedance_pu = faultImpedance_pu;

    if (faultBusId == 0 || faultBusId > nBuses_) {
        result.message = "Invalid fault bus ID";
        return result;
    }

    try {
        SpMatrixC y1 = buildPositiveSequenceNetwork();
        SpMatrixC y2 = buildNegativeSequenceNetwork();
        SpMatrixC y0 = buildZeroSequenceNetwork();

        Complex z1 = calculateTheveninImpedance(y1, faultBusId);
        Complex z2 = calculateTheveninImpedance(y2, faultBusId);
        Complex z0 = calculateTheveninImpedance(y0, faultBusId);
        Complex zf(faultImpedance_pu, 0.0);

        // Single-phase-to-ground: Ia0 = Ia1 = Ia2 = Vf / (Z1 + Z2 + Z0 + 3*Zf)
        Complex zSeq = z1 + z2 + z0 + 3.0 * zf;
        if (std::abs(zSeq) < ZERO_IMPEDANCE_THRESHOLD) {
            result.message = "Sequence impedance near zero";
            return result;
        }

        Complex i1 = 1.0 / zSeq; // Positive sequence current
        double iFault = 3.0 * std::abs(i1); // Ia = 3*Ia1

        result.ik_pu = iFault;
        result.sk_pu = 1.0 * iFault;

        double rxRatio = z1.real() / std::abs(z1.imag());
        rxRatio = std::max(rxRatio, MIN_X_R_RATIO);
        result.ip_pu = calculatePeakCurrent(result.ik_pu, rxRatio);
        result.ib_pu = result.ik_pu;

        result.message = "Single-phase-to-ground fault calculated";

        // Bus voltages
        const auto& buses = system_.getBuses();
        for (size_t i = 0; i < nBuses_; ++i) {
            ShortCircuitBusResult br;
            br.busId = buses[i].id;
            br.busName = buses[i].name;
            if (i == faultBusId - 1) {
                br.faultCurrent_pu = iFault;
                br.faultMVA_pu = result.sk_pu;
                br.faultMVA_MVA = result.sk_pu * baseMVA_;
            }
            result.busResults.push_back(br);
        }

    } catch (const std::exception& e) {
        result.message = std::string("Single-phase fault failed: ") + e.what();
    }

    return result;
}

ShortCircuitResult ShortCircuitSolver::calculateTwoPhaseFault(
    size_t faultBusId,
    double faultImpedance_pu
) {
    ShortCircuitResult result;
    result.faultType = FaultType::TwoPhase;
    result.faultBusId = faultBusId;

    if (faultBusId == 0 || faultBusId > nBuses_) {
        result.message = "Invalid fault bus ID";
        return result;
    }

    try {
        SpMatrixC y1 = buildPositiveSequenceNetwork();
        SpMatrixC y2 = buildNegativeSequenceNetwork();

        Complex z1 = calculateTheveninImpedance(y1, faultBusId);
        Complex z2 = calculateTheveninImpedance(y2, faultBusId);
        Complex zf(faultImpedance_pu, 0.0);

        // Phase-to-phase: Ia0 = 0, Ia1 = -Ia2 = Vf / (Z1 + Z2 + Zf)
        Complex zSeq = z1 + z2 + zf;
        if (std::abs(zSeq) < ZERO_IMPEDANCE_THRESHOLD) {
            result.message = "Sequence impedance near zero";
            return result;
        }

        Complex i1 = 1.0 / zSeq;
        double iFault = std::sqrt(3.0) * std::abs(i1);

        result.ik_pu = iFault;
        result.sk_pu = 1.0 * iFault;

        double rxRatio = z1.real() / std::abs(z1.imag());
        rxRatio = std::max(rxRatio, MIN_X_R_RATIO);
        result.ip_pu = calculatePeakCurrent(result.ik_pu, rxRatio);
        result.ib_pu = result.ik_pu;

        result.message = "Phase-to-phase fault calculated";

        const auto& buses = system_.getBuses();
        for (size_t i = 0; i < nBuses_; ++i) {
            ShortCircuitBusResult br;
            br.busId = buses[i].id;
            br.busName = buses[i].name;
            if (i == faultBusId - 1) {
                br.faultCurrent_pu = iFault;
                br.faultMVA_pu = result.sk_pu;
                br.faultMVA_MVA = result.sk_pu * baseMVA_;
            }
            result.busResults.push_back(br);
        }

    } catch (const std::exception& e) {
        result.message = std::string("Two-phase fault failed: ") + e.what();
    }

    return result;
}

ShortCircuitResult ShortCircuitSolver::calculateTwoPhaseGroundFault(
    size_t faultBusId,
    double faultImpedance_pu
) {
    ShortCircuitResult result;
    result.faultType = FaultType::TwoPhaseG;
    result.faultBusId = faultBusId;

    if (faultBusId == 0 || faultBusId > nBuses_) {
        result.message = "Invalid fault bus ID";
        return result;
    }

    try {
        SpMatrixC y1 = buildPositiveSequenceNetwork();
        SpMatrixC y2 = buildNegativeSequenceNetwork();
        SpMatrixC y0 = buildZeroSequenceNetwork();

        Complex z1 = calculateTheveninImpedance(y1, faultBusId);
        Complex z2 = calculateTheveninImpedance(y2, faultBusId);
        Complex z0 = calculateTheveninImpedance(y0, faultBusId);
        Complex zf(faultImpedance_pu, 0.0);

        // Two-phase-to-ground: sequence networks in parallel
        // Zeq = Z1 + (Z2 * (Z0 + 3Zf) / (Z2 + Z0 + 3Zf))
        Complex z0f = z0 + 3.0 * zf;
        Complex zParallel = z2 * z0f / (z2 + z0f);
        Complex zEq = z1 + zParallel;

        if (std::abs(zEq) < ZERO_IMPEDANCE_THRESHOLD) {
            result.message = "Equivalent impedance near zero";
            return result;
        }

        Complex i1 = 1.0 / zEq;
        // Ground fault current = 3 * I0 = 3 * (-I1 * Z2 / (Z2 + Z0 + 3Zf))
        Complex i0 = -i1 * z2 / (z2 + z0f);
        double iFault = 3.0 * std::abs(i0);

        result.ik_pu = iFault;
        result.sk_pu = 1.0 * iFault;

        double rxRatio = z1.real() / std::abs(z1.imag());
        rxRatio = std::max(rxRatio, MIN_X_R_RATIO);
        result.ip_pu = calculatePeakCurrent(result.ik_pu, rxRatio);
        result.ib_pu = result.ik_pu;

        result.message = "Two-phase-to-ground fault calculated";

        const auto& buses = system_.getBuses();
        for (size_t i = 0; i < nBuses_; ++i) {
            ShortCircuitBusResult br;
            br.busId = buses[i].id;
            br.busName = buses[i].name;
            if (i == faultBusId - 1) {
                br.faultCurrent_pu = iFault;
                br.faultMVA_pu = result.sk_pu;
                br.faultMVA_MVA = result.sk_pu * baseMVA_;
            }
            result.busResults.push_back(br);
        }

    } catch (const std::exception& e) {
        result.message = std::string("Two-phase-ground fault failed: ") + e.what();
    }

    return result;
}

ShortCircuitResult ShortCircuitSolver::calculateUnsymmetricalFault(
    FaultType type,
    size_t faultBusId,
    double faultImpedance_pu
) {
    switch (type) {
        case FaultType::SinglePhase:
        case FaultType::SinglePhaseG:
            return calculateSinglePhaseFault(faultBusId, faultImpedance_pu);
        case FaultType::TwoPhase:
            return calculateTwoPhaseFault(faultBusId, faultImpedance_pu);
        case FaultType::TwoPhaseG:
            return calculateTwoPhaseGroundFault(faultBusId, faultImpedance_pu);
        default:
            throw std::invalid_argument("Unsupported fault type for unsymmetrical calculation");
    }
}

// ============================================================================
// IEC 60909 QUANTITIES
// ============================================================================

double ShortCircuitSolver::calculateInitialCurrent(double zth_pu) const {
    if (zth_pu < ZERO_IMPEDANCE_THRESHOLD) return 0.0;
    return 1.0 / zth_pu; // Vprefault = 1.0 pu
}

double ShortCircuitSolver::calculatePeakCurrent(double ik_pu, double r_x_ratio) const {
    // kappa factor from IEC 60909
    // kappa ≈ 1.02 + 0.98 * exp(-3*R/X)
    double kappa = 1.02 + 0.98 * std::exp(-3.0 * r_x_ratio);
    return kappa * SQRT2 * ik_pu;
}

double ShortCircuitSolver::calculateBreakingCurrent(double ik_pu, double mu) const {
    return mu * ik_pu;
}

double ShortCircuitSolver::calculateShortCircuitPower(double ik_pu) const {
    return SQRT3 * ik_pu; // Sk = sqrt(3) * Un * Ik (Un = 1 pu)
}

// ============================================================================
// CONTRIBUTIONS
// ============================================================================

std::vector<std::pair<size_t, double>> ShortCircuitSolver::calculateContributions(
    const SpMatrixC& y1,
    size_t faultBusId
) {
    std::vector<std::pair<size_t, double>> contributions;
    const auto& generators = system_.getGenerators();

    for (const auto& gen : generators) {
        if (gen.status != 1) continue;

        // Approximate contribution: each generator contributes inversely
        // proportional to its subtransient reactance
        double xdp = gen.xdDoublePrime_pu;
        if (xdp < ZERO_IMPEDANCE_THRESHOLD) continue;

        double contribution = 1.0 / xdp;
        contributions.emplace_back(gen.busId, contribution);
    }

    // Normalize
    double total = 0.0;
    for (const auto& c : contributions) total += c.second;
    if (total > 0) {
        for (auto& c : contributions) c.second /= total;
    }

    return contributions;
}

} // namespace powsys365
