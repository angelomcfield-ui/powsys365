#include "state_estimator.h"

#include <iostream>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <cstring>

namespace powsys365::simulation {

// ============================================================================
// Measurement Implementation
// ============================================================================

Measurement::Measurement(uint32_t mid, MeasurementType mtype, uint32_t bus, double val, double sig)
    : id(mid), type(mtype), busId(bus), value(val), sigma(sig) {
    weight = (sig > 0) ? (1.0 / (sig * sig)) : 1.0;
}

Measurement::Measurement(uint32_t mid, MeasurementType mtype, uint32_t fbus, uint32_t tbus,
                          double val, double sig)
    : id(mid), type(mtype), fromBus(fbus), toBus(tbus), value(val), sigma(sig) {
    weight = (sig > 0) ? (1.0 / (sig * sig)) : 1.0;
}

// ============================================================================
// StateVector Implementation
// ============================================================================

void StateVector::resize(size_t nBuses, size_t nTaps, size_t nShunts) {
    voltage.resize(nBuses, 1.0);
    angle.resize(nBuses, 0.0);
    tapRatio.resize(nTaps, 1.0);
    shuntB.resize(nShunts, 0.0);
}

std::vector<double> StateVector::toFlatVector() const {
    std::vector<double> flat;
    flat.reserve(voltage.size() + angle.size() + tapRatio.size() + shuntB.size());
    flat.insert(flat.end(), voltage.begin(), voltage.end());
    flat.insert(flat.end(), angle.begin(), angle.end());
    flat.insert(flat.end(), tapRatio.begin(), tapRatio.end());
    flat.insert(flat.end(), shuntB.begin(), shuntB.end());
    return flat;
}

void StateVector::fromFlatVector(const std::vector<double>& flat, size_t nBuses) {
    voltage.assign(flat.begin(), flat.begin() + static_cast<long>(nBuses));
    angle.assign(flat.begin() + static_cast<long>(nBuses), flat.begin() + static_cast<long>(2 * nBuses));
}

// ============================================================================
// SparseJacobian Implementation
// ============================================================================

void SparseJacobian::add(uint32_t row, uint32_t col, double val) {
    rowIndices.push_back(row);
    colIndices.push_back(col);
    values.push_back(val);
    if (row >= numRows) numRows = row + 1;
    if (col >= numCols) numCols = col + 1;
}

void SparseJacobian::clear() {
    rowIndices.clear();
    colIndices.clear();
    values.clear();
    numRows = 0;
    numCols = 0;
}

double SparseJacobian::get(uint32_t row, uint32_t col) const {
    for (size_t i = 0; i < rowIndices.size(); ++i) {
        if (rowIndices[i] == row && colIndices[i] == col) {
            return values[i];
        }
    }
    return 0.0;
}

// ============================================================================
// StateEstimator Implementation
// ============================================================================

StateEstimator::StateEstimator() = default;
StateEstimator::StateEstimator(const WLSConfig& config) : m_config(config) {}
StateEstimator::StateEstimator(StateEstimator&&) noexcept = default;
StateEstimator& StateEstimator::operator=(StateEstimator&&) noexcept = default;

// ---------------------------------------------------------------------------
// System Setup
// ---------------------------------------------------------------------------

void StateEstimator::addBus(const BusData& bus) {
    m_buses.push_back(bus);
    m_ybusValid = false;
    m_busIndexMap.clear();
}

void StateEstimator::addBranch(const BranchData& branch) {
    m_branches.push_back(branch);
    m_ybusValid = false;
}

void StateEstimator::addMeasurement(const Measurement& measurement) {
    m_measurements.push_back(measurement);
}

void StateEstimator::setBuses(const std::vector<BusData>& buses) {
    m_buses = buses;
    m_ybusValid = false;
    m_busIndexMap.clear();
}

void StateEstimator::setBranches(const std::vector<BranchData>& branches) {
    m_branches = branches;
    m_ybusValid = false;
}

void StateEstimator::setMeasurements(const std::vector<Measurement>& measurements) {
    m_measurements = measurements;
}

void StateEstimator::clearBuses() {
    m_buses.clear();
    m_ybusValid = false;
    m_busIndexMap.clear();
}

void StateEstimator::clearBranches() {
    m_branches.clear();
    m_ybusValid = false;
}

void StateEstimator::clearMeasurements() {
    m_measurements.clear();
}

void StateEstimator::clearAll() {
    m_buses.clear();
    m_branches.clear();
    m_measurements.clear();
    m_ybusG.clear();
    m_ybusB.clear();
    m_ybusValid = false;
    m_busIndexMap.clear();
    m_slackBusIndex = -1;
}

size_t StateEstimator::numBuses() const noexcept { return m_buses.size(); }
size_t StateEstimator::numBranches() const noexcept { return m_branches.size(); }
size_t StateEstimator::numMeasurements() const noexcept { return m_measurements.size(); }

void StateEstimator::setConfig(const WLSConfig& config) { m_config = config; }
const WLSConfig& StateEstimator::config() const noexcept { return m_config; }

const std::string& StateEstimator::lastError() const noexcept { return m_lastError; }

// ---------------------------------------------------------------------------
// Bus Index Mapping
// ---------------------------------------------------------------------------

void StateEstimator::buildBusIndexMap() {
    m_busIndexMap.clear();
    m_slackBusIndex = -1;

    for (uint32_t i = 0; i < m_buses.size(); ++i) {
        m_busIndexMap[m_buses[i].busId] = i;
        if (m_buses[i].type == BusType::Slack) {
            m_slackBusIndex = static_cast<int>(i);
        }
    }

    // If no slack bus defined, use bus 0 as slack
    if (m_slackBusIndex < 0 && !m_buses.empty()) {
        m_slackBusIndex = 0;
    }
}

uint32_t StateEstimator::getBusIndex(uint32_t busId) const {
    auto it = m_busIndexMap.find(busId);
    if (it != m_busIndexMap.end()) return it->second;
    return 0;
}

// ---------------------------------------------------------------------------
// Y-Bus Matrix Construction
// ---------------------------------------------------------------------------

void StateEstimator::buildYBus() {
    if (m_busIndex.empty()) buildBusIndexMap();
    size_t n = m_buses.size();
    m_ybusG.assign(n, std::vector<double>(n, 0.0));
    m_ybusB.assign(n, std::vector<double>(n, 0.0));

    for (const auto& branch : m_branches) {
        if (!branch.inService) continue;

        uint32_t i = getBusIndex(branch.fromBus);
        uint32_t j = getBusIndex(branch.toBus);
        if (i == j) continue;

        // Series admittance
        double z2 = branch.r * branch.r + branch.x * branch.x;
        if (z2 < 1e-12) continue;

        double g = branch.r / z2;
        double b = -branch.x / z2;

        // Tap ratio
        double tap = branch.tapRatio;
        double tap2 = tap * tap;
        double shift = branch.phaseShift;
        double cosShift = std::cos(shift);
        double sinShift = std::sin(shift);

        // Off-diagonal elements
        double gOff = -(g * cosShift - b * sinShift) / tap;
        double bOff = -(g * sinShift + b * cosShift) / tap;

        double gOffT = -(g * cosShift + b * sinShift) / tap;
        double bOffT = -(-g * sinShift + b * cosShift) / tap;

        // From bus diagonal
        double gii_from = g / tap2;
        double bii_from = (b + branch.b * 0.5) / tap2;

        // To bus diagonal
        double gii_to = g;
        double bii_to = b + branch.b * 0.5;

        m_ybusG[i][j] += gOff;
        m_ybusB[i][j] += bOff;
        m_ybusG[j][i] += gOffT;
        m_ybusB[j][i] += bOffT;

        m_ybusG[i][i] += gii_from;
        m_ybusB[i][i] += bii_from;
        m_ybusG[j][j] += gii_to;
        m_ybusB[j][j] += bii_to;
    }

    // Add shunt elements
    for (uint32_t i = 0; i < n; ++i) {
        m_ybusG[i][i] += m_buses[i].gShunt;
        m_ybusB[i][i] += m_buses[i].bShunt;
    }

    m_ybusValid = true;
}

std::vector<std::vector<double>> StateEstimator::getYBusReal() const {
    return m_ybusG;
}

std::vector<std::vector<double>> StateEstimator::getYBusImag() const {
    return m_ybusB;
}

// ---------------------------------------------------------------------------
// System Validation
// ---------------------------------------------------------------------------

void StateEstimator::validateSystem() {
    m_lastError.clear();

    if (m_buses.empty()) {
        m_lastError = "No buses defined";
        return;
    }

    if (!m_ybusValid) {
        buildBusIndexMap();
        buildYBus();
    }

    if (m_slackBusIndex < 0) {
        m_lastError = "No slack bus defined";
    }
}

bool StateEstimator::isSystemValid() const {
    return !m_buses.empty() && m_slackBusIndex >= 0 && m_ybusValid;
}

// ---------------------------------------------------------------------------
// Measurement Computation h(x)
// ---------------------------------------------------------------------------

double StateEstimator::voltageMagnitude(uint32_t busIdx, const StateVector& state) const {
    return state.voltage[busIdx];
}

double StateEstimator::voltageAngle(uint32_t busIdx, const StateVector& state) const {
    return state.angle[busIdx];
}

double StateEstimator::computeMeasurement(const Measurement& m, const StateVector& state) const {
    uint32_t busIdx = getBusIndex(m.busId);

    switch (m.type) {
        case MeasurementType::VoltageMagnitude: {
            return voltageMagnitude(busIdx, state);
        }
        case MeasurementType::VoltageAngle: {
            return voltageAngle(busIdx, state);
        }
        case MeasurementType::RealPowerInjection: {
            double P = 0.0;
            size_t n = m_buses.size();
            for (uint32_t k = 0; k < n; ++k) {
                double gik = m_ybusG[busIdx][k];
                double bik = m_ybusB[busIdx][k];
                double theta_ik = state.angle[busIdx] - state.angle[k];
                P += state.voltage[busIdx] * state.voltage[k] * (gik * std::cos(theta_ik) + bik * std::sin(theta_ik));
            }
            return P;
        }
        case MeasurementType::ReactivePowerInjection: {
            double Q = 0.0;
            size_t n = m_buses.size();
            for (uint32_t k = 0; k < n; ++k) {
                double gik = m_ybusG[busIdx][k];
                double bik = m_ybusB[busIdx][k];
                double theta_ik = state.angle[busIdx] - state.angle[k];
                Q += state.voltage[busIdx] * state.voltage[k] * (gik * std::sin(theta_ik) - bik * std::cos(theta_ik));
            }
            return Q;
        }
        case MeasurementType::RealPowerFlow: {
            uint32_t j = getBusIndex(m.toBus);
            if (j >= m_buses.size()) return 0.0;
            double theta_ij = state.angle[busIdx] - state.angle[j];
            double gij = m_ybusG[busIdx][j];
            double bij = m_ybusB[busIdx][j];
            return state.voltage[busIdx] * state.voltage[busIdx] * (-gij) +
                   state.voltage[busIdx] * state.voltage[j] * (gij * std::cos(theta_ij) + bij * std::sin(theta_ij));
        }
        case MeasurementType::ReactivePowerFlow: {
            uint32_t j = getBusIndex(m.toBus);
            if (j >= m_buses.size()) return 0.0;
            double theta_ij = state.angle[busIdx] - state.angle[j];
            double gij = m_ybusG[busIdx][j];
            double bij = m_ybusB[busIdx][j];
            return -state.voltage[busIdx] * state.voltage[busIdx] * (-bij) +
                   state.voltage[busIdx] * state.voltage[j] * (gij * std::sin(theta_ij) - bij * std::cos(theta_ij));
        }
        case MeasurementType::CurrentMagnitude: {
            uint32_t j = getBusIndex(m.toBus);
            if (j >= m_buses.size()) return 0.0;
            double theta_ij = state.angle[busIdx] - state.angle[j];
            double gij = m_ybusG[busIdx][j];
            double bij = m_ybusB[busIdx][j];
            double reI = (state.voltage[busIdx] * std::cos(state.angle[busIdx]) - state.voltage[j] * std::cos(state.angle[j])) * gij -
                         (state.voltage[busIdx] * std::sin(state.angle[busIdx]) - state.voltage[j] * std::sin(state.angle[j])) * bij;
            double imI = (state.voltage[busIdx] * std::cos(state.angle[busIdx]) - state.voltage[j] * std::cos(state.angle[j])) * bij +
                         (state.voltage[busIdx] * std::sin(state.angle[busIdx]) - state.voltage[j] * std::sin(state.angle[j])) * gij;
            return std::sqrt(reI * reI + imI * imI);
        }
        default: {
            return 0.0;
        }
    }
}

std::vector<double> StateEstimator::computeMeasurements(const StateVector& state) const {
    return computeMeasurementVector(state);
}

std::vector<double> StateEstimator::computeMeasurementVector(const StateVector& state) const {
    std::vector<double> h(m_measurements.size());
    for (size_t i = 0; i < m_measurements.size(); ++i) {
        h[i] = computeMeasurement(m_measurements[i], state);
    }
    return h;
}

// ---------------------------------------------------------------------------
// Jacobian Computation
// ---------------------------------------------------------------------------

SparseJacobian StateEstimator::computeJacobian(const StateVector& state) const {
    SparseJacobian jac;
    size_t nM = m_measurements.size();
    size_t nB = m_buses.size();
    jac.numRows = static_cast<uint32_t>(nM);
    jac.numCols = static_cast<uint32_t>(2 * nB - 1); // All |V| and all angles except slack

    double delta = 1e-6;  // Finite difference step

    for (size_t mIdx = 0; mIdx < nM; ++mIdx) {
        const auto& m = m_measurements[mIdx];
        double h0 = computeMeasurement(m, state);

        // Partial derivatives with respect to voltage magnitudes
        for (uint32_t vIdx = 0; vIdx < nB; ++vIdx) {
            if (m_buses[vIdx].type == BusType::Slack) continue;

            StateVector perturbed = state;
            perturbed.voltage[vIdx] += delta;
            double hPerturbed = computeMeasurement(m, perturbed);
            double dh_dV = (hPerturbed - h0) / delta;

            if (std::abs(dh_dV) > 1e-12) {
                jac.add(static_cast<uint32_t>(mIdx), vIdx, dh_dV);
            }
        }

        // Partial derivatives with respect to voltage angles
        for (uint32_t aIdx = 0; aIdx < nB; ++aIdx) {
            if (aIdx == static_cast<uint32_t>(m_slackBusIndex)) continue;
            if (m_buses[aIdx].type == BusType::Slack) continue;

            StateVector perturbed = state;
            perturbed.angle[aIdx] += delta;
            double hPerturbed = computeMeasurement(m, perturbed);
            double dh_dA = (hPerturbed - h0) / delta;

            if (std::abs(dh_dA) > 1e-12) {
                uint32_t col = static_cast<uint32_t>(nB + aIdx - (aIdx > static_cast<uint32_t>(m_slackBusIndex) ? 1 : 0));
                jac.add(static_cast<uint32_t>(mIdx), col, dh_dA);
            }
        }
    }

    return jac;
}

std::vector<std::vector<double>> StateEstimator::computeJacobianDense(const StateVector& state) const {
    size_t nM = m_measurements.size();
    size_t nB = m_buses.size();
    size_t nStates = 2 * nB - 1;  // All |V| and all angles except slack

    std::vector<std::vector<double>> H(nM, std::vector<double>(nStates, 0.0));
    double delta = 1e-6;

    for (size_t mIdx = 0; mIdx < nM; ++mIdx) {
        const auto& m = m_measurements[mIdx];
        double h0 = computeMeasurement(m, state);

        // Voltage magnitude derivatives
        for (uint32_t vIdx = 0; vIdx < nB; ++vIdx) {
            if (m_buses[vIdx].type == BusType::Slack) continue;

            StateVector perturbed = state;
            perturbed.voltage[vIdx] += delta;
            double hPerturbed = computeMeasurement(m, perturbed);
            H[mIdx][vIdx] = (hPerturbed - h0) / delta;
        }

        // Angle derivatives
        for (uint32_t aIdx = 0; aIdx < nB; ++aIdx) {
            if (aIdx == static_cast<uint32_t>(m_slackBusIndex)) continue;
            if (m_buses[aIdx].type == BusType::Slack) continue;

            StateVector perturbed = state;
            perturbed.angle[aIdx] += delta;
            double hPerturbed = computeMeasurement(m, perturbed);
            uint32_t col = static_cast<uint32_t>(nB + aIdx - (aIdx > static_cast<uint32_t>(m_slackBusIndex) ? 1 : 0));
            H[mIdx][col] = (hPerturbed - h0) / delta;
        }
    }

    return H;
}

// ---------------------------------------------------------------------------
// Residuals
// ---------------------------------------------------------------------------

std::vector<double> StateEstimator::computeResiduals(const StateVector& state) const {
    std::vector<double> r(m_measurements.size());
    for (size_t i = 0; i < m_measurements.size(); ++i) {
        double h = computeMeasurement(m_measurements[i], state);
        r[i] = m_measurements[i].value - h;
    }
    return r;
}

double StateEstimator::computeObjectiveFunction(const StateVector& state) const {
    double J = 0.0;
    for (size_t i = 0; i < m_measurements.size(); ++i) {
        double h = computeMeasurement(m_measurements[i], state);
        double r = m_measurements[i].value - h;
        J += m_measurements[i].weight * r * r;
    }
    return J;
}

// ---------------------------------------------------------------------------
// Linear System Solvers
// ---------------------------------------------------------------------------

void StateEstimator::solveLinearSystemLU(std::vector<std::vector<double>>& A,
                                           std::vector<double>& b) {
    size_t n = A.size();
    if (n == 0) return;

    // Gaussian elimination with partial pivoting
    for (size_t k = 0; k < n; ++k) {
        // Partial pivoting
        size_t maxRow = k;
        double maxVal = std::abs(A[k][k]);
        for (size_t i = k + 1; i < n; ++i) {
            if (std::abs(A[i][k]) > maxVal) {
                maxVal = std::abs(A[i][k]);
                maxRow = i;
            }
        }

        if (maxVal < m_config.minSingularValue) {
            // Singular or near-singular matrix
            continue;
        }

        // Swap rows
        if (maxRow != k) {
            std::swap(A[k], A[maxRow]);
            std::swap(b[k], b[maxRow]);
        }

        // Eliminate
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
        for (size_t j = static_cast<size_t>(i) + 1; j < n; ++j) {
            b[i] -= A[i][j] * b[j];
        }
        if (std::abs(A[i][i]) > m_config.minSingularValue) {
            b[i] /= A[i][i];
        }
    }
}

void StateEstimator::solveLinearSystemCholesky(std::vector<std::vector<double>>& A,
                                                  std::vector<double>& b) {
    size_t n = A.size();
    if (n == 0) return;

    // LDL^T decomposition (more stable than pure Cholesky)
    std::vector<double> d(n);
    std::vector<std::vector<double>> L(n, std::vector<double>(n, 0.0));

    for (size_t i = 0; i < n; ++i) {
        L[i][i] = 1.0;
        for (size_t j = 0; j < i; ++j) {
            double sum = A[i][j];
            for (size_t k = 0; k < j; ++k) {
                sum -= L[i][k] * L[j][k] * d[k];
            }
            L[i][j] = sum / d[j];
        }

        d[i] = A[i][i];
        for (size_t k = 0; k < i; ++k) {
            d[i] -= L[i][k] * L[i][k] * d[k];
        }

        // Regularization for near-zero pivots
        if (std::abs(d[i]) < m_config.minSingularValue) {
            d[i] = m_config.minSingularValue;
        }
    }

    // Forward substitution: L * y = b
    std::vector<double> y = b;
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < i; ++j) {
            y[i] -= L[i][j] * y[j];
        }
    }

    // Diagonal scaling: D * z = y
    for (size_t i = 0; i < n; ++i) {
        y[i] /= d[i];
    }

    // Back substitution: L^T * x = z
    for (int i = static_cast<int>(n) - 1; i >= 0; --i) {
        for (size_t j = static_cast<size_t>(i) + 1; j < n; ++j) {
            y[i] -= L[j][i] * y[j];
        }
    }

    b = std::move(y);
}

