#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <optional>
#include <stdexcept>
#include <limits>
#include <cstdint>

namespace powsys365::simulation::fmi {

// ============================================================================
// FMI Version Enumeration
// ============================================================================
enum class FMIVersion {
    Unknown,
    FMI1_0,
    FMI2_0,
    FMI3_0
};

// ============================================================================
// Variable Type Enumeration (FMI 2.0 & 3.0 types)
// ============================================================================
enum class FMIType {
    Real,
    Integer,
    Boolean,
    String,
    Enumeration,
    // FMI 3.0 specific
    Float32,
    Float64,
    Int8,
    UInt8,
    Int16,
    UInt16,
    Int32,
    UInt32,
    Int64,
    UInt64,
    Binary,
    Clock
};

// ============================================================================
// Causality Enumeration
// ============================================================================
enum class Causality {
    Local,       // Internal variable (default in FMI 2.0)
    Parameter,   // Tunable parameter
    CalculatedParameter, // FMI 3.0
    Input,       // External input
    Output,      // Model output
    Independent, // Time variable
    StructuralParameter, // FMI 3.0
    Constant     // FMI 3.0
};

// ============================================================================
// Variability Enumeration
// ============================================================================
enum class Variability {
    Constant,
    Fixed,
    Tunable,
    Discrete,
    Continuous,
    Unknown
};

// ============================================================================
// Initial Value Specification Enumeration
// ============================================================================
enum class Initial {
    Exact,       // Start value is exact
    Approx,      // Start value is approximation
    Calculated,  // Value is calculated during initialization
    Automatic    // Determined from causality/variability
};

// ============================================================================
// FMI Variable Structure
// ============================================================================
struct FMIVariable {
    std::string name;
    uint32_t valueReference = 0;
    FMIType type = FMIType::Real;
    Causality causality = Causality::Local;
    Variability variability = Variability::Continuous;
    Initial initial = Initial::Automatic;
    std::string description;
    std::string unit;
    std::string displayUnit;
    double displayUnitFactor = 1.0;
    double displayUnitOffset = 0.0;

    // Numeric values
    double startValue = 0.0;
    double minValue = -std::numeric_limits<double>::infinity();
    double maxValue = std::numeric_limits<double>::infinity();
    double nominal = 1.0;

    // String start value (for String types)
    std::string startValueString;

    // Integer-specific bounds
    int64_t minValueInt = std::numeric_limits<int64_t>::min();
    int64_t maxValueInt = std::numeric_limits<int64_t>::max();
    int64_t startValueInt = 0;

    // Boolean start value
    bool startValueBool = false;

    // Derivative information (for FMI 2.0 Model Exchange)
    uint32_t derivativeOf = 0;  // valueReference of the variable this is derivative of

    // Methods
    bool isInput() const noexcept {
        return causality == Causality::Input;
    }

    bool isOutput() const noexcept {
        return causality == Causality::Output;
    }

    bool isParameter() const noexcept {
        return causality == Causality::Parameter ||
               causality == Causality::CalculatedParameter ||
               causality == Causality::StructuralParameter;
    }

    bool isContinuous() const noexcept {
        return variability == Variability::Continuous;
    }

    bool hasStartValue() const noexcept {
        return initial == Initial::Exact || initial == Initial::Approx;
    }

    std::string toString() const;
};

// ============================================================================
// Model Exchange Information
// ============================================================================
struct ModelExchangeInfo {
    std::string modelIdentifier;
    bool providesDirectionalDerivative = false;
    bool needsExecutionTool = false;
    bool completedIntegratorStepNotNeeded = false;
    bool canBeInstantiatedOnlyOncePerProcess = false;
    bool canNotUseMemoryManagementFunctions = false;
    bool canGetAndSetFMUState = false;
    bool canSerializeFMUState = false;
    bool providesEvaluateDiscreteStates = false;  // FMI 3.0
};

// ============================================================================
// Co-Simulation Information
// ============================================================================
struct CoSimulationInfo {
    std::string modelIdentifier;
    bool canHandleVariableCommunicationStepSize = false;
    bool canInterpolateInputs = false;
    uint32_t maxOutputDerivativeOrder = 0;
    bool canRunAsynchronuously = false;
    bool needsExecutionTool = false;
    bool canBeInstantiatedOnlyOncePerProcess = false;
    bool canNotUseMemoryManagementFunctions = false;
    bool canGetAndSetFMUState = false;
    bool canSerializeFMUState = false;
    bool canHandleCoSimulation = false;
    double fixedInternalStepSize = 0.0;
    bool hasFixedInternalStepSize = false;
    // FMI 3.0
    bool providesIntermediateUpdate = false;
    bool mightReturnEarlyFromDoStep = false;
    bool canReturnEarlyAfterIntermediateUpdate = false;
};

// ============================================================================
// Scheduled Execution Info (FMI 3.0)
// ============================================================================
struct ScheduledExecutionInfo {
    std::string modelIdentifier;
    bool needsExecutionTool = false;
    bool canBeInstantiatedOnlyOncePerProcess = false;
    bool canNotUseMemoryManagementFunctions = false;
    bool canGetAndSetFMUState = false;
    bool canSerializeFMUState = false;
};

// ============================================================================
// Unit Definition
// ============================================================================
struct UnitDefinition {
    std::string name;
    // Base units (SI derived)
    std::unordered_map<std::string, int> baseUnits;  // e.g., "kg" -> 1, "m" -> 1, "s" -> -2
    double factor = 1.0;
    double offset = 0.0;
};

// ============================================================================
// FMI Model Description Parser
// ============================================================================
class FMIModelDescription {
public:
    FMIModelDescription();
    ~FMIModelDescription();

