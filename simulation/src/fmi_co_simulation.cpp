#include "powsy365/simulation/fmi_co_simulation.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace powsys365 {

// ============================================================================
// FmiInstanceState string helper
// ============================================================================
std::string fmiInstanceStateToString(FmiInstanceState state) {
    switch (state) {
        case FmiInstanceState::INSTANTIATED: return "INSTANTIATED";
        case FmiInstanceState::INITIALIZATION_MODE: return "INITIALIZATION_MODE";
        case FmiInstanceState::STEP_COMPLETE: return "STEP_COMPLETE";
        case FmiInstanceState::STEP_FAILED: return "STEP_FAILED";
        case FmiInstanceState::TERMINATED: return "TERMINATED";
        case FmiInstanceState::ERROR_STATE: return "ERROR_STATE";
        default: return "UNKNOWN";
    }
}

// ============================================================================
// FmiInstance
// ============================================================================
FmiInstance::FmiInstance() = default;

FmiInstance::~FmiInstance() {
    if (m_component != 0 && m_freeInstance) {
        m_freeInstance(m_component);
    }
    unloadLibrary();
}

bool FmiInstance::loadLibrary(const std::string& libraryPath) {
    if (m_libraryHandle) return true;

    m_libraryHandle = dlopen(libraryPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!m_libraryHandle) {
        m_lastError = dlerror();
        return false;
    }

    // Load all function pointers
    m_instantiate = (fmi2Instantiate_t)dlsym(m_libraryHandle, "fmi2Instantiate");
    m_freeInstance = (fmi2FreeInstance_t)dlsym(m_libraryHandle, "fmi2FreeInstance");
    m_setupExperiment = (fmi2SetupExperiment_t)dlsym(m_libraryHandle, "fmi2SetupExperiment");
    m_enterInit = (fmi2EnterInitializationMode_t)dlsym(m_libraryHandle, "fmi2EnterInitializationMode");
    m_exitInit = (fmi2ExitInitializationMode_t)dlsym(m_libraryHandle, "fmi2ExitInitializationMode");
    m_terminate = (fmi2Terminate_t)dlsym(m_libraryHandle, "fmi2Terminate");
    m_reset = (fmi2Reset_t)dlsym(m_libraryHandle, "fmi2Reset");
    m_getReal = (fmi2GetReal_t)dlsym(m_libraryHandle, "fmi2GetReal");
    m_getInteger = (fmi2GetInteger_t)dlsym(m_libraryHandle, "fmi2GetInteger");
    m_getBoolean = (fmi2GetBoolean_t)dlsym(m_libraryHandle, "fmi2GetBoolean");
    m_setReal = (fmi2SetReal_t)dlsym(m_libraryHandle, "fmi2SetReal");
    m_setInteger = (fmi2SetInteger_t)dlsym(m_libraryHandle, "fmi2SetInteger");
    m_setBoolean = (fmi2SetBoolean_t)dlsym(m_libraryHandle, "fmi2SetBoolean");
    m_doStep = (fmi2DoStep_t)dlsym(m_libraryHandle, "fmi2DoStep");
    m_getState = (fmi2GetFMUstate_t)dlsym(m_libraryHandle, "fmi2GetFMUstate");
    m_setState = (fmi2SetFMUstate_t)dlsym(m_libraryHandle, "fmi2SetFMUstate");
    m_freeState = (fmi2FreeFMUstate_t)dlsym(m_libraryHandle, "fmi2FreeFMUstate");
    m_serializedStateSize = (fmi2SerializedFMUstateSize_t)dlsym(m_libraryHandle, "fmi2SerializedFMUstateSize");
    m_serializeState = (fmi2SerializeFMUstate_t)dlsym(m_libraryHandle, "fmi2SerializeFMUstate");
    m_deserializeState = (fmi2DeSerializeFMUstate_t)dlsym(m_libraryHandle, "fmi2DeSerializeFMUstate");
    m_getDirectionalDerivative = (fmi2GetDirectionalDerivative_t)dlsym(m_libraryHandle, "fmi2GetDirectionalDerivative");

    // Check required functions
    if (!m_instantiate || !m_freeInstance || !m_doStep || !m_getReal || !m_setReal) {
        unloadLibrary();
        m_lastError = "Missing required FMI functions in library";
        return false;
    }

    return true;
}

