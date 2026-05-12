// =============================================================================
// tests/cpp/test_math_utils.cpp - Math Utilities Unit Tests
// =============================================================================
// Covers:
//   - has_converged with various vectors
//   - calculate_injected_powers at flat start
//   - solve_sparse linear system
//   - Angle conversions
//   - Complex power utilities
//   - SparseLU wrapper class
// =============================================================================

#include <catch2/catch_all.hpp>
#include <powsy365/math_utils.h>
#include <Eigen/Sparse>
#include <cmath>

using namespace powsys365;

// ============================================================================
// TEST_CASE: has_converged
// ============================================================================

TEST_CASE("has_converged with zero vector", "[math][convergence]") {
    // Zero vector should always converge (norm = 0 < tolerance)
    DenseVector zero(5);
    zero.setZero();

    REQUIRE(has_converged(zero, 1e-6) == true);
    REQUIRE(has_converged(zero, 1e-12) == true);
    REQUIRE(has_converged(zero, 1.0) == true);
}

TEST_CASE("has_converged with ones vector", "[math][convergence]") {
    // Vector of ones has infinity norm = 1.0, which is > 1e-6
    DenseVector ones(5);
    ones.setOnes();

    REQUIRE(has_converged(ones, 1e-6) == false);
    REQUIRE(has_converged(ones, 1e-6) == false);
    REQUIRE(has_converged(ones, 1.0) == true);   // tol = 1.0 => 1.0 < 1.0 is false
    REQUIRE(has_converged(ones, 2.0) == true);   // tol = 2.0 => 1.0 < 2.0 is true
}

TEST_CASE("has_converged with small values", "[math][convergence]") {
    // Vector with small values below tolerance should converge
    DenseVector small(3);
    small << 1e-7, 1e-8, 1e-9;

    REQUIRE(has_converged(small, 1e-6) == true);
    REQUIRE(has_converged(small, 1e-8) == false);  // 1e-7 > 1e-8
}

TEST_CASE("has_converged with empty vector", "[math][convergence]") {
    DenseVector empty(0);
    REQUIRE(has_converged(empty, 1e-6) == true);
}

TEST_CASE("has_converged with mixed values", "[math][convergence]") {
    // Mixed values: max is 5e-7, so with tol=1e-6 it converges
    DenseVector mixed(4);
    mixed << 1e-7, 5e-7, 3e-7, 2e-7;

    REQUIRE(has_converged(mixed, 1e-6) == true);
    REQUIRE(has_converged(mixed, 1e-7) == false);  // 5e-7 > 1e-7
}

// ============================================================================
// TEST_CASE: calculate_injected_powers at flat start
// ============================================================================

TEST_CASE("calculate_injected_powers at flat start for isolated bus", "[math][power]") {
    // Build a 2-bus system with only shunt elements (no lines)
    // At flat start V=1+j0, the injected power at an isolated bus should be zero

    const int n = 2;

    // Build a minimal Ybus: just diagonal elements (shunt only)
    std::vector<Triplet> gTriplets;
    std::vector<Triplet> bTriplets;

    // Bus 1: G=0.1, B=-0.5 (shunt)
    gTriplets.emplace_back(0, 0, 0.1);
    bTriplets.emplace_back(0, 0, -0.5);

    // Bus 2: G=0.05, B=-0.3 (shunt)
    gTriplets.emplace_back(1, 1, 0.05);
    bTriplets.emplace_back(1, 1, -0.3);

    // No off-diagonal elements => isolated buses

    SpMatrix g(n, n);
    SpMatrix b(n, n);
    g.setFromTriplets(gTriplets.begin(), gTriplets.end());
    b.setFromTriplets(bTriplets.begin(), bTriplets.end());
    g.makeCompressed();
    b.makeCompressed();

    // Flat start: V=1.0 pu, theta=0
    DenseVector vm(n);
    DenseVector va(n);
    vm.setOnes();
    va.setZero();

    DenseVector pCalc, qCalc;
    calculate_injected_powers(g, b, vm, va, pCalc, qCalc);

    REQUIRE(pCalc.size() == n);
    REQUIRE(qCalc.size() == n);

    // For an isolated bus at flat start with only shunt conductance:
    // P_i = V_i * sum_j(V_j * (Gij*cos(dij) + Bij*sin(dij)))
    // With dij=0: cos=1, sin=0
    // P_i = Vi * (Vi*Gii) = Gii (since Vi=1)
    // So P_1 = 0.1, P_2 = 0.05
    REQUIRE(std::abs(pCalc(0) - 0.1) < 1e-12);
    REQUIRE(std::abs(pCalc(1) - 0.05) < 1e-12);

    // Q_i = Vi * sum_j(V_j * (Gij*sin(dij) - Bij*cos(dij)))
    // With dij=0: sin=0, cos=1
    // Q_i = Vi * (-Vi*Bii) = -Bii (since Vi=1)
    // So Q_1 = 0.5, Q_2 = 0.3
    REQUIRE(std::abs(qCalc(0) - 0.5) < 1e-12);
    REQUIRE(std::abs(qCalc(1) - 0.3) < 1e-12);
}

