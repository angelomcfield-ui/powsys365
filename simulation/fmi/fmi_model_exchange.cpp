#include "fmi_model_exchange.h"

#include <iostream>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <limits>
#include <cstdarg>

// ============================================================================
// CVODE integration via SUNDIALS (optional - compiles without SUNDIALS too)
// ============================================================================

#if defined(POWSYS365_HAS_SUNDIALS)
    #include <cvode/cvode.h>
    #include <nvector/nvector_serial.h>
    #include <sunmatrix/sunmatrix_dense.h>
    #include <sunlinsol/sunlinsol_dense.h>
    #include <sundials/sundials_types.h>
    #include <sundials/sundials_math.h>
#endif

namespace powsys365::simulation::fmi {

// ============================================================================
// StateVector Implementation
// ============================================================================

double StateVector::operator[](const std::string& name) const {
    for (size_t i = 0; i < names.size(); ++i) {
        if (names[i] == name) return values[i];
    }
    return std::numeric_limits<double>::quiet_NaN();
}

double& StateVector::operator[](const std::string& name) {
    for (size_t i = 0; i < names.size(); ++i) {
        if (names[i] == name) return values[i];
    }
    static double nan_val = std::numeric_limits<double>::quiet_NaN();
    return nan_val;
}

// ============================================================================
// CVODEIntegrator Implementation
// ============================================================================

#if defined(POWSYS365_HAS_SUNDIALS)

class CVODEIntegrator::Impl {
public:
    void* cvode_mem = nullptr;
    N_Vector yvec = nullptr;
    SUNMatrix A = nullptr;
    SUNLinearSolver LS = nullptr;
    ODEFunction odeFunc;
    double m_time = 0.0;
    double m_lastStepSize = 0.0;
    size_t m_neq = 0;
    bool m_initialized = false;
    MEStepStats m_stats;
    IntegratorConfig m_config;
    void* m_userData = nullptr;

    ~Impl() {
        cleanup();
    }

    void cleanup() {
        if (cvode_mem) { CVodeFree(&cvode_mem); cvode_mem = nullptr; }
        if (yvec) { N_VDestroy_Serial(yvec); yvec = nullptr; }
        if (A) { SUNMatDestroy(A); A = nullptr; }
        if (LS) { SUNLinSolFree(LS); LS = nullptr; }
        m_initialized = false;
    }

    static int cvodeRHS(double t, N_Vector y, N_Vector ydot, void* userData) {
        auto* impl = static_cast<Impl*>(userData);
        if (!impl) return -1;

        size_t n = impl->m_neq;
        std::vector<double> yvec(n), ydotvec(n);
        for (size_t i = 0; i < n; ++i) {
            yvec[i] = NV_Ith_S(y, static_cast<long>(i));
        }

        bool ok = impl->odeFunc(t, yvec, ydotvec);
        if (!ok) return -1;

        for (size_t i = 0; i < n; ++i) {
            NV_Ith_S(ydot, static_cast<long>(i)) = ydotvec[i];
        }

        impl->m_stats.functionEvals++;
        return 0;
    }
};

#else

// Fallback implementation when SUNDIALS is not available
class CVODEIntegrator::Impl {
public:
    ODEFunction odeFunc;
    double m_time = 0.0;
    double m_lastStepSize = 0.0;
    size_t m_neq = 0;
    bool m_initialized = false;
    MEStepStats m_stats;
    IntegratorConfig m_config;
    void* m_userData = nullptr;
    std::vector<double> m_y;
    std::unique_ptr<RK45Integrator> m_fallback;