void FmiInstance::unloadLibrary() {
    if (m_libraryHandle) {
        dlclose(m_libraryHandle);
        m_libraryHandle = nullptr;
    }
}

bool FmiInstance::isLoaded() const {
    return m_libraryHandle != nullptr;
}

// ---------------------------------------------------------------------------
// FMI lifecycle
// ---------------------------------------------------------------------------
bool FmiInstance::instantiate(const std::string& instanceName, const std::string& fmuType,
                               const std::string& fmuGUID, const std::string& fmuResourceLocation) {
    if (!m_instantiate) return false;

    int fmuTypeInt = 0; // 0 = ModelExchange, 1 = CoSimulation
    if (fmuType == "CoSimulation") fmuTypeInt = 1;

    fmi2String resourceLocation = fmuResourceLocation.empty() ? nullptr : fmuResourceLocation.c_str();

    m_component = m_instantiate(
        instanceName.c_str(),
        fmuTypeInt,
        fmuGUID.c_str(),
        resourceLocation,
        nullptr, // functions (callback)
        fmi2False, // visible
        fmi2False  // loggingOn
    );

    if (m_component == 0) {
        m_lastError = "fmi2Instantiate failed";
        return false;
    }

    m_instanceName = instanceName;
    m_state = FmiInstanceState::INSTANTIATED;
    return true;
}

bool FmiInstance::setupExperiment(double tolerance, double startTime, bool stopTimeDefined, double stopTime) {
    if (!m_setupExperiment || m_component == 0) return false;

    int status = m_setupExperiment(
        m_component,
        tolerance > 0 ? fmi2True : fmi2False,
        tolerance,
        startTime,
        stopTimeDefined ? fmi2True : fmi2False,
        stopTime
    );

    return status == 0; // fmi2OK = 0
}

bool FmiInstance::enterInitializationMode() {
    if (!m_enterInit || m_component == 0) return false;

    int status = m_enterInit(m_component);
    if (status == 0) {
        m_state = FmiInstanceState::INITIALIZATION_MODE;
    }
    return status == 0;
}

bool FmiInstance::exitInitializationMode() {
    if (!m_exitInit || m_component == 0) return false;

    int status = m_exitInit(m_component);
    if (status == 0) {
        m_state = FmiInstanceState::STEP_COMPLETE;
    }
    return status == 0;
}

bool FmiInstance::terminate() {
    if (!m_terminate || m_component == 0) return false;

    int status = m_terminate(m_component);
    if (status == 0) {
        m_state = FmiInstanceState::TERMINATED;
    }
    return status == 0;
}

bool FmiInstance::reset() {
    if (!m_reset || m_component == 0) return false;

    int status = m_reset(m_component);
    if (status == 0) {
        m_state = FmiInstanceState::INSTANTIATED;
    }
    return status == 0;
}

// ---------------------------------------------------------------------------
// Variable access
// ---------------------------------------------------------------------------
bool FmiInstance::getReal(const std::vector<fmi2ValueReference>& vr, std::vector<fmi2Real>& values) {
    if (!m_getReal || m_component == 0 || vr.empty()) return false;

    values.resize(vr.size());
    int status = m_getReal(m_component, vr.data(), vr.size(), values.data());
    return status == 0;
}

bool FmiInstance::getInteger(const std::vector<fmi2ValueReference>& vr, std::vector<fmi2Integer>& values) {
    if (!m_getInteger || m_component == 0 || vr.empty()) return false;

    values.resize(vr.size());
    int status = m_getInteger(m_component, vr.data(), vr.size(), values.data());
    return status == 0;
}

bool FmiInstance::getBoolean(const std::vector<fmi2ValueReference>& vr, std::vector<fmi2Boolean>& values) {
    if (!m_getBoolean || m_component == 0 || vr.empty()) return false;

    values.resize(vr.size());
    int status = m_getBoolean(m_component, vr.data(), vr.size(), values.data());
    return status == 0;
}

