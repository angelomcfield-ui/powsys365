#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include <cstdint>
#include <cmath>
#include <limits>
#include <sstream>
#include <iomanip>

namespace powsys365::simulation {

// ============================================================================
// Bus Type for Power Flow
// ============================================================================
enum class BusType {
    PQ = 0,       // Load bus (P, Q specified)
    PV = 1,       // Generator bus (P, |V| specified)
    Slack = 2,    // Slack/reference bus (|V|, theta specified)
    Isolated = 3  // Isolated bus
};

// ============================================================================
// Measurement Types for Power Systems
// ============================================================================
enum class MeasurementType {
    VoltageMagnitude,     // |V| at a bus
    VoltageAngle,         // theta at a bus
    RealPowerInjection,   // P at a bus
    ReactivePowerInjection, // Q at a bus
    RealPowerFlow,        // P on a line (from bus)
    ReactivePowerFlow,    // Q on a line (from bus)
    CurrentMagnitude,     // |I| on a line
    RealPowerBranch,      // P on a branch
    ReactivePowerBranch,  // Q on a branch
    TapPosition,          // Transformer tap ratio
    ShuntSusceptance,     // Shunt B value
    Generic               // Generic measurement
};

// ============================================================================
// Bus Data
// ============================================================================
struct BusData {
    uint32_t busId = 0;
    double baseVoltage = 1.0;     // Base voltage in kV
    BusType type = BusType::PQ;   // PQ, PV, Slack
    double voltage = 1.0;         // Per-unit voltage magnitude
    double angle = 0.0;           // Voltage angle in radians
    double pGen = 0.0;            // Real generation
    double qGen = 0.0;            // Reactive generation
    double pLoad = 0.0;           // Real load
    double qLoad = 0.0;           // Reactive load
    double gShunt = 0.0;          // Shunt conductance
    double bShunt = 0.0;          // Shunt susceptance
    std::string name;
};

// ============================================================================
// Branch Data
// ============================================================================
struct BranchData {
    uint32_t branchId = 0;
    uint32_t fromBus = 0;
    uint32_t toBus = 0;
    double r = 0.0;               // Resistance (pu)
    double x = 0.0;               // Reactance (pu)
    double g = 0.0;               // Total line charging conductance (pu)
    double b = 0.0;               // Total line charging susceptance (pu)
    double tapRatio = 1.0;        // Transformer tap ratio
    double phaseShift = 0.0;      // Transformer phase shift (radians)
    double rateA = 0.0;           // Rate A (MVA)
    double rateB = 0.0;           // Rate B (MVA)
    double rateC = 0.0;           // Rate C (MVA)
    bool inService = true;        // Is branch in service?
    std::string name;
};

// ============================================================================
// Measurement
// ============================================================================
struct Measurement {
    uint32_t id = 0;
    MeasurementType type = MeasurementType::Generic;
    uint32_t busId = 0;           // Bus number (1-based)
    uint32_t fromBus = 0;         // From bus for branch measurements
    uint32_t toBus = 0;           // To bus for branch measurements
    double value = 0.0;           // Measured value
    double weight = 1.0;          // Measurement weight (1/sigma^2)
    double sigma = 0.01;          // Standard deviation
    bool isPmu = false;           // Is this a PMU measurement?
    bool isActive = true;         // Is this measurement active?
    std::string name;             // Measurement name/label

    Measurement() = default;
    Measurement(uint32_t mid, MeasurementType mtype, uint32_t bus, double val, double sig);
    Measurement(uint32_t mid, MeasurementType mtype, uint32_t fbus, uint32_t tbus,
                double val, double sig);

    double residual() const noexcept { return 0.0; }
    double normalizedResidual() const noexcept { return 0.0; }
};

// ============================================================================
// State Vector
// ============================================================================
struct StateVector {
    std::vector<double> voltage;     // Voltage magnitudes (pu)
    std::vector<double> angle;       // Voltage angles (radians)
    std::vector<double> tapRatio;    // Transformer tap ratios
    std::vector<double> shuntB;      // Shunt susceptance values

    size_t numBuses() const noexcept { return voltage.size(); }
    void resize(size_t nBuses, size_t nTaps = 0, size_t nShunts = 0);
    std::vector<double> toFlatVector() const;
    void fromFlatVector(const std::vector<double>& flat, size_t nBuses);
};

// ============================================================================
// Jacobian Matrix (sparse representation)
// ============================================================================
struct SparseJacobian {
    std::vector<uint32_t> rowIndices;
    std::vector<uint32_t> colIndices;
    std::vector<double> values;
    uint32_t numRows = 0;
    uint32_t numCols = 0;

    void add(uint32_t row, uint32_t col, double val);
    void clear();
    double get(uint32_t row, uint32_t col) const;
};

// ============================================================================
// WLS Estimation Result
// ============================================================================
struct WLSResult {
    StateVector state;
    std::vector<double> residuals;
    std::vector<double> normalizedResiduals;
    double objectiveFunction = 0.0;
    double chiSquare = 0.0;
    int iterations = 0;
    double maxNormalizedResidual = 0.0;
    int maxResidualIndex = -1;
    bool converged = false;
    std::string errorMessage;
    std::vector<bool> badDataDetected;
    SparseJacobian jacobian;
    std::vector<std::vector<double>> covarianceMatrix;
};

// ============================================================================
// Bad Data Detection Result
// ============================================================================
struct BadDataResult {
    std::vector<int> suspectMeasurements;
    std::vector<double> normalizedResiduals;
    double maxNormalizedResidual = 0.0;
    int worstMeasurementIndex = -1;
    bool badDataFound = false;
    double threshold = 3.0;
};

// ============================================================================
// WLS Configuration
// ============================================================================
struct WLSConfig {
    double tolerance = 1e-6;
    int maxIterations = 50;
    double badDataThreshold = 3.0;
    bool enableBadDataDetection = true;
    bool removeBadData = true;
    int maxBadDataRemovals = 3;
    bool useFlatStart = true;
    bool useDecoupled = false;
    double minSingularValue = 1e-12;
};

// ============================================================================
// State Estimator (Weighted Least Squares)
// ============================================================================
class StateEstimator {
public:
    StateEstimator();
    explicit StateEstimator(const WLSConfig& config);
    ~StateEstimator() = default;

