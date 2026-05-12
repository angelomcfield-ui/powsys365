#pragma once

#include <vector>
#include <functional>
#include <memory>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <map>

namespace powsys365 {

// ---------------------------------------------------------------------------
// Integration method enumeration
// ---------------------------------------------------------------------------
enum class IntegrationMethod {
    EULER,
    RK4,           // Runge-Kutta 4th order
    RK45,          // Runge-Kutta-Fehlberg adaptive
    TRAPEZOIDAL,   // Implicit trapezoidal
    BACKWARD_EULER // Implicit backward Euler
};

std::string integrationMethodToString(IntegrationMethod method);

// ---------------------------------------------------------------------------
// State type classification for power systems
// ---------------------------------------------------------------------------
enum class StateType {
    CONTINUOUS,    // Differential state (e.g., generator angle, speed)
    DISCRETE,      // Discrete state (e.g., tap positions, breaker status)
    ALGEBRAIC      // Algebraic variable (e.g., voltage, current)
};

// ---------------------------------------------------------------------------
// State variable descriptor
// ---------------------------------------------------------------------------
struct StateVariable {
    std::string name;
    std::string unit;
    StateType type;
    double value = 0.0;
    double derivative = 0.0;
    double lowerBound = -std::numeric_limits<double>::infinity();
    double upperBound = std::numeric_limits<double>::infinity();
    bool fixed = false;     // If true, value does not change during integration
};

// ---------------------------------------------------------------------------
// ODE function type: computes derivatives given current state and time
// ---------------------------------------------------------------------------
using OdeFunction = std::function<void(double t, const std::vector<double>& y,
                                         std::vector<double>& dydt)>;

// ---------------------------------------------------------------------------
// DAE function types
// ---------------------------------------------------------------------------
using DaeResidualFunction = std::function<void(double t, const std::vector<double>& y,
                                                  const std::vector<double>& ydot,
                                                  std::vector<double>& res)>;
using AlgebraicConstraintFunction = std::function<void(double t, const std::vector<double>& y,
                                                         std::vector<double>& g)>;

// ---------------------------------------------------------------------------
// Integration step result
// ---------------------------------------------------------------------------
struct IntegrationResult {
    double t = 0.0;
    std::vector<double> y;
    std::vector<double> dydt;
    double stepSize = 0.0;
    bool accepted = false;
    int iterations = 0;
    double error = 0.0;
    std::string errorMessage;
};

// ---------------------------------------------------------------------------
// Solver statistics
// ---------------------------------------------------------------------------
struct IntegratorStats {
    uint64_t totalSteps = 0;
    uint64_t acceptedSteps = 0;
    uint64_t rejectedSteps = 0;
    uint64_t functionEvaluations = 0;
    uint64_t jacobianEvaluations = 0;
    uint64_t linearSolves = 0;
    double totalTimeSec = 0.0;
    double minStepSize = std::numeric_limits<double>::max();
    double maxStepSize = 0.0;
    double averageStepSize = 0.0;
    double currentStepSize = 0.0;
    int newtonIterations = 0;
};

// ---------------------------------------------------------------------------
// Solver configuration
// ---------------------------------------------------------------------------
struct IntegratorConfig {
    IntegrationMethod method = IntegrationMethod::RK4;
    double initialStepSize = 0.001;     // initial step [s]
    double minStepSize = 1e-9;          // minimum step [s]
    double maxStepSize = 0.01;          // maximum step [s]
    double absTolerance = 1e-6;         // absolute error tolerance
    double relTolerance = 1e-4;         // relative error tolerance
    int maxIterations = 10;             // Newton max iterations (implicit methods)
    int maxSteps = 1000000;             // maximum number of steps
    bool adaptiveStep = true;           // enable adaptive stepping
    double safetyFactor = 0.9;          // step size safety factor
    double maxErrorGrowth = 5.0;        // maximum error growth for step rejection
    bool denseOutput = false;           // enable dense output (interpolation)
    double denseOutputInterval = 0.001; // dense output time interval
};

// ---------------------------------------------------------------------------
// Dense output interpolation (for fixed-step output)
// ---------------------------------------------------------------------------
struct DenseOutputRecord {
    double t = 0.0;
    std::vector<double> y;
    std::vector<double> dydt;
};

// ---------------------------------------------------------------------------
// Main Integrator class
// ---------------------------------------------------------------------------
class Integrator {
public:
    Integrator();
    ~Integrator();