bool FmiInstance::setReal(const std::vector<fmi2ValueReference>& vr, const std::vector<fmi2Real>& values) {
    if (!m_setReal || m_component == 0 || vr.empty() || vr.size() != values.size()) return false;

    int status = m_setReal(m_component, vr.data(), vr.size(), values.data());
    return status == 0;
}

bool FmiInstance::setInteger(const std::vector<fmi2ValueReference>& vr, const std::vector<fmi2Integer>& values) {
    if (!m_setInteger || m_component == 0 || vr.empty() || vr.size() != values.size()) return false;

    int status = m_setInteger(m_component, vr.data(), vr.size(), values.data());
    return status == 0;
}

bool FmiInstance::setBoolean(const std::vector<fmi2ValueReference>& vr, const std::vector<fmi2Boolean>& values) {
    if (!m_setBoolean || m_component == 0 || vr.empty() || vr.size() != values.size()) return false;

    int status = m_setBoolean(m_component, vr.data(), vr.size(), values.data());
    return status == 0;
}

bool FmiInstance::getReal(fmi2ValueReference vr, fmi2Real& value) {
    std::vector<fmi2Real> values;
    if (!getReal({vr}, values) || values.empty()) return false;
    value = values[0];
    return true;
}

bool FmiInstance::setReal(fmi2ValueReference vr, fmi2Real value) {
    return setReal({vr}, {value});
}

// ---------------------------------------------------------------------------
// Co-simulation step
// ---------------------------------------------------------------------------
FmiStepResult FmiInstance::doStep(double currentTime, double stepSize, bool noSetFMUStatePrior) {
    FmiStepResult result;
    result.time = currentTime;
    result.stepSize = stepSize;

    if (!m_doStep || m_component == 0) {
        result.stepAccepted = false;
        result.statusMessage = "doStep function not available";
        return result;
    }

    int status = m_doStep(m_component, currentTime, stepSize,
                           noSetFMUStatePrior ? fmi2True : fmi2False);

    result.status = status;
    result.stepAccepted = (status == 0); // fmi2OK

    if (status == 0) {
        result.time = currentTime + stepSize;
        m_state = FmiInstanceState::STEP_COMPLETE;
    } else if (status == 1) { // fmi2Warning
        result.stepAccepted = true;
        result.statusMessage = "Warning during step";
        m_state = FmiInstanceState::STEP_COMPLETE;
    } else if (status == 2) { // fmi2Discard
        result.stepAccepted = false;
        result.statusMessage = "Step discarded";
        m_state = FmiInstanceState::STEP_FAILED;
        result.doEarlyReturn = true;
    } else if (status == 3) { // fmi2Error
        result.stepAccepted = false;
        result.statusMessage = "Error during step";
        m_state = FmiInstanceState::ERROR_STATE;
    } else {
        result.stepAccepted = false;
        result.statusMessage = "Fatal error";
        m_state = FmiInstanceState::ERROR_STATE;
    }

    return result;
}

// ---------------------------------------------------------------------------
// State management
// ---------------------------------------------------------------------------
bool FmiInstance::saveState(void** state) {
    if (!m_getState || m_component == 0) return false;
    return m_getState(m_component, state) == 0;
}

bool FmiInstance::restoreState(void* state) {
    if (!m_setState || m_component == 0) return false;
    return m_setState(m_component, state) == 0;
}

void FmiInstance::freeState(void** state) {
    if (m_freeState && m_component != 0) {
        m_freeState(m_component, state);
    }
}

// ---------------------------------------------------------------------------
// Variable info
// ---------------------------------------------------------------------------
std::vector<FmiVariable> FmiInstance::getVariables() const {
    return m_variables;
}

std::optional<FmiVariable> FmiInstance::getVariableByName(const std::string& name) const {
    auto it = m_nameToVR.find(name);
    if (it != m_nameToVR.end()) {
        return getVariableByVR(it->second);
    }
    return std::nullopt;
}

