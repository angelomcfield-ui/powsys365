#pragma once

#include "integrator.h"
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <memory>
#include <mutex>
#include <atomic>
#include <dlfcn.h>
#include <cstdint>

// Minimal FMI 2.0 type definitions (simplified from fmi2TypesPlatform.h)
typedef unsigned int fmi2ValueReference;
typedef double fmi2Real;
typedef int fmi2Integer;
typedef int fmi2Boolean;
typedef char fmi2Char;
typedef const fmi2Char* fmi2String;
typedef char fmi2Byte;
typedef size_t fmi2Component;
static const fmi2Boolean fmi2True = 1;
static const fmi2Boolean fmi2False = 0;

// FMI 2.0 function types
typedef fmi2Component (*fmi2Instantiate_t)(fmi2String, int, fmi2String, fmi2String,
                                            const void*, fmi2Boolean, fmi2Boolean);
typedef void (*fmi2FreeInstance_t)(fmi2Component);
typedef int (*fmi2SetupExperiment_t)(fmi2Component, fmi2Boolean, fmi2Real, fmi2Real,
                                      fmi2Boolean, fmi2Real);
typedef int (*fmi2EnterInitializationMode_t)(fmi2Component);
typedef int (*fmi2ExitInitializationMode_t)(fmi2Component);
typedef int (*fmi2Terminate_t)(fmi2Component);
typedef int (*fmi2Reset_t)(fmi2Component);
typedef int (*fmi2GetReal_t)(fmi2Component, const fmi2ValueReference*, size_t, fmi2Real*);
typedef int (*fmi2GetInteger_t)(fmi2Component, const fmi2ValueReference*, size_t, fmi2Integer*);
typedef int (*fmi2GetBoolean_t)(fmi2Component, const fmi2ValueReference*, size_t, fmi2Boolean*);
typedef int (*fmi2GetString_t)(fmi2Component, const fmi2ValueReference*, size_t, fmi2String*);
typedef int (*fmi2SetReal_t)(fmi2Component, const fmi2ValueReference*, size_t, const fmi2Real*);
typedef int (*fmi2SetInteger_t)(fmi2Component, const fmi2ValueReference*, size_t, const fmi2Integer*);
typedef int (*fmi2SetBoolean_t)(fmi2Component, const fmi2ValueReference*, size_t, const fmi2Boolean*);
typedef int (*fmi2SetString_t)(fmi2Component, const fmi2ValueReference*, size_t, const fmi2String*);
typedef int (*fmi2DoStep_t)(fmi2Component, fmi2Real, fmi2Real, fmi2Boolean);
typedef int (*fmi2GetFMUstate_t)(fmi2Component, void**);
typedef int (*fmi2SetFMUstate_t)(fmi2Component, void*);
typedef int (*fmi2FreeFMUstate_t)(fmi2Component, void**);
typedef int (*fmi2SerializedFMUstateSize_t)(fmi2Component, void*, size_t*);
typedef int (*fmi2SerializeFMUstate_t)(fmi2Component, void*, fmi2Byte*, size_t);
typedef int (*fmi2DeSerializeFMUstate_t)(fmi2Component, const fmi2Byte*, size_t, void**);
typedef int (*fmi2GetDirectionalDerivative_t)(fmi2Component, const fmi2ValueReference*, size_t,
                                               const fmi2ValueReference*, size_t,
                                               const fmi2Real*, fmi2Real*);

