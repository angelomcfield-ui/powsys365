#pragma once
#include "../../commons/types.h"
#include <vector>

namespace powsys365 {

/**
 * YbusBuilder - Constructs the nodal admittance matrix (Ybus) from system topology.
 *
 * The Ybus matrix is fundamental to power flow analysis. Each element Y_ij
 * represents the admittance between buses i and j. The diagonal Y_ii is the
 * sum of all admittances connected to bus i (self-admittance).
 *
 * For a line between buses i and j with series impedance z = r + jx and
 * total shunt charging b: Y_ii += y_ii + b/2, Y_jj += y_ii + b/2, Y_ij = Y_ji = -y_ij
 * where y_ij = 1/z and y_ii = y_ij (for the off-diagonal contribution).
 */
class YbusBuilder {
public:
    YbusBuilder() = default;

    /**
     * Build the complete Ybus matrix from system topology.
     * @param topology System topology containing all network elements
     * @param baseMVA System base MVA for per-unit conversions
     * @return Complex sparse Ybus matrix
     */
    SpMatrixC buildYbus(const SystemTopology& topology, double baseMVA = BASE_MVA_DEFAULT);

    /**
     * Build Ybus considering switch statuses (open/closed).
     * Only elements with status == 1 are included.
     */
    SpMatrixC buildYbusWithSwitches(const SystemTopology& topology, double baseMVA = BASE_MVA_DEFAULT);

    /** Extract real part (conductance matrix G) from Ybus */
    SpMatrix buildG(const SpMatrixC& ybus);

    /** Extract imaginary part (susceptance matrix B) from Ybus */
    SpMatrix buildB(const SpMatrixC& ybus);

    /** Get individual Ybus elements as a list (for inspection/debugging) */
    std::vector<YbusElement> getYbusElements(const SpMatrixC& ybus);

    /** Add contribution of a transmission line to Ybus triplet list */
    void addLineContribution(
        const Line& line,
        std::vector<TripletC>& triplets,
        size_t numBuses
    );

    /** Add contribution of a transformer to Ybus triplet list */
    void addTransformerContribution(
        const Transformer& transformer,
        std::vector<TripletC>& triplets,
        size_t numBuses
    );

    /** Add contribution of shunt elements to Ybus triplet list */
    void addShuntContribution(
        const Shunt& shunt,
        std::vector<TripletC>& triplets,
        size_t numBuses
    );

    /** Add bus shunt contributions (Gsh + jBsh) */
    void addBusShuntContribution(
        const Bus& bus,
        std::vector<TripletC>& triplets
    );

private:
    // Validate that bus indices are within range
    void validateBusIndex(size_t busId, size_t numBuses, const std::string& elementType);
};

} // namespace powsys365
