#include "powsy365/simulation/integrator.h"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <cstring>
#include <chrono>

namespace powsys365 {

// ============================================================================
// String helpers
// ============================================================================
std::string integrationMethodToString(IntegrationMethod method) {
    switch (method) {
        case IntegrationMethod::EULER: return "Euler";
        case IntegrationMethod::RK4: return "RK4";
        case IntegrationMethod::RK45: return "RK45";
        case IntegrationMethod::TRAPEZOIDAL: return "Trapezoidal";
        case IntegrationMethod::BACKWARD_EULER: return "BackwardEuler";
        default: return "Unknown";
    }
}

// ============================================================================
// Integrator
// ============================================================================
Integrator::Integrator() = default;
Integrator::~Integrator() = default;

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
void Integrator::setConfig(const IntegratorConfig& config) {
    m_config = config;
}

IntegratorConfig Integrator::getConfig() const {
    return m_config;
}

// ---------------------------------------------------------------------------
// Initialize with initial conditions
// ---------------------------------------------------------------------------
void Integrator::initialize(double t0, const std::vector<double>& y0) {
    m_t = t0;
    m_y = y0;
    m_dydt.resize(y0.size(), 0.0);
    m_stateNames.resize(y0.size(), "");

    m_work1.resize(y0.size());
    m_work2.resize(y0.size());
    m_work3.resize(y0.size());
    m_work4.resize(y0.size());
    m_work5.resize(y0.size());
    m_work6.resize(y0.size());

    m_nextDenseOutputTime = t0 + m_config.denseOutputInterval;
}

void Integrator::initialize(double t0, const std::vector<StateVariable>& states) {
    m_stateVars = states;
    m_y.reserve(states.size());
    for (const auto& sv : states) {
        m_y.push_back(sv.value);
    }
    m_t = t0;
    m_dydt.resize(states.size(), 0.0);
    m_stateNames.reserve(states.size());
    for (const auto& sv : states) {
        m_stateNames.push_back(sv.name);
    }

    m_work1.resize(states.size());
    m_work2.resize(states.size());
    m_work3.resize(states.size());
    m_work4.resize(states.size());
    m_work5.resize(states.size());
    m_work6.resize(states.size());

    m_nextDenseOutputTime = t0 + m_config.denseOutputInterval;
}

// ---------------------------------------------------------------------------
// Set ODE function
// ---------------------------------------------------------------------------
void Integrator::setOdeFunction(OdeFunction func) {
    m_odeFunc = func;
}

void Integrator::setDaeFunctions(DaeResidualFunction resFunc, AlgebraicConstraintFunction algFunc) {
    m_daeResFunc = resFunc;
    m_algFunc = algFunc;
}

// ---------------------------------------------------------------------------
// Single integration step
// ---------------------------------------------------------------------------
IntegrationResult Integrator::step() {
    double h = m_config.initialStepSize;
    return step(h);
}

IntegrationResult Integrator::step(double dt) {
    IntegrationResult result;
    result.t = m_t;

    if (m_odeFunc == nullptr) {
        result.accepted = false;
        result.errorMessage = "ODE function not set";
        return result;
    }

    auto startTime = std::chrono::steady_clock::now();

    switch (m_config.method) {
        case IntegrationMethod::EULER: {
            eulerStep(m_t, m_y, dt, m_odeFunc, m_work1);
            m_y = m_work1;
            result.accepted = true;
            result.error = 0.0;
            break;
        }
        case IntegrationMethod::RK4: {
            rk4Step(m_t, m_y, dt, m_odeFunc, m_work1);
            m_y = m_work1;
            result.accepted = true;
            result.error = 0.0;
            break;
        }
        case IntegrationMethod::RK45: {
            result = rk45Step(m_t, m_y, dt, m_odeFunc, m_config.absTolerance, m_config.relTolerance);
            if (result.accepted) {
                m_y = result.y;
            }
            break;
        }
        case IntegrationMethod::TRAPEZOIDAL: {
            int iterations = 0;
            trapezoidalStep(m_t, m_y, dt, m_odeFunc, m_config.maxIterations,
                            m_config.absTolerance, m_work1, iterations);
            m_y = m_work1;
            result.accepted = true;
            result.iterations = iterations;
            result.error = m_config.absTolerance;
            break;
        }
        case IntegrationMethod::BACKWARD_EULER: {
            int iterations = 0;
            backwardEulerStep(m_t, m_y, dt, m_odeFunc, m_config.maxIterations,
                               m_config.absTolerance, m_work1, iterations);
            m_y = m_work1;
            result.accepted = true;
            result.iterations = iterations;
            break;
        }
    }

    if (result.accepted) {
        m_t += dt;
        result.t = m_t;
        result.y = m_y;
        evaluateOde(m_t, m_y, m_dydt);
        result.dydt = m_dydt;
        result.stepSize = dt;

        // Update stats
        m_stats.totalSteps++;
        m_stats.acceptedSteps++;
        m_stats.currentStepSize = dt;
        m_stats.minStepSize = std::min(m_stats.minStepSize, dt);
        m_stats.maxStepSize = std::max(m_stats.maxStepSize, dt);
    } else {
        m_stats.rejectedSteps++;
    }

    // Record dense output
    if (m_config.denseOutput && m_t >= m_nextDenseOutputTime) {
        recordDenseOutput();
        m_nextDenseOutputTime += m_config.denseOutputInterval;
    }

    auto endTime = std::chrono::steady_clock::now();
    double stepMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    m_stats.totalTimeSec += stepMs / 1000.0;
    m_stats.functionEvaluations++;

    return result;
}

// ---------------------------------------------------------------------------
// Fixed-step integration
// ---------------------------------------------------------------------------
IntegrationResult Integrator::stepFixed(double dt) {
    IntegrationResult result;
    result.t = m_t;

    if (m_odeFunc == nullptr) {
        result.accepted = false;
        result.errorMessage = "ODE function not set";
        return result;
    }

    switch (m_config.method) {
        case IntegrationMethod::EULER:
            eulerStep(m_t, m_y, dt, m_odeFunc, m_work1);
            m_y = m_work1;
            break;
        case IntegrationMethod::RK4:
            rk4Step(m_t, m_y, dt, m_odeFunc, m_work1);
            m_y = m_work1;
            break;
        default:
            // Default to RK4 for fixed-step
            rk4Step(m_t, m_y, dt, m_odeFunc, m_work1);
            m_y = m_work1;
            break;
    }

    m_t += dt;
    result.t = m_t;
    result.y = m_y;
    result.stepSize = dt;
    result.accepted = true;

    evaluateOde(m_t, m_y, m_dydt);
    result.dydt = m_dydt;

    m_stats.totalSteps++;
    m_stats.acceptedSteps++;
    m_stats.functionEvaluations++;

    // Dense output
    if (m_config.denseOutput && m_t >= m_nextDenseOutputTime) {
        recordDenseOutput();
        m_nextDenseOutputTime += m_config.denseOutputInterval;
    }

    return result;
}

// ---------------------------------------------------------------------------
// Integrate from t0 to tf
// ---------------------------------------------------------------------------
std::vector<IntegrationResult> Integrator::integrate(double tf) {
    std::vector<IntegrationResult> results;

    if (m_t >= tf) return results;

    double h = m_config.initialStepSize;
    uint64_t stepCount = 0;

    while (m_t < tf && stepCount < m_config.maxSteps) {
        // Don't overshoot tf
        if (m_t + h > tf) {
            h = tf - m_t;
        }

        IntegrationResult result;

        if (m_config.adaptiveStep && m_config.method == IntegrationMethod::RK45) {
            result = rk45Step(m_t, m_y, h, m_odeFunc, m_config.absTolerance, m_config.relTolerance);

            if (result.accepted) {
                m_y = result.y;
                m_t += h;
                h = adjustStepSize(h, result.error);
                h = std::clamp(h, m_config.minStepSize, m_config.maxStepSize);
            } else {
                h = adjustStepSize(h, result.error);
                h = std::max(h, m_config.minStepSize);
                m_stats.rejectedSteps++;
                continue;
            }
        } else {
            result = step(h);
            if (!result.accepted) {
                h *= 0.5;
                if (h < m_config.minStepSize) break;
                continue;
            }
        }

        result.t = m_t;
        result.y = m_y;
        evaluateOde(m_t, m_y, m_dydt);
        result.dydt = m_dydt;
        result.stepSize = h;
        results.push_back(result);

        stepCount++;
    }

    // Update stats
    m_stats.totalSteps += stepCount;
    m_stats.acceptedSteps += stepCount;

    return results;
}

// ---------------------------------------------------------------------------
// Integrate with event detection
// ---------------------------------------------------------------------------
std::vector<IntegrationResult> Integrator::integrate(double tf,
                                                        std::function<bool(double, const std::vector<double>&)> eventFunc,
                                                        std::vector<double>& eventTimes) {
    std::vector<IntegrationResult> results;
    eventTimes.clear();

    if (m_t >= tf) return results;

    double h = m_config.initialStepSize;
    uint64_t stepCount = 0;

    while (m_t < tf && stepCount < m_config.maxSteps) {
        if (m_t + h > tf) h = tf - m_t;

        auto result = step(h);
        if (!result.accepted) {
            h *= 0.5;
            if (h < m_config.minStepSize) break;
            continue;
        }

        results.push_back(result);

        // Check for events
        if (eventFunc(m_t, m_y)) {
            eventTimes.push_back(m_t);
        }

        stepCount++;
    }

    return results;
}

// ---------------------------------------------------------------------------
// Current state access
// ---------------------------------------------------------------------------
double Integrator::getCurrentTime() const {
    return m_t;
}

std::vector<double> Integrator::getCurrentState() const {
    return m_y;
}

std::vector<double> Integrator::getCurrentDerivative() const {
    return m_dydt;
}

// ---------------------------------------------------------------------------
// State variable management
// ---------------------------------------------------------------------------
size_t Integrator::getStateCount() const {
    return m_y.size();
}

void Integrator::setState(size_t index, double value) {
    if (index < m_y.size()) {
        m_y[index] = value;
    }
}

double Integrator::getState(size_t index) const {
    if (index < m_y.size()) {
        return m_y[index];
    }
    return 0.0;
}

void Integrator::setStateName(size_t index, const std::string& name) {
    if (index < m_stateNames.size()) {
        m_stateNames[index] = name;
    }
}

std::string Integrator::getStateName(size_t index) const {
    if (index < m_stateNames.size()) {
        return m_stateNames[index];
    }
    return "";
}

// ---------------------------------------------------------------------------
// Dense output
// ---------------------------------------------------------------------------
std::vector<DenseOutputRecord> Integrator::getDenseOutput() const {
    return m_denseOutput;
}

void Integrator::clearDenseOutput() {
    m_denseOutput.clear();
}

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------
IntegratorStats Integrator::getStats() const {
    IntegratorStats stats = m_stats;
    if (stats.totalSteps > 0) {
        stats.averageStepSize = (stats.minStepSize == std::numeric_limits<double>::max()) ? 0.0 :
            stats.acceptedSteps > 0 ? m_t / stats.acceptedSteps : 0.0;
    }
    return stats;
}

void Integrator::resetStats() {
    m_stats = IntegratorStats{};
    m_stats.minStepSize = std::numeric_limits<double>::max();
}

// ---------------------------------------------------------------------------
// Static solver methods
// ---------------------------------------------------------------------------
void Integrator::eulerStep(double t, const std::vector<double>& y, double h,
                             const OdeFunction& f, std::vector<double>& yNext) {
    size_t n = y.size();
    std::vector<double> k1(n);
    f(t, y, k1);

    yNext.resize(n);
    for (size_t i = 0; i < n; ++i) {
        yNext[i] = y[i] + h * k1[i];
    }
}

void Integrator::rk4Step(double t, const std::vector<double>& y, double h,
                            const OdeFunction& f, std::vector<double>& yNext) {
    size_t n = y.size();
    std::vector<double> k1(n), k2(n), k3(n), k4(n), temp(n);

    // k1
    f(t, y, k1);

    // k2
    for (size_t i = 0; i < n; ++i) temp[i] = y[i] + 0.5 * h * k1[i];
    f(t + 0.5 * h, temp, k2);

    // k3
    for (size_t i = 0; i < n; ++i) temp[i] = y[i] + 0.5 * h * k2[i];
    f(t + 0.5 * h, temp, k3);

    // k4
    for (size_t i = 0; i < n; ++i) temp[i] = y[i] + h * k3[i];
    f(t + h, temp, k4);

    // Combine
    yNext.resize(n);
    for (size_t i = 0; i < n; ++i) {
        yNext[i] = y[i] + (h / 6.0) * (k1[i] + 2.0 * k2[i] + 2.0 * k3[i] + k4[i]);
    }
}

IntegrationResult Integrator::rk45Step(double t, const std::vector<double>& y, double h,
                                          const OdeFunction& f, double atol, double rtol) {
    IntegrationResult result;
    size_t n = y.size();

    // Butcher tableau for RK4(5) - Dormand-Prince coefficients
    const double a2 = 1.0 / 5.0;
    const double a3 = 3.0 / 10.0, a32 = 3.0 / 40.0, a33 = 9.0 / 40.0;
    const double a4 = 4.0 / 5.0, a42 = 44.0 / 45.0, a43 = -56.0 / 15.0, a44 = 32.0 / 9.0;
    const double a5 = 8.0 / 9.0;
    const double a52 = 19372.0 / 6561.0, a53 = -25360.0 / 2187.0;
    const double a54 = 64448.0 / 6561.0, a55 = -212.0 / 729.0;
    const double a6 = 1.0;
    const double a62 = 9017.0 / 3168.0, a63 = -355.0 / 33.0;
    const double a64 = 46732.0 / 5247.0, a65 = 49.0 / 176.0, a66 = -5103.0 / 18656.0;
    const double a7 = 1.0;
    const double a72 = 35.0 / 384.0, a73 = 0.0, a74 = 500.0 / 1113.0;
    const double a75 = 125.0 / 192.0, a76 = -2187.0 / 6784.0, a77 = 11.0 / 84.0;

    // 5th order coefficients
    const double b1 = 35.0 / 384.0, b2 = 0.0, b3 = 500.0 / 1113.0;
    const double b4 = 125.0 / 192.0, b5 = -2187.0 / 6784.0, b6 = 11.0 / 84.0, b7 = 0.0;

    // 4th order coefficients (for error estimation)
    const double c1 = 5179.0 / 57600.0, c2 = 0.0, c3 = 7571.0 / 16695.0;
    const double c4 = 393.0 / 640.0, c5 = -92097.0 / 339200.0;
    const double c6 = 187.0 / 2100.0, c7 = 1.0 / 40.0;

    std::vector<double> k1(n), k2(n), k3(n), k4(n), k5(n), k6(n), k7(n), temp(n);
    std::vector<double> y5(n), y4(n);

    f(t, y, k1);

    for (size_t i = 0; i < n; ++i) temp[i] = y[i] + h * a2 * k1[i];
    f(t + a2 * h, temp, k2);

    for (size_t i = 0; i < n; ++i) temp[i] = y[i] + h * (a32 * k1[i] + a33 * k2[i]);
    f(t + a3 * h, temp, k3);

    for (size_t i = 0; i < n; ++i) temp[i] = y[i] + h * (a42 * k1[i] + a43 * k2[i] + a44 * k3[i]);
    f(t + a4 * h, temp, k4);

    for (size_t i = 0; i < n; ++i) {
        temp[i] = y[i] + h * (a52 * k1[i] + a53 * k2[i] + a54 * k3[i] + a55 * k4[i]);
    }
    f(t + a5 * h, temp, k5);

    for (size_t i = 0; i < n; ++i) {
        temp[i] = y[i] + h * (a62 * k1[i] + a63 * k2[i] + a64 * k3[i] + a65 * k4[i] + a66 * k5[i]);
    }
    f(t + a6 * h, temp, k6);

    for (size_t i = 0; i < n; ++i) {
        temp[i] = y[i] + h * (a72 * k1[i] + a73 * k2[i] + a74 * k3[i] + a75 * k4[i] + a76 * k5[i] + a77 * k6[i]);
    }
    f(t + a7 * h, temp, k7);

    // 5th order solution
    for (size_t i = 0; i < n; ++i) {
        y5[i] = y[i] + h * (b1 * k1[i] + b2 * k2[i] + b3 * k3[i] + b4 * k4[i] + b5 * k5[i] + b6 * k6[i] + b7 * k7[i]);
    }

    // 4th order solution for error estimation
    for (size_t i = 0; i < n; ++i) {
        y4[i] = y[i] + h * (c1 * k1[i] + c2 * k2[i] + c3 * k3[i] + c4 * k4[i] + c5 * k5[i] + c6 * k6[i] + c7 * k7[i]);
    }

    // Compute error
    double error = computeError(y5, y5, y4); // y5 as scale reference
    error = std::max(error, 1e-15);

    result.y = y5;
    result.stepSize = h;
    result.error = error;

    // Check acceptance
    double tol = atol + rtol * std::max(
        *std::max_element(y.begin(), y.end(), [](double a, double b) { return std::abs(a) < std::abs(b); }),
        1e-10);

    result.accepted = error <= tol;

    return result;
}

void Integrator::trapezoidalStep(double t, const std::vector<double>& y, double h,
                                    const OdeFunction& f, int maxIter, double tol,
                                    std::vector<double>& yNext, int& iterations) {
    size_t n = y.size();
    yNext.resize(n);
    std::vector<double> fCurrent(n), fNext(n);

    // Evaluate f(t, y)
    f(t, y, fCurrent);

    // Initial guess: explicit Euler
    for (size_t i = 0; i < n; ++i) {
        yNext[i] = y[i] + h * fCurrent[i];
    }

    // Newton iteration
    for (int iter = 0; iter < maxIter; ++iter) {
        f(t + h, yNext, fNext);

        double maxDiff = 0.0;
        for (size_t i = 0; i < n; ++i) {
            double residual = yNext[i] - y[i] - 0.5 * h * (fCurrent[i] + fNext[i]);
            double correction = -residual / (1.0 + 0.5 * h); // simplified
            yNext[i] += correction;
            maxDiff = std::max(maxDiff, std::abs(correction));
        }

        iterations++;
        if (maxDiff < tol) break;
    }
}

void Integrator::backwardEulerStep(double t, const std::vector<double>& y, double h,
                                      const OdeFunction& f, int maxIter, double tol,
                                      std::vector<double>& yNext, int& iterations) {
    size_t n = y.size();
    yNext.resize(n);

    // Initial guess: explicit Euler
    std::vector<double> fCurrent(n);
    f(t, y, fCurrent);
    for (size_t i = 0; i < n; ++i) {
        yNext[i] = y[i] + h * fCurrent[i];
    }

    // Fixed-point iteration
    std::vector<double> fNext(n);
    for (int iter = 0; iter < maxIter; ++iter) {
        f(t + h, yNext, fNext);

        double maxDiff = 0.0;
        for (size_t i = 0; i < n; ++i) {
            double newVal = y[i] + h * fNext[i];
            maxDiff = std::max(maxDiff, std::abs(newVal - yNext[i]));
            yNext[i] = newVal;
        }

        iterations++;
        if (maxDiff < tol) break;
    }
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------
void Integrator::evaluateOde(double t, const std::vector<double>& y, std::vector<double>& dydt) {
    if (m_odeFunc) {
        m_odeFunc(t, y, dydt);
    }
}

double Integrator::computeError(const std::vector<double>& y, const std::vector<double>& yHigh,
                                  const std::vector<double>& yLow) const {
    double maxError = 0.0;
    for (size_t i = 0; i < y.size(); ++i) {
        double scale = m_config.absTolerance + m_config.relTolerance * std::max(std::abs(y[i]), 1.0);
        double localError = std::abs(yHigh[i] - yLow[i]) / scale;
        maxError = std::max(maxError, localError);
    }
    return maxError;
}

double Integrator::adjustStepSize(double h, double error) const {
    // Standard step size control for RK45
    double factor = m_config.safetyFactor * std::pow(1.0 / error, 0.25);
    factor = std::min(factor, m_config.maxErrorGrowth);
    factor = std::max(factor, 0.1); // Don't shrink too fast
    return h * factor;
}

void Integrator::recordDenseOutput() {
    DenseOutputRecord record;
    record.t = m_t;
    record.y = m_y;
    record.dydt = m_dydt;
    m_denseOutput.push_back(record);
}

void Integrator::solveLinearSystem(std::vector<std::vector<double>>& A,
                                     std::vector<double>& b, std::vector<double>& x) {
    size_t n = A.size();
    x.resize(n);

    // Gaussian elimination with partial pivoting
    for (size_t k = 0; k < n; ++k) {
        // Partial pivoting
        size_t maxRow = k;
        for (size_t i = k + 1; i < n; ++i) {
            if (std::abs(A[i][k]) > std::abs(A[maxRow][k])) {
                maxRow = i;
            }
        }
        std::swap(A[k], A[maxRow]);
        std::swap(b[k], b[maxRow]);

        // Forward elimination
        for (size_t i = k + 1; i < n; ++i) {
            double factor = A[i][k] / A[k][k];
            for (size_t j = k; j < n; ++j) {
                A[i][j] -= factor * A[k][j];
            }
            b[i] -= factor * b[k];
        }
    }

    // Back substitution
    for (int i = static_cast<int>(n) - 1; i >= 0; --i) {
        x[i] = b[i];
        for (size_t j = i + 1; j < n; ++j) {
            x[i] -= A[i][j] * x[j];
        }
        x[i] /= A[i][i];
    }
}

void Integrator::computeJacobian(double t, const std::vector<double>& y,
                                  std::vector<std::vector<double>>& J) {
    size_t n = y.size();
    J.resize(n, std::vector<double>(n, 0.0));

    std::vector<double> f0(n), f1(n), yPlus(n);
    f(t, y, f0);

    double eps = std::sqrt(std::numeric_limits<double>::epsilon());

    for (size_t j = 0; j < n; ++j) {
        double h = eps * std::max(std::abs(y[j]), 1.0);
        yPlus = y;
        yPlus[j] += h;
        f(t, yPlus, f1);
        for (size_t i = 0; i < n; ++i) {
            J[i][j] = (f1[i] - f0[i]) / h;
        }
    }

    m_stats.jacobianEvaluations++;
    m_stats.linearSolves++;
}

} // namespace powsys365
