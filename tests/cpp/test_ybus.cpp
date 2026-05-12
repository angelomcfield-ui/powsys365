// =============================================================================
// tests/cpp/test_ybus.cpp - Ybus Matrix Unit Tests
// =============================================================================
// Covers:
//   - Ybus correct dimensions for IEEE 14-bus
//   - Ybus sparsity pattern verification
//   - Ybus diagonal dominance
//   - Ybus symmetry
//   - Ybus rebuilding after topology change
// =============================================================================

#include <catch2/catch_all.hpp>
#include <powsy365/power_system.h>
#include <powsy365/ybus_builder.h>
#include <cmath>

using namespace powsys365;

// ============================================================================
// Helper: build Ybus from IEEE 14
// ============================================================================

static SpMatrixC build_ieee14_ybus() {
    PowerSystem system;
    system.loadIEEE14();
    system.buildYbus();
    return system.getYbus();
}

// ============================================================================
// TEST_CASE: Ybus is correct size
// ============================================================================

TEST_CASE("Ybus is correct size for IEEE 14", "[ybus][dimensions]") {
    SpMatrixC ybus = build_ieee14_ybus();

    REQUIRE(ybus.rows() == 14);
    REQUIRE(ybus.cols() == 14);
}

// ============================================================================
// TEST_CASE: Ybus is square
// ============================================================================

TEST_CASE("Ybus is square", "[ybus][dimensions]") {
    SpMatrixC ybus = build_ieee14_ybus();

    REQUIRE(ybus.rows() == ybus.cols());
}

// ============================================================================
// TEST_CASE: Ybus is sparse
// ============================================================================

TEST_CASE("Ybus is sparse for IEEE 14", "[ybus][sparsity]") {
    SpMatrixC ybus = build_ieee14_ybus();

    const size_t n = static_cast<size_t>(ybus.rows());
    const size_t maxElements = n * n;  // 196 for 14x14

    // Count non-zero elements
    int nnz = 0;
    for (int k = 0; k < ybus.outerSize(); ++k) {
        for (SpMatrixC::InnerIterator it(ybus, k); it; ++it) {
            ++nnz;
        }
    }

    INFO("Ybus nnz = " << nnz << " out of " << maxElements);

    // For a power system, Ybus should be sparse:
    // IEEE 14 has 20 lines + some transformers, so ~14 diagonal + ~40 off-diagonal
    // We expect fewer than 100 non-zeros for a 14-bus system
    REQUIRE(nnz < 100);

    // Each bus should have at least its diagonal element
    REQUIRE(nnz >= static_cast<int>(n));
}

// ============================================================================
// TEST_CASE: Ybus diagonal dominance
// ============================================================================

TEST_CASE("Ybus diagonal dominance", "[ybus][properties]") {
    SpMatrixC ybus = build_ieee14_ybus();

    const int n = ybus.rows();

    for (int i = 0; i < n; ++i) {
        // Get diagonal element
        Complex yii = ybus.coeff(i, i);
        double yiiMag = std::abs(yii);

        // Sum off-diagonal magnitudes in row i
        double offDiagSum = 0.0;
        for (int j = 0; j < n; ++j) {
            if (i != j) {
                Complex yij = ybus.coeff(i, j);
                offDiagSum += std::abs(yij);
            }
        }

        INFO("Bus " << (i + 1) << ": |Y_ii| = " << yiiMag
             << ", sum|Y_ij| = " << offDiagSum);

        // Ybus should be diagonally dominant: |Yii| >= sum|Yij|
        REQUIRE(yiiMag >= offDiagSum);
    }
}

// ============================================================================
// TEST_CASE: Ybus symmetry
// ============================================================================

TEST_CASE("Ybus is symmetric for passive network", "[ybus][properties]") {
    SpMatrixC ybus = build_ieee14_ybus();

    const int n = ybus.rows();

    // For a passive network (no phase shifters), Ybus should be symmetric
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            Complex yij = ybus.coeff(i, j);
            Complex yji = ybus.coeff(j, i);

            // Symmetric: Yij = Yji
            REQUIRE(std::abs(yij - yji) < 1e-12);
        }
    }
}

// ============================================================================
// TEST_CASE: Ybus diagonal elements are non-zero
// ============================================================================

TEST_CASE("Ybus diagonal elements are non-zero", "[ybus][properties]") {
    SpMatrixC ybus = build_ieee14_ybus();

    const int n = ybus.rows();

    for (int i = 0; i < n; ++i) {
        Complex yii = ybus.coeff(i, i);
        INFO("Ybus[" << i << "," << i << "] = " << yii);
        REQUIRE(std::abs(yii) > 0.0);
    }
}

