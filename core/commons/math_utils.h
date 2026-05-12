#pragma once
#include "matrix_types.h"
#include "types.h"
#include "constants.h"
#include <Eigen/SparseLU>
#include <Eigen/SparseQR>
#include <vector>
#include <stdexcept>
#include <cmath>
#include <complex>
#include <string>

namespace powsys365 {

// ============================================================================
// ANGLE CONVERSIONS
// ============================================================================

inline double deg_to_rad(double degrees) {
    return degrees * DEG_TO_RAD;
}

inline double rad_to_deg(double radians) {
    return radians * RAD_TO_DEG;
}

// ============================================================================
// COMPLEX POWER UTILITIES
// ============================================================================

// Complex power S = P + jQ from P and Q components
inline Complex complex_power(double p, double q) {
    return Complex(p, q);
}

// Current from power and voltage: I = S* / V* = conj(S) / conj(V)
inline Complex current_from_power(const Complex& s, const Complex& v) {
    if (std::abs(v) < MIN_VOLTAGE_PU) {
        throw std::runtime_error(
            "current_from_power: voltage magnitude too small (|V|=" +
            std::to_string(std::abs(v)) + ")");
    }
    return std::conj(s) / std::conj(v);
}

// Apparent power magnitude |S| = sqrt(P^2 + Q^2)
inline double apparent_power_magnitude(double p, double q) {
    return std::sqrt(p * p + q * q);
}

// ============================================================================
// CONVERGENCE CHECKING
// ============================================================================

// Check convergence using infinity norm
inline bool has_converged(const DenseVector& mismatch, double tolerance) {
    if (mismatch.size() == 0) return true;
    return mismatch.template lpNorm<Eigen::Infinity>() < tolerance;
}

// Check convergence for complex mismatch vector
inline bool has_converged_complex(const DenseVectorC& mismatch, double tolerance) {
    if (mismatch.size() == 0) return true;
    return mismatch.template lpNorm<Eigen::Infinity>() < tolerance;
}

// Compute mismatch norm (infinity norm)
inline double mismatch_norm(const DenseVector& mismatch) {
    if (mismatch.size() == 0) return 0.0;
    return mismatch.template lpNorm<Eigen::Infinity>();
}

// ============================================================================
// SPARSE LINEAR SYSTEM SOLVERS
// ============================================================================

/**
 * Solve sparse real linear system Ax = b using SparseLU.
 * Returns solution vector x.
 * Throws on singular matrix.
 */
DenseVector solve_sparse(const SpMatrix& A, const DenseVector& b);

/**
 * Solve sparse complex linear system Ax = b using SparseLU.
 * Returns solution vector x.
 * Throws on singular matrix.
 */
DenseVectorC solve_sparse_complex(const SpMatrixC& A, const DenseVectorC& b);

/**
 * Solve sparse real system with multiple right-hand sides.
 */
DenseMatrix solve_sparse_multi_rhs(const SpMatrix& A, const DenseMatrix& B);

// ============================================================================
// SPARSE LU CLASS WRAPPER
// ============================================================================

class SparseLU {
public:
    SparseLU() = default;

    // Factorize matrix A (computes LU decomposition)
    void factorize(const SpMatrix& A);
    void factorize_complex(const SpMatrixC& A);

    // Check if factorization is valid
    bool isFactorized() const { return factorized_; }
    bool isFactorizedComplex() const { return factorizedComplex_; }

    // Solve Ax = b using stored factorization
    DenseVector solve(const DenseVector& b) const;
    DenseVectorC solve_complex(const DenseVectorC& b) const;

    // Solve with multiple RHS
    DenseMatrix solve_multi(const DenseMatrix& B) const;

    // Get determinant (real only)
    double determinant();