std::optional<FmiVariable> FmiInstance::getVariableByVR(fmi2ValueReference vr) const {
    for (const auto& var : m_variables) {
        if (var.valueReference == vr) {
            return var;
        }
    }
    return std::nullopt;
}

FmiInstanceState FmiInstance::getState() const {
    return m_state;
}

std::string FmiInstance::getInstanceName() const {
    return m_instanceName;
}

std::string FmiInstance::getLastErrorMessage() const {
    return m_lastError;
}

// ============================================================================
// FmiCoSimulation
// ============================================================================
FmiCoSimulation::FmiCoSimulation() = default;

FmiCoSimulation::~FmiCoSimulation() {
    terminate();
}

// ---------------------------------------------------------------------------
// FMU management
// ---------------------------------------------------------------------------
bool FmiCoSimulation::addFmu(const std::string& instanceName, const std::string& libraryPath) {
    std::lock_guard<std::mutex> lock(m_fmusMutex);

    auto instance = std::make_shared<FmiInstance>();
    if (!instance->loadLibrary(libraryPath)) {
        return false;
    }

    m_fmus[instanceName] = instance;
    return true;
}

bool FmiCoSimulation::removeFmu(const std::string& instanceName) {
    std::lock_guard<std::mutex> lock(m_fmusMutex);

    auto it = m_fmus.find(instanceName);
    if (it != m_fmus.end()) {
        // Remove connections involving this FMU
        m_connections.erase(
            std::remove_if(m_connections.begin(), m_connections.end(),
                [&instanceName](const FmiConnection& c) {
                    return c.sourceInstance == instanceName || c.targetInstance == instanceName;
                }),
            m_connections.end());
        m_fmus.erase(it);
        return true;
    }
    return false;
}

