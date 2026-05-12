#pragma once

#include "fmi_importer.h"
#include "../event_queue.h"

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>

namespace powsys365::simulation::fmi {

// ============================================================================
// FMU Instance State
// ============================================================================
enum class FMUInstanceState {
    Created,           // Just created, not initialized
    Instantiated,      // fmi2Instantiate called
    InitializationMode,// In initialization mode
    Initialized,       // Initialization complete, ready for simulation
    Stepping,          // Currently in a doStep
    StepCompleted,     // Step completed successfully
    StepFailed,        // Step failed
    Terminated,        // fmi2Terminate called
    Error              // Error state
};

// ============================================================================
// Variable Connection (between FMUs)
// ============================================================================
struct FMUConnection {
    uint32_t sourceFMU = 0;
    uint32_t sourceVR = 0;
    uint32_t targetFMU = 0;
    uint32_t targetVR = 0;
    std::string sourceName;
    std::string targetName;
    double scalingFactor = 1.0;
    double offset = 0.0;
};

// ============================================================================
// Simulation Configuration
// ============================================================================
struct SimulationConfig {
    double startTime = 0.0;
    double stopTime = 1.0;
    double stepSize = 0.001;
    double tolerance = 1e-6;
    bool variableStep = false;
    bool enableEventHandling = true;
    bool enableRealTimeSync = false;
    double realTimeFactor = 1.0;  // 1.0 = real-time, 2.0 = 2x faster
    uint32_t maxStepRetries = 3;
    double minStepSize = 1e-12;
    double maxStepSize = 0.1;
    bool parallelFMUExecution = false;
    std::function<void(double, uint32_t)> progressCallback;  // time, fmuIndex
    std::function<void(const std::string&)> logCallback;
};

// ============================================================================
// Step Result
// ============================================================================
struct StepResult {
    bool success = false;
    double actualStepSize = 0.0;
    double nextCommunicationPoint = 0.0;
    std::string errorMessage;
    bool earlyReturn = false;
    double lastSuccessfulTime = 0.0;
};

// ============================================================================
// Simulation Statistics
// ============================================================================
struct SimulationStats {
    uint64_t totalSteps = 0;
    uint64_t successfulSteps = 0;
    uint64_t failedSteps = 0;
    uint64_t stepRejections = 0;
    uint64_t events = 0;
    double totalSimulatedTime = 0.0;
    double totalWallTime = 0.0;
    double averageStepSize = 0.0;
    double currentTime = 0.0;
    uint32_t activeFMUs = 0;
};

// ============================================================================
// FMU Instance (wrapper around an imported FMU)
// ============================================================================
class FMUInstance {
public:
    FMUInstance(uint32_t index, std::shared_ptr<FMUImporter> importer);
    ~FMUInstance();

    // Disable copy, enable move
    FMUInstance(const FMUInstance&) = delete;
    FMUInstance& operator=(const FMUInstance&) = delete;
    FMUInstance(FMUInstance&&) noexcept;
    FMUInstance& operator=(FMUInstance&&) noexcept;

    // Lifecycle
    bool instantiate(const std::string& instanceName, fmi2Type type,
                     const fmi2CallbackFunctions* callbacks);
    bool setupExperiment(double startTime, double stopTime, double tolerance);
    bool enterInitializationMode();
    bool exitInitializationMode();
    bool terminate();
    bool reset();

    // Stepping
    StepResult doStep(double currentCommunicationPoint, double stepSize);

    // Variable access
    bool getReal(uint32_t vr, double& value);
    bool getReal(const std::vector<fmi2ValueReference>& vrs, std::vector<double>& values);
    bool setReal(uint32_t vr, double value);
    bool setReal(const std::vector<fmi2ValueReference>& vrs, const std::vector<double>& values);
    bool getInteger(uint32_t vr, int32_t& value);
    bool setInteger(uint32_t vr, int32_t value);
    bool getBoolean(uint32_t vr, bool& value);
    bool setBoolean(uint32_t vr, bool value);
    bool getString(uint32_t vr, std::string& value);
    bool setString(uint32_t vr, const std::string& value);

    // Batch operations
    bool setInitialValues(const std::unordered_map<std::string, double>& values);
    bool getVariableValues(std::unordered_map<std::string, double>& values);

    // Status
    FMUInstanceState state() const noexcept;
    const std::string& instanceName() const noexcept;
    uint32_t index() const noexcept;
    const FMIModelDescription* modelDescription() const noexcept;
    FMI2Functions* functions() noexcept;
    const std::string& lastError() const noexcept;
    double currentTime() const noexcept;

    // Variable resolution
    uint32_t getVRByName(const std::string& name) const;
    bool hasVariable(const std::string& name) const;
    const FMIVariable* getVariable(const std::string& name) const;

    // fmi2GetFMUState support
    bool canGetAndSetFMUState() const;
    bool getFMUState(fmi2FMUstate& state);
    bool setFMUState(fmi2FMUstate state);
    bool freeFMUState(fmi2FMUstate& state);
    bool serializeFMUState(fmi2FMUstate state, std::vector<fmi2Byte>& serialized);
    bool deserializeFMUState(const std::vector<fmi2Byte>& serialized, fmi2FMUstate& state);

private:
    uint32_t m_index;
    std::shared_ptr<FMUImporter> m_importer;
    std::string m_instanceName;
    FMUInstanceState m_state = FMUInstanceState::Created;
    fmi2Component m_component = nullptr;
    double m_currentTime = 0.0;
    std::string m_lastError;
    std::unordered_map<std::string, uint32_t> m_nameToVR;

