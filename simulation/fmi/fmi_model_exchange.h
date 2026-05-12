#pragma once

#include "fmi_importer.h"

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>

namespace powsys365::simulation::fmi {

// ============================================================================
// Integrator Type
// ============================================================================
enum class IntegratorType {
    CVODE,    // SUNDIALS CVODE (BDF/Adams)
    RK4,      // 4th order Runge-Kutta
    Euler,    // Forward Euler
    RK23,     // Adaptive Runge-Kutta 2/3
    RK45,     // Adaptive Dormand-Prince 4/5
    ImplicitEuler  // Backward Euler (fixed point iteration)
};

// ============================================================================
// Integrator Configuration
// ============================================================================
struct IntegratorConfig {
    IntegratorType type = IntegratorType::CVODE;
    double relTolerance = 1e-6;
    double absTolerance = 1e-8;
    double maxStep = 0.0;       // 0 = auto
    double minStep = 1e-12;
    double initialStep = 0.0;   // 0 = auto
    int maxOrder = 5;           // For CVODE: up to 5 for Adams, 5 for BDF
    bool useBDF = true;         // CVODE: true=BDF, false=Adams
    int maxSteps = 50000;       // Maximum internal steps
    int maxNonlinIters = 3;     // Max nonlinear iterations
    int maxConvFails = 10;      // Max convergence failures
    bool staldet = true;        // Stiffness detection for Adams
};

// ============================================================================
// Event Info (FMI 2.0 Model Exchange event handling)
// ============================================================================
struct FMIModelExchangeEventInfo {
    bool newDiscreteStatesNeeded = false;
    bool terminateSimulation = false;
    bool nominalsOfContinuousStatesChanged = false;
    bool valuesOfContinuousStatesChanged = false;
    bool nextEventTimeDefined = false;
    double nextEventTime = 0.0;
};

// ============================================================================
// ODE Function Type
// ============================================================================
using ODEFunction = std::function<bool(double t, const std::vector<double>& y, std::vector<double>& ydot)>;
using EventFunction = std::function<bool(double t, const std::vector<double>& y, std::vector<double>& g)>;
using EventCallback = std::function<void(double t, const std::vector<double>& y, int eventIndex)>;

// ============================================================================
// State Vector
// ============================================================================
struct StateVector {
    double time = 0.0;
    std::vector<double> values;
    std::vector<std::string> names;
    std::vector<uint32_t> valueReferences;

    size_t size() const noexcept { return values.size(); }
    bool empty() const noexcept { return values.empty(); }

    double operator[](size_t i) const { return values[i]; }
    double& operator[](size_t i) { return values[i]; }
    double operator[](const std::string& name) const;
    double& operator[](const std::string& name);
};

// ============================================================================
// Step Statistics
// ============================================================================
struct MEStepStats {
    uint64_t totalSteps = 0;
    uint64_t acceptedSteps = 0;
    uint64_t rejectedSteps = 0;
    uint64_t functionEvals = 0;
    uint64_t jacobianEvals = 0;
    uint64_t errorTestFails = 0;
    uint64_t events = 0;
    double currentTime = 0.0;
    double currentStepSize = 0.0;
    double lastSuccessfulStep = 0.0;
};

// ============================================================================
// CVODE Integrator (SUNDIALS wrapper)
// ============================================================================
class CVODEIntegrator {
public:
    CVODEIntegrator();
    ~CVODEIntegrator();

    // Disable copy, enable move
    CVODEIntegrator(const CVODEIntegrator&) = delete;
    CVODEIntegrator& operator=(const CVODEIntegrator&) = delete;
    CVODEIntegrator(CVODEIntegrator&&) noexcept;
    CVODEIntegrator& operator=(CVODEIntegrator&&) noexcept;

    bool initialize(const IntegratorConfig& config,
                    const std::vector<double>& y0,
                    ODEFunction odeFunc);
    bool reinitialize(double t0, const std::vector<double>& y0);
    bool step(double tEnd);
    bool stepFixed(double dt, std::vector<double>& yOut);
    bool interpolate(double t, std::vector<double>& yOut);

    const std::vector<double>& currentY() const noexcept;
    double currentTime() const noexcept;
    double lastStepSize() const noexcept;
    const MEStepStats& stats() const noexcept;
    void setTolerances(double relTol, double absTol);
    void setMaxStep(double maxStep);
    void setUserData(void* userData);
    bool isInitialized() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

// ============================================================================
// Runge-Kutta 4 (Fixed Step)
// ============================================================================
class RK4Integrator {
public:
    RK4Integrator();
    ~RK4Integrator() = default;