TEST_CASE("calculate_injected_powers for simple 2-bus with line", "[math][power]") {
    // Two buses connected by a line with r=0, x=0.1
    // Ybus = [[-j10, j10], [j10, -j10]]
    const int n = 2;

    std::vector<Triplet> gTriplets;
    std::vector<Triplet> bTriplets;

    // G = 0 everywhere (lossless line)
    gTriplets.emplace_back(0, 0, 0.0);
    gTriplets.emplace_back(1, 1, 0.0);

    // B: diagonal = -10, off-diagonal = 10
    bTriplets.emplace_back(0, 0, -10.0);
    bTriplets.emplace_back(0, 1, 10.0);
    bTriplets.emplace_back(1, 0, 10.0);
    bTriplets.emplace_back(1, 1, -10.0);

    SpMatrix g(n, n);
    SpMatrix b(n, n);
    g.setFromTriplets(gTriplets.begin(), gTriplets.end());
    b.setFromTriplets(bTriplets.begin(), bTriplets.end());
    g.makeCompressed();
    b.makeCompressed();

    // Flat start: V1=V2=1.0, theta1=theta2=0
    DenseVector vm(n);
    DenseVector va(n);
    vm.setOnes();
    va.setZero();

    DenseVector pCalc, qCalc;
    calculate_injected_powers(g, b, vm, va, pCalc, qCalc);

    // At flat start with equal voltages and zero angles, no power flows
    // P1 = P2 = 0
    REQUIRE(std::abs(pCalc(0)) < 1e-12);
    REQUIRE(std::abs(pCalc(1)) < 1e-12);

    // Q1 = V1 * (V1*B11 + V2*B12) = 1 * (-10 + 10) = 0
    // Q2 = V2 * (V1*B21 + V2*B22) = 1 * (10 - 10) = 0
    REQUIRE(std::abs(qCalc(0)) < 1e-12);
    REQUIRE(std::abs(qCalc(1)) < 1e-12);
}

// ============================================================================
// TEST_CASE: solve_sparse
// ============================================================================

TEST_CASE("solve_sparse simple 2x2 system", "[math][linear_algebra]") {
    // Solve: [2 1; 1 3] * [x; y] = [5; 8]
    // Solution: x=1.4, y=2.2

    std::vector<Triplet> triplets;
    triplets.emplace_back(0, 0, 2.0);
    triplets.emplace_back(0, 1, 1.0);
    triplets.emplace_back(1, 0, 1.0);
    triplets.emplace_back(1, 1, 3.0);

    SpMatrix A(2, 2);
    A.setFromTriplets(triplets.begin(), triplets.end());
    A.makeCompressed();

    DenseVector b(2);
    b << 5.0, 8.0;

    DenseVector x = solve_sparse(A, b);

    REQUIRE(x.size() == 2);
    REQUIRE(std::abs(x(0) - 1.4) < 1e-10);
    REQUIRE(std::abs(x(1) - 2.2) < 1e-10);
}

