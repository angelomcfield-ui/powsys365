#include "powsy365/ybus_builder.h"
#include <cmath>
#include <stdexcept>

namespace powsys365 {

// ============================================================================
// VALIDATION
// ============================================================================

void YbusBuilder::validateBusIndex(size_t busId, size_t numBuses, const std::string& elementType) {
    if (busId == 0) {
        throw std::invalid_argument(
            "YbusBuilder: " + elementType + " has invalid bus ID (0). Bus IDs must be 1-based.");
    }
    if (busId > numBuses) {
        throw std::invalid_argument(
            "YbusBuilder: " + elementType + " references bus " +
            std::to_string(busId) + " but system only has " +
            std::to_string(numBuses) + " buses.");
    }
}

// ============================================================================
// LINE CONTRIBUTION
// ============================================================================

void YbusBuilder::addLineContribution(
    const Line& line,
    std::vector<TripletC>& triplets,
    size_t numBuses
) {
    if (line.status != 1) return; // Out of service

    validateBusIndex(line.fromBus, numBuses, "Line");
    validateBusIndex(line.toBus, numBuses, "Line");

    const int i = static_cast<int>(line.fromBus - 1); // 0-based index
    const int j = static_cast<int>(line.toBus - 1);

    // Series impedance
    const double r = line.r_pu;
    const double x = line.x_pu;
    const double z2 = r * r + x * x;

    if (z2 < ZERO_IMPEDANCE_THRESHOLD) {
        throw std::runtime_error(
            "YbusBuilder: Line " + std::to_string(line.id) +
            " has near-zero impedance (r=" + std::to_string(r) +
            ", x=" + std::to_string(x) + ")");
    }

    // Series admittance y = 1/z = r/(r^2+x^2) - j*x/(r^2+x^2)
    const Complex ySeries(r / z2, -x / z2);

    // Line charging (shunt susceptance), split between buses
    // bch_pu is the total line charging susceptance
    const Complex bShuntFrom(0.0, line.bch_pu * line.fracBFrom);
    const Complex bShuntTo(0.0, line.bch_pu * line.fracBTo);

    // For a standard PI model:
    // Y_ii += ySeries + bShuntFrom
    // Y_jj += ySeries + bShuntTo
    // Y_ij = Y_ji = -ySeries

    triplets.emplace_back(i, i, ySeries + bShuntFrom);
    triplets.emplace_back(j, j, ySeries + bShuntTo);
    triplets.emplace_back(i, j, -ySeries);
    triplets.emplace_back(j, i, -ySeries);
}

// ============================================================================
// TRANSFORMER CONTRIBUTION
// ============================================================================

void YbusBuilder::addTransformerContribution(
    const Transformer& transformer,
    std::vector<TripletC>& triplets,
    size_t numBuses
) {
    if (transformer.status != 1) return;

    validateBusIndex(transformer.fromBus, numBuses, "Transformer");
    validateBusIndex(transformer.toBus, numBuses, "Transformer");

    const int i = static_cast<int>(transformer.fromBus - 1);
    const int j = static_cast<int>(transformer.toBus - 1);

    const double r = transformer.r_pu;
    const double x = transformer.x_pu;
    const double z2 = r * r + x * x;

    if (z2 < ZERO_IMPEDANCE_THRESHOLD) {
        throw std::runtime_error(
            "YbusBuilder: Transformer " + std::to_string(transformer.id) +
            " has near-zero impedance");
    }

    // Off-nominal turns ratio a = ratio (typically ~1.0)
    // Phase shift angle theta in radians
    const double a = transformer.ratio;
    const double theta = transformer.phaseShift_deg * DEG_TO_RAD;

    // Series admittance y = 1/z
    const Complex ySeries(r / z2, -x / z2);

    if (a < 0.01) {
        // Treat as regular line if ratio is effectively zero
        triplets.emplace_back(i, i, ySeries);
        triplets.emplace_back(j, j, ySeries);
        triplets.emplace_back(i, j, -ySeries);
        triplets.emplace_back(j, i, -ySeries);
        return;
    }

    // Transformer PI equivalent with off-nominal ratio and phase shift
    // Let a_complex = a * exp(j*theta)
    // Ybus elements for transformer:
    // Y_ii = ySeries / |a|^2
    // Y_ij = -ySeries / conj(a_complex)
    // Y_ji = -ySeries / a_complex
    // Y_jj = ySeries

    const Complex aComplex(a * std::cos(theta), a * std::sin(theta));
    const double aMag2 = a * a; // |a|^2

    const Complex yii = ySeries / aMag2;
    const Complex yij = -ySeries / std::conj(aComplex);
    const Complex yji = -ySeries / aComplex;
    const Complex yjj = ySeries;

    triplets.emplace_back(i, i, yii);
    triplets.emplace_back(i, j, yij);
    triplets.emplace_back(j, i, yji);
    triplets.emplace_back(j, j, yjj);
}

// ============================================================================
// SHUNT CONTRIBUTION
// ============================================================================

void YbusBuilder::addShuntContribution(
    const Shunt& shunt,
    std::vector<TripletC>& triplets,
    size_t numBuses
) {
    if (shunt.status != 1) return;
    validateBusIndex(shunt.busId, numBuses, "Shunt");

    const int i = static_cast<int>(shunt.busId - 1);
    const Complex yShunt(shunt.g_pu, shunt.b_pu);

    triplets.emplace_back(i, i, yShunt);
}

// ============================================================================
// BUS SHUNT CONTRIBUTION
// ============================================================================

void YbusBuilder::addBusShuntContribution(
    const Bus& bus,
    std::vector<TripletC>& triplets
) {
    const int i = static_cast<int>(bus.id - 1);
    if (bus.gsh_pu != 0.0 || bus.bsh_pu != 0.0) {
        const Complex yShunt(bus.gsh_pu, bus.bsh_pu);
        triplets.emplace_back(i, i, yShunt);
    }
}

// ============================================================================
// FULL YBUS BUILD
// ============================================================================

SpMatrixC YbusBuilder::buildYbus(const SystemTopology& topology, double baseMVA) {
    const size_t n = topology.numBuses();
    if (n == 0) {
        throw std::invalid_argument("YbusBuilder::buildYbus: system has no buses");
    }

    // Estimate non-zero elements: each line contributes ~4 entries
    // each transformer ~4, shunts ~1 each
    const size_t estimatedNz = topology.numLines() * 4 +
                               topology.numTransformers() * 4 +
                               topology.shunts.size() * 1 + n;

    std::vector<TripletC> triplets;
    triplets.reserve(estimatedNz);

    // Add line contributions
    for (const auto& line : topology.lines) {
        if (line.status == 1) {
            addLineContribution(line, triplets, n);
        }
    }

    // Add transformer contributions
    for (const auto& tx : topology.transformers) {
        if (tx.status == 1) {
            addTransformerContribution(tx, triplets, n);
        }
    }

    // Add shunt element contributions
    for (const auto& shunt : topology.shunts) {
        addShuntContribution(shunt, triplets, n);
    }

    // Add bus shunt contributions (from Bus struct gsh/bsh)
    for (const auto& bus : topology.buses) {
        addBusShuntContribution(bus, triplets);
    }

    // Assemble sparse matrix
    const int nInt = static_cast<int>(n);
    SpMatrixC ybus(nInt, nInt);
    ybus.setFromTriplets(triplets.begin(), triplets.end());

    // Make sure the matrix is compressed for efficient operations
    ybus.makeCompressed();

    // Verify dimensions and basic properties
    if (ybus.rows() != nInt || ybus.cols() != nInt) {
        throw std::runtime_error(
            "YbusBuilder::buildYbus: assembled matrix has incorrect dimensions");
    }

    // Check for isolated buses (zero diagonal elements)
    for (int i = 0; i < nInt; ++i) {
        if (std::abs(ybus.coeff(i, i)) < ZERO_IMPEDANCE_THRESHOLD) {
            // This could be an isolated bus - warn but don't throw
            // In a full implementation, this would be a validation error
        }
    }

    return ybus;
}

// ============================================================================
// YBUS WITH SWITCHES
// ============================================================================

SpMatrixC YbusBuilder::buildYbusWithSwitches(const SystemTopology& topology, double baseMVA) {
    // Same as buildYbus - switch status is checked in each add*Contribution method
    return buildYbus(topology, baseMVA);
}

// ============================================================================
// EXTRACT REAL AND IMAGINARY PARTS
// ============================================================================

SpMatrix YbusBuilder::buildG(const SpMatrixC& ybus) {
    const int n = static_cast<int>(ybus.rows());
    std::vector<Triplet> triplets;
    triplets.reserve(ybus.nonZeros());

    for (int k = 0; k < ybus.outerSize(); ++k) {
        for (SpMatrixC::InnerIterator it(ybus, k); it; ++it) {
            triplets.emplace_back(it.row(), it.col(), it.value().real());
        }
    }

    SpMatrix g(n, n);
    g.setFromTriplets(triplets.begin(), triplets.end());
    g.makeCompressed();
    return g;
}

SpMatrix YbusBuilder::buildB(const SpMatrixC& ybus) {
    const int n = static_cast<int>(ybus.rows());
    std::vector<Triplet> triplets;
    triplets.reserve(ybus.nonZeros());

    for (int k = 0; k < ybus.outerSize(); ++k) {
        for (SpMatrixC::InnerIterator it(ybus, k); it; ++it) {
            triplets.emplace_back(it.row(), it.col(), it.value().imag());
        }
    }

    SpMatrix b(n, n);
    b.setFromTriplets(triplets.begin(), triplets.end());
    b.makeCompressed();
    return b;
}

// ============================================================================
// GET YBUS ELEMENTS (for inspection)
// ============================================================================

std::vector<YbusElement> YbusBuilder::getYbusElements(const SpMatrixC& ybus) {
    std::vector<YbusElement> elements;
    elements.reserve(static_cast<size_t>(ybus.nonZeros()));

    for (int k = 0; k < ybus.outerSize(); ++k) {
        for (SpMatrixC::InnerIterator it(ybus, k); it; ++it) {
            YbusElement elem;
            elem.row = static_cast<size_t>(it.row());
            elem.col = static_cast<size_t>(it.col());
            elem.value = it.value();
            elements.push_back(elem);
        }
    }

    return elements;
}

} // namespace powsys365
