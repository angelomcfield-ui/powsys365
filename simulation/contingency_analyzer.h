#pragma once

#include "state_estimator.h"

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <algorithm>

namespace powsys365::simulation {

// ============================================================================
// Contingency Type
// ============================================================================
enum class ContingencyType {
    LineOutage,         // Transmission line outage (N-1)
    GeneratorOutage,    // Generator outage (N-1)
    TransformerOutage,  // Transformer outage
    BusOutage,          // Bus outage
    ShuntOutage,        // Shunt device outage
    MultipleOutage,     // Multiple simultaneous outages (N-2, N-k)
    LineToGroundFault,  // LG fault
    LineToLineFault,    // LL fault
    ThreePhaseFault,    // 3-phase fault
    Custom              // User-defined contingency
};

// ============================================================================
// Contingency Definition
// ============================================================================
struct Contingency {
    uint32_t id = 0;
    ContingencyType type = ContingencyType::LineOutage;
    uint32_t elementId = 0;          // Primary element ID (lineId, genId, etc.)
    uint32_t secondaryElementId = 0; // For N-2 contingencies
    std::string name;
    std::string description;
    double probability = 0.0;        // Probability of occurrence
    double severityWeight = 1.0;     // Weighting factor for severity

    Contingency() = default;
    Contingency(uint32_t cid, ContingencyType ctype, uint32_t elemId,
                std::string cname, std::string desc = "");
    Contingency(uint32_t cid, ContingencyType ctype, uint32_t elemId1, uint32_t elemId2,
                std::string cname, std::string desc = "");
};

// ============================================================================
// Contingency Result
// ============================================================================
struct ContingencyResult {
    uint32_t contingencyId = 0;
    ContingencyType type = ContingencyType::LineOutage;
    std::string contingencyName;
    bool converged = false;
    bool hasViolation = false;

    // Power flow results
    std::vector<double> busVoltage;       // Post-contingency bus voltages (pu)
    std::vector<double> busAngle;         // Post-contingency bus angles (rad)
    std::vector<double> lineLoading;      // Line loading percentages
    std::vector<double> genOutput;        // Generator outputs

    // Severity indices
    double severityIndex = 0.0;           // Composite severity index
    double maxVoltageDeviation = 0.0;     // Max |V| deviation from base case
    double maxAngleDeviation = 0.0;       // Max angle deviation from base case
    double maxLineLoading = 0.0;          // Maximum line loading (%)
    int overloadedLines = 0;              // Number of overloaded lines
    int lowVoltageBuses = 0;              // Buses with V < 0.95 pu
    int highVoltageBuses = 0;             // Buses with V > 1.05 pu
    double totalLoss = 0.0;              // Total system losses (MW)

    // Performance index (PI) - commonly used in contingency ranking
    double performanceIndex = 0.0;

    // Iteration count
    int iterations = 0;

    // Error info
    std::string errorMessage;
    double computationTime = 0.0;         // Computation time in seconds
};

// ============================================================================
// Severity Ranking
// ============================================================================
struct SeverityRanking {
    uint32_t contingencyId = 0;
    std::string name;
    ContingencyType type;
    double severity = 0.0;
    int rank = 0;
    double maxVoltageDeviation = 0.0;
    double maxLineLoading = 0.0;
    int overloadedLines = 0;
    int lowVoltageBuses = 0;
    int highVoltageBuses = 0;
    double performanceIndex = 0.0;
};

// ============================================================================
// Contingency Analyzer Configuration
// ============================================================================
struct ContingencyConfig {
    double voltageMinLimit = 0.95;        // Lower voltage limit (pu)
    double voltageMaxLimit = 1.05;        // Upper voltage limit (pu)
    double lineLoadingLimit = 100.0;      // Line loading limit (%)
    double emergencyLineLoadingLimit = 120.0; // Emergency loading limit (%)
    double angleDifferenceLimit = 60.0;   // Max angle difference (degrees)
    double severityWeightVoltage = 1.0;   // Weight for voltage deviations
    double severityWeightLoading = 2.0;   // Weight for line loading
    double severityWeightLosses = 0.5;    // Weight for losses
    double severityWeightStability = 1.5; // Weight for stability
    int maxIterations = 20;               // Max power flow iterations
    double tolerance = 1e-6;              // Power flow tolerance
    bool checkAllContingencies = true;    // Check all N-1 contingencies
    bool rankBySeverity = true;           // Rank results by severity
    bool parallelAnalysis = false;        // Parallel contingency analysis
    uint32_t maxContingencies = 1000;     // Max number of contingencies to analyze
    std::function<void(uint32_t, uint32_t)> progressCallback; // (current, total)
};

// ============================================================================
// Power Flow Result (Base Case)
// ============================================================================
struct PowerFlowResult {
    bool converged = false;
    std::vector<double> busVoltage;
    std::vector<double> busAngle;
    std::vector<double> busP;
    std::vector<double> busQ;
    std::vector<double> linePFrom;
    std::vector<double> lineQFrom;
    std::vector<double> linePTo;
    std::vector<double> lineQTo;
    std::vector<double> lineLoading;
    double totalLoss = 0.0;
    double totalGeneration = 0.0;
    double totalLoad = 0.0;
    int iterations = 0;
    std::string errorMessage;
};

// ============================================================================
// Contingency Analyzer
// ============================================================================
class ContingencyAnalyzer {
public:
    ContingencyAnalyzer();
    explicit ContingencyAnalyzer(const ContingencyConfig& config);
    ~ContingencyAnalyzer() = default;

