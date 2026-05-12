#include "contingency_analyzer.h"

#include <cmath>
#include <algorithm>
#include <numeric>
#include <sstream>
#include <chrono>
#include <iostream>

namespace powsys365::simulation {

// ============================================================================
// Contingency Implementation
// ============================================================================

Contingency::Contingency(uint32_t cid, ContingencyType ctype, uint32_t elemId,
                          std::string cname, std::string desc)
    : id(cid), type(ctype), elementId(elemId),
      name(std::move(cname)), description(std::move(desc)) {}

Contingency::Contingency(uint32_t cid, ContingencyType ctype, uint32_t elemId1, uint32_t elemId2,
                          std::string cname, std::string desc)
    : id(cid), type(ctype), elementId(elemId1), secondaryElementId(elemId2),
      name(std::move(cname)), description(std::move(desc)) {}

// ============================================================================
// ContingencyAnalyzer Implementation
// ============================================================================

ContingencyAnalyzer::ContingencyAnalyzer() = default;
ContingencyAnalyzer::ContingencyAnalyzer(const ContingencyConfig& config) : m_config(config) {}
ContingencyAnalyzer::ContingencyAnalyzer(ContingencyAnalyzer&&) noexcept = default;
ContingencyAnalyzer& ContingencyAnalyzer::operator=(ContingencyAnalyzer&&) noexcept = default;

// ---------------------------------------------------------------------------
// System Setup
// ---------------------------------------------------------------------------

void ContingencyAnalyzer::setBuses(const std::vector<BusData>& buses) {
    m_buses = buses;
    m_ybusValid = false;
    m_busIndex.clear();
}

void ContingencyAnalyzer::setBranches(const std::vector<BranchData>& branches) {
    m_branches = branches;
    m_ybusValid = false;
}

void ContingencyAnalyzer::setGenerators(const std::vector<BusData>& generators) {
    m_generators = generators;
}

void ContingencyAnalyzer::addBus(const BusData& bus) {
    m_buses.push_back(bus);
    m_ybusValid = false;
}

void ContingencyAnalyzer::addBranch(const BranchData& branch) {
    m_branches.push_back(branch);
    m_ybusValid = false;
}

void ContingencyAnalyzer::addGenerator(const BusData& generator) {
    m_generators.push_back(generator);
}

void ContingencyAnalyzer::clearBuses() {
    m_buses.clear();
    m_ybusValid = false;
    m_busIndex.clear();
}

void ContingencyAnalyzer::clearBranches() {
    m_branches.clear();
    m_ybusValid = false;
}

void ContingencyAnalyzer::clearGenerators() {
    m_generators.clear();
}

void ContingencyAnalyzer::clearAll() {
    m_buses.clear();
    m_branches.clear();
    m_generators.clear();
    m_contingencies.clear();
    m_ybusG.clear();
    m_ybusB.clear();
    m_ybusValid = false;
    m_busIndex.clear();
    m_slackBus = -1;
    m_baseCase = PowerFlowResult{};
}

void ContingencyAnalyzer::setConfig(const ContingencyConfig& config) { m_config = config; }
const ContingencyConfig& ContingencyAnalyzer::config() const noexcept { return m_config; }

size_t ContingencyAnalyzer::numBuses() const noexcept { return m_buses.size(); }
size_t ContingencyAnalyzer::numBranches() const noexcept { return m_branches.size(); }
size_t ContingencyAnalyzer::numGenerators() const noexcept { return m_generators.size(); }
size_t ContingencyAnalyzer::numContingencies() const noexcept { return m_contingencies.size(); }
const std::string& ContingencyAnalyzer::lastError() const noexcept { return m_lastError; }

// ---------------------------------------------------------------------------
// Bus Index Mapping
// ---------------------------------------------------------------------------

void ContingencyAnalyzer::buildBusIndex() {
    m_busIndex.clear();
    m_slackBus = -1;

    for (uint32_t i = 0; i < m_buses.size(); ++i) {
        m_busIndex[m_buses[i].busId] = i;
        if (m_buses[i].type == BusType::Slack) {
            m_slackBus = static_cast<int>(i);
        }
    }

    if (m_slackBus < 0 && !m_buses.empty()) {
        m_slackBus = 0;
        m_buses[0].type = BusType::Slack;
    }
}

uint32_t ContingencyAnalyzer::getBusIdx(uint32_t busId) const {
    auto it = m_busIndex.find(busId);
    return (it != m_busIndex.end()) ? it->second : 0;
}

// ---------------------------------------------------------------------------
// Y-Bus Matrix Construction
// ---------------------------------------------------------------------------

void ContingencyAnalyzer::buildYBus() {
    size_t n = m_buses.size();
    m_ybusG.assign(n, std::vector<double>(n, 0.0));
    m_ybusB.assign(n, std::vector<double>(n, 0.0));

    for (const auto& branch : m_branches) {
        if (!branch.inService) continue;

        uint32_t i = getBusIdx(branch.fromBus);
        uint32_t j = getBusIdx(branch.toBus);
        if (i == j) continue;
        if (i >= n || j >= n) continue;

        double z2 = branch.r * branch.r + branch.x * branch.x;
        if (z2 < 1e-12) continue;

        double g = branch.r / z2;
        double b = -branch.x / z2;

        double tap = branch.tapRatio;
        double shift = branch.phaseShift;

        if (tap != 1.0 || shift != 0.0) {
            double tap2 = tap * tap;
            double cs = std::cos(shift);
            double ss = std::sin(shift);

            m_ybusG[i][i] += g / tap2;
            m_ybusB[i][i] += (b + branch.b * 0.5) / tap2;
            m_ybusG[j][j] += g;
            m_ybusB[j][j] += b + branch.b * 0.5;

            double gOff = (g * cs - b * ss) / tap;
            double bOff = (g * ss + b * cs) / tap;
            m_ybusG[i][j] -= gOff;
            m_ybusB[i][j] -= bOff;
            m_ybusG[j][i] -= (g * cs + b * ss) / tap;
            m_ybusB[j][i] -= (-g * ss + b * cs) / tap;
        } else {
            m_ybusG[i][j] -= g;
            m_ybusB[i][j] -= b;
            m_ybusG[j][i] -= g;
            m_ybusB[j][i] -= b;

            m_ybusG[i][i] += g;
            m_ybusB[i][i] += b + branch.b * 0.5;
            m_ybusG[j][j] += g;
            m_ybusB[j][j] += b + branch.b * 0.5;
        }
    }

    // Add shunt elements
    for (uint32_t i = 0; i < n; ++i) {
        m_ybusG[i][i] += m_buses[i].gShunt;
        m_ybusB[i][i] += m_buses[i].bShunt;
    }

    m_ybusValid = true;
}

// ---------------------------------------------------------------------------
// Injection Computation
// ---------------------------------------------------------------------------

void ContingencyAnalyzer::computeInjections(const std::vector<double>& V,
                                             const std::vector<double>& theta,
                                             std::vector<double>& P,
                                             std::vector<double>& Q) {
    size_t n = m_buses.size();
    P.assign(n, 0.0);
    Q.assign(n, 0.0);

    for (uint32_t i = 0; i < n; ++i) {
        for (uint32_t j = 0; j < n; ++j) {
            double theta_ij = theta[i] - theta[j];
            P[i] += V[i] * V[j] * (m_ybusG[i][j] * std::cos(theta_ij) + m_ybusB[i][j] * std::sin(theta_ij));
            Q[i] += V[i] * V[j] * (m_ybusG[i][j] * std::sin(theta_ij) - m_ybusB[i][j] * std::cos(theta_ij));
        }
    }
}

// ---------------------------------------------------------------------------
// Power Flow Jacobian
// ---------------------------------------------------------------------------

void ContingencyAnalyzer::computeJacobianPF(const std::vector<double>& V,
                                             const std::vector<double>& theta,
                                             std::vector<std::vector<double>>& J) {
    size_t n = m_buses.size();

    // Count non-slack buses
    uint32_t nPV = 0;
    uint32_t nPQ = 0;
    std::vector<uint32_t> pqIdx;
    std::vector<uint32_t> pvIdx;

    for (uint32_t i = 0; i < n; ++i) {
        if (m_buses[i].type == BusType::PQ) {
            pqIdx.push_back(i);
            nPQ++;
        } else if (m_buses[i].type == BusType::PV) {
            pvIdx.push_back(i);
            nPV++;
        }
    }

    size_t nStates = nPQ * 2 + nPV;
    J.assign(nStates, std::vector<double>(nStates, 0.0));

    std::vector<double> P(n), Q(n);
    computeInjections(V, theta, P, Q);

    auto getRow = [&](uint32_t busIdx, bool isP) -> uint32_t {
        if (m_buses[busIdx].type == BusType::PQ) {
            auto it = std::find(pqIdx.begin(), pqIdx.end(), busIdx);
            uint32_t pqPos = static_cast<uint32_t>(it - pqIdx.begin());
            return isP ? pqPos : (pqPos + nPQ + nPV);
        } else if (m_buses[busIdx].type == BusType::PV) {
            auto it = std::find(pvIdx.begin(), pvIdx.end(), busIdx);
            return static_cast<uint32_t>(it - pvIdx.begin()) + nPQ;
        }
        return 0;
    };

    auto getColAngle = [&](uint32_t busIdx) -> uint32_t {
        if (m_buses[busIdx].type == BusType::PQ) {
            auto it = std::find(pqIdx.begin(), pqIdx.end(), busIdx);
            return static_cast<uint32_t>(it - pqIdx.begin());
        } else if (m_buses[busIdx].type == BusType::PV) {
            auto it = std::find(pvIdx.begin(), pvIdx.end(), busIdx);
            return static_cast<uint32_t>(it - pvIdx.begin()) + nPQ;
        }
        return 0;
    };

    auto getColVoltage = [&](uint32_t busIdx) -> uint32_t {
        auto it = std::find(pqIdx.begin(), pqIdx.end(), busIdx);
        return static_cast<uint32_t>(it - pqIdx.begin()) + nPQ + nPV;
    };

    // Build Jacobian submatrices
    for (uint32_t iIdx = 0; iIdx < n; ++iIdx) {
        if (m_buses[iIdx].type == BusType::Slack) continue;

        // dP/dtheta and dP/dV
        if (m_buses[iIdx].type == BusType::PQ || m_buses[iIdx].type == BusType::PV) {
            uint32_t rowP = getRow(iIdx, true);

            for (uint32_t jIdx = 0; jIdx < n; ++jIdx) {
                if (m_buses[jIdx].type == BusType::Slack) continue;
                if (jIdx == iIdx) continue;

                double theta_ij = theta[iIdx] - theta[jIdx];
                double Gij = m_ybusG[iIdx][jIdx];
                double Bij = m_ybusB[iIdx][jIdx];
                uint32_t colTheta = getColAngle(jIdx);

                J[rowP][colTheta] = V[iIdx] * V[jIdx] * (Gij * std::sin(theta_ij) - Bij * std::cos(theta_ij));

                if (m_buses[jIdx].type == BusType::PQ) {
                    uint32_t colV = getColVoltage(jIdx);
                    J[rowP][colV] = V[iIdx] * (Gij * std::cos(theta_ij) + Bij * std::sin(theta_ij));
                }
            }

            // Diagonal terms
            uint32_t colThetaI = getColAngle(iIdx);
            J[rowP][colThetaI] = -Q[iIdx] - V[iIdx] * V[iIdx] * m_ybusB[iIdx][iIdx];

            if (m_buses[iIdx].type == BusType::PQ) {
                uint32_t colVI = getColVoltage(iIdx);
                J[rowP][colVI] = P[iIdx] / V[iIdx] + V[iIdx] * m_ybusG[iIdx][iIdx];
            }
        }

        // dQ/dtheta and dQ/dV (only for PQ buses)
        if (m_buses[iIdx].type == BusType::PQ) {
            uint32_t rowQ = getRow(iIdx, false);

            for (uint32_t jIdx = 0; jIdx < n; ++jIdx) {
                if (m_buses[jIdx].type == BusType::Slack) continue;
                if (jIdx == iIdx) continue;

                double theta_ij = theta[iIdx] - theta[jIdx];
                double Gij = m_ybusG[iIdx][jIdx];
                double Bij = m_ybusB[iIdx][jIdx];
                uint32_t colTheta = getColAngle(jIdx);

                J[rowQ][colTheta] = -V[iIdx] * V[jIdx] * (Gij * std::cos(theta_ij) + Bij * std::sin(theta_ij));

                if (m_buses[jIdx].type == BusType::PQ) {
                    uint32_t colV = getColVoltage(jIdx);
                    J[rowQ][colV] = V[iIdx] * (Gij * std::sin(theta_ij) - Bij * std::cos(theta_ij));
                }
            }

            uint32_t colThetaI = getColAngle(iIdx);
            J[rowQ][colThetaI] = P[iIdx] - V[iIdx] * V[iIdx] * m_ybusG[iIdx][iIdx];

            uint32_t colVI = getColVoltage(iIdx);
            J[rowQ][colVI] = Q[iIdx] / V[iIdx] - V[iIdx] * m_ybusB[iIdx][iIdx];
        }
    }
}

// ---------------------------------------------------------------------------
// Newton-Raphson Power Flow
// ---------------------------------------------------------------------------

bool ContingencyAnalyzer::newtonRaphsonPF(const std::vector<BusData>& buses,
                                           const std::vector<BranchData>& branches,
                                           std::vector<double>& V,
                                           std::vector<double>& theta,
                                           int maxIter, double tol) {
    size_t n = buses.size();
    std::vector<double> P(n), Q(n);
    std::vector<double> dP, dQ;

    uint32_t nPQ = 0, nPV = 0;
    std::vector<uint32_t> pqIdx, pvIdx;
    for (uint32_t i = 0; i < n; ++i) {
        if (buses[i].type == BusType::PQ) {
            pqIdx.push_back(i);
            nPQ++;
        } else if (buses[i].type == BusType::PV) {
            pvIdx.push_back(i);
            nPV++;
        }
    }

    size_t nStates = nPQ * 2 + nPV;

    for (int iter = 0; iter < maxIter; ++iter) {
        computeInjections(V, theta, P, Q);

        // Compute mismatches
        std::vector<double> mismatches;
        mismatches.reserve(nStates);

        for (uint32_t idx : pqIdx) {
            double pSpec = buses[idx].pGen - buses[idx].pLoad;
            mismatches.push_back(pSpec - P[idx]);
        }
        for (uint32_t idx : pvIdx) {
            double pSpec = buses[idx].pGen - buses[idx].pLoad;
            mismatches.push_back(pSpec - P[idx]);
        }
        for (uint32_t idx : pqIdx) {
            double qSpec = buses[idx].qGen - buses[idx].qLoad;
            mismatches.push_back(qSpec - Q[idx]);
        }

        // Check convergence
        double maxMismatch = 0.0;
        for (double m : mismatches) {
            maxMismatch = std::max(maxMismatch, std::abs(m));
        }

        if (maxMismatch < tol) {
            return true;
        }

        // Build and solve Jacobian
        std::vector<std::vector<double>> J;
        computeJacobianPF(V, theta, J);

        if (J.empty() || J.size() != nStates) {
            return false;
        }

        // Solve J * dx = -mismatches using Gaussian elimination
        std::vector<double> rhs(nStates);
        for (size_t i = 0; i < nStates; ++i) {
            rhs[i] = mismatches[i];
        }

        // LU decomposition with partial pivoting
        for (size_t k = 0; k < nStates; ++k) {
            size_t maxRow = k;
            double maxVal = std::abs(J[k][k]);
            for (size_t i = k + 1; i < nStates; ++i) {
                if (std::abs(J[i][k]) > maxVal) {
                    maxVal = std::abs(J[i][k]);
                    maxRow = i;
                }
            }

            if (maxVal < 1e-12) {
                return false;  // Singular Jacobian
            }

            if (maxRow != k) {
                std::swap(J[k], J[maxRow]);
                std::swap(rhs[k], rhs[maxRow]);
            }

            for (size_t i = k + 1; i < nStates; ++i) {
                double factor = J[i][k] / J[k][k];
                for (size_t j = k; j < nStates; ++j) {
                    J[i][j] -= factor * J[k][j];
                }
                rhs[i] -= factor * rhs[k];
            }
        }

        // Back substitution
        std::vector<double> dx(nStates);
        for (int i = static_cast<int>(nStates) - 1; i >= 0; --i) {
            dx[i] = rhs[i];
            for (size_t j = static_cast<size_t>(i) + 1; j < nStates; ++j) {
                dx[i] -= J[i][j] * dx[j];
            }
            dx[i] /= J[i][i];
        }

        // Update state
        uint32_t pos = 0;
        for (uint32_t idx : pqIdx) {
            theta[idx] += dx[pos++];
        }
        for (uint32_t idx : pvIdx) {
            theta[idx] += dx[pos++];
        }
        for (uint32_t idx : pqIdx) {
            V[idx] += dx[pos++];
            if (V[idx] < 0.5) V[idx] = 0.5;
            if (V[idx] > 1.5) V[idx] = 1.5;
        }
    }

    return false;  // Did not converge
}

// ---------------------------------------------------------------------------
// Base Case Power Flow
// ---------------------------------------------------------------------------

PowerFlowResult ContingencyAnalyzer::solveBaseCase() {
    PowerFlowResult result;
    m_lastError.clear();

    if (m_buses.empty()) {
        m_lastError = "No buses defined";
        result.errorMessage = m_lastError;
        return result;
    }

    if (!m_ybusValid) {
        buildBusIndex();
        buildYBus();
    }

    size_t n = m_buses.size();
    std::vector<double> V(n);
    std::vector<double> theta(n);

    // Initialize
    for (uint32_t i = 0; i < n; ++i) {
        V[i] = m_buses[i].voltage;
        theta[i] = m_buses[i].angle;
    }

    if (newtonRaphsonPF(m_buses, m_branches, V, theta,
                         m_config.maxIterations, m_config.tolerance)) {
        result.converged = true;
        result.busVoltage = V;
        result.busAngle = theta;

        // Compute injections
        std::vector<double> P(n), Q(n);
        computeInjections(V, theta, P, Q);
        result.busP = P;
        result.busQ = Q;

        // Compute line flows
        result.linePFrom.resize(m_branches.size());
        result.lineQFrom.resize(m_branches.size());
        result.linePTo.resize(m_branches.size());
        result.lineQTo.resize(m_branches.size());
        result.lineLoading.resize(m_branches.size());

        for (size_t b = 0; b < m_branches.size(); ++b) {
            uint32_t fi = getBusIdx(m_branches[b].fromBus);
            uint32_t ti = getBusIdx(m_branches[b].toBus);

            result.linePFrom[b] = computeLineFlow(fi, ti, V[fi], V[ti], theta[fi], theta[ti], true);
            result.lineQFrom[b] = computeLineFlow(fi, ti, V[fi], V[ti], theta[fi], theta[ti], false);
            result.lineLoading[b] = computeLineLoading(b, V, theta);
        }

        result.totalLoss = computeTotalLosses(V, theta);

        double totalGen = 0.0, totalLoad = 0.0;
        for (const auto& bus : m_buses) {
            totalGen += bus.pGen;
            totalLoad += bus.pLoad;
        }
        result.totalGeneration = totalGen;
        result.totalLoad = totalLoad;
    } else {
        result.errorMessage = "Base case power flow did not converge";
        m_lastError = result.errorMessage;
    }

    m_baseCase = result;
    return result;
}

const PowerFlowResult& ContingencyAnalyzer::baseCaseResult() const noexcept {
    return m_baseCase;
}

// ---------------------------------------------------------------------------
// Line Flow Computation
// ---------------------------------------------------------------------------

double ContingencyAnalyzer::computeLineFlow(uint32_t fromIdx, uint32_t toIdx,
                                             double Vf, double Vt, double thetaf, double thetat,
                                             bool realPower) const {
    double g = m_ybusG[fromIdx][toIdx];
    double b = m_ybusB[fromIdx][toIdx];
    double thetaDiff = thetaf - thetat;

    if (realPower) {
        return Vf * Vf * (-g) + Vf * Vt * (g * std::cos(thetaDiff) + b * std::sin(thetaDiff));
    } else {
        return Vf * Vf * b + Vf * Vt * (g * std::sin(thetaDiff) - b * std::cos(thetaDiff));
    }
}

double ContingencyAnalyzer::computeLineLoading(uint32_t branchIdx,
                                                const std::vector<double>& V,
                                                const std::vector<double>& theta) const {
    if (branchIdx >= m_branches.size()) return 0.0;

    const BranchData& branch = m_branches[branchIdx];
    uint32_t fi = getBusIdx(branch.fromBus);
    uint32_t ti = getBusIdx(branch.toBus);

    double pf = computeLineFlow(fi, ti, V[fi], V[ti], theta[fi], theta[ti], true);
    double qf = computeLineFlow(fi, ti, V[fi], V[ti], theta[fi], theta[ti], false);
    double sFlow = std::sqrt(pf * pf + qf * qf);

    if (branch.rateA > 0) {
        return (sFlow / branch.rateA) * 100.0;
    }
    return 0.0;
}

double ContingencyAnalyzer::computeTotalLosses(const std::vector<double>& V,
                                                const std::vector<double>& theta) const {
    double losses = 0.0;
    for (const auto& branch : m_branches) {
        if (!branch.inService) continue;
        uint32_t fi = getBusIdx(branch.fromBus);
        uint32_t ti = getBusIdx(branch.toBus);

        double z2 = branch.r * branch.r + branch.x * branch.x;
        if (z2 < 1e-12) continue;

        double g = branch.r / z2;
        double thetaDiff = theta[fi] - theta[ti];
        losses += g * (V[fi] * V[fi] + V[ti] * V[ti] - 2.0 * V[fi] * V[ti] * std::cos(thetaDiff));
    }
    return losses;
}

// ---------------------------------------------------------------------------
// N-1 Contingency Screening
// ---------------------------------------------------------------------------

std::vector<ContingencyResult> ContingencyAnalyzer::screenN1() {
    if (!m_baseCase.converged) {
        solveBaseCase();
    }
    return screenN1(m_baseCase);
}

std::vector<ContingencyResult> ContingencyAnalyzer::screenN1(const PowerFlowResult& currentState) {
    std::vector<ContingencyResult> results;
    m_lastError.clear();

    if (!m_ybusValid) {
        buildBusIndex();
        buildYBus();
    }

    if (!currentState.converged) {
        m_lastError = "Base case not converged";
        return results;
    }

    // Line outages (N-1)
    uint32_t totalContingencies = static_cast<uint32_t>(m_branches.size() + m_generators.size());
    uint32_t currentCount = 0;

    for (uint32_t bIdx = 0; bIdx < m_branches.size() && bIdx < m_config.maxContingencies; ++bIdx) {
        if (!m_branches[bIdx].inService) continue;

        currentCount++;
        logProgress(currentCount, totalContingencies);

        auto result = analyzeLineOutageInternal(bIdx);
        result.contingencyId = bIdx;
        result.type = ContingencyType::LineOutage;
        result.contingencyName = "Line_" + std::to_string(m_branches[bIdx].fromBus) +
                                  "_" + std::to_string(m_branches[bIdx].toBus);
        results.push_back(result);
    }

    // Generator outages (N-1)
    for (uint32_t gIdx = 0; gIdx < m_generators.size(); ++gIdx) {
        currentCount++;
        logProgress(currentCount, totalContingencies);

        auto result = analyzeGeneratorOutageInternal(gIdx);
        result.contingencyId = static_cast<uint32_t>(m_branches.size()) + gIdx;
        result.type = ContingencyType::GeneratorOutage;
        result.contingencyName = "Generator_" + std::to_string(m_generators[gIdx].busId);
        results.push_back(result);
    }

    // Compute severity and rank
    if (m_config.rankBySeverity) {
        for (auto& r : results) {
            computeSeverity(r, currentState);
        }
    }

    return results;
}

// ---------------------------------------------------------------------------
// Individual Contingency Analysis
// ---------------------------------------------------------------------------

ContingencyResult ContingencyAnalyzer::checkLineOutage(uint32_t lineId) {
    for (uint32_t i = 0; i < m_branches.size(); ++i) {
        if (m_branches[i].branchId == lineId) {
            auto result = analyzeLineOutageInternal(i);
            result.contingencyId = lineId;
            result.type = ContingencyType::LineOutage;
            result.contingencyName = "Line_" + std::to_string(m_branches[i].fromBus) +
                                      "_" + std::to_string(m_branches[i].toBus);
            computeSeverity(result, m_baseCase);
            return result;
        }
    }

    ContingencyResult result;
    result.errorMessage = "Line not found: " + std::to_string(lineId);
    return result;
}

ContingencyResult ContingencyAnalyzer::checkLineOutage(uint32_t fromBus, uint32_t toBus) {
    for (uint32_t i = 0; i < m_branches.size(); ++i) {
        if (m_branches[i].fromBus == fromBus && m_branches[i].toBus == toBus) {
            auto result = analyzeLineOutageInternal(i);
            result.contingencyId = m_branches[i].branchId;
            result.type = ContingencyType::LineOutage;
            result.contingencyName = "Line_" + std::to_string(fromBus) + "_" + std::to_string(toBus);
            computeSeverity(result, m_baseCase);
            return result;
        }
    }

    ContingencyResult result;
    result.errorMessage = "Line not found between buses " + std::to_string(fromBus) +
                           " and " + std::to_string(toBus);
    return result;
}

ContingencyResult ContingencyAnalyzer::checkGeneratorOutage(uint32_t genId) {
    for (uint32_t i = 0; i < m_generators.size(); ++i) {
        if (m_generators[i].busId == genId) {
            auto result = analyzeGeneratorOutageInternal(i);
            result.contingencyId = genId;
            result.type = ContingencyType::GeneratorOutage;
            result.contingencyName = "Generator_" + std::to_string(genId);
            computeSeverity(result, m_baseCase);
            return result;
        }
    }

    ContingencyResult result;
    result.errorMessage = "Generator not found: " + std::to_string(genId);
    return result;
}

ContingencyResult ContingencyAnalyzer::checkTransformerOutage(uint32_t transformerId) {
    return checkLineOutage(transformerId);
}

ContingencyResult ContingencyAnalyzer::checkBusOutage(uint32_t busId) {
    ContingencyResult result;
    result.contingencyId = busId;
    result.type = ContingencyType::BusOutage;
    result.contingencyName = "Bus_" + std::to_string(busId);
    result.errorMessage = "Bus outage analysis not yet fully implemented";
    return result;
}

ContingencyResult ContingencyAnalyzer::checkContingency(const Contingency& contingency) {
    switch (contingency.type) {
        case ContingencyType::LineOutage:
            return checkLineOutage(contingency.elementId);
        case ContingencyType::GeneratorOutage:
            return checkGeneratorOutage(contingency.elementId);
        case ContingencyType::TransformerOutage:
            return checkTransformerOutage(contingency.elementId);
        case ContingencyType::BusOutage:
            return checkBusOutage(contingency.elementId);
        default:
            break;
    }

    ContingencyResult result;
    result.errorMessage = "Unsupported contingency type";
    return result;
}

// ---------------------------------------------------------------------------
// Internal Contingency Analysis
// ---------------------------------------------------------------------------

ContingencyResult ContingencyAnalyzer::analyzeLineOutageInternal(uint32_t branchIdx) {
    auto startTime = std::chrono::steady_clock::now();
    ContingencyResult result;

    if (branchIdx >= m_branches.size()) {
        result.errorMessage = "Invalid branch index";
        return result;
    }

    if (!m_ybusValid) {
        buildYBus();
    }

    // Create modified branch list with the line out of service
    std::vector<BranchData> modifiedBranches = m_branches;
    modifiedBranches[branchIdx].inService = false;

    // Rebuild Y-bus for contingency
    std::vector<std::vector<double>> origG = m_ybusG;
    std::vector<std::vector<double>> origB = m_ybusB;

    // Temporarily modify the branch
    bool origService = m_branches[branchIdx].inService;
    m_branches[branchIdx].inService = false;
    buildYBus();

    // Solve power flow with modified system
    size_t n = m_buses.size();
    std::vector<double> V(n);
    std::vector<double> theta(n);

    // Use base case as initial guess
    if (m_baseCase.converged) {
        V = m_baseCase.busVoltage;
        theta = m_baseCase.busAngle;
    } else {
        for (uint32_t i = 0; i < n; ++i) {
            V[i] = m_buses[i].voltage;
            theta[i] = m_buses[i].angle;
        }
    }

    bool converged = newtonRaphsonPF(m_buses, m_branches, V, theta,
                                      m_config.maxIterations, m_config.tolerance);

    // Restore original state
    m_branches[branchIdx].inService = origService;
    m_ybusG = std::move(origG);
    m_ybusB = std::move(origB);

    result.converged = converged;
    result.busVoltage = V;
    result.busAngle = theta;

    if (converged) {
        // Compute line loadings
        result.lineLoading.resize(m_branches.size());
        for (size_t b = 0; b < m_branches.size(); ++b) {
            result.lineLoading[b] = computeLineLoading(b, V, theta);
        }

        // Check violations
        result.maxVoltageDeviation = 0.0;
        result.lowVoltageBuses = 0;
        result.highVoltageBuses = 0;
        result.overloadedLines = 0;
        result.maxLineLoading = 0.0;

        if (m_baseCase.converged) {
            for (size_t i = 0; i < n; ++i) {
                double dev = std::abs(V[i] - m_baseCase.busVoltage[i]);
                result.maxVoltageDeviation = std::max(result.maxVoltageDeviation, dev);

                if (V[i] < m_config.voltageMinLimit) result.lowVoltageBuses++;
                if (V[i] > m_config.voltageMaxLimit) result.highVoltageBuses++;
            }

            for (size_t b = 0; b < m_branches.size(); ++b) {
                if (result.lineLoading[b] > m_config.lineLoadingLimit) result.overloadedLines++;
                result.maxLineLoading = std::max(result.maxLineLoading, result.lineLoading[b]);
            }
        }

        result.totalLoss = computeTotalLosses(V, theta);
        result.hasViolation = (result.lowVoltageBuses > 0) ||
                               (result.highVoltageBuses > 0) ||
                               (result.overloadedLines > 0);
    } else {
        result.errorMessage = "Power flow did not converge for line outage";
    }

    auto endTime = std::chrono::steady_clock::now();
    result.computationTime = std::chrono::duration<double>(endTime - startTime).count();

    return result;
}

ContingencyResult ContingencyAnalyzer::analyzeGeneratorOutageInternal(uint32_t genIdx) {
    auto startTime = std::chrono::steady_clock::now();
    ContingencyResult result;

    if (genIdx >= m_generators.size()) {
        result.errorMessage = "Invalid generator index";
        return result;
    }

    if (!m_ybusValid) {
        buildYBus();
    }

    // Create modified bus list with generator out
    std::vector<BusData> modifiedBuses = m_buses;
    uint32_t genBusIdx = getBusIdx(m_generators[genIdx].busId);

    if (genBusIdx < modifiedBuses.size()) {
        modifiedBuses[genBusIdx].pGen = 0.0;
        modifiedBuses[genBusIdx].qGen = 0.0;

        // If PV bus becomes PQ, change type
        if (modifiedBuses[genBusIdx].type == BusType::PV) {
            modifiedBuses[genBusIdx].type = BusType::PQ;
        }
    }

    // Redistribute lost generation to other generators
    double lostP = m_generators[genIdx].pGen;
    uint32_t nGens = 0;
    for (const auto& gen : m_generators) {
        uint32_t bidx = getBusIdx(gen.busId);
        if (bidx != genBusIdx && bidx < modifiedBuses.size() &&
            modifiedBuses[bidx].type != BusType::Slack) {
            nGens++;
        }
    }

    if (nGens > 0 && lostP > 0) {
        double deltaP = lostP / nGens;
        for (const auto& gen : m_generators) {
            uint32_t bidx = getBusIdx(gen.busId);
            if (bidx != genBusIdx && bidx < modifiedBuses.size() &&
                modifiedBuses[bidx].type != BusType::Slack) {
                modifiedBuses[bidx].pGen += deltaP;
            }
        }
    }

    size_t n = modifiedBuses.size();
    std::vector<double> V(n);
    std::vector<double> theta(n);

    if (m_baseCase.converged) {
        V = m_baseCase.busVoltage;
        theta = m_baseCase.busAngle;
    } else {
        for (uint32_t i = 0; i < n; ++i) {
            V[i] = modifiedBuses[i].voltage;
            theta[i] = modifiedBuses[i].angle;
        }
    }

    bool converged = newtonRaphsonPF(modifiedBuses, m_branches, V, theta,
                                      m_config.maxIterations, m_config.tolerance);

    result.converged = converged;
    result.busVoltage = V;
    result.busAngle = theta;

    if (converged) {
        result.lineLoading.resize(m_branches.size());
        for (size_t b = 0; b < m_branches.size(); ++b) {
            result.lineLoading[b] = computeLineLoading(b, V, theta);
        }

        result.maxVoltageDeviation = 0.0;
        result.lowVoltageBuses = 0;
        result.highVoltageBuses = 0;
        result.overloadedLines = 0;
        result.maxLineLoading = 0.0;

        if (m_baseCase.converged) {
            for (size_t i = 0; i < n; ++i) {
                double dev = std::abs(V[i] - m_baseCase.busVoltage[i]);
                result.maxVoltageDeviation = std::max(result.maxVoltageDeviation, dev);

                if (V[i] < m_config.voltageMinLimit) result.lowVoltageBuses++;
                if (V[i] > m_config.voltageMaxLimit) result.highVoltageBuses++;
            }

            for (size_t b = 0; b < m_branches.size(); ++b) {
                if (result.lineLoading[b] > m_config.lineLoadingLimit) result.overloadedLines++;
                result.maxLineLoading = std::max(result.maxLineLoading, result.lineLoading[b]);
            }
        }

        result.totalLoss = computeTotalLosses(V, theta);
        result.hasViolation = (result.lowVoltageBuses > 0) ||
                               (result.highVoltageBuses > 0) ||
                               (result.overloadedLines > 0);
    } else {
        result.errorMessage = "Power flow did not converge for generator outage";
    }

    auto endTime = std::chrono::steady_clock::now();
    result.computationTime = std::chrono::duration<double>(endTime - startTime).count();

    return result;
}

// ---------------------------------------------------------------------------
// Severity Computation and Ranking
// ---------------------------------------------------------------------------

void ContingencyAnalyzer::computeSeverity(ContingencyResult& result, const PowerFlowResult& base) const {
    double severity = 0.0;

    // Voltage severity
    if (result.maxVoltageDeviation > 0.0) {
        severity += m_config.severityWeightVoltage * result.maxVoltageDeviation * 10.0;
    }

    // Loading severity
    if (result.maxLineLoading > m_config.lineLoadingLimit) {
        double overload = (result.maxLineLoading - m_config.lineLoadingLimit) / 100.0;
        severity += m_config.severityWeightLoading * overload;
    }
    severity += m_config.severityWeightLoading * result.overloadedLines;

    // Voltage limit violations
    severity += 0.5 * (result.lowVoltageBuses + result.highVoltageBuses);

    // Losses severity
    if (base.totalLoss > 0 && result.totalLoss > 0) {
        double lossIncrease = (result.totalLoss - base.totalLoss) / base.totalLoss;
        severity += m_config.severityWeightLosses * std::max(0.0, lossIncrease);
    }

    // Convergence penalty
    if (!result.converged) {
        severity += 100.0;
    }

    // Performance Index (commonly used in industry)
    double pi = 0.0;
    for (size_t i = 0; i < result.lineLoading.size(); ++i) {
        if (m_branches[i].rateA > 0) {
            double loading = result.lineLoading[i] / 100.0;
            pi += loading * loading;
        }
    }

    result.severityIndex = severity;
    result.performanceIndex = pi;
}

double ContingencyAnalyzer::computeSeverityIndex(const ContingencyResult& result) const {
    return result.severityIndex;
}

std::vector<SeverityRanking> ContingencyAnalyzer::rankBySeverity(
    const std::vector<ContingencyResult>& results) const {

    std::vector<SeverityRanking> rankings;
    rankings.reserve(results.size());

    for (const auto& r : results) {
        SeverityRanking rank;
        rank.contingencyId = r.contingencyId;
        rank.name = r.contingencyName;
        rank.type = r.type;
        rank.severity = r.severityIndex;
        rank.maxVoltageDeviation = r.maxVoltageDeviation;
        rank.maxLineLoading = r.maxLineLoading;
        rank.overloadedLines = r.overloadedLines;
        rank.lowVoltageBuses = r.lowVoltageBuses;
        rank.highVoltageBuses = r.highVoltageBuses;
        rank.performanceIndex = r.performanceIndex;
        rankings.push_back(rank);
    }

    // Sort by severity (descending)
    std::sort(rankings.begin(), rankings.end(),
              [](const SeverityRanking& a, const SeverityRanking& b) {
                  return a.severity > b.severity;
              });

    // Assign ranks
    for (size_t i = 0; i < rankings.size(); ++i) {
        rankings[i].rank = static_cast<int>(i) + 1;
    }

    return rankings;
}

std::vector<SeverityRanking> ContingencyAnalyzer::getCriticalContingencies(
    const std::vector<ContingencyResult>& results, double threshold) const {

    auto rankings = rankBySeverity(results);

    std::vector<SeverityRanking> critical;
    double maxSeverity = rankings.empty() ? 1.0 : rankings[0].severity;

    for (const auto& r : rankings) {
        if (r.severity >= threshold * maxSeverity) {
            critical.push_back(r);
        }
    }

    return critical;
}

// ---------------------------------------------------------------------------
// Violation Checks
// ---------------------------------------------------------------------------

bool ContingencyAnalyzer::hasViolations(const ContingencyResult& result) const {
    return result.hasViolation || !result.converged;
}

std::vector<std::string> ContingencyAnalyzer::getViolationReport(const ContingencyResult& result) const {
    std::vector<std::string> report;

    if (!result.converged) {
        report.push_back("Power flow did not converge");
        return report;
    }

    if (result.lowVoltageBuses > 0) {
        report.push_back("Low voltage buses: " + std::to_string(result.lowVoltageBuses));
    }
    if (result.highVoltageBuses > 0) {
        report.push_back("High voltage buses: " + std::to_string(result.highVoltageBuses));
    }
    if (result.overloadedLines > 0) {
        report.push_back("Overloaded lines: " + std::to_string(result.overloadedLines));
    }
    if (result.maxVoltageDeviation > 0.1) {
        report.push_back("Large voltage deviation: " + std::to_string(result.maxVoltageDeviation));
    }

    return report;
}

// ---------------------------------------------------------------------------
// Progress
// ---------------------------------------------------------------------------

void ContingencyAnalyzer::logProgress(uint32_t current, uint32_t total) {
    if (m_config.progressCallback) {
        m_config.progressCallback(current, total);
    }
}

// ---------------------------------------------------------------------------
// Batch Analysis
// ---------------------------------------------------------------------------

std::vector<ContingencyResult> ContingencyAnalyzer::analyzeAllContingencies() {
    if (!m_baseCase.converged) {
        solveBaseCase();
    }
    return screenN1(m_baseCase);
}

std::vector<ContingencyResult> ContingencyAnalyzer::analyzeContingencyList(
    const std::vector<Contingency>& contingencies) {

    std::vector<ContingencyResult> results;

    if (!m_baseCase.converged) {
        solveBaseCase();
    }

    uint32_t total = static_cast<uint32_t>(contingencies.size());

    for (uint32_t i = 0; i < contingencies.size(); ++i) {
        logProgress(i + 1, total);
        auto result = checkContingency(contingencies[i]);
        results.push_back(result);
    }

    return results;
}

} // namespace powsys365::simulation