void StateEstimator::solveNormalEquations(const std::vector<std::vector<double>>& H,
                                            const std::vector<double>& rhs,
                                            std::vector<double>& dx) {
    size_t nStates = H[0].size();
    size_t nMeas = H.size();

    // Compute A = H^T * W * H
    std::vector<std::vector<double>> A(nStates, std::vector<double>(nStates, 0.0));
    for (size_t i = 0; i < nStates; ++i) {
        for (size_t j = 0; j < nStates; ++j) {
            double sum = 0.0;
            for (size_t k = 0; k < nMeas; ++k) {
                sum += H[k][i] * m_measurements[k].weight * H[k][j];
            }
            A[i][j] = sum;
        }
    }

    // Regularization for numerical stability
    for (size_t i = 0; i < nStates; ++i) {
        A[i][i] += 1e-8;
    }

    // Compute b = H^T * W * rhs
    dx.resize(nStates);
    for (size_t i = 0; i < nStates; ++i) {
        double sum = 0.0;
        for (size_t k = 0; k < nMeas; ++k) {
            sum += H[k][i] * m_measurements[k].weight * rhs[k];
        }
        dx[i] = sum;
    }

    // Solve using Cholesky
    solveLinearSystemCholesky(A, dx);
}

// ---------------------------------------------------------------------------
// Initial State
// ---------------------------------------------------------------------------