    // Disable copy, enable move
    FMIModelDescription(const FMIModelDescription&) = delete;
    FMIModelDescription& operator=(const FMIModelDescription&) = delete;
    FMIModelDescription(FMIModelDescription&&) noexcept;
    FMIModelDescription& operator=(FMIModelDescription&&) noexcept;

    // ------------------------------------------------------------------------
    // XML Parsing
    // ------------------------------------------------------------------------
    bool parseFromFile(const std::string& xmlPath);
    bool parseFromString(const std::string& xmlContent);

    // ------------------------------------------------------------------------
    // Model Information
    // ------------------------------------------------------------------------
    const std::string& modelName() const noexcept;
    const std::string& guid() const noexcept;
    const std::string& version() const noexcept;
    const std::string& description() const noexcept;
    const std::string& author() const noexcept;
    const std::string& copyright() const noexcept;
    const std::string& license() const noexcept;
    const std::string& generationTool() const noexcept;
    const std::string& generationDateAndTime() const noexcept;

    FMIVersion fmiVersion() const noexcept;
    double numberOfEventIndicators() const noexcept;
    uint32_t numberOfContinuousStates() const noexcept;

    // ------------------------------------------------------------------------
    // Variable Access
    // ------------------------------------------------------------------------
    const std::vector<FMIVariable>& variables() const noexcept;
    const FMIVariable* findVariableByName(const std::string& name) const;
    const FMIVariable* findVariableByVR(uint32_t vr) const;
    const FMIVariable* findVariableByVR(uint32_t vr, FMIType type) const;

    // ------------------------------------------------------------------------
    // Filtered Variable Lists
    // ------------------------------------------------------------------------
    std::vector<const FMIVariable*> getInputs() const;
    std::vector<const FMIVariable*> getOutputs() const;
    std::vector<const FMIVariable*> getParameters() const;
    std::vector<const FMIVariable*> getContinuousStates() const;
    std::vector<const FMIVariable*> getLocalVariables() const;
    std::vector<const FMIVariable*> getVariablesByCausality(Causality c) const;
    std::vector<const FMIVariable*> getVariablesByType(FMIType t) const;

    // ------------------------------------------------------------------------
    // Interface Information
    // ------------------------------------------------------------------------
    bool supportsModelExchange() const noexcept;
    bool supportsCoSimulation() const noexcept;
    bool supportsScheduledExecution() const noexcept;  // FMI 3.0

    const ModelExchangeInfo& modelExchangeInfo() const noexcept;
    const CoSimulationInfo& coSimulationInfo() const noexcept;
    const ScheduledExecutionInfo& scheduledExecutionInfo() const noexcept;

    // ------------------------------------------------------------------------
    // Unit Definitions
    // ------------------------------------------------------------------------
    const std::vector<UnitDefinition>& unitDefinitions() const noexcept;
    const UnitDefinition* findUnit(const std::string& name) const;

    // ------------------------------------------------------------------------
    // Default Experiment
    // ------------------------------------------------------------------------
    bool hasDefaultExperiment() const noexcept;
    double defaultStartTime() const noexcept;
    double defaultStopTime() const noexcept;
    double defaultTolerance() const noexcept;
    double defaultStepSize() const noexcept;

    // ------------------------------------------------------------------------
    // Validation
    // ------------------------------------------------------------------------
    bool isValid() const noexcept;
    std::vector<std::string> validationErrors() const;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

// ============================================================================
// String Conversion Helpers
// ============================================================================
const char* toString(FMIVersion v);
const char* toString(FMIType t);
const char* toString(Causality c);
const char* toString(Variability v);
const char* toString(Initial i);

FMIVersion parseFMIVersion(const std::string& s);
FMIType parseFMIType(const std::string& s);
Causality parseCausality(const std::string& s);
Variability parseVariability(const std::string& s);
Initial parseInitial(const std::string& s);

// ============================================================================
// Exception Types
// ============================================================================
class FMIParseException : public std::runtime_error {
public:
    explicit FMIParseException(const std::string& msg) : std::runtime_error(msg) {}
};

} // namespace powsys365::simulation::fmi