    bool initialize(const std::vector<double>& y0, ODEFunction odeFunc);
    bool step(double dt);
    const std::vector<double>& currentY() const noexcept;
    double currentTime() const noexcept;
    const MEStepStats& stats() const noexcept;

private:
    std::vector<double> m_y;
    std::vector<double> m_k1, m_k2, m_k3, m_k4, m_tmp;
    ODEFunction m_odeFunc;
    double m_time = 0.0;
    MEStepStats m_stats;
};

// ============================================================================
// Forward Euler (Fixed Step)
// ============================================================================
class EulerIntegrator {
public:
    EulerIntegrator();
    ~EulerIntegrator() = default;

    bool initialize(const std::vector<double>& y0, ODEFunction odeFunc);
    bool step(double dt);
    const std::vector<double>& currentY() const noexcept;
    double currentTime() const noexcept;
    const MEStepStats& stats() const noexcept;

private:
    std::vector<double> m_y;
    std::vector<double> m_ydot;
    ODEFunction m_odeFunc;
    double m_time = 0.0;
    MEStepStats m_stats;
};

// ============================================================================
// Adaptive RK23 (Bogacki-Shampine)
// ============================================================================
class RK23Integrator {
public:
    RK23Integrator();
    ~RK23Integrator() = default;

    bool initialize(const std::vector<double>& y0, ODEFunction odeFunc,
                    double relTol = 1e-6, double absTol = 1e-8);
    bool step(double tEnd);
    const std::vector<double>& currentY() const noexcept;
    double currentTime() const noexcept;
    const MEStepStats& stats() const noexcept;

private:
    std::vector<double> m_y;
    std::vector<double> m_yerr;
    std::vector<double> m_k1, m_k2, m_k3, m_k4;
    std::vector<double> m_tmp;
    ODEFunction m_odeFunc;
    double m_time = 0.0;
    double m_dt = 1e-3;
    double m_relTol = 1e-6;
    double m_absTol = 1e-8;
    MEStepStats m_stats;

    double computeError(const std::vector<double>& err, const std::vector<double>& y);
};

// ============================================================================
// Adaptive RK45 (Dormand-Prince)
// ============================================================================
class RK45Integrator {
public:
    RK45Integrator();
    ~RK45Integrator() = default;

    bool initialize(const std::vector<double>& y0, ODEFunction odeFunc,
                    double relTol = 1e-6, double absTol = 1e-8);
    bool step(double tEnd);
    const std::vector<double>& currentY() const noexcept;
    double currentTime() const noexcept;
    const MEStepStats& stats() const noexcept;

private:
    std::vector<double> m_y;
    std::vector<double> m_yerr;
    std::vector<double> m_k[7];
    std::vector<double> m_tmp;
    ODEFunction m_odeFunc;
    double m_time = 0.0;
    double m_dt = 1e-3;
    double m_relTol = 1e-6;
    double m_absTol = 1e-8;
    MEStepStats m_stats;

    double computeError(const std::vector<double>& err, const std::vector<double>& y);
};

// ============================================================================
// FMI Model Exchange
// ============================================================================
class FMIModelExchange {
public:
    FMIModelExchange();
    ~FMIModelExchange();

    // Disable copy, enable move
    FMIModelExchange(const FMIModelExchange&) = delete;
    FMIModelExchange& operator=(const FMIModelExchange&) = delete;
    FMIModelExchange(FMIModelExchange&&) noexcept;
    FMIModelExchange& operator=(FMIModelExchange&&) noexcept;

    // ------------------------------------------------------------------------
    // FMU Setup
    // ------------------------------------------------------------------------
    bool loadFMU(const std::string& fmuPath);
    bool instantiate(const std::string& instanceName);

    // ------------------------------------------------------------------------
    // Integrator Configuration
    // ------------------------------------------------------------------------
    void setIntegrator(IntegratorType type);
    void setIntegratorConfig(const IntegratorConfig& config);
    const IntegratorConfig& integratorConfig() const noexcept;

    // ------------------------------------------------------------------------
    // Initialization
    // ------------------------------------------------------------------------
    bool initialize(double startTime, double stopTime, double tolerance);
    bool initialize(const std::unordered_map<std::string, double>& initialValues);
    bool setupExperiment(double startTime, double stopTime, double tolerance);

    // ------------------------------------------------------------------------
    // Derivatives (ODE right-hand side)
    // ------------------------------------------------------------------------
    std::vector<double> getDerivatives();
    bool getDerivatives(std::vector<double>& ydot);