StateVector StateEstimator::getInitialState() const {
    StateVector state;
    size_t n = m_buses.size();
    state.voltage.resize(n);
    state.angle.resize(n);

    if (m_config.useFlatStart) {
        for (uint32_t i = 0; i < n; ++i) {
            if (m_buses[i].type == BusType::Slack) {
                state.voltage[i] = m_buses[i].voltage;
                state.angle[i] = m_buses[i].angle;
            } else if (m_buses[i].type == BusType::PV) {
                state.voltage[i] = m_buses[i].voltage;
                state.angle[i] = 0.0;
            } else {
                state.voltage[i] = 1.0;
                state.angle[i] = 0.0;
            }
        }
    } else {
        for (uint32_t i = 0; i < n; ++i) {
            state.voltage[i] = m_buses[i].voltage;
            state.angle[i] = m_buses[i].angle;
        }
    }

    return state;
}

// ---------------------------------------------------------------------------
// WLS Estimation
// ---------------------------------------------------------------------------

WLSResult StateEstimator::estimate() {
    return estimate(getInitialState());
}

WLSResult StateEstimator::estimateFlatStart() {
    return estimate(getInitialState());
}

WLSResult StateEstimator::estimate(const StateVector& initialState) {
    WLSResult result;
    m_lastError.clear();

    validateSystem();
    if (!isSystemValid()) {
        result.errorMessage = m_lastError.empty() ? "System not valid" : m_lastError;
        return result;
    }

    if (m_measurements.empty()) {
        result.errorMessage = "No measurements";
        return result;
    }

    size_t nB = m_buses.size();
    size_t nM = m_measurements.size();
    size_t nStates = 2 * nB - 1;  // All |V| and all angles except slack

    if (nStates == 0) {
        result.errorMessage = "No state variables";
        return result;
    }

    if (nM < nStates) {
        result.errorMessage = "Not enough measurements for observability";
        return result;
    }

    StateVector state = initialState;
    result.state = state;

    // Iterative WLS
    for (int iter = 0; iter < m_config.maxIterations; ++iter) {
        // Compute measurements h(x)
        std::vector<double> h = computeMeasurementVector(state);

        // Compute residuals: z - h(x)
        std::vector<double> rhs(nM);
        for (size_t i = 0; i < nM; ++i) {
            rhs[i] = m_measurements[i].value - h[i];
        }

        // Compute Jacobian
        std::vector<std::vector<double>> H = computeJacobianDense(state);

        // Store Jacobian in result
        SparseJacobian sparseJac;
        for (size_t i = 0; i < nM; ++i) {
            for (size_t j = 0; j < nStates; ++j) {
                if (std::abs(H[i][j]) > 1e-12) {
                    sparseJac.add(static_cast<uint32_t>(i), static_cast<uint32_t>(j), H[i][j]);
                }
            }
        }
        result.jacobian = sparseJac;

        // Solve normal equations: (H^T * W * H) * dx = H^T * W * (z - h)
        std::vector<double> dx;
        solveNormalEquations(H, rhs, dx);

        // Update state
        uint32_t vCol = 0;
        for (uint32_t i = 0; i < nB; ++i) {
            if (m_buses[i].type == BusType::Slack) continue;
            state.voltage[i] += dx[vCol++];
        }

        uint32_t aCol = 0;
        for (uint32_t i = 0; i < nB; ++i) {
            if (static_cast<int>(i) == m_slackBusIndex) continue;
            if (m_buses[i].type == BusType::Slack) continue;
            state.angle[i] += dx[nB - 1 + aCol];
            aCol++;
        }

        // Compute convergence
        double maxDx = 0.0;
        for (double d : dx) {
            maxDx = std::max(maxDx, std::abs(d));
        }

        result.iterations = iter + 1;

        if (maxDx < m_config.tolerance) {
            result.converged = true;
            result.state = state;
            result.residuals = computeResiduals(state);
            result.objectiveFunction = computeObjectiveFunction(state);

            // Compute normalized residuals
            result.normalizedResiduals.resize(nM);
            result.badDataDetected.resize(nM, false);
            result.maxNormalizedResidual = 0.0;
            result.maxResidualIndex = -1;

            for (size_t i = 0; i < nM; ++i) {
                result.normalizedResiduals[i] = result.residuals[i] / m_measurements[i].sigma;
                if (std::abs(result.normalizedResiduals[i]) > result.maxNormalizedResidual) {
                    result.maxNormalizedResidual = std::abs(result.normalizedResiduals[i]);
                    result.maxResidualIndex = static_cast<int>(i);
                }
                if (std::abs(result.normalizedResiduals[i]) > m_config.badDataThreshold) {
                    result.badDataDetected[i] = true;
                }
            }

            // Chi-square test
            result.chiSquare = result.objectiveFunction;

            // Bad data detection
            if (m_config.enableBadDataDetection) {
                BadDataResult bd = detectBadData(result);
                if (bd.badDataFound) {
                    // Re-estimate with bad data removed
                    if (m_config.removeBadData) {
                        for (int idx : bd.suspectMeasurements) {
                            if (idx >= 0 && static_cast<size_t>(idx) < m_measurements.size()) {
                                m_measurements[idx].isActive = false;
                            }
                        }

                        // Re-estimate
                        auto removedMeasurements = m_measurements;
                        m_measurements.erase(
                            std::remove_if(m_measurements.begin(), m_measurements.end(),
                                [](const Measurement& m) { return !m.isActive; }),
                            m_measurements.end()
                        );

                        if (!m_measurements.empty()) {
                            result = estimate(state);
                        }

                        m_measurements = std::move(removedMeasurements);
                    }
                }
            }

            return result;
        }
    }

    // Did not converge
    result.converged = false;
    result.state = state;
    result.errorMessage = "Did not converge in " + std::to_string(m_config.maxIterations) + " iterations";
    result.residuals = computeResiduals(state);
    result.objectiveFunction = computeObjectiveFunction(state);
    return result;
}