TEST_CASE("solve_sparse identity matrix", "[math][linear_algebra]") {
    // A = I, b = [1, 2, 3, 4, 5], solution = b
    const int n = 5;

    std::vector<Triplet> triplets;
    for (int i = 0; i < n; ++i) {
        triplets.emplace_back(i, i, 1.0);
    }

    SpMatrix A(n, n);
    A.setFromTriplets(triplets.begin(), triplets.end());
    A.makeCompressed();

    DenseVector b(n);
    for (int i = 0; i < n; ++i) {
        b(i) = static_cast<double>(i + 1);
    }

    DenseVector x = solve_sparse(A, b);

    for (int i = 0; i < n; ++i) {
        REQUIRE(std::abs(x(i) - b(i)) < 1e-12);
    }
}

TEST_CASE("solve_sparse larger sparse system", "[math][linear_algebra]") {
    // Solve a sparse tridiagonal system: -u_{i-1} + 2*u_i - u_{i+1} = f_i
    const int n = 50;

    std::vector<Triplet> triplets;
    for (int i = 0; i < n; ++i) {
        triplets.emplace_back(i, i, 2.0);
        if (i > 0) triplets.emplace_back(i, i - 1, -1.0);
        if (i < n - 1) triplets.emplace_back(i, i + 1, -1.0);
    }

    SpMatrix A(n, n);
    A.setFromTriplets(triplets.begin(), triplets.end());
    A.makeCompressed();

    // Right-hand side: all ones
    DenseVector b(n);
    b.setOnes();

    DenseVector x = solve_sparse(A, b);

    REQUIRE(x.size() == n);

    // Verify A*x = b
    DenseVector residual = A * x - b;
    double resNorm = residual.template lpNorm<Eigen::Infinity>();
    REQUIRE(resNorm < 1e-10);
}

// ============================================================================
// TEST_CASE: Angle conversions
// ============================================================================

TEST_CASE("deg_to_rad and rad_to_deg conversion", "[math][angles]") {
    REQUIRE(std::abs(deg_to_rad(0.0) - 0.0) < 1e-12);
    REQUIRE(std::abs(deg_to_rad(180.0) - PI) < 1e-12);
    REQUIRE(std::abs(deg_to_rad(90.0) - PI / 2.0) < 1e-12);

    REQUIRE(std::abs(rad_to_deg(0.0) - 0.0) < 1e-12);
    REQUIRE(std::abs(rad_to_deg(PI) - 180.0) < 1e-12);
    REQUIRE(std::abs(rad_to_deg(PI / 2.0) - 90.0) < 1e-12);

    // Round-trip conversion
    for (double deg : {0.0, 45.0, 90.0, 180.0, 270.0, 360.0, -45.0}) {
        double rad = deg_to_rad(deg);
        double deg2 = rad_to_deg(rad);
        REQUIRE(std::abs(deg - deg2) < 1e-12);
    }
}

// ============================================================================
// TEST_CASE: Complex power utilities
// ============================================================================

TEST_CASE("complex_power creates correct complex number", "[math][power]") {
    Complex s = complex_power(3.0, 4.0);
    REQUIRE(s.real() == 3.0);
    REQUIRE(s.imag() == 4.0);
}

TEST_CASE("apparent_power_magnitude calculation", "[math][power]") {
    REQUIRE(std::abs(apparent_power_magnitude(3.0, 4.0) - 5.0) < 1e-12);
    REQUIRE(std::abs(apparent_power_magnitude(0.0, 0.0) - 0.0) < 1e-12);
    REQUIRE(std::abs(apparent_power_magnitude(1.0, 0.0) - 1.0) < 1e-12);
}

TEST_CASE("current_from_power basic calculation", "[math][power]") {
    // S = 1 + j0, V = 1 + j0 => I = conj(S)/conj(V) = 1/1 = 1
    Complex s(1.0, 0.0);
    Complex v(1.0, 0.0);
    Complex i = current_from_power(s, v);
    REQUIRE(std::abs(i - Complex(1.0, 0.0)) < 1e-12);
}

