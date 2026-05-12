#pragma once
#include "matrix_types.h"
#include "constants.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <stdexcept>
#include <optional>

namespace powsys365 {

// ============================================================================
// ENUMERATIONS
// ============================================================================

enum class BusType : int {
    PQ = 1,     // Load bus (P, Q specified; V, delta unknown)
    PV = 2,     // Generator bus (P, V specified; Q, delta unknown)
    Slack = 3   // Reference/slack bus (V, delta specified; P, Q unknown)
};

enum class GeneratorType : int {
    Thermal = 1,
    Hydro = 2,
    Nuclear = 3,
    Wind = 4,
    Solar = 5,
    Diesel = 6,
    Gas = 7,
    Biomass = 8,
    Battery = 9
};

enum class LineModel : int {
    PI = 1,              // Equivalent PI model
    Distributed = 2,     // Distributed parameter model
    T = 3                // T equivalent model
};

enum class LoadModel : int {
    ConstantPQ = 1,      // Constant power (standard power flow)
    ConstantZ = 2,       // Constant impedance
    ConstantI = 3,       // Constant current
    ZIP = 4              // Polynomial ZIP model
};

enum class FaultType : int {
    ThreePhase = 1,      // Three-phase symmetrical fault
    SinglePhase = 2,     // Single-phase to ground
    TwoPhase = 3,        // Phase-to-phase fault
    TwoPhaseG = 4,       // Two-phase to ground
    SinglePhaseG = 5     // Single-phase ground (alternate naming)
};

enum class SolverMethod : int {
    NewtonRaphson = 1,
    FastDecoupledXB = 2,
    FastDecoupledBX = 3,
    GaussSeidel = 4
};

enum class ConvergenceStatus : int {
    Converged = 0,
    MaxIterationsExceeded = 1,
    DivergenceDetected = 2,
    SingularJacobian = 3,
    InvalidInitialConditions = 4,
    NumericalError = 5
};

// ============================================================================
// CORE DATA STRUCTURES
// ============================================================================

struct Bus {
    size_t id = 0;                  // Unique bus identifier
    std::string name;               // Bus name/label
    BusType type = BusType::PQ;     // Bus type classification
    double baseVoltage_kV = 0.0;    // Nominal line-to-line voltage [kV]
    double vm_pu = 1.0;             // Voltage magnitude [pu]
    double va_deg = 0.0;            // Voltage angle [degrees]
    double va_rad = 0.0;            // Voltage angle [radians]
    double pg_pu = 0.0;             // Active power generation [pu on BASE_MVA]
    double qg_pu = 0.0;             // Reactive power generation [pu]
    double pl_pu = 0.0;             // Active power load [pu]
    double ql_pu = 0.0;             // Reactive power load [pu]
    double gsh_pu = 0.0;            // Shunt conductance [pu]
    double bsh_pu = 0.0;            // Shunt susceptance [pu]
    double vmin_pu = VMIN_DEFAULT;  // Minimum voltage limit [pu]
    double vmax_pu = VMAX_DEFAULT;  // Maximum voltage limit [pu]
    int area = 1;                   // Control area number
    int zone = 1;                   // Loss zone
    double lambda_p = 0.0;          // Active power LMP [$/MWh]
    double lambda_q = 0.0;          // Reactive power LMP [$/MVARh]