// ---------------------------------------------------------------------------
// Bad Data Detection
// ---------------------------------------------------------------------------

BadDataResult StateEstimator::detectBadData(const WLSResult& result) const {
    return detectBadData(result.normalizedResiduals);
}

BadDataResult StateEstimator::detectBadData(const std::vector<double>& normalizedResiduals) const {
    BadDataResult bd;
    bd.threshold = m_config.badDataThreshold;
    bd.normalizedResiduals = normalizedResiduals;

    bd.maxNormalizedResidual = 0.0;
    bd.worstMeasurementIndex = -1;

    for (size_t i = 0; i < normalizedResiduals.size(); ++i) {
        double absR = std::abs(normalizedResiduals[i]);
        if (absR > bd.threshold) {
            bd.suspectMeasurements.push_back(static_cast<int>(i));
            bd.badDataFound = true;
        }
        if (absR > bd.maxNormalizedResidual) {
            bd.maxNormalizedResidual = absR;
            bd.worstMeasurementIndex = static_cast<int>(i);
        }
    }

    return bd;
}

bool StateEstimator::removeBadDataMeasurement(size_t measurementIndex) {
    if (measurementIndex >= m_measurements.size()) return false;
    m_measurements[measurementIndex].isActive = false;
    return true;
}

// ---------------------------------------------------------------------------
// Covariance Matrix
// ---------------------------------------------------------------------------