namespace powsys365 {

// ---------------------------------------------------------------------------
// FMI variable description
// ---------------------------------------------------------------------------
struct FmiVariable {
    fmi2ValueReference valueReference = 0;
    std::string name;
    std::string description;
    std::string unit;
    int causality = 0;      // 0=parameter, 1=input, 2=output, 3=local, 4=calculatedParameter
    int variability = 0;    // 0=fixed, 1=tunable, 2=constant, 3=discrete, 4=continuous
    int initial = 0;        // 0=approx, 1=calculated, 2=exact
    double startValue = 0.0;
    double min = 0.0;
    double max = 0.0;
    int dataType = 0;       // 0=Real, 1=Integer, 2=Boolean, 3=String
};

// ---------------------------------------------------------------------------
// FMI instance state
// ---------------------------------------------------------------------------
enum class FmiInstanceState {
    INSTANTIATED,
    INITIALIZATION_MODE,
    STEP_COMPLETE,
    STEP_FAILED,
    TERMINATED,
    ERROR_STATE
};

std::string fmiInstanceStateToString(FmiInstanceState state);

// ---------------------------------------------------------------------------
// Co-simulation step result
// ---------------------------------------------------------------------------
struct FmiStepResult {
    double time = 0.0;
    double stepSize = 0.0;
    bool stepAccepted = false;
    int status = 0; // FMI status code
    std::string statusMessage;
    bool doEarlyReturn = false;
    double earlyReturnTime = 0.0;
    std::map<fmi2ValueReference, double> outputValues;
};

// ---------------------------------------------------------------------------
// FMI wrapper for a single FMU
// ---------------------------------------------------------------------------
class FmiInstance {
public:
    FmiInstance();
    ~FmiInstance();

    // Load FMU shared library
    bool loadLibrary(const std::string& libraryPath);
    void unloadLibrary();
    bool isLoaded() const;

    // FMI lifecycle
    bool instantiate(const std::string& instanceName, const std::string& fmuType,
                      const std::string& fmuGUID, const std::string& fmuResourceLocation);
    bool setupExperiment(double tolerance, double startTime, bool stopTimeDefined, double stopTime);
    bool enterInitializationMode();
    bool exitInitializationMode();
    bool terminate();
    bool reset();

    // Variable access
    bool getReal(const std::vector<fmi2ValueReference>& vr, std::vector<fmi2Real>& values);
    bool getInteger(const std::vector<fmi2ValueReference>& vr, std::vector<fmi2Integer>& values);
    bool getBoolean(const std::vector<fmi2ValueReference>& vr, std::vector<fmi2Boolean>& values);
    bool setReal(const std::vector<fmi2ValueReference>& vr, const std::vector<fmi2Real>& values);
    bool setInteger(const std::vector<fmi2ValueReference>& vr, const std::vector<fmi2Integer>& values);
    bool setBoolean(const std::vector<fmi2ValueReference>& vr, const std::vector<fmi2Boolean>& values);

    bool getReal(fmi2ValueReference vr, fmi2Real& value);
    bool setReal(fmi2ValueReference vr, fmi2Real value);

    // Co-simulation step
    FmiStepResult doStep(double currentTime, double stepSize, bool noSetFMUStatePrior);

    // State management (for rollback)
    bool saveState(void** state);
    bool restoreState(void* state);
    void freeState(void** state);

    // Variable info
    std::vector<FmiVariable> getVariables() const;
    std::optional<FmiVariable> getVariableByName(const std::string& name) const;
    std::optional<FmiVariable> getVariableByVR(fmi2ValueReference vr) const;

    FmiInstanceState getState() const;
    std::string getInstanceName() const;

    // Status
    std::string getLastErrorMessage() const;

private:
    void* m_libraryHandle = nullptr;
    fmi2Component m_component = 0;
    FmiInstanceState m_state = FmiInstanceState::INSTANTIATED;
    std::string m_instanceName;
    std::vector<FmiVariable> m_variables;
    std::map<std::string, fmi2ValueReference> m_nameToVR;

