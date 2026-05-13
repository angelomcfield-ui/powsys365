/**
 * @file thd_calculator.cpp
 * @brief Implementacion del calculador de THD para POWSYS365
 * @author POWSYS365 Team
 * @copyright 2025 XNOX L.L.C.
 * @version 1.0.0
 */

#include "thd_calculator.h"
#include <cmath>
#include <algorithm>

namespace powsys365 {

// ============================================================================
// Constructor / Destructor
// ============================================================================

THDCalculator::THDCalculator() = default;
THDCalculator::~THDCalculator() = default;

// ============================================================================
// Configuracion
// ============================================================================

void THDCalculator::setHarmonicResults(
    const HarmonicLoadFlow::HarmonicLFResults& results) {
    m_results = results;
}

// ============================================================================
// Tension fundamental
// ============================================================================

double THDCalculator::getFundamentalVoltage(int busId) const {
    if (busId < 0 || busId >= m_results.fundamentalVoltage.size()) {
        return 0.0;
    }
    return std::abs(m_results.fundamentalVoltage(busId));
}

// ============================================================================
// Corriente fundamental
// ============================================================================

double THDCalculator::getFundamentalCurrent(int busId) const {
    auto it = m_results.currentsByHarmonic.find(1);
    if (it == m_results.currentsByHarmonic.end()) {
        return 0.0;
    }
    if (busId < 0 || busId >= it->second.size()) {
        return 0.0;
    }
    return std::abs(it->second(busId));
}

// ============================================================================
// THD de tension
// ============================================================================

double THDCalculator::calculateTHDv(int busId) const {
    double v1 = getFundamentalVoltage(busId);
    if (v1 < 1e-12) return 0.0;

    double sumSquares = 0.0;
    for (const auto& [h, voltages] : m_results.voltagesByHarmonic) {
        if (h < 2) continue;  // Ignorar fundamental
        if (busId >= 0 && busId < voltages.size()) {
            sumSquares += std::norm(voltages(busId));
        }
    }

    return (std::sqrt(sumSquares) / v1) * 100.0;
}

// ============================================================================
// THD de corriente
// ============================================================================

double THDCalculator::calculateTHDi(int busId) const {
    // Usar las corrientes inyectadas armonicas
    double i1 = getFundamentalCurrent(busId);
    if (i1 < 1e-12) {
        // Si no hay corriente fundamental, usar las corrientes armonicas directamente
        i1 = 1.0;  // Normalizar a 1.0 para calcular THDi relativo
    }

    double sumSquares = 0.0;
    for (const auto& [h, currents] : m_results.currentsByHarmonic) {
        if (h < 2) continue;
        if (busId >= 0 && busId < currents.size()) {
            sumSquares += std::norm(currents(busId));
        }
    }

    return (std::sqrt(sumSquares) / i1) * 100.0;
}

// ============================================================================
// Total Demand Distortion (TDD)
// ============================================================================

double THDCalculator::calculateTDD(int busId, double maxLoadCurrent) const {
    // TDD = sqrt(sum(I_h^2)) / I_L * 100%
    // Donde I_L es la corriente de carga maxima (demanda)

    double iLoad = maxLoadCurrent;
    if (iLoad < 1e-12) {
        // Si no se proporciona I_L, usar la corriente fundamental como aproximacion
        iLoad = getFundamentalCurrent(busId);
    }
    if (iLoad < 1e-12) return 0.0;

    double sumSquares = 0.0;
    for (const auto& [h, currents] : m_results.currentsByHarmonic) {
        if (h < 2) continue;
        if (busId >= 0 && busId < currents.size()) {
            sumSquares += std::norm(currents(busId));
        }
    }

    return (std::sqrt(sumSquares) / iLoad) * 100.0;
}

// ============================================================================
// THDv para todas las barras
// ============================================================================

Eigen::VectorXd THDCalculator::calculateTHDvAllBuses() const {
    int numBuses = static_cast<int>(m_results.fundamentalVoltage.size());
    Eigen::VectorXd thd(numBuses);

    for (int bus = 0; bus < numBuses; ++bus) {
        thd(bus) = calculateTHDv(bus);
    }

    return thd;
}

// ============================================================================
// THDi para todas las barras
// ============================================================================

Eigen::VectorXd THDCalculator::calculateTHDiAllBuses() const {
    int numBuses = static_cast<int>(m_results.fundamentalVoltage.size());
    Eigen::VectorXd thd(numBuses);

    for (int bus = 0; bus < numBuses; ++bus) {
        thd(bus) = calculateTHDi(bus);
    }

    return thd;
}

// ============================================================================
// Espectro armonico completo
// ============================================================================

std::vector<HarmonicComponent> THDCalculator::getHarmonicSpectrum(int busId) const {
    std::vector<HarmonicComponent> spectrum;

    double v1 = getFundamentalVoltage(busId);
    if (v1 < 1e-12) return spectrum;

    for (const auto& [h, voltages] : m_results.voltagesByHarmonic) {
        if (h < 2) continue;
        if (busId < 0 || busId >= voltages.size()) continue;

        HarmonicComponent comp;
        comp.order = h;
        comp.magnitude = std::abs(voltages(busId));
        comp.angle = std::arg(voltages(busId));
        comp.distortionPercent = (comp.magnitude / v1) * 100.0;

        spectrum.push_back(comp);
    }

    // Ordenar por orden armonico
    std::sort(spectrum.begin(), spectrum.end(),
              [](const HarmonicComponent& a, const HarmonicComponent& b) {
                  return a.order < b.order;
              });

    return spectrum;
}

// ============================================================================
// Distorsion individual
// ============================================================================

double THDCalculator::calculateIndividualDistortion(int busId,
    int harmonicOrder) const {

    double v1 = getFundamentalVoltage(busId);
    if (v1 < 1e-12) return 0.0;

    auto it = m_results.voltagesByHarmonic.find(harmonicOrder);
    if (it == m_results.voltagesByHarmonic.end()) return 0.0;

    if (busId < 0 || busId >= it->second.size()) return 0.0;

    double vh = std::abs(it->second(busId));
    return (vh / v1) * 100.0;
}

// ============================================================================
// Verificacion IEEE 519
// ============================================================================

IEEE519Compliance THDCalculator::checkIEEE519Compliance(int busId,
    int voltageLevel) const {

    IEEE519Compliance compliance;
    compliance.voltageLevel = voltageLevel;

    auto limits = getIEEE519Limits(voltageLevel);
    compliance.thdVoltageLimit = limits.first;
    compliance.thdCurrentLimit = limits.second;

    compliance.actualTHDv = calculateTHDv(busId);
    compliance.actualTHDi = calculateTHDi(busId);

    compliance.compliant = true;

    // Verificar THDv
    if (compliance.actualTHDv > compliance.thdVoltageLimit) {
        compliance.compliant = false;
    }

    // Verificar THDi
    if (compliance.actualTHDi > compliance.thdCurrentLimit) {
        compliance.compliant = false;
    }

    // Verificar distorsion individual por orden
    auto indLimits = getIEEE519IndividualLimits(voltageLevel);
    for (const auto& [h, limit] : indLimits) {
        double di = calculateIndividualDistortion(busId, h);
        if (di > limit) {
            compliance.violatingOrders.push_back(h);
            compliance.compliant = false;
        }
    }

    return compliance;
}

bool THDCalculator::checkIEEE519AllBuses(int voltageLevel) const {
    int numBuses = static_cast<int>(m_results.fundamentalVoltage.size());

    for (int bus = 0; bus < numBuses; ++bus) {
        auto comp = checkIEEE519Compliance(bus, voltageLevel);
        if (!comp.compliant) {
            return false;
        }
    }

    return true;
}

// ============================================================================
// Verificacion IEC 61000-3-6
// ============================================================================

bool THDCalculator::checkIEC61000_3_6(int busId, int voltageLevel) const {
    double thdLimit = (voltageLevel == 1) ? 6.5 : 3.0;  // MV: 6.5%, HV: 3.0%

    double thd = calculateTHDv(busId);
    return (thd <= thdLimit);
}

// ============================================================================
// Verificacion EN 50160
// ============================================================================

bool THDCalculator::checkEN50160(int busId) const {
    const double THD_LIMIT = 8.0;  // EN 50160: THDv <= 8% en LV

    double thd = calculateTHDv(busId);
    if (thd > THD_LIMIT) {
        return false;
    }

    // Verificar distorsion individual <= 5%
    double v1 = getFundamentalVoltage(busId);
    if (v1 < 1e-12) return true;

    for (const auto& [h, voltages] : m_results.voltagesByHarmonic) {
        if (h < 2) continue;
        if (busId < 0 || busId >= voltages.size()) continue;

        double di = (std::abs(voltages(busId)) / v1) * 100.0;
        if (di > 5.0) {
            return false;
        }
    }

    return true;
}

// ============================================================================
// Limites IEEE 519
// ============================================================================

std::pair<double, double> THDCalculator::getIEEE519Limits(int voltageLevel) {
    // Tabla 10.3 de IEEE 519
    // voltageLevel: 0=LV (V <= 69 kV), 1=MV (69 < V <= 161 kV), 2=HV (V > 161 kV)
    switch (voltageLevel) {
        case 0:  // LV
            return {5.0, 8.0};   // THDv <= 5%, THDi <= 8% (para Isc/IL < 20)
        case 1:  // MV
            return {3.0, 5.0};   // THDv <= 3%, THDi <= 5% (para Isc/IL < 20)
        case 2:  // HV
            return {1.5, 2.5};   // THDv <= 1.5%, THDi <= 2.5%
        default:
            return {5.0, 8.0};
    }
}

std::map<int, double> THDCalculator::getIEEE519IndividualLimits(int voltageLevel) {
    std::map<int, double> limits;

    // Tabla 10.3: Limites de distorsion individual de voltaje
    double baseLimit;
    switch (voltageLevel) {
        case 0:  baseLimit = 3.0; break;  // LV
        case 1:  baseLimit = 1.5; break;  // MV
        case 2:  baseLimit = 1.0; break;  // HV
        default: baseLimit = 3.0; break;
    }

    for (int h = 2; h <= 50; ++h) {
        if (h <= 11) {
            limits[h] = baseLimit;
        } else if (h <= 17) {
            limits[h] = baseLimit * 11.0 / h;
        } else if (h <= 23) {
            limits[h] = baseLimit * 11.0 / h;
        } else if (h <= 35) {
            limits[h] = baseLimit * 11.0 / h;
        } else {
            limits[h] = baseLimit * 11.0 / h;
        }
    }

    return limits;
}

} // namespace powsys365