    // ------------------------------------------------------------------------
    // State Access
    // ------------------------------------------------------------------------
    std::vector<double> getState();
    bool setState(const std::vector<double>& state);
    bool getNominalsOfContinuousStates(std::vector<double>& nominals);
    StateVector getStateVector();

    // ------------------------------------------------------------------------
    // Stepping
    // ------------------------------------------------------------------------
    bool step(double dt);
    bool stepTo(double targetTime);

    // ------------------------------------------------------------------------
    // Event Handling
    // ------------------------------------------------------------------------
    bool handleEvents();
    bool enterEventMode();
    bool enterContinuousTimeMode();
    bool newDiscreteStates();
    bool completedIntegratorStep();
    bool getEventIndicators(std::vector<double>& indicators);
    void setEventCallback(EventCallback callback);

    // ------------------------------------------------------------------------
    // Variable Access
    // ------------------------------------------------------------------------
    bool getReal(uint32_t vr, double& value);
    bool setReal(uint32_t vr, double value);
    bool getReal(const std::vector<uint32_t>& vrs, std::vector<double>& values);
    bool setReal(const std::vector<uint32_t>& vrs, const std::vector<double>& values);
    double getVariable(const std::string& name);
    bool setVariable(const std::string& name, double value);

    // ------------------------------------------------------------------------
    // Time
    // ------------------------------------------------------------------------
    bool setTime(double time);
    double getTime() const noexcept;

    // ------------------------------------------------------------------------
    // Termination
    // ------------------------------------------------------------------------
    bool terminate();
    bool reset();

    // ------------------------------------------------------------------------
    // Status
    // ------------------------------------------------------------------------
    bool isInitialized() const noexcept;
    bool isInEventMode() const noexcept;
    const FMIModelDescription* modelDescription() const noexcept;
    const MEStepStats& stats() const noexcept;
    const std::string& lastError() const noexcept;
    IntegratorType currentIntegrator() const noexcept;
    size_t numContinuousStates() const;

    // ------------------------------------------------------------------------
    // Direct ODE interface (for custom integration)
    // ------------------------------------------------------------------------
    ODEFunction getODEFunction();
    void setODEFunction(ODEFunction func);
    void setEventFunction(EventFunction func);

private:
    std::unique_ptr<FMUImporter> m_importer;
    std::unique_ptr<CVODEIntegrator> m_cvodeIntegrator;
    std::unique_ptr<RK4Integrator> m_rk4Integrator;
    std::unique_ptr<EulerIntegrator> m_eulerIntegrator;
    std::unique_ptr<RK23Integrator> m_rk23Integrator;
    std::unique_ptr<RK45Integrator> m_rk45Integrator;

    IntegratorType m_integratorType = IntegratorType::CVODE;
    IntegratorConfig m_integratorConfig;
    MEStepStats m_stats;

    fmi2Component m_component = nullptr;
    double m_time = 0.0;
    double m_startTime = 0.0;
    double m_stopTime = 0.0;
    double m_tolerance = 1e-6;
    bool m_initialized = false;
    bool m_inEventMode = false;
    std::string m_lastError;
    std::string m_instanceName;

    std::vector<double> m_currentState;
    std::vector<double> m_nominals;
    std::vector<double> m_eventIndicators;
    std::vector<double> m_prevEventIndicators;
    std::vector<uint32_t> m_stateVRs;

    EventCallback m_eventCallback;
    ODEFunction m_customODEFunc;
    EventFunction m_customEventFunc;

    // Internal methods
    bool updateStateFromFMU();
    bool pushStateToFMU();
    bool buildStateVRs();
    bool checkEventIndicators();
    bool callODEFunction(double t, const std::vector<double>& y, std::vector<double>& ydot);
    bool callEventFunction(double t, const std::vector<double>& y, std::vector<double>& g);
    fmi2CallbackFunctions getCallbacks();
};

// ============================================================================
// Standalone ODE Integrators (non-FMI, for testing and external use)
// ============================================================================
class ODEIntegrator {
public:
    static std::vector<double> solveRK4(ODEFunction f,
                                         const std::vector<double>& y0,
                                         double t0, double tEnd,
                                         int numSteps);

    static std::vector<double> solveEuler(ODEFunction f,
                                           const std::vector<double>& y0,
                                           double t0, double tEnd,
                                           int numSteps);

    static std::vector<std::pair<double, std::vector<double>>> solveAdaptiveRK45(
        ODEFunction f,
        const std::vector<double>& y0,
        double t0, double tEnd,
        double relTol = 1e-6,
        double absTol = 1e-8);
};

} // namespace powsys365::simulation::fmi