    // Disable copy, enable move
    StateEstimator(const StateEstimator&) = delete;
    StateEstimator& operator=(const StateEstimator&) = delete;
    StateEstimator(StateEstimator&&) noexcept;
    StateEstimator& operator=(StateEstimator&&) noexcept;

    // ------------------------------------------------------------------------
    // System Setup
    // ------------------------------------------------------------------------
    void addBus(const BusData& bus);
    void addBranch(const BranchData& branch);
    void addMeasurement(const Measurement& measurement);

    void setBuses(const std::vector<BusData>& buses);
    void setBranches(const std::vector<BranchData>& branches);
    void setMeasurements(const std::vector<Measurement>& measurements);

    void clearBuses();
    void clearBranches();
    void clearMeasurements();
    void clearAll();

    size_t numBuses() const noexcept;
    size_t numBranches() const noexcept;
    size_t numMeasurements() const noexcept;

    // ------------------------------------------------------------------------
    // Configuration
    // ------------------------------------------------------------------------
    void setConfig(const WLSConfig& config);
    const WLSConfig& config() const noexcept;

    // ------------------------------------------------------------------------
    // WLS Estimation
    // ------------------------------------------------------------------------
    WLSResult estimate();
    WLSResult estimate(const StateVector& initialState);
    WLSResult estimateFlatStart();

    // ------------------------------------------------------------------------
    // Jacobian Computation
    // ------------------------------------------------------------------------
    SparseJacobian computeJacobian(const StateVector& state) const;
    std::vector<std::vector<double>> computeJacobianDense(const StateVector& state) const;

    // ------------------------------------------------------------------------
    // Measurement Functions (h(x))
    // ------------------------------------------------------------------------
    double computeMeasurement(const Measurement& measurement, const StateVector& state) const;
    std::vector<double> computeMeasurements(const StateVector& state) const;

    // ------------------------------------------------------------------------
    // Residuals
    // ------------------------------------------------------------------------
    std::vector<double> computeResiduals(const StateVector& state) const;
    double computeObjectiveFunction(const StateVector& state) const;

    // ------------------------------------------------------------------------
    // Bad Data Detection
    // ------------------------------------------------------------------------
    BadDataResult detectBadData(const WLSResult& result) const;
    BadDataResult detectBadData(const std::vector<double>& normalizedResiduals) const;
    bool removeBadDataMeasurement(size_t measurementIndex);

    // ------------------------------------------------------------------------
    // Covariance Matrix
    // ------------------------------------------------------------------------
    std::vector<std::vector<double>> computeCovarianceMatrix(const SparseJacobian& jacobian) const;

    // ------------------------------------------------------------------------
    // Status
    // ------------------------------------------------------------------------
    bool isSystemValid() const;
    const std::string& lastError() const noexcept;

    // ------------------------------------------------------------------------
    // Admittance Matrix (Ybus)
    // ------------------------------------------------------------------------
    std::vector<std::vector<double>> getYBusReal() const;
    std::vector<std::vector<double>> getYBusImag() const;

private:
    WLSConfig m_config;
    std::vector<BusData> m_buses;
    std::vector<BranchData> m_branches;
    std::vector<Measurement> m_measurements;
    std::string m_lastError;

    std::vector<std::vector<double>> m_ybusG;
    std::vector<std::vector<double>> m_ybusB;
    bool m_ybusValid = false;

    std::unordered_map<uint32_t, uint32_t> m_busIndexMap;
    int m_slackBusIndex = -1;

    void buildBusIndexMap();
    void buildYBus();
    void validateSystem();
    StateVector getInitialState() const;
    void solveNormalEquations(const std::vector<std::vector<double>>& H,
                               const std::vector<double>& rhs,
                               std::vector<double>& dx);
    void solveLinearSystemLU(std::vector<std::vector<double>>& A,
                              std::vector<double>& b);
    void solveLinearSystemCholesky(std::vector<std::vector<double>>& A,
                                    std::vector<double>& b);
    double computeHij(const Measurement& m, const StateVector& state) const;
    double voltageMagnitude(uint32_t busIdx, const StateVector& state) const;
    double voltageAngle(uint32_t busIdx, const StateVector& state) const;
    uint32_t getBusIndex(uint32_t busId) const;
    std::vector<double> computeMeasurementVector(const StateVector& state) const;
};

// ============================================================================
// Matrix utilities
// ============================================================================
namespace MatrixUtils {

    std::vector<double> matVecMul(const std::vector<std::vector<double>>& A,
                                   const std::vector<double>& x);

    std::vector<std::vector<double>> transpose(const std::vector<std::vector<double>>& A);

    std::vector<std::vector<double>> matMul(const std::vector<std::vector<double>>& A,
                                             const std::vector<std::vector<double>>& B);

    void addIdentity(std::vector<std::vector<double>>& A, double lambda);

    std::string toString(const std::vector<std::vector<double>>& A, int precision = 6);

} // namespace MatrixUtils

} // namespace powsys365::simulation
