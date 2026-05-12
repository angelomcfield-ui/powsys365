#include "math_utils.h"
#include <iostream>

namespace powsys365 {

// ============================================================================
// SPARSE LINEAR SYSTEM SOLVERS
// ============================================================================

DenseVector solve_sparse(const SpMatrix& A, const DenseVector& b) {
    if (A.rows() != A.cols()) {
        throw std::invalid_argument(
            "solve_sparse: matrix must be square (got " +
            std::to_string(A.rows()) + "x" + std::to_string(A.cols()) + ")");
    }
    if (A.rows() != b.size()) {
        throw std::invalid_argument(
            "solve_sparse: dimension mismatch between A(" +
            std::to_string(A.rows()) + ") and b(" +
            std::to_string(b.size()) + ")");
    }
    if (A.rows() == 0) {
        return DenseVector(0);
    }

    Eigen::SparseLU<SpMatrix, Eigen::COLAMDOrdering<int>> solver;
    solver.compute(A);
    if (solver.info() != Eigen::Success) {
        throw std::runtime_error(
            "solve_sparse: failed to factorize matrix (Singular Jacobian detected)");
    }
    DenseVector x = solver.solve(b);
    if (solver.info() != Eigen::Success) {
        throw std::runtime_error(
            "solve_sparse: failed to solve linear system");
    }
    return x;
}

DenseVectorC solve_sparse_complex(const SpMatrixC& A, const DenseVectorC& b) {
    if (A.rows() != A.cols()) {
        throw std::invalid_argument(
            "solve_sparse_complex: matrix must be square");
    }
    if (A.rows() != b.size()) {
        throw std::invalid_argument(
            "solve_sparse_complex: dimension mismatch");
    }
    if (A.rows() == 0) {
        return DenseVectorC(0);
    }

    Eigen::SparseLU<SpMatrixC, Eigen::COLAMDOrdering<int>> solver;
    solver.compute(A);
    if (solver.info() != Eigen::Success) {
        throw std::runtime_error(
            "solve_sparse_complex: failed to factorize matrix");
    }
    DenseVectorC x = solver.solve(b);
    if (solver.info() != Eigen::Success) {
        throw std::runtime_error(
            "solve_sparse_complex: failed to solve linear system");
    }
    return x;
}

DenseMatrix solve_sparse_multi_rhs(const SpMatrix& A, const DenseMatrix& B) {
    if (A.rows() != A.cols()) {
        throw std::invalid_argument(
            "solve_sparse_multi_rhs: matrix must be square");
    }
    if (A.rows() != B.rows()) {
        throw std::invalid_argument(
            "solve_sparse_multi_rhs: dimension mismatch");
    }

    Eigen::SparseLU<SpMatrix, Eigen::COLAMDOrdering<int>> solver;
    solver.compute(A);
    if (solver.info() != Eigen::Success) {
        throw std::runtime_error(
            "solve_sparse_multi_rhs: failed to factorize matrix");
    }
    DenseMatrix X = solver.solve(B);
    if (solver.info() != Eigen::Success) {
        throw std::runtime_error(
            "solve_sparse_multi_rhs: failed to solve linear system");
    }
    return X;
}

// ============================================================================
// SPARSE LU CLASS WRAPPER
// ============================================================================

void SparseLU::factorize(const SpMatrix& A) {
    if (A.rows() != A.cols()) {
        throw std::invalid_argument(
            "SparseLU::factorize: matrix must be square");
    }
    solverReal_.compute(A);
    if (solverReal_.info() != Eigen::Success) {
        factorized_ = false;
        throw std::runtime_error(
            "SparseLU::factorize: failed to factorize real matrix");
    }
    factorized_ = true;
}

void SparseLU::factorize_complex(const SpMatrixC& A) {
    if (A.rows() != A.cols()) {
        throw std::invalid_argument(
            "SparseLU::factorize_complex: matrix must be square");
    }
    solverComplex_.compute(A);
    if (solverComplex_.info() != Eigen::Success) {
        factorizedComplex_ = false;
        throw std::runtime_error(
            "SparseLU::factorize_complex: failed to factorize complex matrix");
    }
    factorizedComplex_ = true;
}

DenseVector SparseLU::solve(const DenseVector& b) const {
    if (!factorized_) {
        throw std::runtime_error(
            "SparseLU::solve: matrix not factorized. Call factorize() first.");
    }
    DenseVector x = solverReal_.solve(b);
    if (solverReal_.info() != Eigen::Success) {
        throw std::runtime_error(
            "SparseLU::solve: solve failed");
    }
    return x;
}

DenseVectorC SparseLU::solve_complex(const DenseVectorC& b) const {
    if (!factorizedComplex_) {
        throw std::runtime_error(
            "SparseLU::solve_complex: complex matrix not factorized");
    }
    DenseVectorC x = solverComplex_.solve(b);
    if (solverComplex_.info() != Eigen::Success) {
        throw std::runtime_error(
            "SparseLU::solve_complex: solve failed");
    }
    return x;
}

DenseMatrix SparseLU::solve_multi(const DenseMatrix& B) const {
    if (!factorized_) {
        throw std::runtime_error(
            "SparseLU::solve_multi: matrix not factorized");
    }
    DenseMatrix X = solverReal_.solve(B);
    if (solverReal_.info() != Eigen::Success) {
        throw std::runtime_error(
            "SparseLU::solve_multi: solve failed");
    }
    return X;
}

double SparseLU::determinant() {
    if (!factorized_) {
        throw std::runtime_error(
            "SparseLU::determinant: matrix not factorized");
    }
    return solverReal_.determinant();
}

void SparseLU::reset() {
    solverReal_.~SparseLU();
    new (&solverReal_) Eigen::SparseLU<SpMatrix, Eigen::COLAMDOrdering<int>>();
    solverComplex_.~SparseLU();
    new (&solverComplex_) Eigen::SparseLU<SpMatrixC, Eigen::COLAMDOrdering<int>>();
    factorized_ = false;
    factorizedComplex_ = false;
}

// ============================================================================
// INJECTED POWER CALCULATIONS
// ============================================================================

void calculate_injected_powers(
    const SpMatrix& g,
    const SpMatrix& b,
    const DenseVector& vm,
    const DenseVector& va,
    DenseVector& pOut,
    DenseVector& qOut
) {
    const size_t n = static_cast<size_t>(vm.size());
    if (n == 0) return;

    pOut.resize(static_cast<Eigen::Index>(n));
    qOut.resize(static_cast<Eigen::Index>(n));
    pOut.setZero();
    qOut.setZero();

    // Compute P_i and Q_i for each bus
    // P_i = sum_j |V_i|*|V_j|*(G_ij*cos(d_i-d_j) + B_ij*sin(d_i-d_j))
    // Q_i = sum_j |V_i|*|V_j|*(G_ij*sin(d_i-d_j) - B_ij*cos(d_i-d_j))

    for (size_t i = 0; i < n; ++i) {
        double pi = 0.0;
        double qi = 0.0;
        const double vi = vm(static_cast<Eigen::Index>(i));
        const double di = va(static_cast<Eigen::Index>(i));

        // Iterate over non-zero elements in row i of G and B
        // We iterate over G's outer index for row i
        for (SpMatrix::InnerIterator itG(g, static_cast<Eigen::Index>(i)); itG; ++itG) {
            const size_t j = static_cast<size_t>(itG.col());
            const double gij = itG.value();
            const double bij = b.coeff(static_cast<Eigen::Index>(i), static_cast<Eigen::Index>(j));
            const double vj = vm(static_cast<Eigen::Index>(j));
            const double dj = va(static_cast<Eigen::Index>(j));
            const double dij = di - dj;

            const double cos_dij = std::cos(dij);
            const double sin_dij = std::sin(dij);

            pi += vi * vj * (gij * cos_dij + bij * sin_dij);
            qi += vi * vj * (gij * sin_dij - bij * cos_dij);
        }

        pOut(static_cast<Eigen::Index>(i)) = pi;
        qOut(static_cast<Eigen::Index>(i)) = qi;
    }
}

DenseVectorC calculate_injected_complex_powers(
    const SpMatrixC& ybus,
    const DenseVectorC& v
) {
    const Eigen::Index n = v.size();
    if (n == 0) return DenseVectorC(0);

    // S = V .* conj(Ybus * V)
    DenseVectorC yv = ybus * v;
    DenseVectorC s(n);
    for (Eigen::Index i = 0; i < n; ++i) {
        s(i) = v(i) * std::conj(yv(i));
    }
    return s;
}

// ============================================================================
// POWER MISMATCH COMPUTATION
// ============================================================================

void compute_mismatch(
    const DenseVector& pSpec,
    const DenseVector& qSpec,
    const DenseVector& pCalc,
    const DenseVector& qCalc,
    const std::vector<bool>& isPQ,
    DenseVector& mismatchOut
) {
    const Eigen::Index n = pSpec.size();

    // Count PQ buses to determine mismatch vector size
    int numPQ = 0;
    for (bool pq : isPQ) if (pq) ++numPQ;

    // Mismatch: [dP for all non-slack; dQ for PQ only]
    const int numNonSlack = static_cast<int>(n) - 1; // Assuming 1 slack
    mismatchOut.resize(numNonSlack + numPQ);

    int idx = 0;
    // dP for all buses except slack (index 0 assumed slack)
    for (Eigen::Index i = 1; i < n; ++i) {
        mismatchOut(idx++) = pSpec(i) - pCalc(i);
    }
    // dQ for PQ buses only
    for (Eigen::Index i = 0; i < n; ++i) {
        if (isPQ[static_cast<size_t>(i)]) {
            mismatchOut(idx++) = qSpec(i) - qCalc(i);
        }
    }
}

// ============================================================================
// SEQUENCE NETWORK BUILDERS
// ============================================================================

SpMatrixC build_positive_sequence_network(const SpMatrixC& ybus) {
    return ybus;
}

SpMatrixC build_negative_sequence_network(
    const SpMatrixC& /*ybus*/,
    const std::vector<Generator>& generators,
    const std::vector<Bus>& buses
) {
    const size_t n = buses.size();
    std::vector<TripletC> triplets;
    triplets.reserve(n * 4);

    // Negative sequence: lines same as positive; generators use X2
    // For simplicity, we build from the assumption that negative seq
    // is approximately the same as positive for transmission networks
    // with proper generator subtransient reactance modifications

    // Start with a copy approach: build from generator data
    // Negative sequence admittance at generator buses
    for (const auto& gen : generators) {
        if (gen.status != 1 || gen.busId >= n) continue;
        const size_t i = gen.busId - 1; // Convert to 0-based
        if (i >= n) continue;
        // Y2 = 1 / (j * X2) where X2 = xqDoublePrime_pu typically
        const double x2 = gen.xqDoublePrime_pu > 0.0 ? gen.xqDoublePrime_pu : gen.xdDoublePrime_pu;
        if (x2 > 0) {
            Complex y2_neg(0.0, -1.0 / x2);
            triplets.emplace_back(static_cast<int>(i), static_cast<int>(i), y2_neg);
        }
    }

    SpMatrixC y2(static_cast<int>(n), static_cast<int>(n));
    y2.setFromTriplets(triplets.begin(), triplets.end());
    return y2;
}

SpMatrixC build_zero_sequence_network(
    const std::vector<Line>& lines,
    const std::vector<Transformer>& /*transformers*/,
    const std::vector<Generator>& generators,
    const std::vector<Bus>& buses,
    double baseMVA
) {
    const size_t n = buses.size();
    std::vector<TripletC> triplets;
    triplets.reserve(lines.size() * 6 + generators.size() * 2);

    // Zero sequence network: lines have 3x zero-seq impedance typically
    for (const auto& line : lines) {
        if (line.status != 1) continue;
        const size_t i = line.fromBus - 1;
        const size_t j = line.toBus - 1;
        if (i >= n || j >= n) continue;

        // Zero-seq impedance roughly 3x positive-seq for ungrounded lines
        // or include ground return path; simplified: use 3*(R+jX)
        const double r0 = 3.0 * line.r_pu;
        const double x0 = 3.0 * line.x_pu;
        const double z2 = r0 * r0 + x0 * x0;
        if (z2 < ZERO_IMPEDANCE_THRESHOLD) continue;

        const Complex y0(r0 / z2, -x0 / z2);
        triplets.emplace_back(static_cast<int>(i), static_cast<int>(j), -y0);
        triplets.emplace_back(static_cast<int>(j), static_cast<int>(i), -y0);
        triplets.emplace_back(static_cast<int>(i), static_cast<int>(i), y0);
        triplets.emplace_back(static_cast<int>(j), static_cast<int>(j), y0);

        // Zero-sequence charging (typically small or zero)
        if (std::abs(line.bch_pu) > ZERO_IMPEDANCE_THRESHOLD) {
            const Complex b0(0.0, line.bch_pu);
            triplets.emplace_back(static_cast<int>(i), static_cast<int>(i), b0);
            triplets.emplace_back(static_cast<int>(j), static_cast<int>(j), b0);
        }
    }

    // Generator zero-sequence: grounded through impedance
    for (const auto& gen : generators) {
        if (gen.status != 1 || gen.busId == 0) continue;
        const size_t i = gen.busId - 1;
        if (i >= n) continue;
        // X0 is typically 0.1-0.7 * Xd for grounded generators
        const double x0 = 0.15 * gen.xd_pu; // Simplified
        if (x0 > ZERO_IMPEDANCE_THRESHOLD) {
            Complex yg0(0.0, -baseMVA / (x0 * gen.mbase_pu));
            triplets.emplace_back(static_cast<int>(i), static_cast<int>(i), yg0);
        }
    }

    SpMatrixC y0(static_cast<int>(n), static_cast<int>(n));
    y0.setFromTriplets(triplets.begin(), triplets.end());
    return y0;
}

} // namespace powsys365