    // Configuration
    void setConfig(const IntegratorConfig& config);
    IntegratorConfig getConfig() const;

    // Initialize with initial conditions
    void initialize(double t0, const std::vector<double>& y0);
    void initialize(double t0, const std::vector<StateVariable>& states);

    // Set the ODE function (for pure ODE problems)
    void setOdeFunction(OdeFunction func);

    // Set the DAE functions (for DAE problems)
    void setDaeFunctions(DaeResidualFunction resFunc, AlgebraicConstraintFunction algFunc);

    // Single integration step
    IntegrationResult step();
    IntegrationResult step(double dt);

    // Integrate from t0 to tf
    std::vector<IntegrationResult> integrate(double tf);

    // Integrate with event detection (stop at events)
    std::vector<IntegrationResult> integrate(double tf,
                                               std::function<bool(double t, const std::vector<double>& y)> eventFunc,
                                               std::vector<double>& eventTimes);

    // Fixed-step integration (for co-simulation synchronization)
    IntegrationResult stepFixed(double dt);

    // Current state access
    double getCurrentTime() const;
    std::vector<double> getCurrentState() const;
    std::vector<double> getCurrentDerivative() const;

    // State variable management
    size_t getStateCount() const;
    void setState(size_t index, double value);
    double getState(size_t index) const;
    void setStateName(size_t index, const std::string& name);
    std::string getStateName(size_t index) const;

    // Dense output
    std::vector<DenseOutputRecord> getDenseOutput() const;
    void clearDenseOutput();

    // Statistics
    IntegratorStats getStats() const;
    void resetStats();

    // Solver methods (exposed for testing)
    static void eulerStep(double t, const std::vector<double>& y, double h,
                           const OdeFunction& f, std::vector<double>& yNext);
    static void rk4Step(double t, const std::vector<double>& y, double h,
                         const OdeFunction& f, std::vector<double>& yNext);
    static IntegrationResult rk45Step(double t, const std::vector<double>& y, double h,
                                        const OdeFunction& f, double atol, double rtol);
    static void trapezoidalStep(double t, const std::vector<double>& y, double h,
                                  const OdeFunction& f, int maxIter, double tol,
                                  std::vector<double>& yNext, int& iterations);
    static void backwardEulerStep(double t, const std::vector<double>& y, double h,
                                    const OdeFunction& f, int maxIter, double tol,
                                    std::vector<double>& yNext, int& iterations);

private:
    void evaluateOde(double t, const std::vector<double>& y, std::vector<double>& dydt);
    double computeError(const std::vector<double>& y, const std::vector<double>& yHigh,
                         const std::vector<double>& yLow) const;
    double adjustStepSize(double h, double error) const;
    void recordDenseOutput();
    void solveLinearSystem(std::vector<std::vector<double>>& A, std::vector<double>& b,
                            std::vector<double>& x);
    void computeJacobian(double t, const std::vector<double>& y,
                          std::vector<std::vector<double>>& J);

    IntegratorConfig m_config;
    IntegratorStats m_stats;

    // Current state
    double m_t = 0.0;
    std::vector<double> m_y;
    std::vector<double> m_dydt;
    std::vector<StateVariable> m_stateVars;
    std::vector<std::string> m_stateNames;

    // Functions
    OdeFunction m_odeFunc;
    DaeResidualFunction m_daeResFunc;
    AlgebraicConstraintFunction m_algFunc;

    // Dense output
    std::vector<DenseOutputRecord> m_denseOutput;
    double m_nextDenseOutputTime = 0.0;

    // Workspace
    std::vector<double> m_work1;
    std::vector<double> m_work2;
    std::vector<double> m_work3;
    std::vector<double> m_work4;
    std::vector<double> m_work5;
    std::vector<double> m_work6;
};

} // namespace powsys365