    // Disable copy, enable move
    ContingencyAnalyzer(const ContingencyAnalyzer&) = delete;
    ContingencyAnalyzer& operator=(const ContingencyAnalyzer&) = delete;
    ContingencyAnalyzer(ContingencyAnalyzer&&) noexcept;
    ContingencyAnalyzer& operator=(ContingencyAnalyzer&&) noexcept;

    // ------------------------------------------------------------------------
    // System Setup
    // ------------------------------------------------------------------------
    void setBuses(const std::vector<BusData>& buses);
    void setBranches(const std::vector<BranchData>& branches);
    void setGenerators(const std::vector<BusData>& generators);

    void addBus(const BusData& bus);
    void addBranch(const BranchData& branch);
    void addGenerator(const BusData& generator);

    void clearBuses();
    void clearBranches();
    void clearGenerators();
    void clearAll();

    // ------------------------------------------------------------------------
    // Configuration
    // ------------------------------------------------------------------------
    void setConfig(const ContingencyConfig& config);
    const ContingencyConfig& config() const noexcept;

    // ------------------------------------------------------------------------
    // Base Case Power Flow
    // ------------------------------------------------------------------------
    PowerFlowResult solveBaseCase();
    const PowerFlowResult& baseCaseResult() const noexcept;

    // ------------------------------------------------------------------------
    // N-1 Contingency Screening
    // ------------------------------------------------------------------------
    std::vector<ContingencyResult> screenN1();
    std::vector<ContingencyResult> screenN1(const PowerFlowResult& currentState);

    // ------------------------------------------------------------------------
    // Individual Contingency Checks
    // ------------------------------------------------------------------------
    ContingencyResult checkLineOutage(uint32_t lineId);
    ContingencyResult checkLineOutage(uint32_t fromBus, uint32_t toBus);
    ContingencyResult checkGeneratorOutage(uint32_t genId);
    ContingencyResult checkTransformerOutage(uint32_t transformerId);
    ContingencyResult checkBusOutage(uint32_t busId);
    ContingencyResult checkContingency(const Contingency& contingency);

    // ------------------------------------------------------------------------
    // Severity Analysis
    // ------------------------------------------------------------------------
    double computeSeverityIndex(const ContingencyResult& result) const;
    std::vector<SeverityRanking> rankBySeverity(const std::vector<ContingencyResult>& results) const;
    std::vector<SeverityRanking> getCriticalContingencies(const std::vector<ContingencyResult>& results,
                                                           double threshold = 0.8) const;

    // ------------------------------------------------------------------------
    // Batch Analysis
    // ------------------------------------------------------------------------
    std::vector<ContingencyResult> analyzeAllContingencies();
    std::vector<ContingencyResult> analyzeContingencyList(const std::vector<Contingency>& contingencies);

    // ------------------------------------------------------------------------
    // Post-Processing
    // ------------------------------------------------------------------------
    bool hasViolations(const ContingencyResult& result) const;
    std::vector<std::string> getViolationReport(const ContingencyResult& result) const;

    // ------------------------------------------------------------------------
    // Statistics
    // ------------------------------------------------------------------------
    size_t numBuses() const noexcept;
    size_t numBranches() const noexcept;
    size_t numGenerators() const noexcept;
    size_t numContingencies() const noexcept;
    const std::string& lastError() const noexcept;

private:
    ContingencyConfig m_config;
    std::vector<BusData> m_buses;
    std::vector<BranchData> m_branches;
    std::vector<BusData> m_generators;
    std::vector<Contingency> m_contingencies;
    PowerFlowResult m_baseCase;
    std::string m_lastError;

    // Y-bus matrices
    std::vector<std::vector<double>> m_ybusG;
    std::vector<std::vector<double>> m_ybusB;
    bool m_ybusValid = false;

    // Bus index mapping
    std::unordered_map<uint32_t, uint32_t> m_busIndex;
    int m_slackBus = -1;

    // Internal methods
    void buildBusIndex();
    void buildYBus();
    uint32_t getBusIdx(uint32_t busId) const;

    PowerFlowResult solvePowerFlow(const std::vector<BusData>& buses,
                                    const std::vector<BranchData>& branches);

    void computeInjections(const std::vector<double>& V, const std::vector<double>& theta,
                           std::vector<double>& P, std::vector<double>& Q);

    void computeJacobianPF(const std::vector<double>& V, const std::vector<double>& theta,
                            std::vector<std::vector<double>>& J);

    bool newtonRaphsonPF(const std::vector<BusData>& buses,
                          const std::vector<BranchData>& branches,
                          std::vector<double>& V, std::vector<double>& theta,
                          int maxIter, double tol);

    double computeLineFlow(uint32_t fromIdx, uint32_t toIdx,
                            double Vf, double Vt, double thetaf, double thetat,
                            bool realPower) const;

    double computeLineLoading(uint32_t branchIdx,
                               const std::vector<double>& V,
                               const std::vector<double>& theta) const;

    double computeTotalLosses(const std::vector<double>& V,
                               const std::vector<double>& theta) const;

    ContingencyResult analyzeLineOutageInternal(uint32_t branchIdx);
    ContingencyResult analyzeGeneratorOutageInternal(uint32_t genIdx);

    void computeSeverity(ContingencyResult& result, const PowerFlowResult& base) const;
    void logProgress(uint32_t current, uint32_t total);
};

} // namespace powsys365::simulation