std::vector<std::vector<double>> StateEstimator::computeCovarianceMatrix(
    const SparseJacobian& jacobian) const {

    size_t nM = m_measurements.size();
    size_t nS = jacobian.numCols;

    // Build dense Jacobian from sparse
    std::vector<std::vector<double>> H(nM, std::vector<double>(nS, 0.0));
    for (size_t i = 0; i < jacobian.rowIndices.size(); ++i) {
        H[jacobian.rowIndices[i]][jacobian.colIndices[i]] = jacobian.values[i];
    }

    // G = H^T * W * H
    std::vector<std::vector<double>> G(nS, std::vector<double>(nS, 0.0));
    for (size_t i = 0; i < nS; ++i) {
        for (size_t j = 0; j < nS; ++j) {
            double sum = 0.0;
            for (size_t k = 0; k < nM; ++k) {
                sum += H[k][i] * m_measurements[k].weight * H[k][j];
            }
            G[i][j] = sum;
        }
    }

    // Regularize
    for (size_t i = 0; i < nS; ++i) {
        G[i][i] += 1e-8;
    }

    // Compute G_inv via Gaussian elimination
    size_t n = G.size();
    std::vector<std::vector<double>> inv(n, std::vector<double>(n, 0.0));
    for (size_t i = 0; i < n; ++i) {
        inv[i][i] = 1.0;
    }

    for (size_t k = 0; k < n; ++k) {
        double pivot = G[k][k];
        if (std::abs(pivot) < 1e-12) {
            pivot = 1e-12;
        }

        for (size_t j = 0; j < n; ++j) {
            G[k][j] /= pivot;
            inv[k][j] /= pivot;
        }

        for (size_t i = 0; i < n; ++i) {
            if (i == k) continue;
            double factor = G[i][k];
            for (size_t j = 0; j < n; ++j) {
                G[i][j] -= factor * G[k][j];
                inv[i][j] -= factor * inv[k][j];
            }
        }
    }

    return inv;
}

