/**
 * @file harmonic_load_flow.cpp
 * @brief Implementacion del flujo de carga armonico para POWSYS365
 * @author POWSYS365 Team
 * @copyright 2025 XNOX L.L.C.
 * @version 1.0.0
 */

#include "harmonic_load_flow.h"
#include <cmath>
#include <algorithm>
#include <numeric>

namespace powsys365 {

// ============================================================================
// Constructor / Destructor
// ============================================================================

HarmonicLoadFlow::HarmonicLoadFlow() = default;
HarmonicLoadFlow::~HarmonicLoadFlow() = default;

// ============================================================================
// Configuracion del sistema
// ============================================================================

void HarmonicLoadFlow::setNumBuses(int numBuses) {
    m_numBuses = numBuses;
}

void HarmonicLoadFlow::setFundamentalFrequency(double freq) {
    m_fundamentalFreq = freq;
}

void HarmonicLoadFlow::addHarmonicSource(const HarmonicSource& source) {
    m_sources.push_back(source);
    m_numBuses = std::max(m_numBuses, source.busId + 1);
}

void HarmonicLoadFlow::addLineModel(const HarmonicLineModel& line) {
    m_lines.push_back(line);
    m_numBuses = std::max(m_numBuses, std::max(line.fromBus, line.toBus) + 1);
}

void HarmonicLoadFlow::addTransformerModel(const HarmonicTransformerModel& tx) {
    m_transformers.push_back(tx);
    m_numBuses = std::max(m_numBuses, std::max(tx.bus1, tx.bus2) + 1);
}

void HarmonicLoadFlow::addLoadModel(const HarmonicLoadModel& load) {
    m_loads.push_back(load);
    m_numBuses = std::max(m_numBuses, load.busId + 1);
}

void HarmonicLoadFlow::addCapacitorModel(const HarmonicCapacitorModel& cap) {
    m_capacitors.push_back(cap);
    m_numBuses = std::max(m_numBuses, cap.busId + 1);
}

void HarmonicLoadFlow::clearHarmonicSources() {
    m_sources.clear();
}

void HarmonicLoadFlow::clearAllModels() {
    m_sources.clear();
    m_lines.clear();
    m_transformers.clear();
    m_loads.clear();
    m_capacitors.clear();
    m_numBuses = 0;
    m_results = HarmonicLFResults{};
}

// ============================================================================
// Impedancias armonicas por componente
// ============================================================================

HarmonicLoadFlow::Complex HarmonicLoadFlow::lineImpedance(
    const HarmonicLineModel& line, int h) {

    // R(h) = R1 * sqrt(h) (efecto skin)
    double r_h = line.r1 * std::pow(static_cast<double>(h), line.skinEffectExponent);
    // X(h) = h * X1
    double x_h = static_cast<double>(h) * line.x1;

    return Complex(r_h, x_h);
}

HarmonicLoadFlow::Complex HarmonicLoadFlow::transformerImpedance(
    const HarmonicTransformerModel& tx, int h) {

    // Impedancia de cortocircuito referida a potencia base
    double z_base = tx.uk / 100.0;
    double r_pu = (tx.pcu / 1000.0) / tx.ratedPower;
    if (r_pu < 0.0) r_pu = 0.0;
    double x_pu_sq = z_base * z_base - r_pu * r_pu;
    double x_pu = (x_pu_sq > 0.0) ? std::sqrt(x_pu_sq) : 0.0;

    // Escala con orden armonico
    // R(h) = R1 * sqrt(h) (aproximadamente)
    // X(h) = h * X1 (lineal)
    double r_h = r_pu * std::sqrt(static_cast<double>(h));
    double x_h = x_pu * static_cast<double>(h);

    return Complex(r_h, x_h);
}

HarmonicLoadFlow::Complex HarmonicLoadFlow::loadAdmittance(
    const HarmonicLoadModel& load, int h) {

    // Modelo de carga exponencial:
    // Conductancia G = P1 / |V1|^2 (constante)
    // Susceptancia B = -Q1 / (h * |V1|^2)
    // Para sistemas en pu asumiendo |V1| = 1.0:
    // Y(h) = P1 - j * Q1 / h
    double g = load.pFundamental;
    double b = -load.qFundamental / static_cast<double>(h);

    return Complex(g, b);
}

HarmonicLoadFlow::Complex HarmonicLoadFlow::capacitorImpedance(
    const HarmonicCapacitorModel& cap, int h) {

    // Reactancia capacitiva: Xc(h) = Xc1 / h
    // Admitancia: Yc(h) = j * h / Xc1
    if (cap.xC1 == 0.0) {
        return Complex(0.0, 0.0);
    }
    double x_c_h = cap.xC1 / static_cast<double>(h);
    return Complex(0.0, -x_c_h);  // Representamos como impedancia serie
}

double HarmonicLoadFlow::skinEffectResistance(double r1, int h, double exponent) {
    return r1 * std::pow(static_cast<double>(h), exponent);
}

// ============================================================================
// Construccion de Ybus armonica
// ============================================================================

HarmonicLoadFlow::SparseMatrixXcd HarmonicLoadFlow::buildYbusHarmonic(int h) const {
    if (m_numBuses <= 0) {
        return SparseMatrixXcd(0, 0);
    }

    SparseMatrixXcd ybus(m_numBuses, m_numBuses);
    std::vector<Eigen::Triplet<Complex>> triplets;
    triplets.reserve(m_lines.size() * 4 + m_transformers.size() * 4 +
                     m_loads.size() + m_capacitors.size());

    // --- Contribucion de lineas ---
    for (const auto& line : m_lines) {
        Complex z = lineImpedance(line, h);
        if (std::abs(z) < 1e-12) continue;

        Complex y_series = Complex(1.0, 0.0) / z;

        // Admitancia shunt (capacitancia del cable): B_shunt = h * B1 / 2
        Complex b_shunt(0.0, line.b1 * static_cast<double>(h) * 0.5);

        // Elementos diagonales
        triplets.emplace_back(line.fromBus, line.fromBus, y_series + b_shunt);
        triplets.emplace_back(line.toBus, line.toBus, y_series + b_shunt);
        // Elementos off-diagonal
        triplets.emplace_back(line.fromBus, line.toBus, -y_series);
        triplets.emplace_back(line.toBus, line.fromBus, -y_series);
    }

    // --- Contribucion de transformadores ---
    for (const auto& tx : m_transformers) {
        Complex z = transformerImpedance(tx, h);
        if (std::abs(z) < 1e-12) continue;

        Complex y = Complex(1.0, 0.0) / z;

        triplets.emplace_back(tx.bus1, tx.bus1, y);
        triplets.emplace_back(tx.bus2, tx.bus2, y);
        triplets.emplace_back(tx.bus1, tx.bus2, -y);
        triplets.emplace_back(tx.bus2, tx.bus1, -y);
    }

    // --- Contribucion de cargas (admitancia shunt) ---
    for (const auto& load : m_loads) {
        Complex y = loadAdmittance(load, h);
        triplets.emplace_back(load.busId, load.busId, y);
    }

    // --- Contribucion de capacitores (admitancia shunt) ---
    for (const auto& cap : m_capacitors) {
        if (cap.xC1 == 0.0) continue;
        // Admitancia capacitiva: Yc = j * h / Xc1
        Complex y_cap(0.0, static_cast<double>(h) / cap.xC1);
        triplets.emplace_back(cap.busId, cap.busId, y_cap);
    }

    ybus.setFromTriplets(triplets.begin(), triplets.end());
    return ybus;
}

// ============================================================================
// Ensamblaje de vector de inyecciones
// ============================================================================

HarmonicLoadFlow::VectorXcd HarmonicLoadFlow::assembleInjectionVector(int h) const {
    VectorXcd Ih = VectorXcd::Zero(m_numBuses);

    // Fuentes armonicas explicitas
    for (const auto& source : m_sources) {
        if (source.order == h) {
            Ih(source.busId) = Complex(
                source.magnitude * std::cos(source.angle),
                source.magnitude * std::sin(source.angle)
            );
        }
    }

    // Si no hay fuentes explicitas para este orden, usar espectro de cargas
    if (Ih.isZero(1e-12)) {
        for (const auto& load : m_loads) {
            if (h < static_cast<int>(load.harmonicCurrentSpectrum.size()) &&
                load.harmonicCurrentSpectrum[h] > 0.0) {
                // Corriente fundamental aproximada
                double fundamentalCurrent = std::sqrt(
                    load.pFundamental * load.pFundamental +
                    load.qFundamental * load.qFundamental
                );
                double ih_mag = fundamentalCurrent * load.harmonicCurrentSpectrum[h];
                Ih(load.busId) = Complex(ih_mag, 0.0);
            }
        }
    }

    return Ih;
}

// ============================================================================
// Calculo de tensiones armonicas para un orden
// ============================================================================

HarmonicLoadFlow::VectorXcd HarmonicLoadFlow::calculateHarmonicVoltages(
    int h, const VectorXcd& harmonicCurrents) const {

    if (m_numBuses <= 0) {
        return VectorXcd::Zero(0);
    }

    SparseMatrixXcd ybus = buildYbusHarmonic(h);

    // Resolver V = Ybus^(-1) * I usando SparseLU
    Eigen::SparseLU<SparseMatrixXcd> solver;
    solver.compute(ybus);

    if (solver.info() != Eigen::Success) {
        return VectorXcd::Zero(m_numBuses);
    }

    return solver.solve(harmonicCurrents);
}

// ============================================================================
// Solucion completa del flujo de carga armonico
// ============================================================================

HarmonicLoadFlow::HarmonicLFResults HarmonicLoadFlow::solve(int maxHarmonic) {
    m_results = HarmonicLFResults{};

    if (m_numBuses <= 0) {
        m_results.converged = false;
        return m_results;
    }

    // --- Resolver fundamental (h=1) primero ---
    VectorXcd I1 = VectorXcd::Zero(m_numBuses);
    for (const auto& source : m_sources) {
        if (source.order == 1) {
            I1(source.busId) = Complex(
                source.magnitude * std::cos(source.angle),
                source.magnitude * std::sin(source.angle)
            );
        }
    }

    if (!I1.isZero(1e-12)) {
        m_results.fundamentalVoltage = calculateHarmonicVoltages(1, I1);
    } else {
        m_results.fundamentalVoltage = VectorXcd::Ones(m_numBuses);  // Asumir 1.0 pu
    }

    // --- Resolver para cada orden armonico h = 2 .. maxHarmonic ---
    int solvedCount = 0;
    for (int h = 2; h <= maxHarmonic; ++h) {
        // Ensamblar vector de inyecciones
        VectorXcd Ih = assembleInjectionVector(h);

        if (Ih.isZero(1e-12)) {
            continue;  // No hay fuentes para este orden
        }

        // Construir Ybus y resolver
        SparseMatrixXcd ybus_h = buildYbusHarmonic(h);
        m_results.ybusByHarmonic[h] = ybus_h;

        Eigen::SparseLU<SparseMatrixXcd> solver;
        solver.compute(ybus_h);

        if (solver.info() != Eigen::Success) {
            continue;  // Fallo en factorizacion para este orden
        }

        VectorXcd Vh = solver.solve(Ih);
        m_results.voltagesByHarmonic[h] = Vh;
        m_results.currentsByHarmonic[h] = Ih;
        ++solvedCount;
    }

    m_results.converged = (solvedCount > 0);
    m_results.iterations = solvedCount;

    return m_results;
}

// ============================================================================
// Getters
// ============================================================================

const std::map<int, HarmonicLoadFlow::VectorXcd>&
HarmonicLoadFlow::getHarmonicVoltages() const {
    return m_results.voltagesByHarmonic;
}

const HarmonicLoadFlow::HarmonicLFResults&
HarmonicLoadFlow::getResults() const {
    return m_results;
}

// ============================================================================
// Espectro armonico de convertidores
// ============================================================================

std::map<int, double> HarmonicLoadFlow::getConverterHarmonicSpectrum(
    int numPulses, double fundamentalCurrent, int maxOrder) {

    std::map<int, double> spectrum;

    if (numPulses < 6 || fundamentalCurrent <= 0.0 || maxOrder < 2) {
        return spectrum;
    }

    // Convertidor de np pulsos: armonicos caracteristicos h = np*k +/- 1
    // Magnitud: Ih = I1 / h (caracteristica ideal)
    for (int k = 1; ; ++k) {
        int h1 = numPulses * k - 1;
        int h2 = numPulses * k + 1;

        if (h1 > maxOrder && h2 > maxOrder) break;

        if (h1 >= 2 && h1 <= maxOrder) {
            spectrum[h1] = fundamentalCurrent / static_cast<double>(h1);
        }
        if (h2 >= 2 && h2 <= maxOrder) {
            spectrum[h2] = fundamentalCurrent / static_cast<double>(h2);
        }
    }

    return spectrum;
}

} // namespace powsys365