TEST_CASE("current_from_power throws on zero voltage", "[math][power]") {
    Complex s(1.0, 0.0);
    Complex v(0.0, 0.0);
    REQUIRE_THROWS_AS(current_from_power(s, v), std::runtime_error);
}

// ============================================================================
// TEST_CASE: SparseLU wrapper class
// ============================================================================

TEST_CASE("SparseLU factorize and solve", "[math][linear_algebra]") {
    const int n = 3;
    std::vector<Triplet> triplets;
    triplets.emplace_back(0, 0, 4.0);
    triplets.emplace_back(0, 1, 1.0);
    triplets.emplace_back(1, 0, 1.0);
    triplets.emplace_back(1, 1, 3.0);
    triplets.emplace_back(2, 2, 2.0);

    SpMatrix A(n, n);
    A.setFromTriplets(triplets.begin(), triplets.end());
    A.makeCompressed();

    SparseLU lu;
    lu.factorize(A);

    REQUIRE(lu.isFactorized() == true);

    DenseVector b(n);
    b << 5.0, 4.0, 6.0;

    // Expected solution: x0=1, x1=1, x2=3
    DenseVector x = lu.solve(b);

    REQUIRE(std::abs(x(0) - 1.0) < 1e-10);
    REQUIRE(std::abs(x(1) - 1.0) < 1e-10);
    REQUIRE(std::abs(x(2) - 3.0) < 1e-10);
}

TEST_CASE("SparseLU determinant", "[math][linear_algebra]") {
    // A = diag(2, 3, 4) => det = 24
    const int n = 3;
    std::vector<Triplet> triplets;
    triplets.emplace_back(0, 0, 2.0);
    triplets.emplace_back(1, 1, 3.0);
    triplets.emplace_back(2, 2, 4.0);

    SpMatrix A(n, n);
    A.setFromTriplets(triplets.begin(), triplets.end());
    A.makeCompressed();

    SparseLU lu;
    lu.factorize(A);

    double det = lu.determinant();
    REQUIRE(std::abs(det - 24.0) < 1e-10);
}

TEST_CASE("SparseLU reset clears factorization", "[math][linear_algebra]") {
    const int n = 2;
    std::vector<Triplet> triplets;
    triplets.emplace_back(0, 0, 1.0);
    triplets.emplace_back(1, 1, 1.0);

    SpMatrix A(n, n);
    A.setFromTriplets(triplets.begin(), triplets.end());
    A.makeCompressed();

    SparseLU lu;
    lu.factorize(A);
    REQUIRE(lu.isFactorized() == true);

    lu.reset();
    REQUIRE(lu.isFactorized() == false);
}

// ============================================================================
// TEST_CASE: Numerical safeguards
// ============================================================================

TEST_CASE("clamp_value works correctly", "[math][safeguards]") {
    REQUIRE(clamp_value(5.0, 0.0, 10.0) == 5.0);
    REQUIRE(clamp_value(-5.0, 0.0, 10.0) == 0.0);
    REQUIRE(clamp_value(15.0, 0.0, 10.0) == 10.0);
    REQUIRE(clamp_value(0.0, 0.0, 10.0) == 0.0);
    REQUIRE(clamp_value(10.0, 0.0, 10.0) == 10.0);
}

TEST_CASE("is_effectively_zero works correctly", "[math][safeguards]") {
    REQUIRE(is_effectively_zero(0.0) == true);
    REQUIRE(is_effectively_zero(1e-13) == true);
    REQUIRE(is_effectively_zero(1e-10, 1e-12) == false);
    REQUIRE(is_effectively_zero(1e-10, 1e-9) == true);
}

TEST_CASE("safe_divide works correctly", "[math][safeguards]") {
    REQUIRE(safe_divide(10.0, 2.0) == 5.0);
    REQUIRE(safe_divide(10.0, 0.0) == 0.0);  // default return on zero division
    REQUIRE(safe_divide(10.0, 0.0, -999.0) == -999.0);  // custom default
    REQUIRE(safe_divide(0.0, 5.0) == 0.0);
}