// ============================================================================
// TEST_CASE: Ybus has correct number of non-zero off-diagonals for IEEE 14
// ============================================================================

TEST_CASE("Ybus off-diagonal pattern for IEEE 14", "[ybus][sparsity]") {
    SpMatrixC ybus = build_ieee14_ybus();

    const int n = ybus.rows();

    // Count non-zero off-diagonal elements
    int offDiagNnz = 0;
    for (int k = 0; k < ybus.outerSize(); ++k) {
        for (SpMatrixC::InnerIterator it(ybus, k); it; ++it) {
            if (it.row() != it.col()) {
                ++offDiagNnz;
            }
        }
    }

    // IEEE 14 has 20 lines; each line contributes 2 off-diagonal entries (i,j and j,i)
    // Plus transformers: lines 8, 9, 10, 14, 15 are transformers (based on r=0)
    // Total off-diagonal entries should be 2 * (number of branches)
    // IEEE 14 has 20 lines + some transformer representations
    // The exact count depends on the implementation, but should be reasonable
    INFO("Off-diagonal nnz = " << offDiagNnz);
    REQUIRE(offDiagNnz >= 20);  // At least 20 off-diagonal entries
    REQUIRE(offDiagNnz <= 200); // At most 200 (upper bound)
}

// ============================================================================
// TEST_CASE: Ybus rebuilt after adding line
// ============================================================================

TEST_CASE("Ybus changes after topology modification", "[ybus][topology]") {
    PowerSystem system;
    system.loadIEEE14();
    system.buildYbus();

    SpMatrixC ybusBefore = system.getYbus();

    // Add a new line between bus 1 and bus 14 (strengthening connection)
    Line newLine;
    newLine.id = 99;
    newLine.fromBus = 1;
    newLine.toBus = 14;
    newLine.r_pu = 0.01;
    newLine.x_pu = 0.1;
    newLine.bch_pu = 0.0;
    newLine.status = 1;
    system.addLine(newLine);

    system.buildYbus();
    SpMatrixC ybusAfter = system.getYbus();

    // Ybus should change after adding a line
    bool changed = false;
    for (int i = 0; i < ybusBefore.rows(); ++i) {
        for (int j = 0; j < ybusBefore.cols(); ++j) {
            if (std::abs(ybusBefore.coeff(i, j) - ybusAfter.coeff(i, j)) > 1e-12) {
                changed = true;
                break;
            }
        }
        if (changed) break;
    }

    REQUIRE(changed);
}

// ============================================================================
// TEST_CASE: YbusBuilder produces correct G and B matrices
// ============================================================================

TEST_CASE("YbusBuilder G and B extraction", "[ybus][builder]") {
    PowerSystem system;
    system.loadIEEE14();
    system.buildYbus();

    SpMatrixC ybus = system.getYbus();

    YbusBuilder builder;
    SpMatrix g = builder.buildG(ybus);
    SpMatrix b = builder.buildB(ybus);

    // G and B should have same dimensions as Ybus
    REQUIRE(g.rows() == ybus.rows());
    REQUIRE(g.cols() == ybus.cols());
    REQUIRE(b.rows() == ybus.rows());
    REQUIRE(b.cols() == ybus.cols());

    // Verify: Ybus = G + j*B
    for (int k = 0; k < ybus.outerSize(); ++k) {
        for (SpMatrixC::InnerIterator it(ybus, k); it; ++it) {
            int row = it.row();
            int col = it.col();
            Complex yVal = it.value();
            double gVal = g.coeff(row, col);
            double bVal = b.coeff(row, col);

            Complex reconstructed(gVal, bVal);
            REQUIRE(std::abs(yVal - reconstructed) < 1e-12);
        }
    }
}

// ============================================================================
// TEST_CASE: Ybus elements are finite
// ============================================================================

TEST_CASE("Ybus elements are finite", "[ybus][numerical]") {
    SpMatrixC ybus = build_ieee14_ybus();

    for (int k = 0; k < ybus.outerSize(); ++k) {
        for (SpMatrixC::InnerIterator it(ybus, k); it; ++it) {
            Complex val = it.value();
            INFO("Ybus[" << it.row() << "," << it.col() << "] = " << val);
            REQUIRE(std::isfinite(val.real()));
            REQUIRE(std::isfinite(val.imag()));
        }
    }
}