    // Function pointers
    fmi2Instantiate_t m_instantiate = nullptr;
    fmi2FreeInstance_t m_freeInstance = nullptr;
    fmi2SetupExperiment_t m_setupExperiment = nullptr;
    fmi2EnterInitializationMode_t m_enterInit = nullptr;
    fmi2ExitInitializationMode_t m_exitInit = nullptr;
    fmi2Terminate_t m_terminate = nullptr;
    fmi2Reset_t m_reset = nullptr;
    fmi2GetReal_t m_getReal = nullptr;
    fmi2GetInteger_t m_getInteger = nullptr;
    fmi2GetBoolean_t m_getBoolean = nullptr;
    fmi2SetReal_t m_setReal = nullptr;
    fmi2SetInteger_t m_setInteger = nullptr;
    fmi2SetBoolean_t m_setBoolean = nullptr;
    fmi2DoStep_t m_doStep = nullptr;
    fmi2GetFMUstate_t m_getState = nullptr;
    fmi2SetFMUstate_t m_setState = nullptr;
    fmi2FreeFMUstate_t m_freeState = nullptr;
    fmi2SerializedFMUstateSize_t m_serializedStateSize = nullptr;
    fmi2SerializeFMUstate_t m_serializeState = nullptr;
    fmi2DeSerializeFMUstate_t m_deserializeState = nullptr;
    fmi2GetDirectionalDerivative_t m_getDirectionalDerivative = nullptr;

    std::string m_lastError;
};

// ---------------------------------------------------------------------------
// Connection between two FMUs (for variable exchange)
// ---------------------------------------------------------------------------
struct FmiConnection {
    std::string connectionId;
    std::string sourceInstance;
    fmi2ValueReference sourceVR;
    std::string targetInstance;
    fmi2ValueReference targetVR;
    double gain = 1.0;
    double offset = 0.0;
};

// ---------------------------------------------------------------------------
// Co-simulation master
// ---------------------------------------------------------------------------
class FmiCoSimulation {
public:
    FmiCoSimulation();
    ~FmiCoSimulation();

    // FMU management
    bool addFmu(const std::string& instanceName, const std::string& libraryPath);
    bool removeFmu(const std::string& instanceName);
    std::shared_ptr<FmiInstance> getFmu(const std::string& instanceName) const;
    std::vector<std::string> getFmuNames() const;

    // Connections
    bool addConnection(const FmiConnection& connection);
    bool removeConnection(const std::string& connectionId);
    std::vector<FmiConnection> getConnections() const;

    // Simulation lifecycle
    bool initialize(double startTime, double tolerance);
    FmiStepResult simulateStep(double stepSize);
    std::vector<FmiStepResult> simulate(double stopTime, double stepSize);
    void terminate();

    // Variable exchange
    bool exchangeVariables();
    bool setExternalInput(const std::string& instanceName, fmi2ValueReference vr, double value);
    bool getExternalOutput(const std::string& instanceName, fmi2ValueReference vr, double& value);

    // Synchronization
    void setMasterStepSize(double stepSize);
    double getMasterStepSize() const;
    double getCurrentTime() const;

    // State management
    bool saveAllStates();
    bool restoreAllStates();

    // Callbacks
    void setOnStepComplete(std::function<void(double, const std::vector<FmiStepResult>&)> callback);
    void setOnError(std::function<void(const std::string&)> callback);

    // Statistics
    uint64_t getTotalSteps() const;
    uint64_t getAcceptedSteps() const;
    uint64_t getRejectedSteps() const;
    double getSimulationTime() const;

    // OMSimulator integration
    bool loadOmsModel(const std::string& sspFile);
    bool simulateOms(double stopTime);

private:
    bool doSerialGaussSeidelStep(double stepSize);
    bool doParallelJacobiStep(double stepSize);
    void exchangeCouplingVariables(const FmiConnection& conn);

    std::map<std::string, std::shared_ptr<FmiInstance>> m_fmus;
    std::vector<FmiConnection> m_connections;
    mutable std::mutex m_fmusMutex;

    double m_currentTime = 0.0;
    double m_masterStepSize = 0.001;
    double m_tolerance = 1e-6;
    bool m_initialized = false;

    // State snapshots for rollback
    std::map<std::string, void*> m_savedStates;

    // Callbacks
    std::function<void(double, const std::vector<FmiStepResult>&)> m_onStepComplete;
    std::function<void(const std::string&)> m_onError;

    // Statistics
    uint64_t m_totalSteps = 0;
    uint64_t m_acceptedSteps = 0;
    uint64_t m_rejectedSteps = 0;
};

} // namespace powsys365