    // Convenience: net injected power at bus
    double netP_pu() const { return pg_pu - pl_pu; }
    double netQ_pu() const { return qg_pu - ql_pu; }
};

struct Line {
    size_t id = 0;                  // Unique line identifier
    std::string name;               // Line name/label
    size_t fromBus = 0;             // From bus ID
    size_t toBus = 0;               // To bus ID
    double r_pu = 0.0;              // Series resistance [pu]
    double x_pu = 0.0;              // Series reactance [pu]
    double bch_pu = 0.0;            // Total line charging susceptance [pu]
    double rateA_pu = 0.0;          // MVA rating A (normal) [pu]
    double rateB_pu = 0.0;          // MVA rating B (short term) [pu]
    double rateC_pu = 0.0;          // MVA rating C (emergency) [pu]
    double ratio = 0.0;             // Transformer off-nominal turns ratio (0=line)
    double angle_deg = 0.0;         // Phase shift angle [degrees]
    int status = 1;                 // Service status (1=in, 0=out)
    LineModel model = LineModel::PI;
    double length_km = 0.0;         // Line length [km]
    double fracBFrom = 0.5;         // Fraction of B at from bus (default 50/50)
    double fracBTo = 0.5;           // Fraction of B at to bus
};

struct Transformer {
    size_t id = 0;
    std::string name;
    size_t fromBus = 0;
    size_t toBus = 0;
    double r_pu = 0.0;
    double x_pu = 0.0;
    double ratio = 1.0;             // Off-nominal tap ratio
    double phaseShift_deg = 0.0;
    double rateA_pu = 0.0;
    double rateB_pu = 0.0;
    double rateC_pu = 0.0;
    double tapMin = TAP_MIN;
    double tapMax = TAP_MAX;
    int numTaps = 33;
    int status = 1;
};

struct Generator {
    size_t id = 0;
    std::string name;
    size_t busId = 0;               // Connected bus
    GeneratorType genType = GeneratorType::Thermal;
    double pg_pu = 0.0;             // Real power output [pu]
    double qg_pu = 0.0;             // Reactive power output [pu]
    double qmax_pu = 999.0;         // Max reactive power [pu]
    double qmin_pu = -999.0;        // Min reactive power [pu]
    double pgMax_pu = 999.0;        // Max real power [pu]
    double pgMin_pu = 0.0;          // Min real power [pu]
    double vmSet_pu = 1.0;          // Voltage setpoint [pu]
    double mbase_pu = BASE_MVA_DEFAULT; // Machine base MVA
    double rs_pu = 0.0;             // Stator resistance [pu]
    double xs_pu = 0.0;             // Synchronous reactance [pu]
    double xd_pu = 1.0;             // d-axis reactance [pu]
    double xdPrime_pu = 0.3;        // d-axis transient reactance [pu]
    double xdDoublePrime_pu = 0.2;  // d-axis subtransient reactance [pu]
    double xq_pu = 0.6;             // q-axis reactance [pu]
    double xqPrime_pu = 0.4;        // q-axis transient reactance [pu]
    double xqDoublePrime_pu = 0.2;  // q-axis subtransient reactance [pu]
    double td0Prime_s = 5.0;        // d-axis open-circuit transient time constant [s]
    double td0DoublePrime_s = 0.05; // d-axis subtransient time constant [s]
    double tq0Prime_s = 0.5;        // q-axis open-circuit transient time constant [s]
    double tq0DoublePrime_s = 0.03; // q-axis subtransient time constant [s]
    double h_inertia_s = 5.0;       // Inertia constant [s]
    double d_damping = 0.0;         // Damping coefficient
    int status = 1;                 // In service

    // Cost curve coefficients: cost = c0 + c1*P + c2*P^2 [$/h]
    double cost_c2 = 0.0;
    double cost_c1 = 0.0;
    double cost_c0 = 0.0;
};

struct Load {
    size_t id = 0;
    std::string name;
    size_t busId = 0;
    double pl_pu = 0.0;             // Active power [pu]
    double ql_pu = 0.0;             // Reactive power [pu]
    LoadModel model = LoadModel::ConstantPQ;
    double zip_z = 1.0;             // Constant impedance fraction
    double zip_i = 0.0;             // Constant current fraction
    double zip_p = 0.0;             // Constant power fraction (derived)
    int status = 1;
};

struct Shunt {
    size_t id = 0;
    size_t busId = 0;
    double g_pu = 0.0;
    double b_pu = 0.0;
    int status = 1;
};

// ============================================================================
// YBUS ELEMENT
// ============================================================================

struct YbusElement {
    size_t row = 0;
    size_t col = 0;
    Complex value{0.0, 0.0};
};

// ============================================================================
// SOLVER CONFIGURATION
// ============================================================================

struct SolverConfig {
    SolverMethod method = SolverMethod::NewtonRaphson;
    double tolerance = TOLERANCE_DEFAULT;
    int maxIterations = MAX_ITERATIONS_DEFAULT;
    bool enforceQLimits = true;
    bool flatStart = true;
    double baseMVA = BASE_MVA_DEFAULT;
    int maxQLimitIterations = 10;
    bool verbose = false;
    std::function<void(int, double, const std::string&)> progressCallback;
};

// ============================================================================
// SYSTEM TOPOLOGY
// ============================================================================

struct SystemTopology {
    std::vector<Bus> buses;
    std::vector<Line> lines;
    std::vector<Transformer> transformers;
    std::vector<Generator> generators;
    std::vector<Load> loads;
    std::vector<Shunt> shunts;

    void clear() {
        buses.clear();
        lines.clear();
        transformers.clear();
        generators.clear();
        loads.clear();
        shunts.clear();
    }