// ============================================================================
// Matrix Utilities
// ============================================================================

namespace MatrixUtils {

std::vector<double> matVecMul(const std::vector<std::vector<double>>& A,
                               const std::vector<double>& x) {
    std::vector<double> y(A.size(), 0.0);
    for (size_t i = 0; i < A.size(); ++i) {
        for (size_t j = 0; j < x.size() && j < A[i].size(); ++j) {
            y[i] += A[i][j] * x[j];
        }
    }
    return y;
}

std::vector<std::vector<double>> transpose(const std::vector<std::vector<double>>& A) {
    if (A.empty()) return {};
    size_t rows = A.size();
    size_t cols = A[0].size();
    std::vector<std::vector<double>> AT(cols, std::vector<double>(rows));
    for (size_t i = 0; i < rows; ++i) {
        for (size_t j = 0; j < cols; ++j) {
            AT[j][i] = A[i][j];
        }
    }
    return AT;
}

std::vector<std::vector<double>> matMul(const std::vector<std::vector<double>>& A,
                                         const std::vector<std::vector<double>>& B) {
    if (A.empty() || B.empty() || A[0].size() != B.size()) return {};
    size_t m = A.size();
    size_t n = B[0].size();
    size_t p = B.size();
    std::vector<std::vector<double>> C(m, std::vector<double>(n, 0.0));
    for (size_t i = 0; i < m; ++i) {
        for (size_t j = 0; j < n; ++j) {
            for (size_t k = 0; k < p; ++k) {
                C[i][j] += A[i][k] * B[k][j];
            }
        }
    }
    return C;
}

void addIdentity(std::vector<std::vector<double>>& A, double lambda) {
    for (size_t i = 0; i < A.size() && i < A[i].size(); ++i) {
        A[i][i] += lambda;
    }
}

std::string toString(const std::vector<std::vector<double>>& A, int precision) {
    std::ostringstream oss;
    oss << std::scientific << std::setprecision(precision);
    for (const auto& row : A) {
        for (const auto& val : row) {
            oss << val << " ";
        }
        oss << "\n";
    }
    return oss.str();
}

} // namespace MatrixUtils

} // namespace powsys365::simulation