    ~Impl() = default;
    void cleanup() {
        m_initialized = false;
        m_fallback.reset();
    }
};

#endif

CVODEIntegrator::CVODEIntegrator() : pImpl(std::make_unique<Impl>()) {}
CVODEIntegrator::~CVODEIntegrator() = default;
CVODEIntegrator::CVODEIntegrator(CVODEIntegrator&&) noexcept = default;
CVODEIntegrator& CVODEIntegrator::operator=(CVODEIntegrator&&) noexcept = default;

bool CVODEIntegrator::initialize(const IntegratorConfig& config,
                                 const std::vector<double>& y0,
                                 ODEFunction odeFunc) {
    pImpl->cleanup();
    pImpl->m_config = config;
    pImpl->odeFunc = odeFunc;
    pImpl->m_neq = y0.size();
    pImpl->m_time = 0.0;
    pImpl->m_stats = MEStepStats{};

    if (y0.empty()) return false;

#if defined(POWSYS365_HAS_SUNDIALS)
    // Create N_Vector
    pImpl->yvec = N_VNew_Serial(static_cast<long>(pImpl->m_neq));
    if (!pImpl->yvec) return false;

    for (size_t i = 0; i < pImpl->m_neq; ++i) {
        NV_Ith_S(pImpl->yvec, static_cast<long>(i)) = y0[i];
    }

    // Create CVODE memory
    pImpl->cvode_mem = CVodeCreate(config.useBDF ? CV_BDF : CV_ADAMS);
    if (!pImpl->cvode_mem) return false;

    // Initialize CVODE
    int flag = CVodeInit(pImpl->cvode_mem, Impl::cvodeRHS, 0.0, pImpl->yvec);
    if (flag != CV_SUCCESS) return false;

    // Set tolerances
    flag = CVodeSStolerances(pImpl->cvode_mem, config.relTolerance, config.absTolerance);
    if (flag != CV_SUCCESS) return false;

    // Create dense matrix and linear solver
    pImpl->A = SUNDenseMatrix(static_cast<long>(pImpl->m_neq), static_cast<long>(pImpl->m_neq));
    if (!pImpl->A) return false;

    pImpl->LS = SUNDenseLinearSolver(pImpl->yvec, pImpl->A);
    if (!pImpl->LS) return false;

    // Attach linear solver
    flag = CVDlsSetLinearSolver(pImpl->cvode_mem, pImpl->LS, pImpl->A);
    if (flag != CV_SUCCESS) return false;

    // Set max steps
    CVodeSetMaxNumSteps(pImpl->cvode_mem, config.maxSteps);

    // Set max step
    if (config.maxStep > 0.0) {
        CVodeSetMaxStep(pImpl->cvode_mem, config.maxStep);
    }

    // Set min step
    if (config.minStep > 0.0) {
        CVodeSetMinStep(pImpl->cvode_mem, config.minStep);
    }

    // Set initial step
    if (config.initialStep > 0.0) {
        CVodeSetInitStep(pImpl->cvode_mem, config.initialStep);
    }

    // Set max order
    if (config.maxOrder > 0) {
        CVodeSetMaxOrd(pImpl->cvode_mem, config.maxOrder);
    }

    // Set user data
    CVodeSetUserData(pImpl->cvode_mem, pImpl.get());

    pImpl->m_initialized = true;
    return true;
#else
    // Fallback: use RK45 integrator
    pImpl->m_y = y0;
    pImpl->m_fallback = std::make_unique<RK45Integrator>();
    pImpl->m_fallback->initialize(y0, odeFunc, config.relTolerance, config.absTolerance);
    pImpl->m_initialized = true;
    return true;
#endif
}

bool CVODEIntegrator::reinitialize(double t0, const std::vector<double>& y0) {
    if (!pImpl->m_initialized) return false;

#if defined(POWSYS365_HAS_SUNDIALS)
    if (!pImpl->cvode_mem || !pImpl->yvec) return false;

    for (size_t i = 0; i < y0.size() && i < pImpl->m_neq; ++i) {
        NV_Ith_S(pImpl->yvec, static_cast<long>(i)) = y0[i];
    }

    int flag = CVodeReInit(pImpl->cvode_mem, t0, pImpl->yvec);
    if (flag != CV_SUCCESS) return false;

    pImpl->m_time = t0;
    return true;
#else
    if (pImpl->m_fallback) {
        return pImpl->m_fallback->initialize(y0, pImpl->odeFunc,
                                               pImpl->m_config.relTolerance,
                                               pImpl->m_config.absTolerance);
    }
    return false;
#endif
}

bool CVODEIntegrator::step(double tEnd) {
    if (!pImpl->m_initialized) return false;

#if defined(POWSYS365_HAS_SUNDIALS)
    if (!pImpl->cvode_mem || !pImpl->yvec) return false;

    double tout = tEnd;
    int flag = CVode(pImpl->cvode_mem, tout, pImpl->yvec, &pImpl->m_time, CV_NORMAL);

    if (flag < 0) {
        pImpl->m_stats.rejectedSteps++;
        return false;
    }

    pImpl->m_stats.acceptedSteps++;
    pImpl->m_stats.totalSteps++;
    pImpl->m_lastStepSize = pImpl->m_time - (tout - (tout - pImpl->m_time));
    return true;
#else
    if (pImpl->m_fallback) {
        bool ok = pImpl->m_fallback->step(tEnd);
        pImpl->m_time = pImpl->m_fallback->currentTime();
        pImpl->m_stats = pImpl->m_fallback->stats();
        return ok;
    }
    return false;
#endif
}

bool CVODEIntegrator::stepFixed(double dt, std::vector<double>& yOut) {
    if (!pImpl->m_initialized) return false;
    return step(pImpl->m_time + dt);
}

bool CVODEIntegrator::interpolate(double t, std::vector<double>& yOut) {
    if (!pImpl->m_initialized) return false;

#if defined(POWSYS365_HAS_SUNDIALS)
    if (!pImpl->cvode_mem) return false;

    yOut.resize(pImpl->m_neq);
    N_Vector ywork = N_VNew_Serial(static_cast<long>(pImpl->m_neq));
    if (!ywork) return false;

    int flag = CVodeGetDky(pImpl->cvode_mem, t, 0, ywork);
    if (flag == CV_SUCCESS) {
        for (size_t i = 0; i < pImpl->m_neq; ++i) {
            yOut[i] = NV_Ith_S(ywork, static_cast<long>(i));
        }
    }

    N_VDestroy_Serial(ywork);
    return (flag == CV_SUCCESS);
#else
    return false;
#endif
}

const std::vector<double>& CVODEIntegrator::currentY() const noexcept {
    static std::vector<double> empty;
#if defined(POWSYS365_HAS_SUNDIALS)
    if (pImpl->yvec && pImpl->m_neq > 0) {
        static std::vector<double> y;
        y.resize(pImpl->m_neq);
        for (size_t i = 0; i < pImpl->m_neq; ++i) {
            y[i] = NV_Ith_S(pImpl->yvec, static_cast<long>(i));
        }
        return y;
    }
#endif
    if (pImpl->m_fallback) {
        return pImpl->m_fallback->currentY();
    }
    return empty;
}

double CVODEIntegrator::currentTime() const noexcept { return pImpl->m_time; }
double CVODEIntegrator::lastStepSize() const noexcept { return pImpl->m_lastStepSize; }
const MEStepStats& CVODEIntegrator::stats() const noexcept { return pImpl->m_stats; }
bool CVODEIntegrator::isInitialized() const noexcept { return pImpl->m_initialized; }

void CVODEIntegrator::setTolerances(double relTol, double absTol) {
#if defined(POWSYS365_HAS_SUNDIALS)
    if (pImpl->cvode_mem) {
        CVodeSStolerances(pImpl->cvode_mem, relTol, absTol);
    }
#endif
    pImpl->m_config.relTolerance = relTol;
    pImpl->m_config.absTolerance = absTol;
}

void CVODEIntegrator::setMaxStep(double maxStep) {
#if defined(POWSYS365_HAS_SUNDIALS)
    if (pImpl->cvode_mem) {
        CVodeSetMaxStep(pImpl->cvode_mem, maxStep);
    }
#endif
    pImpl->m_config.maxStep = maxStep;
}

void CVODEIntegrator::setUserData(void* userData) {
    pImpl->m_userData = userData;
#if defined(POWSYS365_HAS_SUNDIALS)
    if (pImpl->cvode_mem) {
        CVodeSetUserData(pImpl->cvode_mem, pImpl.get());
    }
#endif
}

// ============================================================================
// RK4 Integrator Implementation
// ============================================================================

RK4Integrator::RK4Integrator() = default;

bool RK4Integrator::initialize(const std::vector<double>& y0, ODEFunction odeFunc) {
    m_y = y0;
    m_odeFunc = odeFunc;
    m_time = 0.0;
    m_stats = MEStepStats{};
    m_k1.resize(y0.size());
    m_k2.resize(y0.size());
    m_k3.resize(y0.size());
    m_k4.resize(y0.size());
    m_tmp.resize(y0.size());
    return true;
}

bool RK4Integrator::step(double dt) {
    if (!m_odeFunc || m_y.empty()) return false;

    size_t n = m_y.size();

    // k1 = f(t, y)
    if (!m_odeFunc(m_time, m_y, m_k1)) return false;
    m_stats.functionEvals++;

    // k2 = f(t + dt/2, y + dt*k1/2)
    for (size_t i = 0; i < n; ++i) m_tmp[i] = m_y[i] + 0.5 * dt * m_k1[i];
    if (!m_odeFunc(m_time + 0.5 * dt, m_tmp, m_k2)) return false;
    m_stats.functionEvals++;

    // k3 = f(t + dt/2, y + dt*k2/2)
    for (size_t i = 0; i < n; ++i) m_tmp[i] = m_y[i] + 0.5 * dt * m_k2[i];
    if (!m_odeFunc(m_time + 0.5 * dt, m_tmp, m_k3)) return false;
    m_stats.functionEvals++;

    // k4 = f(t + dt, y + dt*k3)
    for (size_t i = 0; i < n; ++i) m_tmp[i] = m_y[i] + dt * m_k3[i];
    if (!m_odeFunc(m_time + dt, m_tmp, m_k4)) return false;
    m_stats.functionEvals++;

    // y_{n+1} = y_n + dt/6 * (k1 + 2*k2 + 2*k3 + k4)
    for (size_t i = 0; i < n; ++i) {
        m_y[i] += (dt / 6.0) * (m_k1[i] + 2.0 * m_k2[i] + 2.0 * m_k3[i] + m_k4[i]);
    }

    m_time += dt;
    m_stats.totalSteps++;
    m_stats.acceptedSteps++;
    m_stats.currentTime = m_time;
    m_stats.currentStepSize = dt;

    return true;
}

const std::vector<double>& RK4Integrator::currentY() const noexcept { return m_y; }
double RK4Integrator::currentTime() const noexcept { return m_time; }
const MEStepStats& RK4Integrator::stats() const noexcept { return m_stats; }

// ============================================================================
// Euler Integrator Implementation
// ============================================================================

EulerIntegrator::EulerIntegrator() = default;

bool EulerIntegrator::initialize(const std::vector<double>& y0, ODEFunction odeFunc) {
    m_y = y0;
    m_ydot.resize(y0.size());
    m_odeFunc = odeFunc;
    m_time = 0.0;
    m_stats = MEStepStats{};
    return true;
}

bool EulerIntegrator::step(double dt) {
    if (!m_odeFunc || m_y.empty()) return false;

    if (!m_odeFunc(m_time, m_y, m_ydot)) return false;
    m_stats.functionEvals++;

    for (size_t i = 0; i < m_y.size(); ++i) {
        m_y[i] += dt * m_ydot[i];
    }

    m_time += dt;
    m_stats.totalSteps++;
    m_stats.acceptedSteps++;
    m_stats.currentTime = m_time;
    m_stats.currentStepSize = dt;

    return true;
}

const std::vector<double>& EulerIntegrator::currentY() const noexcept { return m_y; }
double EulerIntegrator::currentTime() const noexcept { return m_time; }
const MEStepStats& EulerIntegrator::stats() const noexcept { return m_stats; }

// ============================================================================
// RK23 Integrator (Adaptive Bogacki-Shampine)
// ============================================================================

RK23Integrator::RK23Integrator() = default;

bool RK23Integrator::initialize(const std::vector<double>& y0, ODEFunction odeFunc,
                                 double relTol, double absTol) {
    m_y = y0;
    m_odeFunc = odeFunc;
    m_relTol = relTol;
    m_absTol = absTol;
    m_time = 0.0;
    m_dt = 1e-3;
    m_stats = MEStepStats{};

    size_t n = y0.size();
    m_yerr.resize(n);
    m_k1.resize(n);
    m_k2.resize(n);
    m_k3.resize(n);
    m_k4.resize(n);
    m_tmp.resize(n);
    return true;
}

bool RK23Integrator::step(double tEnd) {
    if (!m_odeFunc || m_y.empty()) return false;

    while (m_time < tEnd) {
        double dt = std::min(m_dt, tEnd - m_time);
        if (dt <= 0) break;

        size_t n = m_y.size();

        // Bogacki-Shampine coefficients
        // Stage 1
        if (!m_odeFunc(m_time, m_y, m_k1)) return false;
        m_stats.functionEvals++;

        // Stage 2
        for (size_t i = 0; i < n; ++i) m_tmp[i] = m_y[i] + 0.5 * dt * m_k1[i];
        if (!m_odeFunc(m_time + 0.5 * dt, m_tmp, m_k2)) return false;
        m_stats.functionEvals++;

        // Stage 3
        for (size_t i = 0; i < n; ++i) m_tmp[i] = m_y[i] + 0.75 * dt * m_k2[i];
        if (!m_odeFunc(m_time + 0.75 * dt, m_tmp, m_k3)) return false;
        m_stats.functionEvals++;

        // 3rd order solution (accepted)
        for (size_t i = 0; i < n; ++i) {
            m_tmp[i] = m_y[i] + (dt / 9.0) * (2.0 * m_k1[i] + 3.0 * m_k2[i] + 4.0 * m_k3[i]);
        }

        // Stage 4 for error estimation
        if (!m_odeFunc(m_time + dt, m_tmp, m_k4)) return false;
        m_stats.functionEvals++;

        // 2nd order solution for error
        for (size_t i = 0; i < n; ++i) {
            double y2 = m_y[i] + (dt / 24.0) * (7.0 * m_k1[i] + 6.0 * m_k2[i] + 8.0 * m_k3[i] + 3.0 * m_k4[i]);
            m_yerr[i] = m_tmp[i] - y2;
        }

        // Error estimation
        double err = computeError(m_yerr, m_y);

        if (err <= 1.0) {
            // Accept step
            m_y = m_tmp;
            m_time += dt;
            m_stats.acceptedSteps++;
            m_stats.totalSteps++;
            m_stats.currentTime = m_time;
            m_stats.currentStepSize = dt;
            m_stats.lastSuccessfulStep = dt;

            // Increase step size for next step
            double factor = std::min(5.0, 0.9 * std::pow(1.0 / err, 1.0 / 3.0));
            m_dt *= factor;
        } else {
            // Reject step
            m_stats.rejectedSteps++;
            m_stats.errorTestFails++;
            m_stats.totalSteps++;

            // Decrease step size
            double factor = 0.9 * std::pow(1.0 / err, 1.0 / 3.0);
            factor = std::max(factor, 0.1);
            m_dt *= factor;
        }
    }

    return true;
}

double RK23Integrator::computeError(const std::vector<double>& err, const std::vector<double>& y) {
    double maxErr = 0.0;
    for (size_t i = 0; i < err.size(); ++i) {
        double scale = m_absTol + m_relTol * std::abs(y[i]);
        double e = std::abs(err[i]) / scale;
        if (e > maxErr) maxErr = e;
    }
    return maxErr;
}

const std::vector<double>& RK23Integrator::currentY() const noexcept { return m_y; }
double RK23Integrator::currentTime() const noexcept { return m_time; }
const MEStepStats& RK23Integrator::stats() const noexcept { return m_stats; }

// ============================================================================
// RK45 Integrator (Adaptive Dormand-Prince)
// ============================================================================

RK45Integrator::RK45Integrator() = default;

bool RK45Integrator::initialize(const std::vector<double>& y0, ODEFunction odeFunc,
                                 double relTol, double absTol) {
    m_y = y0;
    m_odeFunc = odeFunc;
    m_relTol = relTol;
    m_absTol = absTol;
    m_time = 0.0;
    m_dt = 1e-3;
    m_stats = MEStepStats{};

    size_t n = y0.size();
    m_yerr.resize(n);
    m_tmp.resize(n);
    for (int i = 0; i < 7; ++i) {
        m_k[i].resize(n);
    }
    return true;
}

bool RK45Integrator::step(double tEnd) {
    if (!m_odeFunc || m_y.empty()) return false;

    // Dormand-Prince coefficients
    const double a2 = 1.0/5.0;
    const double a3 = 3.0/10.0, a32 = 3.0/40.0, a33 = 9.0/40.0;
    const double a4 = 4.0/5.0, a42 = 44.0/45.0, a43 = -56.0/15.0, a44 = 32.0/9.0;
    const double a5 = 8.0/9.0, a52 = 19372.0/6561.0, a53 = -25360.0/2187.0, a54 = 64448.0/6561.0, a55 = -212.0/729.0;
    const double a6 = 1.0, a62 = 9017.0/3168.0, a63 = -355.0/33.0, a64 = 46732.0/5247.0, a65 = 49.0/176.0, a66 = -5103.0/18656.0;
    const double a7 = 1.0, a72 = 35.0/384.0, a73 = 0.0, a74 = 500.0/1113.0, a75 = 125.0/192.0, a76 = -2187.0/6784.0, a77 = 11.0/84.0;

    // 5th order weights
    const double b1 = 35.0/384.0, b2 = 0.0, b3 = 500.0/1113.0, b4 = 125.0/192.0, b5 = -2187.0/6784.0, b6 = 11.0/84.0, b7 = 0.0;
    // 4th order weights
    const double b1_4 = 5179.0/57600.0, b2_4 = 0.0, b3_4 = 7571.0/16695.0, b4_4 = 393.0/640.0, b5_4 = -92097.0/339200.0, b6_4 = 187.0/2100.0, b7_4 = 1.0/40.0;

    while (m_time < tEnd) {
        double dt = std::min(m_dt, tEnd - m_time);
        if (dt <= 0) break;

        size_t n = m_y.size();

        // Stage 1
        if (!m_odeFunc(m_time, m_y, m_k[0])) return false;
        m_stats.functionEvals++;

        // Stage 2
        for (size_t i = 0; i < n; ++i) m_tmp[i] = m_y[i] + dt * a2 * m_k[0][i];
        if (!m_odeFunc(m_time + a2 * dt, m_tmp, m_k[1])) return false;
        m_stats.functionEvals++;

        // Stage 3
        for (size_t i = 0; i < n; ++i) m_tmp[i] = m_y[i] + dt * (a32 * m_k[0][i] + a33 * m_k[1][i]);
        if (!m_odeFunc(m_time + a3 * dt, m_tmp, m_k[2])) return false;
        m_stats.functionEvals++;

        // Stage 4
        for (size_t i = 0; i < n; ++i) m_tmp[i] = m_y[i] + dt * (a42 * m_k[1][i] + a43 * m_k[2][i] + a44 * m_k[3][i]);
        if (!m_odeFunc(m_time + a4 * dt, m_tmp, m_k[3])) return false;
        m_stats.functionEvals++;

        // Stage 5
        for (size_t i = 0; i < n; ++i) {
            m_tmp[i] = m_y[i] + dt * (a52 * m_k[1][i] + a53 * m_k[2][i] + a54 * m_k[3][i] + a55 * m_k[4][i]);
        }
        if (!m_odeFunc(m_time + a5 * dt, m_tmp, m_k[4])) return false;
        m_stats.functionEvals++;

        // Stage 6
        for (size_t i = 0; i < n; ++i) {
            m_tmp[i] = m_y[i] + dt * (a62 * m_k[1][i] + a63 * m_k[2][i] + a64 * m_k[3][i] + a65 * m_k[4][i] + a66 * m_k[5][i]);
        }
        if (!m_odeFunc(m_time + a6 * dt, m_tmp, m_k[5])) return false;
        m_stats.functionEvals++;

        // Stage 7 (5th order)
        for (size_t i = 0; i < n; ++i) {
            m_tmp[i] = m_y[i] + dt * (a72 * m_k[1][i] + a73 * m_k[2][i] + a74 * m_k[3][i] + a75 * m_k[4][i] + a76 * m_k[5][i] + a77 * m_k[6][i]);
        }

        // Compute 5th order solution and error
        for (size_t i = 0; i < n; ++i) {
            double y5 = m_y[i] + dt * (b1 * m_k[0][i] + b2 * m_k[1][i] + b3 * m_k[2][i] + b4 * m_k[3][i] + b5 * m_k[4][i] + b6 * m_k[5][i] + b7 * m_k[6][i]);
            double y4 = m_y[i] + dt * (b1_4 * m_k[0][i] + b2_4 * m_k[1][i] + b3_4 * m_k[2][i] + b4_4 * m_k[3][i] + b5_4 * m_k[4][i] + b6_4 * m_k[5][i] + b7_4 * m_k[6][i]);
            m_tmp[i] = y5;
            m_yerr[i] = y5 - y4;
        }

        // Error estimation
        double err = computeError(m_yerr, m_y);

        if (err <= 1.0) {
            // Accept step
            m_y = m_tmp;
            m_time += dt;
            m_stats.acceptedSteps++;
            m_stats.totalSteps++;
            m_stats.currentTime = m_time;
            m_stats.currentStepSize = dt;
            m_stats.lastSuccessfulStep = dt;

            // Recompute k7 for next step (FSAL)
            if (!m_odeFunc(m_time, m_y, m_k[6])) return false;
            m_stats.functionEvals++;

            // Increase step size
            double factor = std::min(5.0, 0.9 * std::pow(1.0 / err, 1.0 / 5.0));
            m_dt *= factor;
        } else {
            // Reject step
            m_stats.rejectedSteps++;
            m_stats.errorTestFails++;
            m_stats.totalSteps++;

            // Decrease step size
            double factor = 0.9 * std::pow(1.0 / err, 1.0 / 5.0);
            factor = std::max(factor, 0.1);
            m_dt *= factor;
        }
    }

    return true;
}

double RK45Integrator::computeError(const std::vector<double>& err, const std::vector<double>& y) {
    double maxErr = 0.0;
    for (size_t i = 0; i < err.size(); ++i) {
        double scale = m_absTol + m_relTol * std::abs(y[i]);
        double e = std::abs(err[i]) / scale;
        if (e > maxErr) maxErr = e;
    }
    return maxErr;
}

const std::vector<double>& RK45Integrator::currentY() const noexcept { return m_y; }
double RK45Integrator::currentTime() const noexcept { return m_time; }
const MEStepStats& RK45Integrator::stats() const noexcept { return m_stats; }

// ============================================================================
// ODEIntegrator Standalone Functions
// ============================================================================

std::vector<double> ODEIntegrator::solveRK4(ODEFunction f,
                                             const std::vector<double>& y0,
                                             double t0, double tEnd,
                                             int numSteps) {
    RK4Integrator integrator;
    integrator.initialize(y0, f);

    double dt = (tEnd - t0) / numSteps;
    for (int i = 0; i < numSteps; ++i) {
        integrator.step(dt);
    }

    return integrator.currentY();
}

std::vector<double> ODEIntegrator::solveEuler(ODEFunction f,
                                               const std::vector<double>& y0,
                                               double t0, double tEnd,
                                               int numSteps) {
    EulerIntegrator integrator;
    integrator.initialize(y0, f);

    double dt = (tEnd - t0) / numSteps;
    for (int i = 0; i < numSteps; ++i) {
        integrator.step(dt);
    }

    return integrator.currentY();
}

std::vector<std::pair<double, std::vector<double>>> ODEIntegrator::solveAdaptiveRK45(
    ODEFunction f,
    const std::vector<double>& y0,
    double t0, double tEnd,
    double relTol,
    double absTol) {

    std::vector<std::pair<double, std::vector<double>>> trajectory;
    trajectory.reserve(1000);

    RK45Integrator integrator;
    integrator.initialize(y0, f, relTol, absTol);

    // Store initial point
    trajectory.emplace_back(t0, y0);

    double t = t0;
    while (t < tEnd) {
        integrator.step(tEnd);
        t = integrator.currentTime();
        trajectory.emplace_back(t, integrator.currentY());
    }

    return trajectory;
}

// ============================================================================
// FMIModelExchange Implementation
// ============================================================================

FMIModelExchange::FMIModelExchange() = default;

FMIModelExchange::~FMIModelExchange() {
    if (m_component && m_importer && m_importer->fmi2Functions() &&
        m_importer->fmi2Functions()->freeInstance) {
        m_importer->fmi2Functions()->freeInstance(m_component);
    }
}

FMIModelExchange::FMIModelExchange(FMIModelExchange&&) noexcept = default;
FMIModelExchange& FMIModelExchange::operator=(FMIModelExchange&&) noexcept = default;

bool FMIModelExchange::loadFMU(const std::string& fmuPath) {
    m_importer = std::make_unique<FMUImporter>(fmuPath);

    if (!m_importer->extract()) {
        m_lastError = "Failed to extract FMU: " + m_importer->lastError();
        return false;
    }

    if (!m_importer->parseModelDescription()) {
        m_lastError = "Failed to parse modelDescription";
        return false;
    }

    if (!m_importer->loadLibrary()) {
        m_lastError = "Failed to load FMU library: " + m_importer->lastError();
        return false;
    }

    // Verify ModelExchange is supported
    const FMIModelDescription* md = m_importer->modelDescription();
    if (!md || !md->supportsModelExchange()) {
        m_lastError = "FMU does not support ModelExchange";
        return false;
    }

    return true;
}

namespace {

// Static callback functions for Model Exchange
void meCallbackLogger(fmi2ComponentEnvironment /*componentEnvironment*/,
                      fmi2String instanceName,
                      fmi2Status status,
                      fmi2String category,
                      fmi2String message, ...) {
    const char* statusStr;
    switch (status) {
        case fmi2OK: statusStr = "OK"; break;
        case fmi2Warning: statusStr = "WARNING"; break;
        case fmi2Discard: statusStr = "DISCARD"; break;
        case fmi2Error: statusStr = "ERROR"; break;
        case fmi2Fatal: statusStr = "FATAL"; break;
        case fmi2Pending: statusStr = "PENDING"; break;
        default: statusStr = "UNKNOWN"; break;
    }

    std::cerr << "[ME FMU " << (instanceName ? instanceName : "?") << "] "
              << "[" << statusStr << "] [" << (category ? category : "?") << "] ";
    if (message) {
        va_list args;
        va_start(args, message);
        vfprintf(stderr, message, args);
        va_end(args);
    }
    std::cerr << std::endl;
}

void* meCallbackAllocateMemory(size_t nobj, size_t size) {
    return calloc(nobj, size);
}

void meCallbackFreeMemory(void* obj) {
    free(obj);
}

void meStepFinished(fmi2ComponentEnvironment /*componentEnvironment*/, fmi2Status /*status*/) {}

fmi2CallbackFunctions g_meCallbacks = {
    meCallbackLogger,
    meCallbackAllocateMemory,
    meCallbackFreeMemory,
    meStepFinished,
    nullptr
};

} // anonymous namespace

fmi2CallbackFunctions FMIModelExchange::getCallbacks() {
    return g_meCallbacks;
}

bool FMIModelExchange::instantiate(const std::string& instanceName) {
    if (!m_importer || !m_importer->fmi2Functions()) {
        m_lastError = "No FMU loaded";
        return false;
    }

    const FMIModelDescription* md = m_importer->modelDescription();
    if (!md) {
        m_lastError = "No model description";
        return false;
    }

    m_instanceName = instanceName;

    std::string resourceLocation = "file:///" + m_importer->extractedPath();
    std::replace(resourceLocation.begin(), resourceLocation.end(), '\\', '/');

    m_component = m_importer->fmi2Functions()->instantiate(
        instanceName.c_str(),
        fmi2ModelExchange,
        md->guid().c_str(),
        resourceLocation.c_str(),
        &g_meCallbacks,
        fmi2Boolean(0),
        fmi2Boolean(0)
    );

    if (!m_component) {
        m_lastError = "fmi2Instantiate returned null";
        return false;
    }

    // Build state variable list
    buildStateVRs();

    return true;
}

void FMIModelExchange::setIntegrator(IntegratorType type) {
    m_integratorType = type;
}

void FMIModelExchange::setIntegratorConfig(const IntegratorConfig& config) {
    m_integratorConfig = config;
}

const IntegratorConfig& FMIModelExchange::integratorConfig() const noexcept {
    return m_integratorConfig;
}

bool FMIModelExchange::setupExperiment(double startTime, double stopTime, double tolerance) {
    if (!m_component || !m_importer || !m_importer->fmi2Functions()) {
        m_lastError = "No FMU instance";
        return false;
    }

    m_startTime = startTime;
    m_stopTime = stopTime;
    m_tolerance = tolerance;

    fmi2Status status = m_importer->fmi2Functions()->setupExperiment(
        m_component,
        tolerance > 0 ? fmi2Boolean(1) : fmi2Boolean(0),
        static_cast<fmi2Real>(tolerance),
        static_cast<fmi2Real>(startTime),
        stopTime > startTime ? fmi2Boolean(1) : fmi2Boolean(0),
        static_cast<fmi2Real>(stopTime)
    );

    if (status != fmi2OK && status != fmi2Warning) {
        m_lastError = "fmi2SetupExperiment failed";
        return false;
    }

    return true;
}

bool FMIModelExchange::initialize(double startTime, double stopTime, double tolerance) {
    if (!setupExperiment(startTime, stopTime, tolerance)) {
        return false;
    }

    // Enter initialization mode
    fmi2Status status = m_importer->fmi2Functions()->enterInitializationMode(m_component);
    if (status != fmi2OK && status != fmi2Warning) {
        m_lastError = "fmi2EnterInitializationMode failed";
        return false;
    }

    // Exit initialization mode
    status = m_importer->fmi2Functions()->exitInitializationMode(m_component);
    if (status != fmi2OK && status != fmi2Warning) {
        m_lastError = "fmi2ExitInitializationMode failed";
        return false;
    }

    // Set initial time
    m_time = startTime;
    status = m_importer->fmi2Functions()->setTime(m_component, static_cast<fmi2Real>(m_time));
    if (status != fmi2OK) {
        m_lastError = "fmi2SetTime failed";
        return false;
    }

    // Get initial state
    updateStateFromFMU();

    // Initialize integrator
    if (m_currentState.empty()) {
        m_lastError = "No continuous states found in FMU";
        return false;
    }

    ODEFunction odeFunc = getODEFunction();
    if (!odeFunc) {
        m_lastError = "Failed to create ODE function";
        return false;
    }

    switch (m_integratorType) {
        case IntegratorType::CVODE: {
            m_cvodeIntegrator = std::make_unique<CVODEIntegrator>();
            if (!m_cvodeIntegrator->initialize(m_integratorConfig, m_currentState, odeFunc)) {
                m_lastError = "Failed to initialize CVODE integrator";
                return false;
            }
            break;
        }
        case IntegratorType::RK4: {
            m_rk4Integrator = std::make_unique<RK4Integrator>();
            m_rk4Integrator->initialize(m_currentState, odeFunc);
            break;
        }
        case IntegratorType::Euler: {
            m_eulerIntegrator = std::make_unique<EulerIntegrator>();
            m_eulerIntegrator->initialize(m_currentState, odeFunc);
            break;
        }
        case IntegratorType::RK23: {
            m_rk23Integrator = std::make_unique<RK23Integrator>();
            m_rk23Integrator->initialize(m_currentState, odeFunc,
                                         m_integratorConfig.relTolerance,
                                         m_integratorConfig.absTolerance);
            break;
        }
        case IntegratorType::RK45: {
            m_rk45Integrator = std::make_unique<RK45Integrator>();
            m_rk45Integrator->initialize(m_currentState, odeFunc,
                                         m_integratorConfig.relTolerance,
                                         m_integratorConfig.absTolerance);
            break;
        }
        default: {
            m_rk4Integrator = std::make_unique<RK4Integrator>();
            m_rk4Integrator->initialize(m_currentState, odeFunc);
            break;
        }
    }

    // Get event indicators
    const FMIModelDescription* md = m_importer->modelDescription();
    if (md) {
        size_t nEventInd = static_cast<size_t>(md->numberOfEventIndicators());
        if (nEventInd > 0) {
            m_eventIndicators.resize(nEventInd);
            m_prevEventIndicators.resize(nEventInd);
            getEventIndicators(m_eventIndicators);
            m_prevEventIndicators = m_eventIndicators;
        }
    }

    m_initialized = true;
    return true;
}

bool FMIModelExchange::initialize(const std::unordered_map<std::string, double>& initialValues) {
    // Set initial values before initialization
    for (const auto& [name, value] : initialValues) {
        if (!setVariable(name, value)) {
            // Not fatal, log and continue
        }
    }

    return initialize(m_startTime, m_stopTime, m_tolerance);
}

std::vector<double> FMIModelExchange::getDerivatives() {
    std::vector<double> ydot;
    getDerivatives(ydot);
    return ydot;
}

bool FMIModelExchange::getDerivatives(std::vector<double>& ydot) {
    if (!m_component || !m_importer || !m_importer->fmi2Functions() ||
        !m_importer->fmi2Functions()->getDerivatives) {
        m_lastError = "getDerivatives function not available";
        return false;
    }

    size_t n = m_currentState.size();
    ydot.resize(n);
    std::vector<fmi2Real> fmiDeriv(n);

    fmi2Status status = m_importer->fmi2Functions()->getDerivatives(
        m_component, fmiDeriv.data(), n
    );

    if (status == fmi2OK || status == fmi2Warning) {
        for (size_t i = 0; i < n; ++i) {
            ydot[i] = static_cast<double>(fmiDeriv[i]);
        }
        return true;
    }

    m_lastError = "fmi2GetDerivatives failed";
    return false;
}

std::vector<double> FMIModelExchange::getState() {
    updateStateFromFMU();
    return m_currentState;
}

bool FMIModelExchange::setState(const std::vector<double>& state) {
    if (!m_component || !m_importer || !m_importer->fmi2Functions() ||
        !m_importer->fmi2Functions()->setContinuousStates) {
        m_lastError = "setContinuousStates function not available";
        return false;
    }

    std::vector<fmi2Real> fmiState(state.size());
    for (size_t i = 0; i < state.size(); ++i) {
        fmiState[i] = static_cast<fmi2Real>(state[i]);
    }

    fmi2Status status = m_importer->fmi2Functions()->setContinuousStates(
        m_component, fmiState.data(), state.size()
    );

    if (status == fmi2OK || status == fmi2Warning) {
        m_currentState = state;
        return true;
    }

    m_lastError = "fmi2SetContinuousStates failed";
    return false;
}

bool FMIModelExchange::getNominalsOfContinuousStates(std::vector<double>& nominals) {
    if (!m_component || !m_importer || !m_importer->fmi2Functions() ||
        !m_importer->fmi2Functions()->getNominalsOfContinuousStates) {
        return false;
    }

    size_t n = m_currentState.size();
    nominals.resize(n);
    std::vector<fmi2Real> fmiNom(n);

    fmi2Status status = m_importer->fmi2Functions()->getNominalsOfContinuousStates(
        m_component, fmiNom.data(), n
    );

    if (status == fmi2OK || status == fmi2Warning) {
        for (size_t i = 0; i < n; ++i) {
            nominals[i] = static_cast<double>(fmiNom[i]);
        }
        return true;
    }

    return false;
}

StateVector FMIModelExchange::getStateVector() {
    StateVector sv;
    sv.time = m_time;
    updateStateFromFMU();
    sv.values = m_currentState;

    const FMIModelDescription* md = modelDescription();
    if (md) {
        for (const auto& var : md->variables()) {
            if (var.variability == Variability::Continuous) {
                sv.names.push_back(var.name);
                sv.valueReferences.push_back(var.valueReference);
            }
        }
    }

    return sv;
}

bool FMIModelExchange::step(double dt) {
    return stepTo(m_time + dt);
}

bool FMIModelExchange::stepTo(double targetTime) {
    if (!m_initialized) {
        m_lastError = "Not initialized";
        return false;
    }

    double target = std::min(targetTime, m_stopTime);

    switch (m_integratorType) {
        case IntegratorType::CVODE: {
            if (!m_cvodeIntegrator) return false;
            if (!m_cvodeIntegrator->step(target)) return false;
            m_currentState = m_cvodeIntegrator->currentY();
            m_time = m_cvodeIntegrator->currentTime();
            break;
        }
        case IntegratorType::RK4: {
            if (!m_rk4Integrator) return false;
            if (!m_rk4Integrator->step(target - m_time)) return false;
            m_currentState = m_rk4Integrator->currentY();
            m_time = m_rk4Integrator->currentTime();
            break;
        }
        case IntegratorType::Euler: {
            if (!m_eulerIntegrator) return false;
            if (!m_eulerIntegrator->step(target - m_time)) return false;
            m_currentState = m_eulerIntegrator->currentY();
            m_time = m_eulerIntegrator->currentTime();
            break;
        }
        case IntegratorType::RK23: {
            if (!m_rk23Integrator) return false;
            if (!m_rk23Integrator->step(target)) return false;
            m_currentState = m_rk23Integrator->currentY();
            m_time = m_rk23Integrator->currentTime();
            break;
        }
        case IntegratorType::RK45: {
            if (!m_rk45Integrator) return false;
            if (!m_rk45Integrator->step(target)) return false;
            m_currentState = m_rk45Integrator->currentY();
            m_time = m_rk45Integrator->currentTime();
            break;
        }
        default: {
            return false;
        }
    }

    // Push state to FMU
    if (!pushStateToFMU()) return false;

    // Set time in FMU
    fmi2Status status = m_importer->fmi2Functions()->setTime(
        m_component, static_cast<fmi2Real>(m_time)
    );
    if (status != fmi2OK) {
        m_lastError = "fmi2SetTime failed after step";
        return false;
    }

    // Check event indicators
    if (m_eventIndicators.empty()) {
        completedIntegratorStep();
        return true;
    }

    m_prevEventIndicators = m_eventIndicators;
    getEventIndicators(m_eventIndicators);

    bool enterEventMode = false;
    for (size_t i = 0; i < m_eventIndicators.size(); ++i) {
        if (m_prevEventIndicators[i] * m_eventIndicators[i] < 0.0) {
            // Event detected - sign change
            enterEventMode = true;
            if (m_eventCallback) {
                m_eventCallback(m_time, m_currentState, static_cast<int>(i));
            }
        }
    }

    if (enterEventMode) {
        handleEvents();
    } else {
        completedIntegratorStep();
    }

    return true;
}

// ---------------------------------------------------------------------------
// Event Handling
// ---------------------------------------------------------------------------

bool FMIModelExchange::handleEvents() {
    if (!enterEventMode()) return false;

    bool continueIteration = true;
    int maxIter = 100;
    int iter = 0;

    while (continueIteration && iter < maxIter) {
        if (!newDiscreteStates()) return false;
        // The newDiscreteStates updates the event info internally
        // In a real implementation we'd parse the event info struct
        continueIteration = false;  // Simplified - would check eventInfo
        iter++;
    }

    if (!enterContinuousTimeMode()) return false;

    return true;
}

bool FMIModelExchange::enterEventMode() {
    if (!m_component || !m_importer || !m_importer->fmi2Functions() ||
        !m_importer->fmi2Functions()->enterEventMode) {
        return false;
    }

    fmi2Status status = m_importer->fmi2Functions()->enterEventMode(m_component);
    if (status == fmi2OK || status == fmi2Warning) {
        m_inEventMode = true;
        return true;
    }

    m_lastError = "fmi2EnterEventMode failed";
    return false;
}

bool FMIModelExchange::enterContinuousTimeMode() {
    if (!m_component || !m_importer || !m_importer->fmi2Functions() ||
        !m_importer->fmi2Functions()->enterContinuousTimeMode) {
        return false;
    }

    fmi2Status status = m_importer->fmi2Functions()->enterContinuousTimeMode(m_component);
    if (status == fmi2OK || status == fmi2Warning) {
        m_inEventMode = false;
        return true;
    }

    m_lastError = "fmi2EnterContinuousTimeMode failed";
    return false;
}

bool FMIModelExchange::newDiscreteStates() {
    if (!m_component || !m_importer || !m_importer->fmi2Functions() ||
        !m_importer->fmi2Functions()->newDiscreteStates) {
        return false;
    }

    // FMI 2.0 event info struct
    struct fmi2EventInfo {
        fmi2Boolean newDiscreteStatesNeeded;
        fmi2Boolean terminateSimulation;
        fmi2Boolean nominalsOfContinuousStatesChanged;
        fmi2Boolean valuesOfContinuousStatesChanged;
        fmi2Boolean nextEventTimeDefined;
        fmi2Real nextEventTime;
    } eventInfo;

    std::memset(&eventInfo, 0, sizeof(eventInfo));

    fmi2Status status = m_importer->fmi2Functions()->newDiscreteStates(m_component, &eventInfo);

    if (status == fmi2OK || status == fmi2Warning) {
        if (eventInfo.valuesOfContinuousStatesChanged) {
            updateStateFromFMU();
        }
        if (eventInfo.terminateSimulation) {
            m_initialized = false;
        }
        return true;
    }

    m_lastError = "fmi2NewDiscreteStates failed";
    return false;
}

bool FMIModelExchange::completedIntegratorStep() {
    if (!m_component || !m_importer || !m_importer->fmi2Functions() ||
        !m_importer->fmi2Functions()->completedIntegratorStep) {
        return false;
    }

    fmi2Boolean enterEventMode = fmi2Boolean(0);
    fmi2Boolean terminateSimulation = fmi2Boolean(0);

    fmi2Status status = m_importer->fmi2Functions()->completedIntegratorStep(
        m_component,
        fmi2Boolean(0),  // noSetFMUStatePriorToCurrentPoint
        &enterEventMode,
        &terminateSimulation
    );

    if (status == fmi2OK || status == fmi2Warning) {
        if (enterEventMode != fmi2Boolean(0)) {
            handleEvents();
        }
        if (terminateSimulation != fmi2Boolean(0)) {
            m_initialized = false;
        }
        return true;
    }

    m_lastError = "fmi2CompletedIntegratorStep failed";
    return false;
}

bool FMIModelExchange::getEventIndicators(std::vector<double>& indicators) {
    if (!m_component || !m_importer || !m_importer->fmi2Functions() ||
        !m_importer->fmi2Functions()->getEventIndicators) {
        return false;
    }

    indicators.resize(m_eventIndicators.size());
    if (indicators.empty()) return true;

    std::vector<fmi2Real> fmiInd(indicators.size());
    fmi2Status status = m_importer->fmi2Functions()->getEventIndicators(
        m_component, fmiInd.data(), indicators.size()
    );

    if (status == fmi2OK || status == fmi2Warning) {
        for (size_t i = 0; i < indicators.size(); ++i) {
            indicators[i] = static_cast<double>(fmiInd[i]);
        }
        return true;
    }

    return false;
}

void FMIModelExchange::setEventCallback(EventCallback callback) {
    m_eventCallback = callback;
}

// ---------------------------------------------------------------------------
// Variable Access
// ---------------------------------------------------------------------------

bool FMIModelExchange::getReal(uint32_t vr, double& value) {
    if (!m_component || !m_importer || !m_importer->fmi2Functions()) return false;

    fmi2ValueReference vref = vr;
    fmi2Real val;
    fmi2Status status = m_importer->fmi2Functions()->getReal(m_component, &vref, 1, &val);
    if (status == fmi2OK || status == fmi2Warning) {
        value = static_cast<double>(val);
        return true;
    }
    return false;
}

bool FMIModelExchange::setReal(uint32_t vr, double value) {
    if (!m_component || !m_importer || !m_importer->fmi2Functions()) return false;

    fmi2ValueReference vref = vr;
    fmi2Real val = static_cast<fmi2Real>(value);
    fmi2Status status = m_importer->fmi2Functions()->setReal(m_component, &vref, 1, &val);
    return (status == fmi2OK || status == fmi2Warning);
}

bool FMIModelExchange::getReal(const std::vector<uint32_t>& vrs, std::vector<double>& values) {
    if (!m_component || !m_importer || !m_importer->fmi2Functions() || vrs.empty()) return false;

    values.resize(vrs.size());
    std::vector<fmi2ValueReference> fmiVRs(vrs.begin(), vrs.end());
    std::vector<fmi2Real> fmiVals(vrs.size());

    fmi2Status status = m_importer->fmi2Functions()->getReal(
        m_component, fmiVRs.data(), vrs.size(), fmiVals.data()
    );

    if (status == fmi2OK || status == fmi2Warning) {
        for (size_t i = 0; i < vrs.size(); ++i) {
            values[i] = static_cast<double>(fmiVals[i]);
        }
        return true;
    }
    return false;
}

bool FMIModelExchange::setReal(const std::vector<uint32_t>& vrs, const std::vector<double>& values) {
    if (!m_component || !m_importer || !m_importer->fmi2Functions() || vrs.empty() || vrs.size() != values.size()) return false;

    std::vector<fmi2ValueReference> fmiVRs(vrs.begin(), vrs.end());
    std::vector<fmi2Real> fmiVals(values.begin(), values.end());

    fmi2Status status = m_importer->fmi2Functions()->setReal(
        m_component, fmiVRs.data(), vrs.size(), fmiVals.data()
    );
    return (status == fmi2OK || status == fmi2Warning);
}

double FMIModelExchange::getVariable(const std::string& name) {
    const FMIModelDescription* md = modelDescription();
    if (!md) return std::numeric_limits<double>::quiet_NaN();

    const FMIVariable* var = md->findVariableByName(name);
    if (!var) return std::numeric_limits<double>::quiet_NaN();

    double value = 0.0;
    if (getReal(var->valueReference, value)) {
        return value;
    }
    return std::numeric_limits<double>::quiet_NaN();
}

bool FMIModelExchange::setVariable(const std::string& name, double value) {
    const FMIModelDescription* md = modelDescription();
    if (!md) return false;

    const FMIVariable* var = md->findVariableByName(name);
    if (!var) return false;

    return setReal(var->valueReference, value);
}

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------

bool FMIModelExchange::setTime(double time) {
    if (!m_component || !m_importer || !m_importer->fmi2Functions() ||
        !m_importer->fmi2Functions()->setTime) {
        return false;
    }

    fmi2Status status = m_importer->fmi2Functions()->setTime(
        m_component, static_cast<fmi2Real>(time)
    );

    if (status == fmi2OK || status == fmi2Warning) {
        m_time = time;
        return true;
    }

    return false;
}

double FMIModelExchange::getTime() const noexcept { return m_time; }

// ---------------------------------------------------------------------------
// Termination
// ---------------------------------------------------------------------------

bool FMIModelExchange::terminate() {
    if (!m_component || !m_importer || !m_importer->fmi2Functions() ||
        !m_importer->fmi2Functions()->terminate) {
        m_initialized = false;
        return false;
    }

    fmi2Status status = m_importer->fmi2Functions()->terminate(m_component);
    m_initialized = false;
    return (status == fmi2OK || status == fmi2Warning);
}

bool FMIModelExchange::reset() {
    m_initialized = false;
    m_cvodeIntegrator.reset();
    m_rk4Integrator.reset();
    m_eulerIntegrator.reset();
    m_rk23Integrator.reset();
    m_rk45Integrator.reset();

    if (!m_component || !m_importer || !m_importer->fmi2Functions() ||
        !m_importer->fmi2Functions()->reset) {
        return false;
    }

    fmi2Status status = m_importer->fmi2Functions()->reset(m_component);
    return (status == fmi2OK || status == fmi2Warning);
}

// ---------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------

bool FMIModelExchange::isInitialized() const noexcept { return m_initialized; }
bool FMIModelExchange::isInEventMode() const noexcept { return m_inEventMode; }
const FMIModelDescription* FMIModelExchange::modelDescription() const noexcept {
    return m_importer ? m_importer->modelDescription() : nullptr;
}
const MEStepStats& FMIModelExchange::stats() const noexcept { return m_stats; }
const std::string& FMIModelExchange::lastError() const noexcept { return m_lastError; }
IntegratorType FMIModelExchange::currentIntegrator() const noexcept { return m_integratorType; }
size_t FMIModelExchange::numContinuousStates() const { return m_currentState.size(); }

// ---------------------------------------------------------------------------
// Internal Methods
// ---------------------------------------------------------------------------

bool FMIModelExchange::updateStateFromFMU() {
    if (!m_component || !m_importer || !m_importer->fmi2Functions() ||
        !m_importer->fmi2Functions()->getContinuousStates) {
        return false;
    }

    size_t n = numContinuousStates();
    if (n == 0) {
        // Get from model description
        const FMIModelDescription* md = modelDescription();
        if (md) {
            n = static_cast<size_t>(md->numberOfContinuousStates());
        }
    }
    if (n == 0) return true;

    m_currentState.resize(n);
    std::vector<fmi2Real> fmiState(n);

    fmi2Status status = m_importer->fmi2Functions()->getContinuousStates(
        m_component, fmiState.data(), n
    );

    if (status == fmi2OK || status == fmi2Warning) {
        for (size_t i = 0; i < n; ++i) {
            m_currentState[i] = static_cast<double>(fmiState[i]);
        }
        return true;
    }

    return false;
}

bool FMIModelExchange::pushStateToFMU() {
    if (!m_component || !m_importer || !m_importer->fmi2Functions() ||
        !m_importer->fmi2Functions()->setContinuousStates ||
        m_currentState.empty()) {
        return true;  // Not an error if no states
    }

    std::vector<fmi2Real> fmiState(m_currentState.size());
    for (size_t i = 0; i < m_currentState.size(); ++i) {
        fmiState[i] = static_cast<fmi2Real>(m_currentState[i]);
    }

    fmi2Status status = m_importer->fmi2Functions()->setContinuousStates(
        m_component, fmiState.data(), m_currentState.size()
    );

    return (status == fmi2OK || status == fmi2Warning);
}

bool FMIModelExchange::buildStateVRs() {
    m_stateVRs.clear();
    const FMIModelDescription* md = modelDescription();
    if (!md) return false;

    for (const auto& var : md->variables()) {
        if (var.variability == Variability::Continuous &&
            var.causality != Causality::Independent) {
            m_stateVRs.push_back(var.valueReference);
        }
    }

    return !m_stateVRs.empty();
}

bool FMIModelExchange::checkEventIndicators() {
    if (m_eventIndicators.empty()) return false;

    m_prevEventIndicators = m_eventIndicators;
    getEventIndicators(m_eventIndicators);

    for (size_t i = 0; i < m_eventIndicators.size(); ++i) {
        if (m_prevEventIndicators[i] * m_eventIndicators[i] < 0.0) {
            return true;
        }
    }

    return false;
}

ODEFunction FMIModelExchange::getODEFunction() {
    if (m_customODEFunc) {
        return m_customODEFunc;
    }

    // Create ODE function wrapper for FMI
    return [this](double t, const std::vector<double>& y, std::vector<double>& ydot) -> bool {
        // Set time
        if (!this->m_component || !this->m_importer || !this->m_importer->fmi2Functions()) return false;

        fmi2Status status = this->m_importer->fmi2Functions()->setTime(
            this->m_component, static_cast<fmi2Real>(t)
        );
        if (status != fmi2OK && status != fmi2Warning) return false;

        // Set continuous states
        std::vector<fmi2Real> fmiState(y.size());
        for (size_t i = 0; i < y.size(); ++i) {
            fmiState[i] = static_cast<fmi2Real>(y[i]);
        }

        status = this->m_importer->fmi2Functions()->setContinuousStates(
            this->m_component, fmiState.data(), y.size()
        );
        if (status != fmi2OK && status != fmi2Warning) return false;

        // Get derivatives
        ydot.resize(y.size());
        std::vector<fmi2Real> fmiDeriv(y.size());

        status = this->m_importer->fmi2Functions()->getDerivatives(
            this->m_component, fmiDeriv.data(), y.size()
        );

        if (status == fmi2OK || status == fmi2Warning) {
            for (size_t i = 0; i < y.size(); ++i) {
                ydot[i] = static_cast<double>(fmiDeriv[i]);
            }
            return true;
        }

        return false;
    };
}

void FMIModelExchange::setODEFunction(ODEFunction func) {
    m_customODEFunc = func;
}

void FMIModelExchange::setEventFunction(EventFunction func) {
    m_customEventFunc = func;
}

bool FMIModelExchange::callODEFunction(double t, const std::vector<double>& y, std::vector<double>& ydot) {
    if (m_customODEFunc) {
        return m_customODEFunc(t, y, ydot);
    }
    return false;
}

bool FMIModelExchange::callEventFunction(double t, const std::vector<double>& y, std::vector<double>& g) {
    if (m_customEventFunc) {
        return m_customEventFunc(t, y, g);
    }
    return false;
}

} // namespace powsys365::simulation::fmi