    size_t numBuses() const { return buses.size(); }
    size_t numLines() const { return lines.size(); }
    size_t numTransformers() const { return transformers.size(); }
};

// ============================================================================
// RESULTS STRUCTURES
// ============================================================================

struct PowerFlowBusResult {
    size_t busId = 0;
    std::string busName;
    BusType type = BusType::PQ;
    double vm_pu = 0.0;
    double va_deg = 0.0;
    double va_rad = 0.0;
    double pg_pu = 0.0;
    double qg_pu = 0.0;
    double pl_pu = 0.0;
    double ql_pu = 0.0;
    double pInyected_pu = 0.0;      // Net P injected
    double qInyected_pu = 0.0;      // Net Q injected
    double pMismatch_pu = 0.0;
    double qMismatch_pu = 0.0;
    bool voltageViolation = false;
};

struct PowerFlowLineResult {
    size_t lineId = 0;
    std::string lineName;
    size_t fromBus = 0;
    size_t toBus = 0;
    Complex sFrom_pu{0.0, 0.0};     // Complex power at from end
    Complex sTo_pu{0.0, 0.0};       // Complex power at to end
    Complex sLoss_pu{0.0, 0.0};     // Complex power loss
    double pFrom_pu = 0.0;
    double qFrom_pu = 0.0;
    double pTo_pu = 0.0;
    double qTo_pu = 0.0;
    double pLoss_pu = 0.0;
    double qLoss_pu = 0.0;
    double currentFrom_pu = 0.0;
    double currentTo_pu = 0.0;
    double loading_pu = 0.0;        // MVA flow / rating
    bool overload = false;
};

struct SystemSummary {
    double totalPg_pu = 0.0;
    double totalQg_pu = 0.0;
    double totalPl_pu = 0.0;
    double totalQl_pu = 0.0;
    double totalPloss_pu = 0.0;
    double totalQloss_pu = 0.0;
    double totalPshunt_pu = 0.0;
    double totalQshunt_pu = 0.0;
    double totalPslack_pu = 0.0;
    double totalQslack_pu = 0.0;
    double mismatchP_pu = 0.0;
    double mismatchQ_pu = 0.0;
    double totalCost_h = 0.0;
    int numPVBuses = 0;
    int numPQBuses = 0;
    int numSlackBuses = 0;
};

struct PowerFlowResult {
    ConvergenceStatus status = ConvergenceStatus::MaxIterationsExceeded;
    int iterations = 0;
    double finalMismatch = 0.0;
    double solveTime_ms = 0.0;
    std::vector<PowerFlowBusResult> busResults;
    std::vector<PowerFlowLineResult> lineResults;
    SystemSummary summary;
    std::string message;

    bool converged() const {
        return status == ConvergenceStatus::Converged;
    }
};

struct ShortCircuitBusResult {
    size_t busId = 0;
    std::string busName;
    double faultCurrent_pu = 0.0;
    double faultCurrent_kA = 0.0;
    double faultMVA_pu = 0.0;
    double faultMVA_MVA = 0.0;
    Complex voltageDuringFault_pu{0.0, 0.0};
};

struct ShortCircuitResult {
    FaultType faultType = FaultType::ThreePhase;
    size_t faultBusId = 0;
    double faultImpedance_pu = 0.0;
    double ik_pu = 0.0;             // Initial symmetrical short-circuit current
    double ip_pu = 0.0;             // Peak short-circuit current
    double ib_pu = 0.0;             // Symmetrical breaking current
    double sk_pu = 0.0;             // Short-circuit power
    std::vector<ShortCircuitBusResult> busResults;
    std::vector<std::pair<size_t, double>> sourceContributions;
    std::string message;
};

// ============================================================================
// VIOLATION / MONITORING
// ============================================================================

enum class ViolationType : int {
    OverVoltage = 1,
    UnderVoltage = 2,
    LineOverload = 3,
    TransformerOverload = 4,
    GeneratorOverP = 5,
    GeneratorUnderP = 6,
    GeneratorOverQ = 7,
    GeneratorUnderQ = 8,
    FrequencyDeviation = 9
};

struct Violation {
    ViolationType type;
    size_t elementId;
    std::string elementName;
    double value;
    double limit;
    double severity;                // value/limit ratio
    std::string description;
};

// ============================================================================
// STABILITY RESULTS
// ============================================================================

struct EigenvalueResult {
    Complex value;
    double dampingRatio = 0.0;
    double frequency_Hz = 0.0;
    bool stable = false;
    std::string modeDescription;
};

struct TransientResult {
    double time_s = 0.0;
    std::unordered_map<size_t, double> rotorAngles_deg;
    std::unordered_map<size_t, double> rotorSpeeds_pu;
    std::unordered_map<size_t, double> voltages_pu;
    bool stable = true;
};

struct StabilityResult {
    std::vector<EigenvalueResult> eigenvalues;
    std::vector<TransientResult> timeSeries;
    bool smallSignalStable = true;
    bool transientStable = true;
    double criticalClearingTime_s = 0.0;
    std::string message;
};

// ============================================================================
// OPF RESULTS
// ============================================================================

struct OPFGeneratorResult {
    size_t genId = 0;
    size_t busId = 0;
    double pg_pu = 0.0;
    double qg_pu = 0.0;
    double marginalCost = 0.0;
    bool atLimit = false;
};

struct OPFResult {
    bool converged = false;
    double totalCost_h = 0.0;
    double totalLosses_pu = 0.0;
    std::vector<OPFGeneratorResult> genDispatch;
    std::vector<PowerFlowBusResult> busResults;
    std::vector<PowerFlowLineResult> lineResults;
    std::vector<Violation> violations;
    double solveTime_ms = 0.0;
    std::string message;
};

} // namespace powsys365