    void buildVariableMap();
    bool checkStatus(fmi2Status status, const std::string& operation);
};

// ============================================================================
// FMI Co-Simulation Master
// ============================================================================
class FMICoSimulationMaster {
public:
    FMICoSimulationMaster();
    ~FMICoSimulationMaster();

    // Disable copy, enable move
    FMICoSimulationMaster(const FMICoSimulationMaster&) = delete;
    FMICoSimulationMaster& operator=(const FMICoSimulationMaster&) = delete;
    FMICoSimulationMaster(FMICoSimulationMaster&&) noexcept;
    FMICoSimulationMaster& operator=(FMICoSimulationMaster&&) noexcept;

    // ------------------------------------------------------------------------
    // FMU Management
    // ------------------------------------------------------------------------
    uint32_t addFMU(const std::string& fmuPath);
    uint32_t addFMU(const std::string& fmuPath, const std::string& instanceName);
    bool removeFMU(uint32_t fmuIndex);
    size_t numFMUs() const noexcept;
    FMUInstance* getFMU(uint32_t fmuIndex);
    const FMUInstance* getFMU(uint32_t fmuIndex) const;

    // ------------------------------------------------------------------------
    // Connections
    // ------------------------------------------------------------------------
    bool addConnection(uint32_t sourceFMU, const std::string& sourceVar,
                       uint32_t targetFMU, const std::string& targetVar,
                       double scalingFactor = 1.0, double offset = 0.0);
    bool addConnectionVR(uint32_t sourceFMU, uint32_t sourceVR,
                         uint32_t targetFMU, uint32_t targetVR,
                         double scalingFactor = 1.0, double offset = 0.0);
    bool removeConnection(uint32_t connIndex);
    void clearConnections();
    const std::vector<FMUConnection>& connections() const noexcept;

    // ------------------------------------------------------------------------
    // Initialization
    // ------------------------------------------------------------------------
    bool initialize(double startTime, double stopTime, double tolerance);
    bool initialize(const SimulationConfig& config);

    // ------------------------------------------------------------------------
    // Stepping
    // ------------------------------------------------------------------------
    StepResult doStep(double stepSize);
    bool doStepAll(double stepSize);

    // ------------------------------------------------------------------------
    // Variable Access (post-initialization)
    // ------------------------------------------------------------------------
    double getVariable(uint32_t fmuIndex, const std::string& varName);
    bool setVariable(uint32_t fmuIndex, const std::string& varName, double value);
    bool getVariables(uint32_t fmuIndex, std::unordered_map<std::string, double>& values);
    bool setVariables(uint32_t fmuIndex, const std::unordered_map<std::string, double>& values);

    // ------------------------------------------------------------------------
    // Connection Propagation
    // ------------------------------------------------------------------------
    bool propagateConnections();
    bool propagateConnections(uint32_t sourceFMU);

    // ------------------------------------------------------------------------
    // Full Simulation
    // ------------------------------------------------------------------------
    bool runSimulation();

    // ------------------------------------------------------------------------
    // Termination
    // ------------------------------------------------------------------------
    void terminate();

    // ------------------------------------------------------------------------
    // Status & Statistics
    // ------------------------------------------------------------------------
    const SimulationConfig& config() const noexcept;
    const SimulationStats& stats() const noexcept;
    double currentTime() const noexcept;
    bool isRunning() const noexcept;
    bool isInitialized() const noexcept;
    const std::string& lastError() const noexcept;

    // ------------------------------------------------------------------------
    // Real-time Synchronization
    // ------------------------------------------------------------------------
    void setRealTimeSync(bool enable, double realTimeFactor = 1.0);
    bool isRealTimeSyncEnabled() const noexcept;

    // ------------------------------------------------------------------------
    // Event Handling (optional)
    // ------------------------------------------------------------------------
    void setEventQueue(std::shared_ptr<EventQueue> queue);
    bool processEvents();

private:
    std::vector<std::shared_ptr<FMUInstance>> m_fmus;
    std::vector<std::shared_ptr<FMUImporter>> m_importers;
    std::vector<FMUConnection> m_connections;
    SimulationConfig m_config;
    SimulationStats m_stats;
    double m_currentTime = 0.0;
    bool m_initialized = false;
    bool m_running = false;
    bool m_terminated = false;
    std::string m_lastError;
    std::shared_ptr<EventQueue> m_eventQueue;

    // Real-time sync
    bool m_realTimeSync = false;
    double m_realTimeFactor = 1.0;
    std::chrono::steady_clock::time_point m_wallClockStart;

    // Internal
    bool initializeAllFMUs();
    bool doStepSingle(uint32_t fmuIndex, double currentTime, double stepSize);
    bool doStepParallel(double currentTime, double stepSize);
    void syncRealTime();
    void log(const std::string& message);
};

} // namespace powsys365::simulation::fmi
