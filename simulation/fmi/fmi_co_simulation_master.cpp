#include "fmi_co_simulation_master.h"

#include <iostream>
#include <sstream>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <cstring>
#include <cstdarg>
#include <thread>

namespace powsys365::simulation::fmi {

// ============================================================================
// FMUInstance Implementation
// ============================================================================

namespace {

// Standard FMI 2.0 callback logger
void fmi2CallbackLogger(fmi2ComponentEnvironment /*componentEnvironment*/,
                        fmi2String instanceName,
                        fmi2Status status,
                        fmi2String category,
                        fmi2String message, ...) {
    const char* statusStr;
    switch (status) {
        case fmi2OK:      statusStr = "OK"; break;
        case fmi2Warning: statusStr = "WARNING"; break;
        case fmi2Discard: statusStr = "DISCARD"; break;
        case fmi2Error:   statusStr = "ERROR"; break;
        case fmi2Fatal:   statusStr = "FATAL"; break;
        case fmi2Pending: statusStr = "PENDING"; break;
        default:          statusStr = "UNKNOWN"; break;
    }

    std::cerr << "[FMU " << (instanceName ? instanceName : "?") << "] "
              << "[" << statusStr << "] "
              << "[" << (category ? category : "?") << "] ";

    // Print variadic message
    if (message) {
        va_list args;
        va_start(args, message);
        vfprintf(stderr, message, args);
        va_end(args);
    }
    std::cerr << std::endl;
}

// Standard memory callbacks
void* fmi2CallbackAllocateMemory(size_t nobj, size_t size) {
    return calloc(nobj, size);
}

void fmi2CallbackFreeMemory(void* obj) {
    free(obj);
}

void fmi2StepFinished(fmi2ComponentEnvironment /*componentEnvironment*/, fmi2Status /*status*/) {
    // Async step finished - not used in synchronous mode
}

// Default callbacks singleton
fmi2CallbackFunctions g_defaultCallbacks = {
    fmi2CallbackLogger,
    fmi2CallbackAllocateMemory,
    fmi2CallbackFreeMemory,
    fmi2StepFinished,
    nullptr  // componentEnvironment
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// FMUInstance
// ---------------------------------------------------------------------------

FMUInstance::FMUInstance(uint32_t index, std::shared_ptr<FMUImporter> importer)
    : m_index(index), m_importer(std::move(importer)) {
    if (m_importer && m_importer->modelDescription()) {
        buildVariableMap();
    }
}

FMUInstance::~FMUInstance() {
    if (m_component && m_importer && m_importer->fmi2Functions() && 
        m_importer->fmi2Functions()->freeInstance) {
        m_importer->fmi2Functions()->freeInstance(m_component);
        m_component = nullptr;
    }
}

FMUInstance::FMUInstance(FMUInstance&& other) noexcept
    : m_index(other.m_index), m_importer(std::move(other.m_importer)),
      m_instanceName(std::move(other.m_instanceName)),
      m_state(other.m_state), m_component(other.m_component),
      m_currentTime(other.m_currentTime), m_lastError(std::move(other.m_lastError)),
      m_nameToVR(std::move(other.m_nameToVR)) {
    other.m_component = nullptr;
}

FMUInstance& FMUInstance::operator=(FMUInstance&& other) noexcept {
    if (this != &other) {
        if (m_component && m_importer && m_importer->fmi2Functions() &&
            m_importer->fmi2Functions()->freeInstance) {
            m_importer->fmi2Functions()->freeInstance(m_component);
        }
        m_index = other.m_index;
        m_importer = std::move(other.m_importer);
        m_instanceName = std::move(other.m_instanceName);
        m_state = other.m_state;
        m_component = other.m_component;
        m_currentTime = other.m_currentTime;
        m_lastError = std::move(other.m_lastError);
        m_nameToVR = std::move(other.m_nameToVR);
        other.m_component = nullptr;
    }
    return *this;
}

void FMUInstance::buildVariableMap() {
    m_nameToVR.clear();
    const FMIModelDescription* md = modelDescription();
    if (!md) return;

    for (const auto& var : md->variables()) {
        m_nameToVR[var.name] = var.valueReference;
    }
}

bool FMUInstance::checkStatus(fmi2Status status, const std::string& operation) {
    switch (status) {
        case fmi2OK:
            return true;
        case fmi2Warning:
            m_lastError = "Warning in " + operation;
            return true;
        case fmi2Discard:
            m_lastError = "Discard from " + operation;
            return false;
        case fmi2Error:
            m_lastError = "Error in " + operation;
            m_state = FMUInstanceState::Error;
            return false;
        case fmi2Fatal:
            m_lastError = "Fatal error in " + operation;
            m_state = FMUInstanceState::Error;
            return false;
        case fmi2Pending:
            m_lastError = "Pending operation: " + operation;
            return false;
        default:
            m_lastError = "Unknown status from " + operation;
            return false;
    }
}

bool FMUInstance::instantiate(const std::string& instanceName, fmi2Type type,
                               const fmi2CallbackFunctions* callbacks) {
    if (!m_importer || !m_importer->fmi2Functions() || !m_importer->fmi2Functions()->instantiate) {
        m_lastError = "Importer or instantiate function not available";
        return false;
    }

    const FMIModelDescription* md = m_importer->modelDescription();
    if (!md || !md->isValid()) {
        m_lastError = "Model description not available or invalid";
        return false;
    }

    m_instanceName = instanceName;

    // Build resource location path
    std::string resourceLocation = "file:///" + m_importer->extractedPath();
    // Replace backslashes with forward slashes
    std::replace(resourceLocation.begin(), resourceLocation.end(), '\\', '/');

    // Use provided callbacks or defaults
    const fmi2CallbackFunctions* cb = callbacks ? callbacks : &g_defaultCallbacks;

    m_component = m_importer->fmi2Functions()->instantiate(
        m_instanceName.c_str(),
        type,
        md->guid().c_str(),
        resourceLocation.c_str(),
        cb,
        fmi2Boolean(0),  // visible
        fmi2Boolean(0)   // loggingOn
    );

    if (!m_component) {
        m_lastError = "fmi2Instantiate returned null for instance: " + instanceName;
        m_state = FMUInstanceState::Error;
        return false;
    }

    m_state = FMUInstanceState::Instantiated;
    return true;
}

bool FMUInstance::setupExperiment(double startTime, double stopTime, double tolerance) {
    if (!m_component || !m_importer || !m_importer->fmi2Functions() ||
        !m_importer->fmi2Functions()->setupExperiment) {
        m_lastError = "Cannot setup experiment: component or function not available";
        return false;
    }

    fmi2Boolean toleranceDefined = (tolerance > 0.0) ? fmi2Boolean(1) : fmi2Boolean(0);
    fmi2Boolean stopTimeDefined = (stopTime > startTime) ? fmi2Boolean(1) : fmi2Boolean(0);

    fmi2Status status = m_importer->fmi2Functions()->setupExperiment(
        m_component, toleranceDefined, tolerance, startTime, stopTimeDefined, stopTime
    );

    return checkStatus(status, "fmi2SetupExperiment");
}

bool FMUInstance::enterInitializationMode() {
    if (!m_component || !m_importer || !m_importer->fmi2Functions() ||
        !m_importer->fmi2Functions()->enterInitializationMode) {
        m_lastError = "Cannot enter initialization mode";
        return false;
    }

    fmi2Status status = m_importer->fmi2Functions()->enterInitializationMode(m_component);
    if (checkStatus(status, "fmi2EnterInitializationMode")) {
        m_state = FMUInstanceState::InitializationMode;
        return true;
    }
    return false;
}

bool FMUInstance::exitInitializationMode() {
    if (!m_component || !m_importer || !m_importer->fmi2Functions() ||
        !m_importer->fmi2Functions()->exitInitializationMode) {
        m_lastError = "Cannot exit initialization mode";
        return false;
    }

    fmi2Status status = m_importer->fmi2Functions()->exitInitializationMode(m_component);
    if (checkStatus(status, "fmi2ExitInitializationMode")) {
        m_state = FMUInstanceState::Initialized;
        return true;
    }
    return false;
}

bool FMUInstance::terminate() {
    if (!m_component || !m_importer || !m_importer->fmi2Functions() ||
        !m_importer->fmi2Functions()->terminate) {
        m_state = FMUInstanceState::Terminated;
        return false;
    }

    fmi2Status status = m_importer->fmi2Functions()->terminate(m_component);
    m_state = FMUInstanceState::Terminated;
    return checkStatus(status, "fmi2Terminate");
}

bool FMUInstance::reset() {
    if (!m_component || !m_importer || !m_importer->fmi2Functions() ||
        !m_importer->fmi2Functions()->reset) {
        m_lastError = "Cannot reset FMU";
        return false;
    }

    fmi2Status status = m_importer->fmi2Functions()->reset(m_component);
    if (checkStatus(status, "fmi2Reset")) {
        m_state = FMUInstanceState::Instantiated;
        m_currentTime = 0.0;
        return true;
    }
    return false;
}

StepResult FMUInstance::doStep(double currentCommunicationPoint, double stepSize) {
    StepResult result;

    if (!m_component || !m_importer || !m_importer->fmi2Functions() ||
        !m_importer->fmi2Functions()->doStep) {
        m_lastError = "Cannot do step: component or doStep function not available";
        result.errorMessage = m_lastError;
        m_state = FMUInstanceState::StepFailed;
        return result;
    }

    if (m_state == FMUInstanceState::Error || m_state == FMUInstanceState::Terminated) {
        m_lastError = "FMU in invalid state for doStep";
        result.errorMessage = m_lastError;
        return result;
    }

    m_state = FMUInstanceState::Stepping;

    fmi2Status status = m_importer->fmi2Functions()->doStep(
        m_component,
        static_cast<fmi2Real>(currentCommunicationPoint),
        static_cast<fmi2Real>(stepSize),
        fmi2Boolean(0)  // noSetFMUStatePriorToCurrentPoint
    );

    result.actualStepSize = stepSize;
    result.nextCommunicationPoint = currentCommunicationPoint + stepSize;

    if (status == fmi2OK) {
        result.success = true;
        m_currentTime = currentCommunicationPoint + stepSize;
        m_state = FMUInstanceState::StepCompleted;
    } else if (status == fmi2Warning) {
        result.success = true;
        m_currentTime = currentCommunicationPoint + stepSize;
        m_state = FMUInstanceState::StepCompleted;
        m_lastError = "Warning during fmi2DoStep";
    } else if (status == fmi2Discard) {
        result.success = false;
        m_lastError = "Step discarded by FMU";
        result.errorMessage = m_lastError;
        m_state = FMUInstanceState::StepFailed;

        // Try to get last successful time
        if (m_importer->fmi2Functions()->getRealStatus) {
            fmi2Real lastTime;
            fmi2Status st = m_importer->fmi2Functions()->getRealStatus(
                m_component, fmi2LastSuccessfulTime, &lastTime);
            if (st == fmi2OK) {
                result.lastSuccessfulTime = static_cast<double>(lastTime);
            }
        }
    } else {
        result.success = false;
        m_lastError = "Step failed with status: " + std::to_string(static_cast<int>(status));
        result.errorMessage = m_lastError;
        m_state = FMUInstanceState::StepFailed;
    }

    return result;
}

// ---------------------------------------------------------------------------
// Variable Access
// ---------------------------------------------------------------------------

bool FMUInstance::getReal(uint32_t vr, double& value) {
    if (!m_component || !m_importer || !m_importer->fmi2Functions()) return false;

    fmi2ValueReference vref = vr;
    fmi2Real val;
    fmi2Status status = m_importer->fmi2Functions()->getReal(m_component, &vref, 1, &val);
    if (checkStatus(status, "fmi2GetReal")) {
        value = static_cast<double>(val);
        return true;
    }
    return false;
}

bool FMUInstance::getReal(const std::vector<fmi2ValueReference>& vrs, std::vector<double>& values) {
    if (!m_component || !m_importer || !m_importer->fmi2Functions() || vrs.empty()) return false;

    values.resize(vrs.size());
    std::vector<fmi2Real> fmiValues(vrs.size());
    fmi2Status status = m_importer->fmi2Functions()->getReal(
        m_component, vrs.data(), vrs.size(), fmiValues.data()
    );
    if (checkStatus(status, "fmi2GetReal (batch)")) {
        for (size_t i = 0; i < vrs.size(); ++i) {
            values[i] = static_cast<double>(fmiValues[i]);
        }
        return true;
    }
    return false;
}

bool FMUInstance::setReal(uint32_t vr, double value) {
    if (!m_component || !m_importer || !m_importer->fmi2Functions()) return false;

    fmi2ValueReference vref = vr;
    fmi2Real val = static_cast<fmi2Real>(value);
    fmi2Status status = m_importer->fmi2Functions()->setReal(m_component, &vref, 1, &val);
    return checkStatus(status, "fmi2SetReal");
}

bool FMUInstance::setReal(const std::vector<fmi2ValueReference>& vrs, const std::vector<double>& values) {
    if (!m_component || !m_importer || !m_importer->fmi2Functions() || vrs.empty() || vrs.size() != values.size()) return false;

    std::vector<fmi2Real> fmiValues(values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        fmiValues[i] = static_cast<fmi2Real>(values[i]);
    }
    fmi2Status status = m_importer->fmi2Functions()->setReal(
        m_component, vrs.data(), vrs.size(), fmiValues.data()
    );
    return checkStatus(status, "fmi2SetReal (batch)");
}

bool FMUInstance::getInteger(uint32_t vr, int32_t& value) {
    if (!m_component || !m_importer || !m_importer->fmi2Functions()) return false;

    fmi2ValueReference vref = vr;
    fmi2Integer val;
    fmi2Status status = m_importer->fmi2Functions()->getInteger(m_component, &vref, 1, &val);
    if (checkStatus(status, "fmi2GetInteger")) {
        value = static_cast<int32_t>(val);
        return true;
    }
    return false;
}

bool FMUInstance::setInteger(uint32_t vr, int32_t value) {
    if (!m_component || !m_importer || !m_importer->fmi2Functions()) return false;

    fmi2ValueReference vref = vr;
    fmi2Integer val = static_cast<fmi2Integer>(value);
    fmi2Status status = m_importer->fmi2Functions()->setInteger(m_component, &vref, 1, &val);
    return checkStatus(status, "fmi2SetInteger");
}

bool FMUInstance::getBoolean(uint32_t vr, bool& value) {
    if (!m_component || !m_importer || !m_importer->fmi2Functions()) return false;

    fmi2ValueReference vref = vr;
    fmi2Boolean val;
    fmi2Status status = m_importer->fmi2Functions()->getBoolean(m_component, &vref, 1, &val);
    if (checkStatus(status, "fmi2GetBoolean")) {
        value = (val != 0);
        return true;
    }
    return false;
}

bool FMUInstance::setBoolean(uint32_t vr, bool value) {
    if (!m_component || !m_importer || !m_importer->fmi2Functions()) return false;

    fmi2ValueReference vref = vr;
    fmi2Boolean val = value ? fmi2Boolean(1) : fmi2Boolean(0);
    fmi2Status status = m_importer->fmi2Functions()->setBoolean(m_component, &vref, 1, &val);
    return checkStatus(status, "fmi2SetBoolean");
}

bool FMUInstance::getString(uint32_t vr, std::string& value) {
    if (!m_component || !m_importer || !m_importer->fmi2Functions()) return false;

    fmi2ValueReference vref = vr;
    fmi2String val = nullptr;
    fmi2Status status = m_importer->fmi2Functions()->getString(m_component, &vref, 1, &val);
    if (checkStatus(status, "fmi2GetString")) {
        value = val ? val : "";
        return true;
    }
    return false;
}

bool FMUInstance::setString(uint32_t vr, const std::string& value) {
    if (!m_component || !m_importer || !m_importer->fmi2Functions()) return false;

    fmi2ValueReference vref = vr;
    fmi2String val = value.c_str();
    fmi2Status status = m_importer->fmi2Functions()->setString(m_component, &vref, 1, &val);
    return checkStatus(status, "fmi2SetString");
}

bool FMUInstance::setInitialValues(const std::unordered_map<std::string, double>& values) {
    bool allOk = true;

    std::vector<fmi2ValueReference> realVRs;
    std::vector<fmi2Real> realVals;
    std::vector<fmi2ValueReference> intVRs;
    std::vector<fmi2Integer> intVals;
    std::vector<fmi2ValueReference> boolVRs;
    std::vector<fmi2Boolean> boolVals;

    for (const auto& [name, val] : values) {
        auto it = m_nameToVR.find(name);
        if (it == m_nameToVR.end()) {
            m_lastError = "Variable not found: " + name;
            allOk = false;
            continue;
        }

        const FMIVariable* var = getVariable(name);
        if (!var) continue;

        switch (var->type) {
            case FMIType::Real:
            case FMIType::Float64:
            case FMIType::Float32:
                realVRs.push_back(it->second);
                realVals.push_back(static_cast<fmi2Real>(val));
                break;
            case FMIType::Integer:
            case FMIType::Int8:
            case FMIType::UInt8:
            case FMIType::Int16:
            case FMIType::UInt16:
            case FMIType::Int32:
            case FMIType::UInt32:
            case FMIType::Int64:
            case FMIType::UInt64:
                intVRs.push_back(it->second);
                intVals.push_back(static_cast<fmi2Integer>(val));
                break;
            case FMIType::Boolean:
                boolVRs.push_back(it->second);
                boolVals.push_back(val != 0.0 ? fmi2Boolean(1) : fmi2Boolean(0));
                break;
            default:
                break;
        }
    }

    if (!realVRs.empty()) {
        fmi2Status status = m_importer->fmi2Functions()->setReal(
            m_component, realVRs.data(), realVRs.size(), realVals.data()
        );
        if (!checkStatus(status, "fmi2SetReal (batch init)")) allOk = false;
    }
    if (!intVRs.empty()) {
        fmi2Status status = m_importer->fmi2Functions()->setInteger(
            m_component, intVRs.data(), intVRs.size(), intVals.data()
        );
        if (!checkStatus(status, "fmi2SetInteger (batch init)")) allOk = false;
    }
    if (!boolVRs.empty()) {
        fmi2Status status = m_importer->fmi2Functions()->setBoolean(
            m_component, boolVRs.data(), boolVRs.size(), boolVals.data()
        );
        if (!checkStatus(status, "fmi2SetBoolean (batch init)")) allOk = false;
    }

    return allOk;
}

bool FMUInstance::getVariableValues(std::unordered_map<std::string, double>& values) {
    bool allOk = true;

    std::vector<fmi2ValueReference> realVRs;
    std::vector<std::string> realNames;
    std::vector<fmi2ValueReference> intVRs;
    std::vector<std::string> intNames;

    for (const auto& [name, vr] : m_nameToVR) {
        const FMIVariable* var = getVariable(name);
        if (!var) continue;

        switch (var->type) {
            case FMIType::Real:
            case FMIType::Float64:
            case FMIType::Float32:
                realVRs.push_back(vr);
                realNames.push_back(name);
                break;
            case FMIType::Integer:
            case FMIType::Int8:
            case FMIType::Int16:
            case FMIType::Int32:
                intVRs.push_back(vr);
                intNames.push_back(name);
                break;
            default:
                break;
        }
    }

    if (!realVRs.empty()) {
        std::vector<fmi2Real> fmiValues(realVRs.size());
        fmi2Status status = m_importer->fmi2Functions()->getReal(
            m_component, realVRs.data(), realVRs.size(), fmiValues.data()
        );
        if (checkStatus(status, "fmi2GetReal (batch values)")) {
            for (size_t i = 0; i < realVRs.size(); ++i) {
                values[realNames[i]] = static_cast<double>(fmiValues[i]);
            }
        } else {
            allOk = false;
        }
    }

    if (!intVRs.empty()) {
        std::vector<fmi2Integer> fmiValues(intVRs.size());
        fmi2Status status = m_importer->fmi2Functions()->getInteger(
            m_component, intVRs.data(), intVRs.size(), fmiValues.data()
        );
        if (checkStatus(status, "fmi2GetInteger (batch values)")) {
            for (size_t i = 0; i < intVRs.size(); ++i) {
                values[intNames[i]] = static_cast<double>(fmiValues[i]);
            }
        } else {
            allOk = false;
        }
    }

    // Get boolean values too
    std::vector<fmi2ValueReference> boolVRs;
    std::vector<std::string> boolNames;
    for (const auto& [name, vr] : m_nameToVR) {
        const FMIVariable* var = getVariable(name);
        if (var && var->type == FMIType::Boolean) {
            boolVRs.push_back(vr);
            boolNames.push_back(name);
        }
    }
    if (!boolVRs.empty()) {
        std::vector<fmi2Boolean> fmiValues(boolVRs.size());
        fmi2Status status = m_importer->fmi2Functions()->getBoolean(
            m_component, boolVRs.data(), boolVRs.size(), fmiValues.data()
        );
        if (checkStatus(status, "fmi2GetBoolean (batch values)")) {
            for (size_t i = 0; i < boolVRs.size(); ++i) {
                values[boolNames[i]] = fmiValues[i] != 0 ? 1.0 : 0.0;
            }
        }
    }

    return allOk;
}

// Status
FMUInstanceState FMUInstance::state() const noexcept { return m_state; }
const std::string& FMUInstance::instanceName() const noexcept { return m_instanceName; }
uint32_t FMUInstance::index() const noexcept { return m_index; }
const FMIModelDescription* FMUInstance::modelDescription() const noexcept { return m_importer ? m_importer->modelDescription() : nullptr; }
FMI2Functions* FMUInstance::functions() noexcept { return m_importer ? m_importer->fmi2Functions() : nullptr; }
const std::string& FMUInstance::lastError() const noexcept { return m_lastError; }
double FMUInstance::currentTime() const noexcept { return m_currentTime; }

uint32_t FMUInstance::getVRByName(const std::string& name) const {
    auto it = m_nameToVR.find(name);
    return (it != m_nameToVR.end()) ? it->second : 0;
}

bool FMUInstance::hasVariable(const std::string& name) const {
    return m_nameToVR.find(name) != m_nameToVR.end();
}

const FMIVariable* FMUInstance::getVariable(const std::string& name) const {
    if (!m_importer || !m_importer->modelDescription()) return nullptr;
    return m_importer->modelDescription()->findVariableByName(name);
}

bool FMUInstance::canGetAndSetFMUState() const {
    if (!m_importer || !m_importer->modelDescription()) return false;
    return m_importer->modelDescription()->coSimulationInfo().canGetAndSetFMUState;
}

bool FMUInstance::getFMUState(fmi2FMUstate& state) {
    // Note: FMI 2.0 getFMUState is not directly stored in our function pointers.
    // This would require fmi2GetFMUstate/fmi2SetFMUstate symbols.
    m_lastError = "getFMUState not fully implemented - requires dynamic lookup";
    return false;
}

bool FMUInstance::setFMUState(fmi2FMUstate /*state*/) {
    m_lastError = "setFMUState not fully implemented - requires dynamic lookup";
    return false;
}

bool FMUInstance::freeFMUState(fmi2FMUstate& /*state*/) {
    m_lastError = "freeFMUState not fully implemented - requires dynamic lookup";
    return false;
}

bool FMUInstance::serializeFMUState(fmi2FMUstate /*state*/, std::vector<fmi2Byte>& /*serialized*/) {
    m_lastError = "serializeFMUState not fully implemented - requires dynamic lookup";
    return false;
}

bool FMUInstance::deserializeFMUState(const std::vector<fmi2Byte>& /*serialized*/, fmi2FMUstate& /*state*/) {
    m_lastError = "deserializeFMUState not fully implemented - requires dynamic lookup";
    return false;
}

// ============================================================================
// FMICoSimulationMaster Implementation
// ============================================================================

FMICoSimulationMaster::FMICoSimulationMaster() = default;

FMICoSimulationMaster::~FMICoSimulationMaster() {
    terminate();
}

FMICoSimulationMaster::FMICoSimulationMaster(FMICoSimulationMaster&&) noexcept = default;
FMICoSimulationMaster& FMICoSimulationMaster::operator=(FMICoSimulationMaster&&) noexcept = default;

// ---------------------------------------------------------------------------
// FMU Management
// ---------------------------------------------------------------------------

uint32_t FMICoSimulationMaster::addFMU(const std::string& fmuPath) {
    return addFMU(fmuPath, "FMU_" + std::to_string(m_fmus.size()));
}

uint32_t FMICoSimulationMaster::addFMU(const std::string& fmuPath, const std::string& instanceName) {
    uint32_t index = static_cast<uint32_t>(m_fmus.size());

    try {
        auto importer = std::make_shared<FMUImporter>(fmuPath);

        if (!importer->extract()) {
            m_lastError = "Failed to extract FMU: " + importer->lastError();
            return static_cast<uint32_t>(-1);
        }

        if (!importer->parseModelDescription()) {
            m_lastError = "Failed to parse modelDescription: " + fmuPath;
            return static_cast<uint32_t>(-1);
        }

        // Load library
        if (!importer->loadLibrary()) {
            m_lastError = "Failed to load FMU library: " + importer->lastError();
            return static_cast<uint32_t>(-1);
        }

        // Verify CoSimulation is supported
        const FMIModelDescription* md = importer->modelDescription();
        if (!md || !md->supportsCoSimulation()) {
            m_lastError = "FMU does not support CoSimulation: " + fmuPath;
            return static_cast<uint32_t>(-1);
        }

        // Create FMU instance
        auto instance = std::make_shared<FMUInstance>(index, importer);

        // Instantiate the FMU
        if (!instance->instantiate(instanceName, fmi2CoSimulation, nullptr)) {
            m_lastError = "Failed to instantiate FMU: " + instance->lastError();
            return static_cast<uint32_t>(-1);
        }

        m_importers.push_back(importer);
        m_fmus.push_back(instance);

        return index;

    } catch (const std::exception& e) {
        m_lastError = std::string("Exception adding FMU: ") + e.what();
        return static_cast<uint32_t>(-1);
    }
}

bool FMICoSimulationMaster::removeFMU(uint32_t fmuIndex) {
    if (fmuIndex >= m_fmus.size()) {
        m_lastError = "Invalid FMU index";
        return false;
    }

    // Terminate the FMU
    if (m_fmus[fmuIndex]) {
        m_fmus[fmuIndex]->terminate();
    }

    // Remove from vectors
    m_fmus.erase(m_fmus.begin() + fmuIndex);
    if (fmuIndex < m_importers.size()) {
        m_importers.erase(m_importers.begin() + fmuIndex);
    }

    // Update remaining FMU indices
    for (uint32_t i = fmuIndex; i < m_fmus.size(); ++i) {
        // FMUInstance doesn't have a setIndex, but we track it internally
    }

    // Remove connections involving this FMU
    auto newEnd = std::remove_if(m_connections.begin(), m_connections.end(),
        [fmuIndex](const FMUConnection& c) {
            return c.sourceFMU == fmuIndex || c.targetFMU == fmuIndex;
        });
    m_connections.erase(newEnd, m_connections.end());

    return true;
}

size_t FMICoSimulationMaster::numFMUs() const noexcept {
    return m_fmus.size();
}

FMUInstance* FMICoSimulationMaster::getFMU(uint32_t fmuIndex) {
    if (fmuIndex < m_fmus.size()) return m_fmus[fmuIndex].get();
    return nullptr;
}

const FMUInstance* FMICoSimulationMaster::getFMU(uint32_t fmuIndex) const {
    if (fmuIndex < m_fmus.size()) return m_fmus[fmuIndex].get();
    return nullptr;
}

// ---------------------------------------------------------------------------
// Connections
// ---------------------------------------------------------------------------

bool FMICoSimulationMaster::addConnection(uint32_t sourceFMU, const std::string& sourceVar,
                                           uint32_t targetFMU, const std::string& targetVar,
                                           double scalingFactor, double offset) {
    if (sourceFMU >= m_fmus.size() || targetFMU >= m_fmus.size()) {
        m_lastError = "Invalid FMU index in connection";
        return false;
    }

    const FMUInstance* src = m_fmus[sourceFMU].get();
    const FMUInstance* tgt = m_fmus[targetFMU].get();

    if (!src->hasVariable(sourceVar)) {
        m_lastError = "Source variable not found: " + sourceVar;
        return false;
    }
    if (!tgt->hasVariable(targetVar)) {
        m_lastError = "Target variable not found: " + targetVar;
        return false;
    }

    FMUConnection conn;
    conn.sourceFMU = sourceFMU;
    conn.sourceVR = src->getVRByName(sourceVar);
    conn.targetFMU = targetFMU;
    conn.targetVR = tgt->getVRByName(targetVar);
    conn.sourceName = sourceVar;
    conn.targetName = targetVar;
    conn.scalingFactor = scalingFactor;
    conn.offset = offset;

    m_connections.push_back(conn);
    return true;
}

bool FMICoSimulationMaster::addConnectionVR(uint32_t sourceFMU, uint32_t sourceVR,
                                              uint32_t targetFMU, uint32_t targetVR,
                                              double scalingFactor, double offset) {
    if (sourceFMU >= m_fmus.size() || targetFMU >= m_fmus.size()) {
        m_lastError = "Invalid FMU index in connection";
        return false;
    }

    FMUConnection conn;
    conn.sourceFMU = sourceFMU;
    conn.sourceVR = sourceVR;
    conn.targetFMU = targetFMU;
    conn.targetVR = targetVR;
    conn.scalingFactor = scalingFactor;
    conn.offset = offset;

    m_connections.push_back(conn);
    return true;
}

bool FMICoSimulationMaster::removeConnection(uint32_t connIndex) {
    if (connIndex >= m_connections.size()) {
        m_lastError = "Invalid connection index";
        return false;
    }
    m_connections.erase(m_connections.begin() + connIndex);
    return true;
}

void FMICoSimulationMaster::clearConnections() {
    m_connections.clear();
}

const std::vector<FMUConnection>& FMICoSimulationMaster::connections() const noexcept {
    return m_connections;
}

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------

bool FMICoSimulationMaster::initialize(double startTime, double stopTime, double tolerance) {
    SimulationConfig config;
    config.startTime = startTime;
    config.stopTime = stopTime;
    config.tolerance = tolerance;
    config.stepSize = (stopTime - startTime) / 1000.0; // Default 1000 steps
    return initialize(config);
}

bool FMICoSimulationMaster::initialize(const SimulationConfig& config) {
    m_config = config;
    m_currentTime = config.startTime;
    m_stats = SimulationStats{};
    m_initialized = false;
    m_terminated = false;
    m_lastError.clear();

    if (m_fmus.empty()) {
        m_lastError = "No FMUs to initialize";
        return false;
    }

    // Setup experiment for all FMUs
    for (auto& fmu : m_fmus) {
        if (!fmu->setupExperiment(config.startTime, config.stopTime, config.tolerance)) {
            m_lastError = "Failed to setup experiment for FMU: " + fmu->instanceName();
            return false;
        }
    }

    // Enter initialization mode
    for (auto& fmu : m_fmus) {
        if (!fmu->enterInitializationMode()) {
            m_lastError = "Failed to enter initialization mode for FMU: " + fmu->instanceName();
            return false;
        }
    }

    // Propagate connections in initialization mode
    if (!m_connections.empty()) {
        if (!propagateConnections()) {
            // Not fatal, just log
            log("Warning: connection propagation during init failed");
        }
    }

    // Exit initialization mode
    for (auto& fmu : m_fmus) {
        if (!fmu->exitInitializationMode()) {
            m_lastError = "Failed to exit initialization mode for FMU: " + fmu->instanceName();
            return false;
        }
    }

    m_initialized = true;
    m_stats.activeFMUs = static_cast<uint32_t>(m_fmus.size());
    m_wallClockStart = std::chrono::steady_clock::now();

    return true;
}

// ---------------------------------------------------------------------------
// Stepping
// ---------------------------------------------------------------------------

StepResult FMICoSimulationMaster::doStep(double stepSize) {
    StepResult result;

    if (!m_initialized) {
        m_lastError = "Simulation not initialized";
        result.errorMessage = m_lastError;
        return result;
    }

    if (m_terminated) {
        m_lastError = "Simulation already terminated";
        result.errorMessage = m_lastError;
        return result;
    }

    // Clamp step size
    double actualStep = stepSize;
    if (actualStep > m_config.maxStepSize) actualStep = m_config.maxStepSize;
    if (actualStep < m_config.minStepSize) actualStep = m_config.minStepSize;

    // Check if we would exceed stop time
    if (m_currentTime + actualStep > m_config.stopTime) {
        actualStep = m_config.stopTime - m_currentTime;
    }

    if (actualStep <= 0.0) {
        result.success = true;
        result.actualStepSize = 0.0;
        result.nextCommunicationPoint = m_config.stopTime;
        return result;
    }

    m_stats.totalSteps++;

    // Propagate connections before step
    if (!m_connections.empty()) {
        if (!propagateConnections()) {
            log("Connection propagation failed before step");
        }
    }

    // Process any pending events
    if (m_config.enableEventHandling && m_eventQueue) {
        processEvents();
    }

    // Execute step on all FMUs
    bool allOk = true;
    if (m_config.parallelFMUExecution && m_fmus.size() > 1) {
        allOk = doStepParallel(m_currentTime, actualStep);
    } else {
        allOk = doStepAll(actualStep);
    }

    // Propagate connections after step
    if (!m_connections.empty()) {
        if (!propagateConnections()) {
            log("Connection propagation failed after step");
        }
    }

    if (allOk) {
        result.success = true;
        result.actualStepSize = actualStep;
        m_currentTime += actualStep;
        result.nextCommunicationPoint = m_currentTime;
        m_stats.successfulSteps++;
        m_stats.currentTime = m_currentTime;

        // Real-time synchronization
        if (m_config.enableRealTimeSync || m_realTimeSync) {
            syncRealTime();
        }

        // Progress callback
        if (m_config.progressCallback) {
            m_config.progressCallback(m_currentTime, static_cast<uint32_t>(m_fmus.size()));
        }
    } else {
        result.success = false;
        m_stats.failedSteps++;
        m_lastError = "Step failed for one or more FMUs";
        result.errorMessage = m_lastError;

        // Retry with smaller step if configured
        if (m_config.variableStep && actualStep > m_config.minStepSize * 2.0) {
            double retryStep = actualStep / 2.0;
            result = doStep(retryStep);
            m_stats.stepRejections++;
        }
    }

    m_stats.totalSimulatedTime = m_currentTime - m_config.startTime;
    if (m_stats.totalSteps > 0) {
        m_stats.averageStepSize = m_stats.totalSimulatedTime / m_stats.totalSteps;
    }

    return result;
}

bool FMICoSimulationMaster::doStepAll(double stepSize) {
    bool allOk = true;

    for (size_t i = 0; i < m_fmus.size(); ++i) {
        if (!m_fmus[i]) continue;

        auto result = m_fmus[i]->doStep(m_currentTime, stepSize);
        if (!result.success) {
            allOk = false;
            m_lastError = "Step failed for FMU " + std::to_string(i) + ": " + result.errorMessage;
        }
    }

    return allOk;
}

bool FMICoSimulationMaster::doStepSingle(uint32_t fmuIndex, double currentTime, double stepSize) {
    if (fmuIndex >= m_fmus.size() || !m_fmus[fmuIndex]) return false;
    auto result = m_fmus[fmuIndex]->doStep(currentTime, stepSize);
    return result.success;
}

bool FMICoSimulationMaster::doStepParallel(double currentTime, double stepSize) {
    std::vector<std::thread> threads;
    std::atomic<bool> allOk{true};

    for (size_t i = 0; i < m_fmus.size(); ++i) {
        if (!m_fmus[i]) continue;

        threads.emplace_back([this, i, currentTime, stepSize, &allOk]() {
            auto result = m_fmus[i]->doStep(currentTime, stepSize);
            if (!result.success) {
                allOk.store(false, std::memory_order_relaxed);
            }
        });
    }

    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }

    return allOk.load();
}

// ---------------------------------------------------------------------------
// Variable Access
// ---------------------------------------------------------------------------

double FMICoSimulationMaster::getVariable(uint32_t fmuIndex, const std::string& varName) {
    if (fmuIndex >= m_fmus.size() || !m_fmus[fmuIndex]) {
        m_lastError = "Invalid FMU index";
        return std::numeric_limits<double>::quiet_NaN();
    }

    uint32_t vr = m_fmus[fmuIndex]->getVRByName(varName);
    if (vr == 0) {
        m_lastError = "Variable not found: " + varName;
        return std::numeric_limits<double>::quiet_NaN();
    }

    double value = 0.0;
    if (m_fmus[fmuIndex]->getReal(vr, value)) {
        return value;
    }

    return std::numeric_limits<double>::quiet_NaN();
}

bool FMICoSimulationMaster::setVariable(uint32_t fmuIndex, const std::string& varName, double value) {
    if (fmuIndex >= m_fmus.size() || !m_fmus[fmuIndex]) {
        m_lastError = "Invalid FMU index";
        return false;
    }

    uint32_t vr = m_fmus[fmuIndex]->getVRByName(varName);
    if (vr == 0) {
        m_lastError = "Variable not found: " + varName;
        return false;
    }

    return m_fmus[fmuIndex]->setReal(vr, value);
}

bool FMICoSimulationMaster::getVariables(uint32_t fmuIndex, std::unordered_map<std::string, double>& values) {
    if (fmuIndex >= m_fmus.size() || !m_fmus[fmuIndex]) {
        m_lastError = "Invalid FMU index";
        return false;
    }
    return m_fmus[fmuIndex]->getVariableValues(values);
}

bool FMICoSimulationMaster::setVariables(uint32_t fmuIndex, const std::unordered_map<std::string, double>& values) {
    if (fmuIndex >= m_fmus.size() || !m_fmus[fmuIndex]) {
        m_lastError = "Invalid FMU index";
        return false;
    }
    return m_fmus[fmuIndex]->setInitialValues(values);
}

// ---------------------------------------------------------------------------
// Connection Propagation
// ---------------------------------------------------------------------------

bool FMICoSimulationMaster::propagateConnections() {
    bool allOk = true;

    for (const auto& conn : m_connections) {
        if (conn.sourceFMU >= m_fmus.size() || conn.targetFMU >= m_fmus.size()) {
            continue;
        }

        double value = 0.0;
        if (!m_fmus[conn.sourceFMU]->getReal(conn.sourceVR, value)) {
            allOk = false;
            continue;
        }

        // Apply scaling and offset
        double scaledValue = value * conn.scalingFactor + conn.offset;

        if (!m_fmus[conn.targetFMU]->setReal(conn.targetVR, scaledValue)) {
            allOk = false;
        }
    }

    return allOk;
}

bool FMICoSimulationMaster::propagateConnections(uint32_t sourceFMU) {
    bool allOk = true;

    for (const auto& conn : m_connections) {
        if (conn.sourceFMU != sourceFMU) continue;
        if (conn.targetFMU >= m_fmus.size()) continue;

        double value = 0.0;
        if (!m_fmus[conn.sourceFMU]->getReal(conn.sourceVR, value)) {
            allOk = false;
            continue;
        }

        double scaledValue = value * conn.scalingFactor + conn.offset;

        if (!m_fmus[conn.targetFMU]->setReal(conn.targetVR, scaledValue)) {
            allOk = false;
        }
    }

    return allOk;
}

// ---------------------------------------------------------------------------
// Full Simulation
// ---------------------------------------------------------------------------

bool FMICoSimulationMaster::runSimulation() {
    if (!m_initialized) {
        m_lastError = "Simulation not initialized";
        return false;
    }

    m_running = true;
    m_stats = SimulationStats{};
    m_stats.activeFMUs = static_cast<uint32_t>(m_fmus.size());

    log("Starting simulation from t=" + std::to_string(m_config.startTime) +
        " to t=" + std::to_string(m_config.stopTime));

    while (m_currentTime < m_config.stopTime - m_config.minStepSize) {
        StepResult result = doStep(m_config.stepSize);

        if (!result.success) {
            if (m_config.variableStep && result.actualStepSize > m_config.minStepSize) {
                // Already handled in doStep with retry
                continue;
            }

            log("Simulation failed at t=" + std::to_string(m_currentTime) +
                ": " + result.errorMessage);
            m_running = false;
            return false;
        }

        // Check if we've reached stop time
        if (m_currentTime >= m_config.stopTime - m_config.minStepSize) {
            break;
        }
    }

    // Calculate total wall time
    auto now = std::chrono::steady_clock::now();
    m_stats.totalWallTime = std::chrono::duration<double>(now - m_wallClockStart).count();

    log("Simulation completed. Total steps: " + std::to_string(m_stats.totalSteps) +
        ", Successful: " + std::to_string(m_stats.successfulSteps) +
        ", Failed: " + std::to_string(m_stats.failedSteps) +
        ", Wall time: " + std::to_string(m_stats.totalWallTime) + "s");

    m_running = false;
    return true;
}

// ---------------------------------------------------------------------------
// Termination
// ---------------------------------------------------------------------------

void FMICoSimulationMaster::terminate() {
    if (m_terminated) return;

    for (auto& fmu : m_fmus) {
        if (fmu) {
            fmu->terminate();
        }
    }

    m_terminated = true;
    m_initialized = false;
    m_running = false;
    log("Simulation terminated");
}

// ---------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------

const SimulationConfig& FMICoSimulationMaster::config() const noexcept { return m_config; }
const SimulationStats& FMICoSimulationMaster::stats() const noexcept { return m_stats; }
double FMICoSimulationMaster::currentTime() const noexcept { return m_currentTime; }
bool FMICoSimulationMaster::isRunning() const noexcept { return m_running; }
bool FMICoSimulationMaster::isInitialized() const noexcept { return m_initialized; }
const std::string& FMICoSimulationMaster::lastError() const noexcept { return m_lastError; }

// ---------------------------------------------------------------------------
// Real-time Synchronization
// ---------------------------------------------------------------------------

void FMICoSimulationMaster::setRealTimeSync(bool enable, double realTimeFactor) {
    m_realTimeSync = enable;
    m_realTimeFactor = realTimeFactor;
    m_config.enableRealTimeSync = enable;
    m_config.realTimeFactor = realTimeFactor;
}

bool FMICoSimulationMaster::isRealTimeSyncEnabled() const noexcept {
    return m_realTimeSync || m_config.enableRealTimeSync;
}

void FMICoSimulationMaster::syncRealTime() {
    double expectedWallTime = (m_currentTime - m_config.startTime) / m_realTimeFactor;
    auto expectedWallDuration = std::chrono::duration<double>(expectedWallTime);
    auto expectedTime = m_wallClockStart + std::chrono::duration_cast<std::chrono::steady_clock::duration>(expectedWallDuration);

    auto now = std::chrono::steady_clock::now();
    if (expectedTime > now) {
        std::this_thread::sleep_until(expectedTime);
    }
}

// ---------------------------------------------------------------------------
// Event Handling
// ---------------------------------------------------------------------------

void FMICoSimulationMaster::setEventQueue(std::shared_ptr<EventQueue> queue) {
    m_eventQueue = queue;
}

bool FMICoSimulationMaster::processEvents() {
    if (!m_eventQueue || m_eventQueue->empty()) return true;

    bool processed = false;
    while (!m_eventQueue->empty() && m_eventQueue->peek().time <= m_currentTime) {
        Event event = m_eventQueue->pop();
        m_stats.events++;

        // Process event based on type
        switch (event.type) {
            case EventType::SetValue: {
                // data format: "fmuIndex,vr,value"
                std::istringstream iss(event.data);
                uint32_t fmuIdx;
                uint32_t vr;
                double value;
                char comma;
                if (iss >> fmuIdx >> comma >> vr >> comma >> value) {
                    if (fmuIdx < m_fmus.size()) {
                        m_fmus[fmuIdx]->setReal(vr, value);
                    }
                }
                break;
            }
            case EventType::StepSizeChange: {
                // data contains new step size
                try {
                    double newStep = std::stod(event.data);
                    m_config.stepSize = std::clamp(newStep, m_config.minStepSize, m_config.maxStepSize);
                } catch (...) {}
                break;
            }
            case EventType::Terminate: {
                terminate();
                return false;
            }
            default:
                break;
        }

        processed = true;
    }

    return processed;
}

void FMICoSimulationMaster::log(const std::string& message) {
    if (m_config.logCallback) {
        m_config.logCallback(message);
    }
}

} // namespace powsys365::simulation::fmi