    // Reset
    void reset();

private:
    Eigen::SparseLU<SpMatrix, Eigen::COLAMDOrdering<int>> solverReal_;
    Eigen::SparseLU<SpMatrixC, Eigen::COLAMDOrdering<int>> solverComplex_;
    bool factorized_ = false;
    bool factorizedComplex_ = false;
};

// ============================================================================
// INJECTED POWER CALCULATIONS
// ============================================================================

/**
 * Calculate injected active and reactive powers at all buses.
 *
 * P_i = sum_j |V_i|*|V_j|*(G_ij*cos(delta_i-delta_j) + B_ij*sin(delta_i-delta_j))
 * Q_i = sum_j |V_i|*|V_j|*(G_ij*sin(delta_i-delta_j) - B_ij*cos(delta_i-delta_j))
 *
 * @param g    Real part of Ybus (conductance matrix)
 * @param b    Imaginary part of Ybus (susceptance matrix)
 * @param vm   Voltage magnitude vector [pu]
 * @param va   Voltage angle vector [radians]
 * @param pOut Output active power injections [pu]
 * @param qOut Output reactive power injections [pu]
 */
void calculate_injected_powers(
    const SpMatrix& g,
    const SpMatrix& b,
    const DenseVector& vm,
    const DenseVector& va,
    DenseVector& pOut,
    DenseVector& qOut
);

/**
 * Calculate injected complex powers at all buses.
 * S_i = V_i * conj(sum_j Y_ij * V_j)
 */
DenseVectorC calculate_injected_complex_powers(
    const SpMatrixC& ybus,
    const DenseVectorC& v
);

// ============================================================================
// POWER MISMATCH COMPUTATION
// ============================================================================

/**
 * Compute power flow mismatch vectors for NR method.
 *
 * @param pSpec  Specified active power injections (P_sch)
 * @param qSpec  Specified reactive power injections (Q_sch)
 * @param pCalc  Calculated active power injections
 * @param qCalc  Calculated reactive power injections
 * @param isPQ   Boolean mask: true for PQ buses (controls which Q mismatches to include)
 * @param mismatchOut Output mismatch vector [dP; dQ] stacked
 */
void compute_mismatch(
    const DenseVector& pSpec,
    const DenseVector& qSpec,
    const DenseVector& pCalc,
    const DenseVector& qCalc,
    const std::vector<bool>& isPQ,
    DenseVector& mismatchOut
);

// ============================================================================
// JACOBIAN ELEMENT COMPUTATION HELPERS
// ============================================================================

// H_ij = dP_i/dDelta_j = |V_i|*|V_j|*(G_ij*sin(d_i-d_j) - B_ij*cos(d_i-d_j))
// H_ii = -Q_i - B_ii*|V_i|^2
inline double jacobian_h_offdiag(double vi, double vj, double gij, double bij,
                                  double di, double dj) {
    const double dij = di - dj;
    return vi * vj * (gij * std::sin(dij) - bij * std::cos(dij));
}

inline double jacobian_h_diag(double qi, double bii, double vi) {
    return -qi - bii * vi * vi;
}

// N_ij = dP_i/d|V_j|*|V_j| = |V_i|*|V_j|*(G_ij*cos(d_i-d_j) + B_ij*sin(d_i-d_j))
// N_ii = P_i + G_ii*|V_i|^2
inline double jacobian_n_offdiag(double vi, double vj, double gij, double bij,
                                  double di, double dj) {
    const double dij = di - dj;
    return vi * vj * (gij * std::cos(dij) + bij * std::sin(dij));
}

inline double jacobian_n_diag(double pi, double gii, double vi) {
    return pi + gii * vi * vi;
}

// J_ij = dQ_i/dDelta_j = -|V_i|*|V_j|*(G_ij*cos(d_i-d_j) + B_ij*sin(d_i-d_j))
// J_ii = P_i - G_ii*|V_i|^2
inline double jacobian_j_offdiag(double vi, double vj, double gij, double bij,
                                  double di, double dj) {
    return -jacobian_n_offdiag(vi, vj, gij, bij, di, dj);
}

inline double jacobian_j_diag(double pi, double gii, double vi) {
    return pi - gii * vi * vi;
}

// L_ij = dQ_i/d|V_j|*|V_j| = |V_i|*|V_j|*(G_ij*sin(d_i-d_j) - B_ij*cos(d_i-d_j))
// L_ii = Q_i - B_ii*|V_i|^2
inline double jacobian_l_offdiag(double vi, double vj, double gij, double bij,
                                  double di, double dj) {
    return jacobian_h_offdiag(vi, vj, gij, bij, di, dj);
}

inline double jacobian_l_diag(double qi, double bii, double vi) {
    return qi - bii * vi * vi;
}

// ============================================================================
// SEQUENCE NETWORK HELPERS
// ============================================================================

// Build positive sequence network (standard Ybus)
SpMatrixC build_positive_sequence_network(const SpMatrixC& ybus);

// Build negative sequence network (generators: X2, loads: open/connected)
SpMatrixC build_negative_sequence_network(
    const SpMatrixC& ybus,
    const std::vector<Generator>& generators,
    const std::vector<Bus>& buses
);

// Build zero sequence network (different transformer connections, generator grounding)
SpMatrixC build_zero_sequence_network(
    const std::vector<Line>& lines,
    const std::vector<Transformer>& transformers,
    const std::vector<Generator>& generators,
    const std::vector<Bus>& buses,
    double baseMVA
);

// ============================================================================
// NUMERICAL SAFEGUARDS
// ============================================================================

// Clamp value to [minVal, maxVal]
inline double clamp_value(double value, double minVal, double maxVal) {
    if (value < minVal) return minVal;
    if (value > maxVal) return maxVal;
    return value;
}

// Check if a value is effectively zero
inline bool is_effectively_zero(double value, double threshold = 1e-12) {
    return std::abs(value) < threshold;
}

// Safe division with zero check
inline double safe_divide(double numerator, double denominator, double defaultValue = 0.0) {
    if (std::abs(denominator) < 1e-15) return defaultValue;
    return numerator / denominator;
}

} // namespace powsys365
