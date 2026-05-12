#pragma once
#include "../../commons/types.h"
#include "../../commons/math_utils.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <stdexcept>
#include <memory>

namespace powsys365 {

/**
 * PowerSystem - Central model class representing an electric power system.
 *
 * Manages all network elements (buses, lines, transformers, generators, loads)
 * and provides methods for building the Ybus matrix, initializing voltages,
 * and computing power injections.
 */
class PowerSystem {
public:
    PowerSystem();
    explicit PowerSystem(double baseMVA);

    // ========================================================================
    // ELEMENT ADDERS
    // ========================================================================

    /** Add a bus to the system. Bus ID must be >= 1. */
    void addBus(const Bus& bus);

    /** Add a transmission line. */
    void addLine(const Line& line);

    /** Add a transformer. */
    void addTransformer(const Transformer& transformer);

    /** Add a generator. */
    void addGenerator(const Generator& generator);

    /** Add a load. */
    void addLoad(const Load& load);

    /** Add a shunt element. */
    void addShunt(const Shunt& shunt);

    // ========================================================================
    // YBUS MANAGEMENT
    // ========================================================================

    /** Build Ybus from current topology. Must have at least 1 bus. */
    void buildYbus();

    /** Get the complex Ybus matrix. buildYbus() must be called first. */
    const SpMatrixC& getYbus() const;

    /** Get real part (G) of Ybus. */
    SpMatrix getG() const;

    /** Get imaginary part (B) of Ybus. */
    SpMatrix getB() const;

    // ========================================================================
    // VOLTAGE MANAGEMENT
    // ========================================================================

    /** Initialize voltages: flat start (1.0 pu, 0 deg) or from PV setpoints. */
    void initializeVoltages();

    /** Update bus voltage magnitudes and angles from vectors. */
    void updateBusVoltages(const DenseVector& vm, const DenseVector& va_rad);

    /** Get voltage magnitudes [pu] as vector (order matches bus IDs). */
    DenseVector getVm() const;

    /** Get voltage angles [radians] as vector. */
    DenseVector getVa() const;

    /** Get voltage angles [degrees] as vector. */
    DenseVector getVaDegrees() const;

    /** Get complex voltage vector. */
    DenseVectorC getComplexVoltages() const;

    // ========================================================================
    // POWER CALCULATIONS
    // ========================================================================

    /** Calculate P and Q injections at all buses using current voltages. */
    void calculateInjectedPowers(DenseVector& pOut, DenseVector& qOut) const;

    /** Get total real power generation [pu]. */
    double getTotalPGen() const;

    /** Get total reactive power generation [pu]. */
    double getTotalQGen() const;

    /** Get total real power load [pu]. */
    double getTotalPLoad() const;

    /** Get total reactive power load [pu]. */
    double getTotalQLoad() const;

    /** Get total shunt conductance losses [pu]. */
    double getTotalShuntPLoss() const;

    /** Get total shunt susceptance injection [pu]. */
    double getTotalShuntQInjection() const;

    // ========================================================================
    // VALIDATION
    // ========================================================================

    /** Check system validity: has buses, has slack, is connected. */
    bool isValid() const;

    /** Check if system has at least one slack bus. */
    bool hasSlackBus() const;

    /** Check if the network graph is connected (all buses reachable from slack). */
    bool isConnected() const;

    /** Check all bus voltages against their limits. Returns violations. */
    std::vector<Violation> checkVoltageLimits() const;

    // ========================================================================
    // IEEE TEST SYSTEM LOADING
    // ========================================================================

    /** Load the complete IEEE 14-bus test system. */
    void loadIEEE14();

    /** Load minimal IEEE 30-bus skeleton (full data via database). */
    void loadIEEE30();

    /** Load IEEE 57-bus test system. */
    void loadIEEE57();

    /** Load IEEE 118-bus test system. */
    void loadIEEE118();

    // ========================================================================
    // ACCESSORS
    // ========================================================================

    size_t numBuses() const { return topology_.buses.size(); }
    size_t numLines() const { return topology_.lines.size(); }
    size_t numTransformers() const { return topology_.transformers.size(); }
    size_t numGenerators() const { return topology_.generators.size(); }
    size_t numLoads() const { return topology_.loads.size(); }
    size_t numShunts() const { return topology_.shunts.size(); }

    const std::vector<Bus>& getBuses() const { return topology_.buses; }
    const std::vector<Line>& getLines() const { return topology_.lines; }
    const std::vector<Transformer>& getTransformers() const { return topology_.transformers; }
    const std::vector<Generator>& getGenerators() const { return topology_.generators; }
    const std::vector<Load>& getLoads() const { return topology_.loads; }

    Bus* getBus(size_t id);
    const Bus* getBus(size_t id) const;
    Line* getLine(size_t id);
    Generator* getGenerator(size_t id);

    double getBaseMVA() const { return baseMVA_; }
    void setBaseMVA(double baseMVA) { baseMVA_ = baseMVA; }

    const SystemTopology& getTopology() const { return topology_; }

    /** Check if Ybus has been built. */
    bool hasYbus() const { return ybusBuilt_; }

    /** Clear all data and reset to empty system. */
    void clear();

private:
    SystemTopology topology_;
    SpMatrixC ybus_;
    bool ybusBuilt_ = false;
    double baseMVA_ = BASE_MVA_DEFAULT;

    // Internal bus ID to index mapping (1-based ID -> 0-based index)
    std::unordered_map<size_t, size_t> busIndexMap_;

    void rebuildBusIndexMap();
    size_t getBusIndex(size_t busId) const;
    bool busExists(size_t busId) const;
};

} // namespace powsys365
