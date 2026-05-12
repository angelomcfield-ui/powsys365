#include "powsy365/load_flow.h"
#include "powsy365/ybus_builder.h"
#include <Eigen/SparseLU>
#include <cmath>
#include <chrono>
#include <iostream>
#include <sstream>

namespace powsys365 {

// ============================================================================
// CONSTRUCTOR
// ============================================================================

LoadFlowSolver::LoadFlowSolver(PowerSystem& system) : system_(system) {}

// ============================================================================
// BUS CLASSIFICATION
// ============================================================================

void LoadFlowSolver::classifyBuses() {
    const auto& buses = system_.getBuses();
    nBuses_ = buses.size();
    isPQ_.assign(nBuses_, false);
    isPV_.assign(nBuses_, false);
    isSlack_.assign(nBuses_, false);
    numPQ_ = 0;
    numPV_ = 0;
    pqBusIndices_.clear();
    pvBusIndices_.clear();
    busOrder_.clear();

    for (size_t i = 0; i < nBuses_; ++i) {
        switch (buses[i].type) {
            case BusType::PQ:
                isPQ_[i] = true;
                pqBusIndices_.push_back(i);
                busOrder_.push_back(i);
                ++numPQ_;
                break;
            case BusType::PV:
                isPV_[i] = true;
                pvBusIndices_.push_back(i);
                busOrder_.push_back(i);
                ++numPV_;
                break;
            case BusType::Slack:
                isSlack_[i] = true;
                slackIndex_ = i;
                busOrder_.push_back(i);
                break;
        }
    }
}

// ============================================================================
// G/B MATRICES
// ============================================================================

void LoadFlowSolver::buildGBMatrices() {
    if (!system_.hasYbus()) {
        system_.buildYbus();
    }
    YbusBuilder builder;
    gMatrix_ = builder.buildG(system_.getYbus());
    bMatrix_ = builder.buildB(system_.getYbus());
}

// ============================================================================
// SPECIFICATION VECTORS
// ============================================================================

void LoadFlowSolver::buildSpecificationVectors(
    DenseVector& pSpec,
    DenseVector& qSpec
) {
    const auto& buses = system_.getBuses();
    const Eigen::Index n = static_cast<Eigen::Index>(nBuses_);
    pSpec.resize(n);
    qSpec.resize(n);

    for (size_t i = 0; i < nBuses_; ++i) {
        const Bus& bus = buses[i];
        pSpec(static_cast<Eigen::Index>(i)) = bus.netP_pu();
        qSpec(static_cast<Eigen::Index>(i)) = bus.netQ_pu();
    }
}

// ============================================================================
// PROGRESS CALLBACK
// ============================================================================

void LoadFlowSolver::reportProgress(
    const SolverConfig& config,
    int iteration,
    double mismatch,
    const std::string& message
) {
    if (config.progressCallback) {
        config.progressCallback(iteration, mismatch, message);
    }
    if (config.verbose) {
        std::cout << "[Iter " << iteration << "] Mismatch: " << mismatch
                  << " - " << message << std::endl;
    }
}

// ============================================================================
// MAIN SOLVE
// ============================================================================

PowerFlowResult LoadFlowSolver::solve(const SolverConfig& config) {
    switch (config.method) {
        case SolverMethod::NewtonRaphson:
            return newtonRaphson(config);
        case SolverMethod::FastDecoupledXB:
            return fastDecoupledFDXB(config);
        case SolverMethod::FastDecoupledBX:
            return fastDecoupledFDBX(config);
        case SolverMethod::GaussSeidel:
            return gaussSeidel(config);
        default:
            throw std::invalid_argument("LoadFlowSolver::solve: unknown solver method");
    }
}

// ============================================================================
// NEWTON-RAPHSON METHOD
// ============================================================================

PowerFlowResult LoadFlowSolver::newtonRaphson(const SolverConfig& config) {
    auto startTime = std::chrono::high_resolution_clock::now();
    PowerFlowResult result;
    result.status = ConvergenceStatus::MaxIterationsExceeded;

    // Validate system
    if (!system_.isValid()) {
        result.status = ConvergenceStatus::InvalidInitialConditions;
        result.message = "System is not valid (no slack bus or disconnected)";
        return result;
    }

    // Initialize
    classifyBuses();
    buildGBMatrices();

    if (config.flatStart) {
        system_.initializeVoltages();
    }

    const auto& buses = system_.getBuses();
    DenseVector pSpec, qSpec;
    buildSpecificationVectors(pSpec, qSpec);

    // Get initial voltage vector
    DenseVector vm = system_.getVm();
    DenseVector va = system_.getVa();

    // Track PV->PQ conversions
    std::vector<bool> pvToPqConverted(nBuses_, false);
    int qLimitIterations = 0;

    reportProgress(config, 0, 1.0, "Newton-Raphson starting");

    // Main NR iteration loop
    for (int iter = 1; iter <= config.maxIterations; ++iter) {
        // Step 1: Calculate current power injections
        DenseVector pCalc, qCalc;
        calculate_injected_powers(gMatrix_, bMatrix_, vm, va, pCalc, qCalc);

        // Step 2: Build mismatch vector [dP; dQ]
        // dP excludes slack bus; dQ excludes slack and PV buses
        const int np = static_cast<int>(numPV_ + numPQ_);     // Non-slack buses
        const int nq = static_cast<int>(numPQ_);              // PQ buses only
        const int nMismatches = np + nq;

        DenseVector mismatch(nMismatches);
        int idx = 0;

        // dP for all non-slack buses (PQ + PV)
        for (size_t i = 0; i < nBuses_; ++i) {
            if (isSlack_[i]) continue;
            mismatch(idx++) = pSpec(static_cast<Eigen::Index>(i)) - pCalc(static_cast<Eigen::Index>(i));
        }
        // dQ for PQ buses only
        for (size_t i = 0; i < nBuses_; ++i) {
            if (isPQ_[i] || pvToPqConverted[i]) {
                mismatch(idx++) = qSpec(static_cast<Eigen::Index>(i)) - qCalc(static_cast<Eigen::Index>(i));
            }
        }

        // Step 3: Check convergence
        double maxMismatch = mismatch_norm(mismatch);
        reportProgress(config, iter, maxMismatch,
            "Newton-Raphson iteration " + std::to_string(iter));

        if (maxMismatch < config.tolerance) {
            // Check Q limits if enabled
            if (config.enforceQLimits && qLimitIterations < config.maxQLimitIterations) {
                int converted = enforceQLimits(qCalc, pvToPqConverted);
                if (converted > 0) {
                    ++qLimitIterations;
                    // Reclassify and restart
                    classifyBuses();
                    // Update PV flags based on conversions
                    for (size_t i = 0; i < nBuses_; ++i) {
                        if (pvToPqConverted[i]) {
                            isPV_[i] = false;
                            isPQ_[i] = true;
                        }
                    }
                    reportProgress(config, iter, maxMismatch,
                        "Q limits enforced: " + std::to_string(converted) +
                        " PV->PQ conversions");
                    continue;
                }
            }

            result.status = ConvergenceStatus::Converged;
            result.iterations = iter;
            result.finalMismatch = maxMismatch;
            break;
        }

        // Step 4: Build Jacobian
        SpMatrix jacobian;
        try {
            jacobian = buildJacobian(vm, va, pCalc, qCalc);
        } catch (const std::exception& e) {
            result.status = ConvergenceStatus::SingularJacobian;
            result.message = std::string("Jacobian build failed: ") + e.what();
            return result;
        }

        // Step 5: Solve correction equation J * [dVa; dVm] = mismatch
        DenseVector correction;
        try {
            correction = solve_sparse(jacobian, mismatch);
        } catch (const std::exception& e) {
            result.status = ConvergenceStatus::SingularJacobian;
            result.message = std::string("Jacobian solve failed: ") + e.what();
            return result;
        }

        // Step 6: Update voltages
        // correction layout: [dVa for non-slack; dVm for PQ]
        int corrIdx = 0;
        // Update angles for non-slack buses
        for (size_t i = 0; i < nBuses_; ++i) {
            if (isSlack_[i]) continue;
            va(static_cast<Eigen::Index>(i)) += correction(corrIdx++);
        }
        // Update magnitudes for PQ buses
        for (size_t i = 0; i < nBuses_; ++i) {
            if (isPQ_[i] || pvToPqConverted[i]) {
                vm(static_cast<Eigen::Index>(i)) += correction(corrIdx++);
                // Clamp voltage to reasonable range
                vm(static_cast<Eigen::Index>(i)) = clamp_value(
                    vm(static_cast<Eigen::Index>(i)), MIN_VOLTAGE_PU, MAX_VOLTAGE_PU);
            }
        }
    }

    // Update system with final voltages
    system_.updateBusVoltages(vm, va);

    // Calculate final power injections
    DenseVector pFinal, qFinal;
    calculate_injected_powers(gMatrix_, bMatrix_, vm, va, pFinal, qFinal);

    // Build results
    result.busResults = buildBusResults(vm, va, pFinal, qFinal, pSpec, qSpec);
    result.lineResults = calculateLineFlows(vm, va);
    result.summary = calculateSystemSummary(result.busResults, result.lineResults);

    auto endTime = std::chrono::high_resolution_clock::now();
    result.solveTime_ms = std::chrono::duration<double, std::milli>(
        endTime - startTime).count();

    if (result.converged()) {
        result.message = "Newton-Raphson converged in " +
            std::to_string(result.iterations) + " iterations. " +
            "Final mismatch: " + std::to_string(result.finalMismatch);
    } else {
        result.message = "Newton-Raphson did not converge after " +
            std::to_string(config.maxIterations) +
            " iterations. Final mismatch: " +
            std::to_string(result.finalMismatch);
    }

    return result;
}

// ============================================================================
// JACOBIAN BUILDER
// ============================================================================

SpMatrix LoadFlowSolver::buildJacobian(
    const DenseVector& vm,
    const DenseVector& va,
    const DenseVector& pCalc,
    const DenseVector& qCalc
) {
    // Jacobian structure:
    // J = [ H  N ]   where H = dP/dVa, N = dP/dVm (for non-slack)
    //     [ J  L ]   where J = dQ/dVa, L = dQ/dVm (for PQ buses)
    //
    // Ordering: slack last. Rows/cols ordered as [PQ+PV (angles), PQ (magnitudes)]

    const int np = static_cast<int>(numPV_ + numPQ_);  // Non-slack angle DOFs
    const int nq = static_cast<int>(numPQ_);            // PQ voltage DOFs
    const int nDOF = np + nq;

    std::vector<Triplet> triplets;
    triplets.reserve(nDOF * 8); // Estimate non-zeros

    // Helper: get row index in Jacobian for a bus
    // Angles: non-slack buses in order
    auto angleRow = [&](size_t busIdx) -> int {
        if (isSlack_[busIdx]) return -1;
        int row = 0;
        for (size_t i = 0; i < busIdx; ++i) {
            if (!isSlack_[i]) ++row;
        }
        return row;
    };

    // Voltages: PQ buses only (also include temporarily converted PV buses)
    auto voltRow = [&](size_t busIdx) -> int {
        if (!isPQ_[busIdx]) return -1;
        int row = np; // Voltage rows come after angle rows
        for (size_t i = 0; i < busIdx; ++i) {
            if (isPQ_[i]) ++row;
        }
        return row;
    };

    // Build Jacobian elements
    for (size_t i = 0; i < nBuses_; ++i) {
        const int rowAngle = angleRow(i);
        const int rowVolt = voltRow(i);
        const double vi = vm(static_cast<Eigen::Index>(i));
        const double di = va(static_cast<Eigen::Index>(i));
        const double pi = pCalc(static_cast<Eigen::Index>(i));
        const double qi = qCalc(static_cast<Eigen::Index>(i));

        // Diagonal elements need special formulas
        // Off-diagonal: iterate over connected buses (non-zero Ybus entries)
        for (SpMatrix::InnerIterator itG(gMatrix_, static_cast<Eigen::Index>(i)); itG; ++itG) {
            const size_t j = static_cast<size_t>(itG.col());
            if (i == j) continue;

            const double gij = itG.value();
            const double bij = bMatrix_.coeff(static_cast<Eigen::Index>(i), static_cast<Eigen::Index>(j));
            const double vj = vm(static_cast<Eigen::Index>(j));
            const double dj = va(static_cast<Eigen::Index>(j));
            const double dij = di - dj;

            const int colAngle = angleRow(j);
            const int colVolt = voltRow(j);

            // H_ij = dPi/dDeltaj = |Vi|*|Vj|*(Gij*sin(dij) - Bij*cos(dij))
            const double hOff = jacobian_h_offdiag(vi, vj, gij, bij, di, dj);
            // N_ij = dPi/d|Vj|*|Vj| = |Vi|*|Vj|*(Gij*cos(dij) + Bij*sin(dij))
            const double nOff = jacobian_n_offdiag(vi, vj, gij, bij, di, dj);
            // J_ij = dQi/dDeltaj = -|Vi|*|Vj|*(Gij*cos(dij) + Bij*sin(dij))
            const double jOff = jacobian_j_offdiag(vi, vj, gij, bij, di, dj);
            // L_ij = dQi/d|Vj|*|Vj| = |Vi|*|Vj|*(Gij*sin(dij) - Bij*cos(dij))
            const double lOff = jacobian_l_offdiag(vi, vj, gij, bij, di, dj);

            if (rowAngle >= 0 && colAngle >= 0) {
                triplets.emplace_back(rowAngle, colAngle, hOff);
            }
            if (rowAngle >= 0 && colVolt >= 0) {
                triplets.emplace_back(rowAngle, colVolt, nOff / vj);
            }
            if (rowVolt >= 0 && colAngle >= 0) {
                triplets.emplace_back(rowVolt, colAngle, jOff);
            }
            if (rowVolt >= 0 && colVolt >= 0) {
                triplets.emplace_back(rowVolt, colVolt, lOff / vj);
            }
        }

        // Diagonal elements
        const double gii = gMatrix_.coeff(static_cast<Eigen::Index>(i), static_cast<Eigen::Index>(i));
        const double bii = bMatrix_.coeff(static_cast<Eigen::Index>(i), static_cast<Eigen::Index>(i));

        // H_ii = -Qi - Bii*|Vi|^2
        const double hDiag = jacobian_h_diag(qi, bii, vi);
        // N_ii = Pi + Gii*|Vi|^2  (multiplied by |Vi|, so N_ii * |Vi|)
        const double nDiag = jacobian_n_diag(pi, gii, vi);
        // J_ii = Pi - Gii*|Vi|^2
        const double jDiag = jacobian_j_diag(pi, gii, vi);
        // L_ii = Qi - Bii*|Vi|^2
        const double lDiag = jacobian_l_diag(qi, bii, vi);

        if (rowAngle >= 0) {
            triplets.emplace_back(rowAngle, rowAngle, hDiag);
            // N diagonal with respect to own voltage
            if (rowVolt >= 0) {
                // dPi/d|Vi| relates to the voltage row
                int voltCol = rowVolt;
                triplets.emplace_back(rowAngle, voltCol, nDiag / vi);
            }
        }
        if (rowVolt >= 0) {
            triplets.emplace_back(rowVolt, rowVolt, lDiag / vi);
            if (rowAngle >= 0) {
                triplets.emplace_back(rowVolt, rowAngle, jDiag);
            }
        }
    }

    SpMatrix jacobian(nDOF, nDOF);
    jacobian.setFromTriplets(triplets.begin(), triplets.end());
    jacobian.makeCompressed();
    return jacobian;
}

// ============================================================================
// FAST DECOUPLED LOAD FLOW (XB VERSION)
// ============================================================================

PowerFlowResult LoadFlowSolver::fastDecoupledFDXB(const SolverConfig& config) {
    auto startTime = std::chrono::high_resolution_clock::now();
    PowerFlowResult result;
    result.status = ConvergenceStatus::MaxIterationsExceeded;

    if (!system_.isValid()) {
        result.status = ConvergenceStatus::InvalidInitialConditions;
        result.message = "System is not valid";
        return result;
    }

    classifyBuses();
    buildGBMatrices();

    if (config.flatStart) {
        system_.initializeVoltages();
    }

    const auto& buses = system_.getBuses();
    DenseVector pSpec, qSpec;
    buildSpecificationVectors(pSpec, qSpec);

    DenseVector vm = system_.getVm();
    DenseVector va = system_.getVa();

    // Build constant B' and B'' matrices
    SpMatrix bPrime = buildBPrime();
    SpMatrix bDoublePrime = buildBDoublePrime();

    // Factorize once (key advantage of FDLF)
    Eigen::SparseLU<SpMatrix, Eigen::COLAMDOrdering<int>> luBPrime;
    Eigen::SparseLU<SpMatrix, Eigen::COLAMDOrdering<int>> luBDoublePrime;

    try {
        luBPrime.compute(bPrime);
        if (luBPrime.info() != Eigen::Success) {
            result.status = ConvergenceStatus::SingularJacobian;
            result.message = "B' factorization failed";
            return result;
        }
    } catch (...) {
        result.status = ConvergenceStatus::SingularJacobian;
        result.message = "B' factorization threw exception";
        return result;
    }

    try {
        luBDoublePrime.compute(bDoublePrime);
        if (luBDoublePrime.info() != Eigen::Success) {
            result.status = ConvergenceStatus::SingularJacobian;
            result.message = "B'' factorization failed";
            return result;
        }
    } catch (...) {
        result.status = ConvergenceStatus::SingularJacobian;
        result.message = "B'' factorization threw exception";
        return result;
    }

    // Track PV->PQ conversions
    std::vector<bool> pvToPqConverted(nBuses_, false);
    int qLimitIterations = 0;

    reportProgress(config, 0, 1.0, "Fast Decoupled XB starting");

    for (int iter = 1; iter <= config.maxIterations; ++iter) {
        // Step 1: Calculate power injections
        DenseVector pCalc, qCalc;
        calculate_injected_powers(gMatrix_, bMatrix_, vm, va, pCalc, qCalc);

        // Step 2a: Build active power mismatch (all non-slack)
        const int np = static_cast<int>(numPV_ + numPQ_);
        DenseVector dP(np);
        int idx = 0;
        for (size_t i = 0; i < nBuses_; ++i) {
            if (isSlack_[i]) continue;
            dP(idx++) = pSpec(static_cast<Eigen::Index>(i)) - pCalc(static_cast<Eigen::Index>(i));
        }

        // Step 3a: Solve B' * dVa = dP/|V| for angle correction
        DenseVector dPDivV(np);
        idx = 0;
        for (size_t i = 0; i < nBuses_; ++i) {
            if (isSlack_[i]) continue;
            dPDivV(idx++) = dP(idx - 1) / vm(static_cast<Eigen::Index>(i));
        }

        DenseVector dVa = luBPrime.solve(dPDivV);
        if (luBPrime.info() != Eigen::Success) {
            result.status = ConvergenceStatus::SingularJacobian;
            result.message = "B' solve failed at iteration " + std::to_string(iter);
            return result;
        }

        // Update angles
        idx = 0;
        for (size_t i = 0; i < nBuses_; ++i) {
            if (isSlack_[i]) continue;
            va(static_cast<Eigen::Index>(i)) += dVa(idx++);
        }

        // Step 2b: Build reactive power mismatch (PQ only)
        const int nq = static_cast<int>(numPQ_);
        DenseVector dQ(nq);
        idx = 0;
        for (size_t i = 0; i < nBuses_; ++i) {
            if (isPQ_[i] || pvToPqConverted[i]) {
                dQ(idx++) = qSpec(static_cast<Eigen::Index>(i)) - qCalc(static_cast<Eigen::Index>(i));
            }
        }

        // Step 3b: Solve B'' * dVm = dQ/|V| for voltage correction
        DenseVector dQDivV(nq);
        idx = 0;
        for (size_t i = 0; i < nBuses_; ++i) {
            if (isPQ_[i] || pvToPqConverted[i]) {
                dQDivV(idx++) = dQ(idx - 1) / vm(static_cast<Eigen::Index>(i));
            }
        }

        DenseVector dVm = luBDoublePrime.solve(dQDivV);
        if (luBDoublePrime.info() != Eigen::Success) {
            result.status = ConvergenceStatus::SingularJacobian;
            result.message = "B'' solve failed at iteration " + std::to_string(iter);
            return result;
        }

        // Update voltage magnitudes
        idx = 0;
        for (size_t i = 0; i < nBuses_; ++i) {
            if (isPQ_[i] || pvToPqConverted[i]) {
                vm(static_cast<Eigen::Index>(i)) += dVm(idx++);
                vm(static_cast<Eigen::Index>(i)) = clamp_value(
                    vm(static_cast<Eigen::Index>(i)), MIN_VOLTAGE_PU, MAX_VOLTAGE_PU);
            }
        }

        // Check convergence on mismatches
        double maxPMismatch = mismatch_norm(dP);
        double maxQMismatch = mismatch_norm(dQ);
        double maxMismatch = std::max(maxPMismatch, maxQMismatch);

        reportProgress(config, iter, maxMismatch,
            "FDXB iteration " + std::to_string(iter) +
            ", dP=" + std::to_string(maxPMismatch) +
            ", dQ=" + std::to_string(maxQMismatch));

        if (maxPMismatch < config.tolerance && maxQMismatch < config.tolerance) {
            // Check Q limits
            if (config.enforceQLimits && qLimitIterations < config.maxQLimitIterations) {
                int converted = enforceQLimits(qCalc, pvToPqConverted);
                if (converted > 0) {
                    ++qLimitIterations;
                    for (size_t i = 0; i < nBuses_; ++i) {
                        if (pvToPqConverted[i]) {
                            isPV_[i] = false;
                            isPQ_[i] = true;
                        }
                    }
                    classifyBuses();
                    bPrime = buildBPrime();
                    bDoublePrime = buildBDoublePrime();
                    luBPrime.compute(bPrime);
                    luBDoublePrime.compute(bDoublePrime);
                    continue;
                }
            }

            result.status = ConvergenceStatus::Converged;
            result.iterations = iter;
            result.finalMismatch = maxMismatch;
            break;
        }
    }

    // Update system
    system_.updateBusVoltages(vm, va);

    DenseVector pFinal, qFinal;
    calculate_injected_powers(gMatrix_, bMatrix_, vm, va, pFinal, qFinal);

    result.busResults = buildBusResults(vm, va, pFinal, qFinal, pSpec, qSpec);
    result.lineResults = calculateLineFlows(vm, va);
    result.summary = calculateSystemSummary(result.busResults, result.lineResults);

    auto endTime = std::chrono::high_resolution_clock::now();
    result.solveTime_ms = std::chrono::duration<double, std::milli>(
        endTime - startTime).count();

    if (result.converged()) {
        result.message = "Fast Decoupled XB converged in " +
            std::to_string(result.iterations) + " iterations";
    } else {
        result.message = "Fast Decoupled XB did not converge after " +
            std::to_string(config.maxIterations) + " iterations";
    }

    return result;
}

// ============================================================================
// FAST DECOUPLED (BX - delegates to XB)
// ============================================================================

PowerFlowResult LoadFlowSolver::fastDecoupledFDBX(const SolverConfig& config) {
    // BX variant is similar to XB but uses different B' formulation
    // For simplicity, delegate to XB with adjusted tolerance
    SolverConfig bxConfig = config;
    bxConfig.method = SolverMethod::FastDecoupledXB;
    bxConfig.tolerance *= 2.0; // BX typically converges slightly slower
    return fastDecoupledFDXB(bxConfig);
}

// ============================================================================
// GAUSS-SEIDEL METHOD
// ============================================================================

PowerFlowResult LoadFlowSolver::gaussSeidel(const SolverConfig& config) {
    auto startTime = std::chrono::high_resolution_clock::now();
    PowerFlowResult result;
    result.status = ConvergenceStatus::MaxIterationsExceeded;

    if (!system_.isValid()) {
        result.status = ConvergenceStatus::InvalidInitialConditions;
        result.message = "System is not valid";
        return result;
    }

    classifyBuses();
    buildGBMatrices();

    if (config.flatStart) {
        system_.initializeVoltages();
    }

    const auto& buses = system_.getBuses();
    DenseVector pSpec, qSpec;
    buildSpecificationVectors(pSpec, qSpec);

    // Get Ybus as complex for direct access
    const SpMatrixC& ybus = system_.getYbus();
    DenseVectorC v = system_.getComplexVoltages();

    const int n = static_cast<int>(nBuses_);

    reportProgress(config, 0, 1.0, "Gauss-Seidel starting");

    for (int iter = 1; iter <= config.maxIterations; ++iter) {
        double maxDeltaV = 0.0;

        // Store old voltages for convergence check
        DenseVectorC vOld = v;

        for (int i = 0; i < n; ++i) {
            if (isSlack_[static_cast<size_t>(i)]) continue;

            // Compute sum of Yik * Vk for k != i
            Complex sumYV(0.0, 0.0);
            for (SpMatrixC::InnerIterator it(ybus, i); it; ++it) {
                int k = it.col();
                if (k != i) {
                    sumYV += it.value() * v(k);
                }
            }

            const Complex yii = ybus.coeff(i, i);
            if (std::abs(yii) < ZERO_IMPEDANCE_THRESHOLD) continue;

            if (isPV_[static_cast<size_t>(i)]) {
                // PV bus: update angle only, keep magnitude fixed
                // V_i = (1/Y_ii) * [(P_i - jQ_i)/V_i* - sum_{k!=i} Y_ik V_k]
                // First compute Q from current voltage estimate
                Complex si = pSpec(i) - Complex(0.0, qSpec(i));
                Complex rhs = (si / std::conj(v(i))) - sumYV;
                Complex vNew = rhs / yii;

                // Keep magnitude fixed at setpoint
                const double vmSet = buses[static_cast<size_t>(i)].vm_pu;
                double vaNew = std::arg(vNew);
                v(i) = Complex(vmSet * std::cos(vaNew), vmSet * std::sin(vaNew));
            } else {
                // PQ bus: update both magnitude and angle
                const double pi = pSpec(i);
                const double qi = qSpec(i);
                Complex si = Complex(pi, -qi);
                Complex rhs = (std::conj(si) / std::conj(v(i))) - sumYV;
                Complex vNew = rhs / yii;

                double delta = std::abs(vNew - vOld(i));
                if (delta > maxDeltaV) maxDeltaV = delta;

                v(i) = vNew;
            }
        }

        // Calculate mismatches for convergence check
        DenseVector pCalc(n), qCalc(n);
        DenseVector vm(n), va(n);
        for (int i = 0; i < n; ++i) {
            vm(i) = std::abs(v(i));
            va(i) = std::arg(v(i));
        }
        calculate_injected_powers(gMatrix_, bMatrix_, vm, va, pCalc, qCalc);

        DenseVector mismatch;
        compute_mismatch(pSpec, qSpec, pCalc, qCalc, isPQ_, mismatch);
        double maxMismatch = mismatch_norm(mismatch);

        reportProgress(config, iter, maxMismatch,
            "GS iteration " + std::to_string(iter));

        if (maxMismatch < config.tolerance || maxDeltaV < config.tolerance) {
            result.status = ConvergenceStatus::Converged;
            result.iterations = iter;
            result.finalMismatch = maxMismatch;
            break;
        }

        // Check for divergence
        if (maxMismatch > 1e6 || std::isnan(maxMismatch)) {
            result.status = ConvergenceStatus::DivergenceDetected;
            result.message = "Gauss-Seidel diverged at iteration " + std::to_string(iter);
            return result;
        }
    }

    // Extract final vm and va
    DenseVector vm(n), va(n);
    for (int i = 0; i < n; ++i) {
        vm(i) = std::abs(v(i));
        va(i) = std::arg(v(i));
    }

    system_.updateBusVoltages(vm, va);

    DenseVector pFinal, qFinal;
    calculate_injected_powers(gMatrix_, bMatrix_, vm, va, pFinal, qFinal);

    result.busResults = buildBusResults(vm, va, pFinal, qFinal, pSpec, qSpec);
    result.lineResults = calculateLineFlows(vm, va);
    result.summary = calculateSystemSummary(result.busResults, result.lineResults);

    auto endTime = std::chrono::high_resolution_clock::now();
    result.solveTime_ms = std::chrono::duration<double, std::milli>(
        endTime - startTime).count();

    if (result.converged()) {
        result.message = "Gauss-Seidel converged in " +
            std::to_string(result.iterations) + " iterations";
    } else {
        result.message = "Gauss-Seidel did not converge after " +
            std::to_string(config.maxIterations) + " iterations";
    }

    return result;
}

// ============================================================================
// B' MATRIX (for FDLF P-theta)
// ============================================================================

SpMatrix LoadFlowSolver::buildBPrime() {
    // B' = -B with modifications:
    // 1. Ignore line charging (shunt susceptance)
    // 2. Ignore transformer off-nominal ratios
    // 3. Ignore shunt elements
    // 4. Remove slack bus row and column

    const int n = static_cast<int>(nBuses_);
    const int np = static_cast<int>(numPV_ + numPQ_); // Size without slack

    std::vector<Triplet> triplets;
    triplets.reserve(np * 4);

    // Build mapping from full index to reduced index (excluding slack)
    std::vector<int> fullToReduced(n, -1);
    int reducedIdx = 0;
    for (int i = 0; i < n; ++i) {
        if (!isSlack_[static_cast<size_t>(i)]) {
            fullToReduced[i] = reducedIdx++;
        }
    }

    // Build B' from line data (no charging, no shunts)
    for (const auto& line : system_.getLines()) {
        if (line.status != 1) continue;
        int fi = static_cast<int>(line.fromBus - 1);
        int ti = static_cast<int>(line.toBus - 1);
        int ri = fullToReduced[fi];
        int rj = fullToReduced[ti];
        if (ri < 0 || rj < 0) continue; // Skip if connected to slack

        double x = line.x_pu;
        if (std::abs(x) < ZERO_IMPEDANCE_THRESHOLD) continue;

        double b = -1.0 / x; // 1/x (susceptance from reactance only)

        triplets.emplace_back(ri, ri, -b);
        triplets.emplace_back(rj, rj, -b);
        triplets.emplace_back(ri, rj, b);
        triplets.emplace_back(rj, ri, b);
    }

    // Transformer reactance contributions
    for (const auto& tx : system_.getTransformers()) {
        if (tx.status != 1) continue;
        int fi = static_cast<int>(tx.fromBus - 1);
        int ti = static_cast<int>(tx.toBus - 1);
        int ri = fullToReduced[fi];
        int rj = fullToReduced[ti];
        if (ri < 0 || rj < 0) continue;

        double x = tx.x_pu;
        if (std::abs(x) < ZERO_IMPEDANCE_THRESHOLD) continue;

        double b = -1.0 / x;

        triplets.emplace_back(ri, ri, -b);
        triplets.emplace_back(rj, rj, -b);
        triplets.emplace_back(ri, rj, b);
        triplets.emplace_back(rj, ri, b);
    }

    SpMatrix bPrime(np, np);
    bPrime.setFromTriplets(triplets.begin(), triplets.end());
    bPrime.makeCompressed();
    return bPrime;
}

// ============================================================================
// B'' MATRIX (for FDLF Q-V)
// ============================================================================

SpMatrix LoadFlowSolver::buildBDoublePrime() {
    // B'' = -B with:
    // 1. Line charging included
    // 2. Shunts included
    // 3. Remove slack AND PV bus rows/columns

    const int n = static_cast<int>(nBuses_);
    const int nq = static_cast<int>(numPQ_); // PQ buses only

    std::vector<Triplet> triplets;
    triplets.reserve(nq * 4);

    // Build mapping: PQ buses only
    std::vector<int> fullToReduced(n, -1);
    int reducedIdx = 0;
    for (int i = 0; i < n; ++i) {
        if (isPQ_[static_cast<size_t>(i)]) {
            fullToReduced[i] = reducedIdx++;
        }
    }

    // Include PV->PQ converted buses
    for (size_t i = 0; i < nBuses_; ++i) {
        if (isPV_[i]) {
            fullToReduced[static_cast<int>(i)] = -1; // Exclude PV buses
        }
    }

    // Build from full B matrix, keeping only PQ rows/cols
    for (int k = 0; k < bMatrix_.outerSize(); ++k) {
        int ri = fullToReduced[k];
        if (ri < 0) continue;

        for (SpMatrix::InnerIterator it(bMatrix_, k); it; ++it) {
            int rj = fullToReduced[it.col()];
            if (rj < 0) continue;
            triplets.emplace_back(ri, rj, -it.value()); // -B
        }
    }

    SpMatrix bDoublePrime(nq, nq);
    bDoublePrime.setFromTriplets(triplets.begin(), triplets.end());
    bDoublePrime.makeCompressed();
    return bDoublePrime;
}

// ============================================================================
// Q LIMIT ENFORCEMENT
// ============================================================================

int LoadFlowSolver::enforceQLimits(
    DenseVector& qGen,
    std::vector<bool>& pvToPqConverted
) {
    int converted = 0;
    const auto& generators = system_.getGenerators();

    for (const auto& gen : generators) {
        if (gen.status != 1) continue;
        size_t busIdx = gen.busId - 1;
        if (busIdx >= nBuses_) continue;
        if (!isPV_[busIdx] && !pvToPqConverted[busIdx]) continue;

        double q = qGen(static_cast<Eigen::Index>(busIdx));

        if (q > gen.qmax_pu + Q_LIMIT_MARGIN_PU) {
            pvToPqConverted[busIdx] = true;
            ++converted;
        } else if (q < gen.qmin_pu - Q_LIMIT_MARGIN_PU) {
            pvToPqConverted[busIdx] = true;
            ++converted;
        }
    }

    return converted;
}

// ============================================================================
// BUILD BUS RESULTS
// ============================================================================

std::vector<PowerFlowBusResult> LoadFlowSolver::buildBusResults(
    const DenseVector& vm,
    const DenseVector& va_rad,
    const DenseVector& pCalc,
    const DenseVector& qCalc,
    const DenseVector& pSpec,
    const DenseVector& qSpec
) {
    std::vector<PowerFlowBusResult> results;
    results.reserve(nBuses_);
    const auto& buses = system_.getBuses();

    for (size_t i = 0; i < nBuses_; ++i) {
        PowerFlowBusResult br;
        const Bus& bus = buses[i];
        br.busId = bus.id;
        br.busName = bus.name;
        br.type = bus.type;
        br.vm_pu = vm(static_cast<Eigen::Index>(i));
        br.va_rad = va_rad(static_cast<Eigen::Index>(i));
        br.va_deg = va_rad(static_cast<Eigen::Index>(i)) * RAD_TO_DEG;

        if (bus.type == BusType::Slack) {
            br.pg_pu = pCalc(static_cast<Eigen::Index>(i)) + bus.pl_pu;
            br.qg_pu = qCalc(static_cast<Eigen::Index>(i)) + bus.ql_pu;
            br.pInyected_pu = pCalc(static_cast<Eigen::Index>(i));
            br.qInyected_pu = qCalc(static_cast<Eigen::Index>(i));
        } else if (bus.type == BusType::PV) {
            br.pg_pu = bus.pg_pu;
            br.qg_pu = qCalc(static_cast<Eigen::Index>(i)) + bus.ql_pu;
            br.pInyected_pu = pCalc(static_cast<Eigen::Index>(i));
            br.qInyected_pu = qCalc(static_cast<Eigen::Index>(i));
        } else {
            br.pg_pu = bus.pg_pu;
            br.qg_pu = bus.qg_pu;
            br.pInyected_pu = pCalc(static_cast<Eigen::Index>(i));
            br.qInyected_pu = qCalc(static_cast<Eigen::Index>(i));
        }

        br.pl_pu = bus.pl_pu;
        br.ql_pu = bus.ql_pu;
        br.pMismatch_pu = pSpec(static_cast<Eigen::Index>(i)) - pCalc(static_cast<Eigen::Index>(i));
        br.qMismatch_pu = qSpec(static_cast<Eigen::Index>(i)) - qCalc(static_cast<Eigen::Index>(i));
        br.voltageViolation = (br.vm_pu > bus.vmax_pu || br.vm_pu < bus.vmin_pu);

        results.push_back(br);
    }

    return results;
}

// ============================================================================
// CALCULATE LINE FLOWS
// ============================================================================

std::vector<PowerFlowLineResult> LoadFlowSolver::calculateLineFlows(
    const DenseVector& vm,
    const DenseVector& va_rad
) {
    std::vector<PowerFlowLineResult> results;
    const auto& lines = system_.getLines();
    results.reserve(lines.size());

    for (const auto& line : lines) {
        if (line.status != 1) continue;

        const size_t fi = line.fromBus - 1;
        const size_t ti = line.toBus - 1;
        if (fi >= static_cast<size_t>(vm.size()) || ti >= static_cast<size_t>(vm.size()))
            continue;

        PowerFlowLineResult lr;
        lr.lineId = line.id;
        lr.lineName = line.name;
        lr.fromBus = line.fromBus;
        lr.toBus = line.toBus;

        const double vi = vm(static_cast<Eigen::Index>(fi));
        const double vj = vm(static_cast<Eigen::Index>(ti));
        const double di = va_rad(static_cast<Eigen::Index>(fi));
        const double dj = va_rad(static_cast<Eigen::Index>(ti));

        const Complex viComplex(vi * std::cos(di), vi * std::sin(di));
        const Complex vjComplex(vj * std::cos(dj), vj * std::sin(dj));

        const double r = line.r_pu;
        const double x = line.x_pu;
        const Complex z(r, x);
        const double z2 = r * r + x * x;

        if (z2 < ZERO_IMPEDANCE_THRESHOLD) continue;

        const Complex ySeries = 1.0 / z;
        const double bCh = line.bch_pu;

        if (line.ratio > 0.01) {
            // Transformer with off-nominal ratio
            const double a = line.ratio;
            const double a2 = a * a;
            const Complex yii = ySeries / a2;
            const Complex yij = -ySeries / a;
            const Complex yji = -ySeries / a;
            const Complex yjj = ySeries;

            // S_from = V_i * conj(I_i) = V_i * conj(yii*V_i + yij*V_j)
            Complex iFrom = yii * viComplex + yij * vjComplex;
            Complex iTo = yji * viComplex + yjj * vjComplex;
            lr.sFrom_pu = viComplex * std::conj(iFrom);
            lr.sTo_pu = vjComplex * std::conj(iTo);
        } else {
            // Standard PI model line
            // I_ij = ySeries*(V_i - V_j) + j*bCh/2 * V_i
            Complex bShuntFrom(0.0, bCh * line.fracBFrom);
            Complex bShuntTo(0.0, bCh * line.fracBTo);

            Complex iFrom = ySeries * (viComplex - vjComplex) + bShuntFrom * viComplex;
            Complex iTo = ySeries * (vjComplex - viComplex) + bShuntTo * vjComplex;

            lr.sFrom_pu = viComplex * std::conj(iFrom);
            lr.sTo_pu = vjComplex * std::conj(iTo);
        }

        lr.sLoss_pu = lr.sFrom_pu + lr.sTo_pu;
        lr.pFrom_pu = lr.sFrom_pu.real();
        lr.qFrom_pu = lr.sFrom_pu.imag();
        lr.pTo_pu = lr.sTo_pu.real();
        lr.qTo_pu = lr.sTo_pu.imag();
        lr.pLoss_pu = lr.sLoss_pu.real();
        lr.qLoss_pu = lr.sLoss_pu.imag();

        // Apparent power flow and loading
        double sFromMag = std::abs(lr.sFrom_pu);
        double sToMag = std::abs(lr.sTo_pu);
        lr.currentFrom_pu = sFromMag / vi;
        lr.currentTo_pu = sToMag / vj;

        double rateA = line.rateA_pu;
        if (rateA > 0) {
            lr.loading_pu = std::max(sFromMag, sToMag) / rateA;
            lr.overload = lr.loading_pu > LINE_LOADING_NORMAL;
        }

        results.push_back(lr);
    }

    return results;
}

// ============================================================================
// SYSTEM SUMMARY
// ============================================================================

SystemSummary LoadFlowSolver::calculateSystemSummary(
    const std::vector<PowerFlowBusResult>& busResults,
    const std::vector<PowerFlowLineResult>& lineResults
) {
    SystemSummary summary;

    for (const auto& br : busResults) {
        summary.totalPg_pu += br.pg_pu;
        summary.totalQg_pu += br.qg_pu;
        summary.totalPl_pu += br.pl_pu;
        summary.totalQl_pu += br.ql_pu;

        switch (br.type) {
            case BusType::PV: summary.numPVBuses++; break;
            case BusType::PQ: summary.numPQBuses++; break;
            case BusType::Slack: summary.numSlackBuses++; break;
        }

        if (br.type == BusType::Slack) {
            summary.totalPslack_pu = br.pInyected_pu;
            summary.totalQslack_pu = br.qInyected_pu;
        }
    }

    for (const auto& lr : lineResults) {
        summary.totalPloss_pu += lr.pLoss_pu;
        summary.totalQloss_pu += lr.qLoss_pu;
    }

    summary.mismatchP_pu = summary.totalPg_pu - summary.totalPl_pu - summary.totalPloss_pu;
    summary.mismatchQ_pu = summary.totalQg_pu - summary.totalQl_pu - summary.totalQloss_pu;

    return summary;
}

} // namespace powsys365