std::shared_ptr<FmiInstance> FmiCoSimulation::getFmu(const std::string& instanceName) const {
    std::lock_guard<std::mutex> lock(m_fmusMutex);
    auto it = m_fmus.find(instanceName);
    if (it != m_fmus.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<std::string> FmiCoSimulation::getFmuNames() const {
    std::lock_guard<std::mutex> lock(m_fmusMutex);
    std::vector<std::string> names;
    for (const auto& [name, fmu] : m_fmus) {
        (void)fmu;
        names.push_back(name);
    }
    return names;
}

// ---------------------------------------------------------------------------
// Connections
// ---------------------------------------------------------------------------
bool FmiCoSimulation::addConnection(const FmiConnection& connection) {
    std::lock_guard<std::mutex> lock(m_fmusMutex);

    // Verify both FMUs exist
    if (m_fmus.find(connection.sourceInstance) == m_fmus.end() ||
        m_fmus.find(connection.targetInstance) == m_fmus.end()) {
        return false;
    }

    m_connections.push_back(connection);
    return true;
}

bool FmiCoSimulation::removeConnection(const std::string& connectionId) {
    auto it = std::remove_if(m_connections.begin(), m_connections.end(),
        [&connectionId](const FmiConnection& c) { return c.connectionId == connectionId; });
    if (it != m_connections.end()) {
        m_connections.erase(it, m_connections.end());
        return true;
    }
    return false;
}

std::vector<FmiConnection> FmiCoSimulation::getConnections() const {
    return m_connections;
}

// ---------------------------------------------------------------------------
// Simulation lifecycle
// ---------------------------------------------------------------------------
bool FmiCoSimulation::initialize(double startTime, double tolerance) {
    std::lock_guard<std::mutex> lock(m_fmusMutex);

    if (m_fmus.empty()) return false;

    for (auto& [name, fmu] : m_fmus) {
        (void)name;
        if (!fmu->isLoaded()) return false;

        // Setup experiment
        if (fmu->setupExperiment(tolerance, startTime, false, 0.0)) {
            // Enter initialization mode
            if (fmu->enterInitializationMode()) {
                fmu->exitInitializationMode();
            }
        }
    }

    m_currentTime = startTime;
    m_tolerance = tolerance;
    m_initialized = true;

    return true;
}

FmiStepResult FmiCoSimulation::simulateStep(double stepSize) {
    FmiStepResult masterResult;
    masterResult.time = m_currentTime;
    masterResult.stepSize = stepSize;

    if (!m_initialized) {
        masterResult.stepAccepted = false;
        masterResult.statusMessage = "Not initialized";
        return masterResult;
    }

    // Exchange input variables before stepping
    exchangeVariables();

    // Step all FMUs (serial Gauss-Seidel by default)
    bool allAccepted = true;
    std::vector<FmiStepResult> fmuResults;

    {
        std::lock_guard<std::mutex> lock(m_fmusMutex);
        for (auto& [name, fmu] : m_fmus) {
            (void)name;
            if (fmu->getState() == FmiInstanceState::ERROR_STATE ||
                fmu->getState() == FmiInstanceState::TERMINATED) {
                continue;
            }

            auto result = fmu->doStep(m_currentTime, stepSize, false);
            fmuResults.push_back(result);

            if (!result.stepAccepted) {
                allAccepted = false;
            }
        }
    }

    if (allAccepted) {
        m_currentTime += stepSize;
        masterResult.stepAccepted = true;
        masterResult.time = m_currentTime;
        m_acceptedSteps++;
    } else {
        masterResult.stepAccepted = false;
        masterResult.statusMessage = "Some FMU step was rejected";
        m_rejectedSteps++;
    }

    m_totalSteps++;

    // Callback
    if (m_onStepComplete) {
        m_onStepComplete(m_currentTime, fmuResults);
    }

    return masterResult;
}

std::vector<FmiStepResult> FmiCoSimulation::simulate(double stopTime, double stepSize) {
    std::vector<FmiStepResult> results;

    if (!m_initialized) return results;

    while (m_currentTime < stopTime) {
        double remaining = stopTime - m_currentTime;
        double h = std::min(stepSize, remaining);

        auto result = simulateStep(h);
        results.push_back(result);

        if (!result.stepAccepted) {
            if (m_onError) {
                m_onError("Step rejected at t=" + std::to_string(m_currentTime));
            }
            break;
        }

        // Exchange outputs after successful step
        exchangeVariables();
    }

    return results;
}

void FmiCoSimulation::terminate() {
    std::lock_guard<std::mutex> lock(m_fmusMutex);
    for (auto& [name, fmu] : m_fmus) {
        (void)name;
        fmu->terminate();
    }
    m_initialized = false;
}

// ---------------------------------------------------------------------------
// Variable exchange
// ---------------------------------------------------------------------------
bool FmiCoSimulation::exchangeVariables() {
    std::lock_guard<std::mutex> lock(m_fmusMutex);

    for (const auto& conn : m_connections) {
        auto sourceFmu = m_fmus.find(conn.sourceInstance);
        auto targetFmu = m_fmus.find(conn.targetInstance);

        if (sourceFmu == m_fmus.end() || targetFmu == m_fmus.end()) continue;

        fmi2Real value = 0.0;
        if (sourceFmu->second->getReal(conn.sourceVR, value)) {
            double adjustedValue = conn.gain * value + conn.offset;
            targetFmu->second->setReal(conn.targetVR, adjustedValue);
        }
    }

    return true;
}

bool FmiCoSimulation::setExternalInput(const std::string& instanceName, fmi2ValueReference vr, double value) {
    std::lock_guard<std::mutex> lock(m_fmusMutex);
    auto it = m_fmus.find(instanceName);
    if (it == m_fmus.end()) return false;
    return it->second->setReal(vr, value);
}

bool FmiCoSimulation::getExternalOutput(const std::string& instanceName, fmi2ValueReference vr, double& value) {
    std::lock_guard<std::mutex> lock(m_fmusMutex);
    auto it = m_fmus.find(instanceName);
    if (it == m_fmus.end()) return false;
    return it->second->getReal(vr, value);
}

// ---------------------------------------------------------------------------
// Synchronization
// ---------------------------------------------------------------------------
void FmiCoSimulation::setMasterStepSize(double stepSize) {
    m_masterStepSize = stepSize;
}

double FmiCoSimulation::getMasterStepSize() const {
    return m_masterStepSize;
}

double FmiCoSimulation::getCurrentTime() const {
    return m_currentTime;
}

// ---------------------------------------------------------------------------
// State management
// ---------------------------------------------------------------------------
bool FmiCoSimulation::saveAllStates() {
    std::lock_guard<std::mutex> lock(m_fmusMutex);

    // Free old states
    for (auto& [name, state] : m_savedStates) {
        (void)name;
        auto fmuIt = m_fmus.find(name);
        if (fmuIt != m_fmus.end()) {
            fmuIt->second->freeState(&state);
        }
    }
    m_savedStates.clear();

    // Save new states
    for (auto& [name, fmu] : m_fmus) {
        void* state = nullptr;
        if (fmu->saveState(&state)) {
            m_savedStates[name] = state;
        } else {
            return false;
        }
    }

    return true;
}

bool FmiCoSimulation::restoreAllStates() {
    std::lock_guard<std::mutex> lock(m_fmusMutex);

    for (auto& [name, state] : m_savedStates) {
        auto fmuIt = m_fmus.find(name);
        if (fmuIt != m_fmus.end()) {
            if (!fmuIt->second->restoreState(state)) {
                return false;
            }
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------
void FmiCoSimulation::setOnStepComplete(
    std::function<void(double, const std::vector<FmiStepResult>&)> callback) {
    m_onStepComplete = callback;
}

void FmiCoSimulation::setOnError(std::function<void(const std::string&)> callback) {
    m_onError = callback;
}

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------
uint64_t FmiCoSimulation::getTotalSteps() const {
    return m_totalSteps;
}

uint64_t FmiCoSimulation::getAcceptedSteps() const {
    return m_acceptedSteps;
}

uint64_t FmiCoSimulation::getRejectedSteps() const {
    return m_rejectedSteps;
}

double FmiCoSimulation::getSimulationTime() const {
    return m_currentTime;
}

// ---------------------------------------------------------------------------
// OMSimulator integration stubs
// ---------------------------------------------------------------------------
bool FmiCoSimulation::loadOmsModel(const std::string&) {
    // OMSimulator integration would require the OMSimulator library
    // This is a placeholder for future integration
    return false;
}

bool FmiCoSimulation::simulateOms(double) {
    return false;
}

// ---------------------------------------------------------------------------
// Private step methods
// ---------------------------------------------------------------------------
bool FmiCoSimulation::doSerialGaussSeidelStep(double stepSize) {
    std::lock_guard<std::mutex> lock(m_fmusMutex);

    // Exchange inputs
    for (const auto& conn : m_connections) {
        auto sourceIt = m_fmus.find(conn.sourceInstance);
        auto targetIt = m_fmus.find(conn.targetInstance);
        if (sourceIt == m_fmus.end() || targetIt == m_fmus.end()) continue;

        fmi2Real value = 0.0;
        if (sourceIt->second->getReal(conn.sourceVR, value)) {
            targetIt->second->setReal(conn.targetVR, conn.gain * value + conn.offset);
        }
    }

    // Step each FMU
    bool allAccepted = true;
    for (auto& [name, fmu] : m_fmus) {
        (void)name;
        auto result = fmu->doStep(m_currentTime, stepSize, false);
        if (!result.stepAccepted) {
            allAccepted = false;
        }
    }

    return allAccepted;
}

bool FmiCoSimulation::doParallelJacobiStep(double) {
    // Parallel Jacobi: first read all outputs, then write all inputs, then step all FMUs
    // Implementation would require thread pool
    return doSerialGaussSeidelStep(m_masterStepSize);
}

void FmiCoSimulation::exchangeCouplingVariables(const FmiConnection& conn) {
    auto sourceIt = m_fmus.find(conn.sourceInstance);
    auto targetIt = m_fmus.find(conn.targetInstance);
    if (sourceIt == m_fmus.end() || targetIt == m_fmus.end()) return;

    fmi2Real value = 0.0;
    if (sourceIt->second->getReal(conn.sourceVR, value)) {
        double adjustedValue = conn.gain * value + conn.offset;
        targetIt->second->setReal(conn.targetVR, adjustedValue);
    }
}

} // namespace powsys365
