#include "state_estimator_wls.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <random>

namespace powsys365 {

StateEstimatorWLS::StateEstimatorWLS()
{
    params_.num_buses = 0;
}

StateEstimatorWLS::StateEstimatorWLS(const WLSParameters& params)
    : params_(params)
{
}

void StateEstimatorWLS::setParameters(const WLSParameters& params)
{
    params_ = params;
}

void StateEstimatorWLS::setTopology(const SystemTopology& topology)
{
    topology_ = topology;
    params_.num_buses = topology.num_buses;
    topology_set_ = true;
}

void StateEstimatorWLS::addMeasurement(const Measurement& measurement)
{
    Measurement m = measurement;
    if (m.id < 0) {
        m.id = static_cast<int>(measurements_.size());
    }
    if (m.sigma > 0.0) {
        m.weight = 1.0 / (m.sigma * m.sigma);
    }
    if (m.is_pmu) {
        m.weight *= params_.pmu_weight_multiplier;
    }
    measurements_.push_back(m);
}

void StateEstimatorWLS::addMeasurements(const std::vector<Measurement>& measurements)
{
    for (const auto& m : measurements) {
        addMeasurement(m);
    }
}

void StateEstimatorWLS::clearMeasurements()
{
    measurements_.clear();
}

void StateEstimatorWLS::updateMeasurement(int measurement_id, double new_value)
{
    for (auto& m : measurements_) {
        if (m.id == measurement_id) {
            m.value = new_value;
            break;
        }
    }
}

Eigen::SparseMatrix<std::complex<double>> StateEstimatorWLS::buildYBus() const
{
    int n = params_.num_buses;
    std::vector<Eigen::Triplet<std::complex<double>>> triplets;

    for (int i = 0; i < n; ++i) {
        std::complex<double> y_shunt_i(0.0, 0.0);
        if (i < static_cast<int>(topology_.y_shunt.size())) {
            y_shunt_i = topology_.y_shunt[i];
        }
        triplets.emplace_back(i, i, y_shunt_i);
    }

    for (size_t k = 0; k < topology_.branches.size(); ++k) {
        int i = topology_.branches[k].first;
        int j = topology_.branches[k].second;
        if (i < 0 || j < 0 || i >= n || j >= n) continue;

        std::complex<double> y_series_k(0.0, 0.0);
        if (k < topology_.y_series.size()) {
            y_series_k = topology_.y_series[k];
        }

        // Y_ii acumula y_series, Y_ij = -y_series
        triplets.emplace_back(i, i, y_series_k);
        triplets.emplace_back(j, j, y_series_k);
        triplets.emplace_back(i, j, -y_series_k);
        triplets.emplace_back(j, i, -y_series_k);
    }

    Eigen::SparseMatrix<std::complex<double>> Ybus(n, n);
    Ybus.setFromTriplets(triplets.begin(), triplets.end());
    return Ybus;
}

std::complex<double> StateEstimatorWLS::getYBus(int i, int j) const
{
    static Eigen::SparseMatrix<std::complex<double>> Ybus;
    static bool built = false;
    if (!built) {
        Ybus = buildYBus();
        built = true;
    }
    // Access element - for sparse, use coeff
    if (i < Ybus.rows() && j < Ybus.cols()) {
        return Ybus.coeff(i, j);
    }
    return std::complex<double>(0.0, 0.0);
}

double StateEstimatorWLS::computePInjection(int bus_i,
    const std::vector<double>& vm,
    const std::vector<double>& va) const
{
    double p = 0.0;
    int n = params_.num_buses;
    if (bus_i >= n) return 0.0;

    for (int j = 0; j < n; ++j) {
        std::complex<double> Yij = getYBus(bus_i, j);
        double Gij = Yij.real();
        double Bij = Yij.imag();
        double theta_ij = va[bus_i] - va[j];
        p += vm[bus_i] * vm[j] * (Gij * std::cos(theta_ij) + Bij * std::sin(theta_ij));
    }
    return p;
}

double StateEstimatorWLS::computeQInjection(int bus_i,
    const std::vector<double>& vm,
    const std::vector<double>& va) const
{
    double q = 0.0;
    int n = params_.num_buses;
    if (bus_i >= n) return 0.0;

    for (int j = 0; j < n; ++j) {
        std::complex<double> Yij = getYBus(bus_i, j);
        double Gij = Yij.real();
        double Bij = Yij.imag();
        double theta_ij = va[bus_i] - va[j];
        q += vm[bus_i] * vm[j] * (Gij * std::sin(theta_ij) - Bij * std::cos(theta_ij));
    }
    return q;
}

double StateEstimatorWLS::computePFlow(int from, int to,
    const std::vector<double>& vm,
    const std::vector<double>& va) const
{
    if (from >= params_.num_buses || to >= params_.num_buses) return 0.0;
    std::complex<double> Y_series = getYBus(from, to);
    if (std::abs(Y_series) < 1e-12) {
        // Buscar en ramas
        for (size_t k = 0; k < topology_.branches.size(); ++k) {
            if (topology_.branches[k].first == from && topology_.branches[k].second == to) {
                if (k < topology_.y_series.size()) {
                    Y_series = topology_.y_series[k];
                }
                break;
            }
        }
    }
    double G = -Y_series.real();
    double B = -Y_series.imag();
    double theta = va[from] - va[to];
    return vm[from] * vm[from] * G - vm[from] * vm[to] * (G * std::cos(theta) + B * std::sin(theta));
}

double StateEstimatorWLS::computeQFlow(int from, int to,
    const std::vector<double>& vm,
    const std::vector<double>& va) const
{
    if (from >= params_.num_buses || to >= params_.num_buses) return 0.0;
    std::complex<double> Y_series = getYBus(from, to);
    if (std::abs(Y_series) < 1e-12) {
        for (size_t k = 0; k < topology_.branches.size(); ++k) {
            if (topology_.branches[k].first == from && topology_.branches[k].second == to) {
                if (k < topology_.y_series.size()) {
                    Y_series = topology_.y_series[k];
                }
                break;
            }
        }
    }
    double G = -Y_series.real();
    double B = -Y_series.imag();
    double theta = va[from] - va[to];
    return -vm[from] * vm[from] * B - vm[from] * vm[to] * (G * std::sin(theta) - B * std::cos(theta));
}

double StateEstimatorWLS::computeCurrentMagnitude(int from, int to,
    const std::vector<double>& vm,
    const std::vector<double>& va) const
{
    if (from >= params_.num_buses || to >= params_.num_buses) return 0.0;
    std::complex<double> V_i(vm[from] * std::cos(va[from]), vm[from] * std::sin(va[from]));
    std::complex<double> V_j(vm[to] * std::cos(va[to]), vm[to] * std::sin(va[to]));
    std::complex<double> Y_series = getYBus(from, to);
    if (std::abs(Y_series) < 1e-12) {
        for (size_t k = 0; k < topology_.branches.size(); ++k) {
            if (topology_.branches[k].first == from && topology_.branches[k].second == to) {
                if (k < topology_.y_series.size()) {
                    Y_series = topology_.y_series[k];
                }
                break;
            }
        }
    }
    std::complex<double> I = (V_i - V_j) * Y_series;
    return std::abs(I);
}

std::vector<double> StateEstimatorWLS::computeMeasurementFunction(
    const std::vector<double>& voltage_magnitude,
    const std::vector<double>& voltage_angle) const
{
    std::vector<double> h;
    h.reserve(measurements_.size());

    for (const auto& m : measurements_) {
        double val = 0.0;
        switch (m.type) {
            case MeasurementType::VOLTAGE_MAG:
            case MeasurementType::PMU_VOLTAGE_MAG:
                if (m.bus_from >= 0 && m.bus_from < params_.num_buses) {
                    val = voltage_magnitude[m.bus_from];
                }
                break;
            case MeasurementType::VOLTAGE_ANGLE:
            case MeasurementType::PMU_VOLTAGE_ANGLE:
                if (m.bus_from >= 0 && m.bus_from < params_.num_buses) {
                    val = voltage_angle[m.bus_from];
                }
                break;
            case MeasurementType::POWER_INJECTION_P:
                val = computePInjection(m.bus_from, voltage_magnitude, voltage_angle);
                break;
            case MeasurementType::POWER_INJECTION_Q:
                val = computeQInjection(m.bus_from, voltage_magnitude, voltage_angle);
                break;
            case MeasurementType::POWER_FLOW_P:
                val = computePFlow(m.bus_from, m.bus_to, voltage_magnitude, voltage_angle);
                break;
            case MeasurementType::POWER_FLOW_Q:
                val = computeQFlow(m.bus_from, m.bus_to, voltage_magnitude, voltage_angle);
                break;
            case MeasurementType::CURRENT_MAG:
            case MeasurementType::PMU_CURRENT_MAG:
                val = computeCurrentMagnitude(m.bus_from, m.bus_to,
                                               voltage_magnitude, voltage_angle);
                break;
            case MeasurementType::PMU_CURRENT_ANGLE: {
                if (m.bus_from >= 0 && m.bus_from < params_.num_buses &&
                    m.bus_to >= 0 && m.bus_to < params_.num_buses) {
                    std::complex<double> V_i(
                        voltage_magnitude[m.bus_from] * std::cos(voltage_angle[m.bus_from]),
                        voltage_magnitude[m.bus_from] * std::sin(voltage_angle[m.bus_from]));
                    std::complex<double> V_j(
                        voltage_magnitude[m.bus_to] * std::cos(voltage_angle[m.bus_to]),
                        voltage_magnitude[m.bus_to] * std::sin(voltage_angle[m.bus_to]));
                    std::complex<double> Y_series = getYBus(m.bus_from, m.bus_to);
                    std::complex<double> I = (V_i - V_j) * Y_series;
                    val = std::arg(I);
                }
                break;
            }
        }
        h.push_back(val);
    }

    return h;
}

// Derivadas parciales del Jacobiano
double StateEstimatorWLS::dPi_dThi(int i, const std::vector<double>& vm,
    const std::vector<double>& va) const
{
    double sum = 0.0;
    int n = params_.num_buses;
    for (int j = 0; j < n; ++j) {
        if (j == i) continue;
        std::complex<double> Yij = getYBus(i, j);
        double Gij = Yij.real();
        double Bij = Yij.imag();
        double theta_ij = va[i] - va[j];
        sum += vm[i] * vm[j] * (-Gij * std::sin(theta_ij) + Bij * std::cos(theta_ij));
    }
    return sum;
}

double StateEstimatorWLS::dPi_dThj(int i, int j, const std::vector<double>& vm,
    const std::vector<double>& va) const
{
    if (i == j) return 0.0;
    std::complex<double> Yij = getYBus(i, j);
    double Gij = Yij.real();
    double Bij = Yij.imag();
    double theta_ij = va[i] - va[j];
    return vm[i] * vm[j] * (Gij * std::sin(theta_ij) - Bij * std::cos(theta_ij));
}

double StateEstimatorWLS::dPi_dVi(int i, const std::vector<double>& vm,
    const std::vector<double>& va) const
{
    double sum = 0.0;
    int n = params_.num_buses;
    for (int j = 0; j < n; ++j) {
        std::complex<double> Yij = getYBus(i, j);
        double Gij = Yij.real();
        double Bij = Yij.imag();
        double theta_ij = va[i] - va[j];
        sum += vm[j] * (Gij * std::cos(theta_ij) + Bij * std::sin(theta_ij));
    }
    return 2.0 * vm[i] * getYBus(i, i).real() + sum;
}

double StateEstimatorWLS::dPi_dVj(int i, int j, const std::vector<double>& vm,
    const std::vector<double>& va) const
{
    if (i == j) return 0.0;
    std::complex<double> Yij = getYBus(i, j);
    double Gij = Yij.real();
    double Bij = Yij.imag();
    double theta_ij = va[i] - va[j];
    return vm[i] * (Gij * std::cos(theta_ij) + Bij * std::sin(theta_ij));
}

double StateEstimatorWLS::dQi_dThi(int i, const std::vector<double>& vm,
    const std::vector<double>& va) const
{
    double sum = 0.0;
    int n = params_.num_buses;
    for (int j = 0; j < n; ++j) {
        if (j == i) continue;
        std::complex<double> Yij = getYBus(i, j);
        double Gij = Yij.real();
        double Bij = Yij.imag();
        double theta_ij = va[i] - va[j];
        sum += vm[i] * vm[j] * (Gij * std::cos(theta_ij) + Bij * std::sin(theta_ij));
    }
    return -sum;
}

double StateEstimatorWLS::dQi_dThj(int i, int j, const std::vector<double>& vm,
    const std::vector<double>& va) const
{
    if (i == j) return 0.0;
    std::complex<double> Yij = getYBus(i, j);
    double Gij = Yij.real();
    double Bij = Yij.imag();
    double theta_ij = va[i] - va[j];
    return vm[i] * vm[j] * (-Gij * std::cos(theta_ij) - Bij * std::sin(theta_ij));
}

double StateEstimatorWLS::dQi_dVi(int i, const std::vector<double>& vm,
    const std::vector<double>& va) const
{
    double sum = 0.0;
    int n = params_.num_buses;
    for (int j = 0; j < n; ++j) {
        if (j == i) continue;
        std::complex<double> Yij = getYBus(i, j);
        double Gij = Yij.real();
        double Bij = Yij.imag();
        double theta_ij = va[i] - va[j];
        sum += vm[j] * (Gij * std::sin(theta_ij) - Bij * std::cos(theta_ij));
    }
    return -2.0 * vm[i] * getYBus(i, i).imag() + sum;
}

double StateEstimatorWLS::dQi_dVj(int i, int j, const std::vector<double>& vm,
    const std::vector<double>& va) const
{
    if (i == j) return 0.0;
    std::complex<double> Yij = getYBus(i, j);
    double Gij = Yij.real();
    double Bij = Yij.imag();
    double theta_ij = va[i] - va[j];
    return vm[i] * (Gij * std::sin(theta_ij) - Bij * std::cos(theta_ij));
}

double StateEstimatorWLS::dPij_dThi(int i, int j, const std::vector<double>& vm,
    const std::vector<double>& va) const
{
    std::complex<double> Y_series = getYBus(i, j);
    if (std::abs(Y_series) < 1e-12) {
        for (size_t k = 0; k < topology_.branches.size(); ++k) {
            if (topology_.branches[k].first == i && topology_.branches[k].second == j) {
                if (k < topology_.y_series.size()) Y_series = topology_.y_series[k];
                break;
            }
        }
    }
    double G = -Y_series.real();
    double B = -Y_series.imag();
    double theta = va[i] - va[j];
    return vm[i] * vm[j] * (G * std::sin(theta) - B * std::cos(theta));
}

double StateEstimatorWLS::dPij_dThj(int i, int j, const std::vector<double>& vm,
    const std::vector<double>& va) const
{
    return -dPij_dThi(i, j, vm, va);
}

double StateEstimatorWLS::dPij_dVi(int i, int j, const std::vector<double>& vm,
    const std::vector<double>& va) const
{
    std::complex<double> Y_series = getYBus(i, j);
    if (std::abs(Y_series) < 1e-12) {
        for (size_t k = 0; k < topology_.branches.size(); ++k) {
            if (topology_.branches[k].first == i && topology_.branches[k].second == j) {
                if (k < topology_.y_series.size()) Y_series = topology_.y_series[k];
                break;
            }
        }
    }
    double G = -Y_series.real();
    double B = -Y_series.imag();
    double theta = va[i] - va[j];
    return 2.0 * vm[i] * G - vm[j] * (G * std::cos(theta) + B * std::sin(theta));
}

double StateEstimatorWLS::dPij_dVj(int i, int j, const std::vector<double>& vm,
    const std::vector<double>& va) const
{
    std::complex<double> Y_series = getYBus(i, j);
    if (std::abs(Y_series) < 1e-12) {
        for (size_t k = 0; k < topology_.branches.size(); ++k) {
            if (topology_.branches[k].first == i && topology_.branches[k].second == j) {
                if (k < topology_.y_series.size()) Y_series = topology_.y_series[k];
                break;
            }
        }
    }
    double G = -Y_series.real();
    double B = -Y_series.imag();
    double theta = va[i] - va[j];
    return -vm[i] * (G * std::cos(theta) + B * std::sin(theta));
}

double StateEstimatorWLS::dIij_dThi(int i, int j, const std::vector<double>& vm,
    const std::vector<double>& va) const
{
    std::complex<double> Y_series = getYBus(i, j);
    if (std::abs(Y_series) < 1e-12) {
        for (size_t k = 0; k < topology_.branches.size(); ++k) {
            if (topology_.branches[k].first == i && topology_.branches[k].second == j) {
                if (k < topology_.y_series.size()) Y_series = topology_.y_series[k];
                break;
            }
        }
    }
    double G = Y_series.real();
    double B = Y_series.imag();
    double theta = va[i] - va[j];
    double I_mag = computeCurrentMagnitude(i, j, vm, va);
    if (I_mag < 1e-12) return 0.0;

    double dIr = -vm[i] * G * std::sin(va[i]) + vm[i] * B * std::cos(va[i])
               + vm[j] * G * std::sin(va[j]) - vm[j] * B * std::cos(va[j]);
    double dIi = vm[i] * G * std::cos(va[i]) + vm[i] * B * std::sin(va[i])
               - vm[j] * G * std::cos(va[j]) - vm[j] * B * std::sin(va[j]);

    double Ir = vm[i] * (G * std::cos(va[i]) - B * std::sin(va[i]))
              - vm[j] * (G * std::cos(va[j]) - B * std::sin(va[j]));
    double Ii = vm[i] * (G * std::sin(va[i]) + B * std::cos(va[i]))
              - vm[j] * (G * std::sin(va[j]) + B * std::cos(va[j]));

    return (Ir * dIr + Ii * dIi) / I_mag;
}

double StateEstimatorWLS::dIij_dVi(int i, int j, const std::vector<double>& vm,
    const std::vector<double>& va) const
{
    std::complex<double> Y_series = getYBus(i, j);
    if (std::abs(Y_series) < 1e-12) {
        for (size_t k = 0; k < topology_.branches.size(); ++k) {
            if (topology_.branches[k].first == i && topology_.branches[k].second == j) {
                if (k < topology_.y_series.size()) Y_series = topology_.y_series[k];
                break;
            }
        }
    }
    double G = Y_series.real();
    double B = Y_series.imag();
    double I_mag = computeCurrentMagnitude(i, j, vm, va);
    if (I_mag < 1e-12) return 0.0;

    double Ir = vm[i] * (G * std::cos(va[i]) - B * std::sin(va[i]))
              - vm[j] * (G * std::cos(va[j]) - B * std::sin(va[j]));
    double Ii = vm[i] * (G * std::sin(va[i]) + B * std::cos(va[i]))
              - vm[j] * (G * std::sin(va[j]) + B * std::cos(va[j]));

    double dIr_dVi = G * std::cos(va[i]) - B * std::sin(va[i]);
    double dIi_dVi = G * std::sin(va[i]) + B * std::cos(va[i]);

    return (Ir * dIr_dVi + Ii * dIi_dVi) / I_mag;
}

Eigen::SparseMatrix<double> StateEstimatorWLS::computeJacobian(
    const std::vector<double>& voltage_magnitude,
    const std::vector<double>& voltage_angle) const
{
    int n = params_.num_buses;
    int m = static_cast<int>(measurements_.size());

    // Variables de estado: theta_1..theta_n, |V|_1..|V|_n
    // El slack se mantiene fijo
    std::vector<Eigen::Triplet<double>> triplets;
    triplets.reserve(m * 4);

    for (int k = 0; k < m; ++k) {
        const auto& meas = measurements_[k];
        int row = k;

        switch (meas.type) {
            case MeasurementType::VOLTAGE_MAG:
            case MeasurementType::PMU_VOLTAGE_MAG: {
                int bus = meas.bus_from;
                if (bus >= 0 && bus < n) {
                    triplets.emplace_back(row, n + bus, 1.0); // d|V|/d|V|
                }
                break;
            }
            case MeasurementType::VOLTAGE_ANGLE:
            case MeasurementType::PMU_VOLTAGE_ANGLE: {
                int bus = meas.bus_from;
                if (bus >= 0 && bus < n) {
                    triplets.emplace_back(row, bus, 1.0); // dtheta/dtheta
                }
                break;
            }
            case MeasurementType::POWER_INJECTION_P: {
                int i = meas.bus_from;
                if (i < 0 || i >= n) break;
                triplets.emplace_back(row, i, dPi_dThi(i, voltage_magnitude, voltage_angle));
                triplets.emplace_back(row, n + i, dPi_dVi(i, voltage_magnitude, voltage_angle));
                for (int j = 0; j < n; ++j) {
                    if (j == i) continue;
                    std::complex<double> Yij = getYBus(i, j);
                    if (std::abs(Yij) > 1e-12) {
                        triplets.emplace_back(row, j, dPi_dThj(i, j, voltage_magnitude, voltage_angle));
                        triplets.emplace_back(row, n + j, dPi_dVj(i, j, voltage_magnitude, voltage_angle));
                    }
                }
                break;
            }
            case MeasurementType::POWER_INJECTION_Q: {
                int i = meas.bus_from;
                if (i < 0 || i >= n) break;
                triplets.emplace_back(row, i, dQi_dThi(i, voltage_magnitude, voltage_angle));
                triplets.emplace_back(row, n + i, dQi_dVi(i, voltage_magnitude, voltage_angle));
                for (int j = 0; j < n; ++j) {
                    if (j == i) continue;
                    std::complex<double> Yij = getYBus(i, j);
                    if (std::abs(Yij) > 1e-12) {
                        triplets.emplace_back(row, j, dQi_dThj(i, j, voltage_magnitude, voltage_angle));
                        triplets.emplace_back(row, n + j, dQi_dVj(i, j, voltage_magnitude, voltage_angle));
                    }
                }
                break;
            }
            case MeasurementType::POWER_FLOW_P: {
                int i = meas.bus_from;
                int j = meas.bus_to;
                if (i < 0 || i >= n || j < 0 || j >= n) break;
                triplets.emplace_back(row, i, dPij_dThi(i, j, voltage_magnitude, voltage_angle));
                triplets.emplace_back(row, j, dPij_dThj(i, j, voltage_magnitude, voltage_angle));
                triplets.emplace_back(row, n + i, dPij_dVi(i, j, voltage_magnitude, voltage_angle));
                triplets.emplace_back(row, n + j, dPij_dVj(i, j, voltage_magnitude, voltage_angle));
                break;
            }
            case MeasurementType::POWER_FLOW_Q: {
                // Derivadas similares a P flow pero para Q
                int i = meas.bus_from;
                int j = meas.bus_to;
                if (i < 0 || i >= n || j < 0 || j >= n) break;
                std::complex<double> Y_series = getYBus(i, j);
                if (std::abs(Y_series) < 1e-12) {
                    for (size_t b = 0; b < topology_.branches.size(); ++b) {
                        if (topology_.branches[b].first == i && topology_.branches[b].second == j) {
                            if (b < topology_.y_series.size()) Y_series = topology_.y_series[b];
                            break;
                        }
                    }
                }
                double G = -Y_series.real();
                double B = -Y_series.imag();
                double theta = voltage_angle[i] - voltage_angle[j];
                // dQij/dthi = -vm[i]*vm[j]*(G*cos(theta) + B*sin(theta))
                triplets.emplace_back(row, i, -voltage_magnitude[i] * voltage_magnitude[j]
                    * (G * std::cos(theta) + B * std::sin(theta)));
                triplets.emplace_back(row, j, voltage_magnitude[i] * voltage_magnitude[j]
                    * (G * std::cos(theta) + B * std::sin(theta)));
                // dQij/dVi = -2*vm[i]*B - vm[j]*(G*sin(theta) - B*cos(theta))
                triplets.emplace_back(row, n + i, -2.0 * voltage_magnitude[i] * B
                    - voltage_magnitude[j] * (G * std::sin(theta) - B * std::cos(theta)));
                triplets.emplace_back(row, n + j, -voltage_magnitude[i]
                    * (G * std::sin(theta) - B * std::cos(theta)));
                break;
            }
            case MeasurementType::CURRENT_MAG:
            case MeasurementType::PMU_CURRENT_MAG: {
                int i = meas.bus_from;
                int j = meas.bus_to;
                if (i < 0 || i >= n || j < 0 || j >= n) break;
                triplets.emplace_back(row, i, dIij_dThi(i, j, voltage_magnitude, voltage_angle));
                triplets.emplace_back(row, j, -dIij_dThi(i, j, voltage_magnitude, voltage_angle));
                triplets.emplace_back(row, n + i, dIij_dVi(i, j, voltage_magnitude, voltage_angle));
                // dIij/dVj es complejo, aproximamos
                std::complex<double> Y_series = getYBus(i, j);
                double I_mag = computeCurrentMagnitude(i, j, voltage_magnitude, voltage_angle);
                if (I_mag > 1e-12) {
                    double G = Y_series.real();
                    double B = Y_series.imag();
                    double Ir = voltage_magnitude[i] * (G * std::cos(voltage_angle[i]) - B * std::sin(voltage_angle[i]))
                              - voltage_magnitude[j] * (G * std::cos(voltage_angle[j]) - B * std::sin(voltage_angle[j]));
                    double Ii = voltage_magnitude[i] * (G * std::sin(voltage_angle[i]) + B * std::cos(voltage_angle[i]))
                              - voltage_magnitude[j] * (G * std::sin(voltage_angle[j]) + B * std::cos(voltage_angle[j]));
                    double dIr_dVj = -(G * std::cos(voltage_angle[j]) - B * std::sin(voltage_angle[j]));
                    double dIi_dVj = -(G * std::sin(voltage_angle[j]) + B * std::cos(voltage_angle[j]));
                    triplets.emplace_back(row, n + j, (Ir * dIr_dVj + Ii * dIi_dVj) / I_mag);
                }
                break;
            }
            case MeasurementType::PMU_CURRENT_ANGLE: {
                // Simplified: angle derivative
                int i = meas.bus_from;
                int j = meas.bus_to;
                if (i < 0 || i >= n || j < 0 || j >= n) break;
                triplets.emplace_back(row, i, 1.0);
                triplets.emplace_back(row, j, -1.0);
                break;
            }
        }
    }

    Eigen::SparseMatrix<double> H(m, 2 * n);
    H.setFromTriplets(triplets.begin(), triplets.end());
    return H;
}

Eigen::SparseMatrix<double> StateEstimatorWLS::buildWeightMatrix() const
{
    int m = static_cast<int>(measurements_.size());
    std::vector<Eigen::Triplet<double>> triplets;
    for (int i = 0; i < m; ++i) {
        triplets.emplace_back(i, i, measurements_[i].weight);
    }
    Eigen::SparseMatrix<double> W(m, m);
    W.setFromTriplets(triplets.begin(), triplets.end());
    return W;
}

Eigen::SparseMatrix<double> StateEstimatorWLS::buildMeasurementCovarianceMatrix() const
{
    int m = static_cast<int>(measurements_.size());
    std::vector<Eigen::Triplet<double>> triplets;
    for (int i = 0; i < m; ++i) {
        double sigma_sq = measurements_[i].sigma * measurements_[i].sigma;
        if (sigma_sq > 0) {
            triplets.emplace_back(i, i, sigma_sq);
        }
    }
    Eigen::SparseMatrix<double> R(m, m);
    R.setFromTriplets(triplets.begin(), triplets.end());
    return R;
}

Eigen::SparseMatrix<double> StateEstimatorWLS::computeGainMatrix(
    const Eigen::SparseMatrix<double>& H) const
{
    auto W = buildWeightMatrix();
    // G = H^T * W * H
    Eigen::SparseMatrix<double> G = H.transpose() * W * H;
    return G;
}

Eigen::VectorXd StateEstimatorWLS::solveSparseLU(
    const Eigen::SparseMatrix<double>& A,
    const Eigen::VectorXd& b) const
{
    Eigen::SparseLU<Eigen::SparseMatrix<double>> solver;
    solver.compute(A);
    if (solver.info() != Eigen::Success) {
        return Eigen::VectorXd::Zero(b.size());
    }
    return solver.solve(b);
}

Eigen::VectorXd StateEstimatorWLS::computeCorrection(
    const Eigen::SparseMatrix<double>& H,
    const Eigen::VectorXd& residual) const
{
    auto W = buildWeightMatrix();

    // G = H^T * W * H
    Eigen::SparseMatrix<double> G = computeGainMatrix(H);

    // rhs = H^T * W * residual
    Eigen::VectorXd rhs = H.transpose() * (W * residual);

    // dx = G^-1 * rhs
    Eigen::VectorXd dx = solveSparseLU(G, rhs);

    return dx;
}

void StateEstimatorWLS::initializeFlatStart()
{
    int n = params_.num_buses;
    initial_voltage_mag_.resize(n, 1.0);
    initial_voltage_ang_.resize(n, 0.0);

    // Slack bus: angulo = 0 (ya esta)
    for (int slack : topology_.slack_bus) {
        if (slack >= 0 && slack < n) {
            initial_voltage_ang_[slack] = 0.0;
        }
    }
}

std::vector<double> StateEstimatorWLS::getResiduals(
    const std::vector<double>& voltage_magnitude,
    const std::vector<double>& voltage_angle) const
{
    auto h = computeMeasurementFunction(voltage_magnitude, voltage_angle);
    std::vector<double> residuals;
    residuals.reserve(measurements_.size());

    for (size_t i = 0; i < measurements_.size() && i < h.size(); ++i) {
        residuals.push_back(measurements_[i].value - h[i]);
    }
    return residuals;
}

double StateEstimatorWLS::getChiSquare(
    const std::vector<double>& voltage_magnitude,
    const std::vector<double>& voltage_angle) const
{
    auto residuals = getResiduals(voltage_magnitude, voltage_angle);
    double chi2 = 0.0;
    for (size_t i = 0; i < residuals.size() && i < measurements_.size(); ++i) {
        double w = measurements_[i].weight;
        chi2 += w * residuals[i] * residuals[i];
    }
    return chi2;
}

std::vector<double> StateEstimatorWLS::getNormalizedResiduals(
    const std::vector<double>& voltage_magnitude,
    const std::vector<double>& voltage_angle) const
{
    auto residuals = getResiduals(voltage_magnitude, voltage_angle);
    std::vector<double> norm_residuals;
    norm_residuals.reserve(residuals.size());

    for (size_t i = 0; i < residuals.size() && i < measurements_.size(); ++i) {
        double sigma = measurements_[i].sigma;
        if (sigma > 0) {
            norm_residuals.push_back(residuals[i] / sigma);
        } else {
            norm_residuals.push_back(0.0);
        }
    }
    return norm_residuals;
}

std::vector<int> StateEstimatorWLS::detectBadData(const StateEstimate& state, double threshold)
{
    std::vector<int> bad_data;

    if (state.normalized_residuals.empty()) return bad_data;

    for (size_t i = 0; i < state.normalized_residuals.size() && i < measurements_.size(); ++i) {
        if (std::abs(state.normalized_residuals[i]) > threshold) {
            bad_data.push_back(measurements_[i].id);
        }
    }

    return bad_data;
}

StateEstimate StateEstimatorWLS::estimateWithBadDataRemoval()
{
    StateEstimate result = estimate();

    for (int round = 0; round < params_.max_bad_data_iterations; ++round) {
        auto bad_data = detectBadData(result, params_.bad_data_threshold);
        if (bad_data.empty()) break;

        // Marcar mediciones como eliminadas
        for (auto& m : measurements_) {
            if (std::find(bad_data.begin(), bad_data.end(), m.id) != bad_data.end()) {
                m.weight = 0.0; // Eliminar peso
            }
        }

        result = estimate();

        // Restaurar pesos
        for (auto& m : measurements_) {
            if (m.weight == 0.0 && m.sigma > 0.0) {
                m.weight = 1.0 / (m.sigma * m.sigma);
                if (m.is_pmu) {
                    m.weight *= params_.pmu_weight_multiplier;
                }
            }
        }
    }

    return result;
}

StateEstimate StateEstimatorWLS::estimate(const std::vector<double>& initial_guess)
{
    auto start_time = std::chrono::steady_clock::now();
    StateEstimate result;

    int n = params_.num_buses;
    if (n <= 0 || measurements_.empty()) {
        result.converged = false;
        return result;
    }

    // Inicializacion
    std::vector<double> vm, va;
    if (!initial_guess.empty() && initial_guess.size() >= static_cast<size_t>(2 * n)) {
        for (int i = 0; i < n; ++i) {
            va.push_back(initial_guess[i]);
            vm.push_back(initial_guess[n + i]);
        }
    } else if (params_.use_flat_start || initial_voltage_mag_.empty()) {
        initializeFlatStart();
        vm = initial_voltage_mag_;
        va = initial_voltage_ang_;
    } else {
        vm = initial_voltage_mag_;
        va = initial_voltage_ang_;
    }

    // Asegurar slack fijo
    for (int slack : topology_.slack_bus) {
        if (slack >= 0 && slack < n) {
            va[slack] = 0.0;
        }
    }

    int m = static_cast<int>(measurements_.size());
    result.degrees_of_freedom = m - 2 * n;

    Eigen::VectorXd z(m);
    for (int i = 0; i < m; ++i) {
        z(i) = measurements_[i].value;
    }

    // Iteraciones Gauss-Newton
    for (int iter = 0; iter < params_.max_iterations; ++iter) {
        // h(x)
        auto h = computeMeasurementFunction(vm, va);
        Eigen::VectorXd h_vec(m);
        for (int i = 0; i < m; ++i) {
            h_vec(i) = h[i];
        }

        // Residual r = z - h(x)
        Eigen::VectorXd r = z - h_vec;

        // Jacobiano H
        auto H = computeJacobian(vm, va);

        // Fijar filas/columnas del slack
        for (int slack : topology_.slack_bus) {
            if (slack >= 0 && slack < n) {
                // Fijar angulo del slack
                for (int k = 0; k < H.outerSize(); ++k) {
                    for (Eigen::SparseMatrix<double>::InnerIterator it(H, k); it; ++it) {
                        if (it.col() == slack) {
                            it.valueRef() = (it.row() == it.col()) ? 1.0 : 0.0;
                        }
                    }
                }
            }
        }

        // dx = (H^T * W * H)^-1 * H^T * W * r
        auto dx = computeCorrection(H, r);

        // Verificar convergencia
        double max_dx = dx.cwiseAbs().maxCoeff();
        if (max_dx < params_.convergence_tolerance) {
            result.converged = true;
            result.convergence_tolerance = max_dx;
            result.iterations = iter + 1;
            break;
        }

        // Actualizar estado (solo barras no-slack)
        for (int i = 0; i < n; ++i) {
            bool is_slack = false;
            for (int s : topology_.slack_bus) {
                if (s == i) { is_slack = true; break; }
            }
            if (!is_slack) {
                va[i] += dx(i);
            }
            vm[i] += dx(n + i);
            if (vm[i] < 0.5) vm[i] = 0.5;
            if (vm[i] > 2.0) vm[i] = 2.0;
        }
    }

    // Resultados finales
    result.voltage_magnitude = vm;
    result.voltage_angle = va;
    result.complex_voltage.resize(n);
    for (int i = 0; i < n; ++i) {
        result.complex_voltage[i] = std::polar(vm[i], va[i]);
    }

    auto h_final = computeMeasurementFunction(vm, va);
    result.residuals.reserve(m);
    double obj_val = 0.0;
    for (int i = 0; i < m; ++i) {
        double resid = measurements_[i].value - h_final[i];
        result.residuals.push_back(resid);
        obj_val += measurements_[i].weight * resid * resid;
    }
    result.objective_value = obj_val;
    result.chi_square = obj_val;
    result.max_residual = 0.0;
    for (const auto& r : result.residuals) {
        if (std::abs(r) > result.max_residual) result.max_residual = std::abs(r);
    }

    // Normalized residuals
    result.normalized_residuals = getNormalizedResiduals(vm, va);

    // Bad data detection
    if (params_.detect_bad_data) {
        result.bad_data_detected = detectBadData(result, params_.bad_data_threshold);
    }

    // Covarianza de estados
    auto H_final = computeJacobian(vm, va);
    auto G = computeGainMatrix(H_final);
    result.state_covariance = computeStateCovariance(G);

    // Numero de condicion
    result.condition_number = computeConditionNumber(G);

    auto end_time = std::chrono::steady_clock::now();
    result.computation_time_ms = std::chrono::duration<double, std::milli>(
        end_time - start_time).count();

    if (!result.converged) {
        result.iterations = params_.max_iterations;
    }

    previous_estimate_ = result;
    return result;
}

bool StateEstimatorWLS::checkObservability() const
{
    int n = params_.num_buses;
    if (n <= 0 || measurements_.empty()) return false;

    // Simple observability: al menos una medicion por barra
    std::vector<bool> has_measurement(n, false);
    for (const auto& m : measurements_) {
        if (m.bus_from >= 0 && m.bus_from < n) {
            has_measurement[m.bus_from] = true;
        }
        if (m.bus_to >= 0 && m.bus_to < n && m.bus_to != m.bus_from) {
            has_measurement[m.bus_to] = true;
        }
    }

    // Slack siempre observable
    for (int s : topology_.slack_bus) {
        if (s >= 0 && s < n) has_measurement[s] = true;
    }

    for (bool h : has_measurement) {
        if (!h) return false;
    }
    return true;
}

std::vector<std::vector<int>> StateEstimatorWLS::findUnobservableIslands() const
{
    std::vector<std::vector<int>> islands;
    int n = params_.num_buses;
    if (n <= 0) return islands;

    std::vector<bool> visited(n, false);

    // Identificar barras con mediciones directas
    std::vector<bool> has_direct_meas(n, false);
    for (const auto& m : measurements_) {
        if (m.bus_from >= 0 && m.bus_from < n) {
            if (m.type == MeasurementType::VOLTAGE_MAG ||
                m.type == MeasurementType::VOLTAGE_ANGLE ||
                m.type == MeasurementType::POWER_INJECTION_P ||
                m.type == MeasurementType::POWER_INJECTION_Q) {
                has_direct_meas[m.bus_from] = true;
            }
        }
    }

    // Barras slack siempre observables
    for (int s : topology_.slack_bus) {
        if (s >= 0 && s < n) has_direct_meas[s] = true;
    }

    // DFS para encontrar islas no observables
    for (int i = 0; i < n; ++i) {
        if (visited[i] || has_direct_meas[i]) continue;

        std::vector<int> island;
        std::vector<int> stack;
        stack.push_back(i);
        visited[i] = true;

        while (!stack.empty()) {
            int current = stack.back();
            stack.pop_back();
            island.push_back(current);

            // Vecinos conectados
            for (const auto& branch : topology_.branches) {
                int neighbor = -1;
                if (branch.first == current) neighbor = branch.second;
                else if (branch.second == current) neighbor = branch.first;

                if (neighbor >= 0 && neighbor < n && !visited[neighbor] && !has_direct_meas[neighbor]) {
                    visited[neighbor] = true;
                    stack.push_back(neighbor);
                }
            }
        }

        if (!island.empty()) {
            islands.push_back(island);
        }
    }

    return islands;
}

void StateEstimatorWLS::addPseudoMeasurements()
{
    auto islands = findUnobservableIslands();

    for (const auto& island : islands) {
        for (int bus : island) {
            // Agregar pseudo-medicion de voltaje
            Measurement pseudo_v;
            pseudo_v.id = static_cast<int>(measurements_.size());
            pseudo_v.type = MeasurementType::VOLTAGE_MAG;
            pseudo_v.value = 1.0; // Flat start assumption
            pseudo_v.sigma = 0.1; // Baja confianza
            pseudo_v.weight = 1.0 / (0.1 * 0.1); // 100
            pseudo_v.bus_from = bus;
            pseudo_v.is_pseudo = true;
            measurements_.push_back(pseudo_v);

            // Pseudo-medicion de angulo
            Measurement pseudo_a;
            pseudo_a.id = static_cast<int>(measurements_.size());
            pseudo_a.type = MeasurementType::VOLTAGE_ANGLE;
            pseudo_a.value = 0.0;
            pseudo_a.sigma = 0.1;
            pseudo_a.weight = 1.0 / (0.1 * 0.1);
            pseudo_a.bus_from = bus;
            pseudo_a.is_pseudo = true;
            measurements_.push_back(pseudo_a);
        }
    }
}

void StateEstimatorWLS::processPMUMeasurements()
{
    if (!params_.enable_pmu) return;

    for (auto& m : measurements_) {
        if (m.is_pmu) {
            // Las mediciones PMU tienen mayor peso
            m.weight = 1.0 / (m.sigma * m.sigma) * params_.pmu_weight_multiplier;
        }
    }
}

std::pair<double, double> StateEstimatorWLS::pmuRectangularToPolar(double re, double im) const
{
    double mag = std::sqrt(re * re + im * im);
    double ang = std::atan2(im, re);
    return {mag, ang};
}

std::vector<double> StateEstimatorWLS::computeStateCovariance(
    const Eigen::SparseMatrix<double>& gain_matrix) const
{
    try {
        Eigen::SparseLU<Eigen::SparseMatrix<double>> solver;
        solver.compute(gain_matrix);
        if (solver.info() != Eigen::Success) {
            return std::vector<double>(params_.num_buses * 2, 0.0);
        }

        int dim = gain_matrix.cols();
        std::vector<double> variances;
        variances.reserve(dim);

        for (int i = 0; i < dim; ++i) {
            Eigen::VectorXd e_i = Eigen::VectorXd::Unit(dim, i);
            Eigen::VectorXd col = solver.solve(e_i);
            variances.push_back(col(i));
        }

        return variances;
    } catch (...) {
        return std::vector<double>(params_.num_buses * 2, 0.0);
    }
}

double StateEstimatorWLS::computeConditionNumber(
    const Eigen::SparseMatrix<double>& G) const
{
    // Approximate condition number using diagonal scaling
    double max_diag = 0.0, min_diag = std::numeric_limits<double>::max();
    for (int k = 0; k < G.outerSize(); ++k) {
        for (Eigen::SparseMatrix<double>::InnerIterator it(G, k); it; ++it) {
            if (it.row() == it.col()) {
                max_diag = std::max(max_diag, std::abs(it.value()));
                if (std::abs(it.value()) > 1e-12) {
                    min_diag = std::min(min_diag, std::abs(it.value()));
                }
            }
        }
    }
    if (min_diag < 1e-12) return std::numeric_limits<double>::max();
    return max_diag / min_diag;
}

std::string StateEstimatorWLS::generateEstimationReport(const StateEstimate& estimate) const
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6);

    oss << "==================================================================\n";
    oss << "  POWSYS365 - Weighted Least Squares State Estimation Report\n";
    oss << "==================================================================\n\n";

    oss << "--- Convergence ---\n";
    oss << "Converged : " << (estimate.converged ? "YES" : "NO") << "\n";
    oss << "Iterations: " << estimate.iterations << "\n";
    oss << "Tolerance : " << estimate.convergence_tolerance << "\n";
    oss << "Time      : " << estimate.computation_time_ms << " ms\n";
    oss << "\n";

    oss << "--- Objective Function ---\n";
    oss << "J(x)      : " << estimate.objective_value << "\n";
    oss << "Chi-square: " << estimate.chi_square << "\n";
    oss << "Max res.  : " << estimate.max_residual << "\n";
    oss << "Cond. #   : " << estimate.condition_number << "\n";
    oss << "\n";

    oss << "--- State Vector ---\n";
    int n = params_.num_buses;
    oss << "Bus | |V| [pu] | Angle [rad] | Angle [deg]\n";
    oss << "----+----------+---------------+-------------\n";
    for (int i = 0; i < n && i < static_cast<int>(estimate.voltage_magnitude.size()); ++i) {
        oss << std::setw(3) << i << " | "
            << std::setw(8) << estimate.voltage_magnitude[i] << " | "
            << std::setw(13) << estimate.voltage_angle[i] << " | "
            << std::setw(11) << estimate.voltage_angle[i] * 180.0 / M_PI << "\n";
    }
    oss << "\n";

    if (!estimate.bad_data_detected.empty()) {
        oss << "--- Bad Data Detected ---\n";
        for (int id : estimate.bad_data_detected) {
            oss << "  Measurement ID: " << id << "\n";
        }
        oss << "\n";
    }

    return oss.str();
}

} // namespace powsys365
