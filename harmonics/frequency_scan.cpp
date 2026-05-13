/**
 * @file frequency_scan.cpp
 * @brief Implementacion del frequency scan para POWSYS365
 * @author POWSYS365 Team
 * @copyright 2025 XNOX L.L.C.
 * @version 1.0.0
 */

#include "frequency_scan.h"
#include <cmath>
#include <algorithm>

namespace powsys365 {

// ============================================================================
// Constructor / Destructor
// ============================================================================

FrequencyScanner::FrequencyScanner(const HarmonicLoadFlow& hlf)
    : m_hlf(hlf) {}

FrequencyScanner::~FrequencyScanner() = default;

// ============================================================================
// Construccion de Ybus a frecuencia arbitraria
// ============================================================================

FrequencyScanner::SparseMatrixXcd
FrequencyScanner::buildYbusAtFrequency(double frequency) const {
    // Mapeamos la frecuencia a un "orden armonico equivalente"
    // h_eq = f / f_fundamental
    double h_eq = frequency / m_fundamentalFreq;
    int h = static_cast<int>(std::round(h_eq));
    if (h < 1) h = 1;

    // Reutilizamos la construccion de Ybus del HarmonicLoadFlow
    // Pasando h como proxy de la frecuencia
    return m_hlf.buildYbusHarmonic(h);
}

FrequencyScanner::Complex
FrequencyScanner::getDiagonalImpedance(const SparseMatrixXcd& ybus, int busId) const {
    // Extraer Y_ii del elemento diagonal de Ybus
    Complex yii(0.0, 0.0);
    for (Eigen::SparseMatrix<Complex>::InnerIterator it(ybus, busId); it; ++it) {
        if (it.row() == busId) {
            yii = it.value();
            break;
        }
    }

    // Z_th = 1 / Y_ii
    if (std::abs(yii) > 1e-15) {
        return Complex(1.0, 0.0) / yii;
    }
    return Complex(0.0, 0.0);
}

// ============================================================================
// Impedancia de Thevenin a frecuencia dada
// ============================================================================

FrequencyScanner::Complex
FrequencyScanner::calculateTheveninImpedance(int busId, double frequency) const {
    SparseMatrixXcd ybus = buildYbusAtFrequency(frequency);
    return getDiagonalImpedance(ybus, busId);
}

// ============================================================================
// Frequency scan para una barra
// ============================================================================

std::vector<FrequencyScanPoint> FrequencyScanner::scan(int busId,
    double f_min, double f_max, int steps) {

    std::vector<FrequencyScanPoint> result;
    result.reserve(steps + 1);

    if (steps <= 0) return result;

    double df = (f_max - f_min) / static_cast<double>(steps);

    for (int i = 0; i <= steps; ++i) {
        double freq = f_min + i * df;

        // Construir Ybus a esta frecuencia
        double h_eq = freq / m_fundamentalFreq;
        int h = static_cast<int>(std::round(h_eq));
        if (h < 1) h = 1;

        SparseMatrixXcd ybus = m_hlf.buildYbusHarmonic(h);
        Complex z_th = getDiagonalImpedance(ybus, busId);

        FrequencyScanPoint point;
        point.frequency = freq;
        point.impedance = z_th;
        point.magnitude = std::abs(z_th);
        point.angle = std::arg(z_th);

        result.push_back(point);
    }

    m_scanResults[busId] = result;
    return result;
}

// ============================================================================
// Frequency scan para todas las barras
// ============================================================================

std::map<int, std::vector<FrequencyScanPoint>>
FrequencyScanner::scanAllBuses(double f_min, double f_max, int steps) {
    m_scanResults.clear();
    m_resonancePoints.clear();

    // Asumimos un numero razonable de barras (obtenido del HLF)
    // Usamos los modelos para inferir el numero de barras
    // Escaneamos barras 0..N-1 donde N se determina heurísticamente
    const int maxBuses = 1000;  // Límite de seguridad

    for (int busId = 0; busId < maxBuses; ++busId) {
        auto scanResult = scan(busId, f_min, f_max, steps);

        if (scanResult.empty()) break;

        // Detectar resonancias para esta barra
        auto resonances = detectResonance(scanResult);
        if (!resonances.empty()) {
            m_resonancePoints[busId] = resonances;
        }
    }

    return m_scanResults;
}

// ============================================================================
// Deteccion de resonancias
// ============================================================================

std::vector<ResonancePoint> FrequencyScanner::detectResonance(
    const std::vector<FrequencyScanPoint>& scanResult) const {

    std::vector<ResonancePoint> resonances;
    if (scanResult.size() < 3) return resonances;

    const double RESONANCE_THRESHOLD_PARALLEL = 3.0;  // |Z| debe crecer > 3x
    const double RESONANCE_THRESHOLD_SERIES = 0.33;   // |Z| debe decrecer < 1/3
    const size_t MIN_PEAK_DISTANCE = 3;               // Minimo puntos entre picos

    // --- Deteccion de resonancias paralelo (picos en |Z|) ---
    for (size_t i = 1; i + 1 < scanResult.size(); ++i) {
        double prevMag = scanResult[i - 1].magnitude;
        double currMag = scanResult[i].magnitude;
        double nextMag = scanResult[i + 1].magnitude;

        // Pico local: crece y luego decrece
        if (currMag > prevMag && currMag > nextMag) {
            // Verificar que sea significativo
            if (prevMag > 1e-12 && currMag / prevMag > RESONANCE_THRESHOLD_PARALLEL) {
                ResonancePoint rp;
                rp.frequency = scanResult[i].frequency;
                rp.harmonicOrder = static_cast<int>(
                    std::round(rp.frequency / m_fundamentalFreq));
                rp.impedanceMagnitude = currMag;
                rp.type = ResonancePoint::PARALLEL;

                // Calcular Q factor
                rp.qFactor = calculateQFactor(scanResult, i);

                // Verificar que no esté demasiado cerca del anterior
                if (resonances.empty() ||
                    (i - std::find_if(scanResult.begin(), scanResult.end(),
                        [&rp](const auto& p) {
                            return std::abs(p.frequency - rp.frequency) < 0.1;
                        }) >= scanResult.begin() + static_cast<long>(MIN_PEAK_DISTANCE))) {

                    // Evitar duplicados cercanos
                    bool duplicate = false;
                    for (const auto& existing : resonances) {
                        if (std::abs(existing.frequency - rp.frequency) <
                            m_fundamentalFreq * 0.5) {
                            duplicate = true;
                            break;
                        }
                    }
                    if (!duplicate) {
                        resonances.push_back(rp);
                    }
                }
            }
        }

        // --- Deteccion de resonancias serie (valles en |Z|) ---
        if (currMag < prevMag && currMag < nextMag) {
            if (currMag > 1e-12 && nextMag > 1e-12 &&
                currMag / nextMag < RESONANCE_THRESHOLD_SERIES) {
                ResonancePoint rp;
                rp.frequency = scanResult[i].frequency;
                rp.harmonicOrder = static_cast<int>(
                    std::round(rp.frequency / m_fundamentalFreq));
                rp.impedanceMagnitude = currMag;
                rp.type = ResonancePoint::SERIES;
                rp.qFactor = 0.0;  // Q no es significativo para resonancia serie

                // Evitar duplicados cercanos
                bool duplicate = false;
                for (const auto& existing : resonances) {
                    if (std::abs(existing.frequency - rp.frequency) <
                        m_fundamentalFreq * 0.5) {
                        duplicate = true;
                        break;
                    }
                }
                if (!duplicate) {
                    resonances.push_back(rp);
                }
            }
        }
    }

    // Ordenar por frecuencia
    std::sort(resonances.begin(), resonances.end(),
              [](const ResonancePoint& a, const ResonancePoint& b) {
                  return a.frequency < b.frequency;
              });

    return resonances;
}

// ============================================================================
// Deteccion de resonancias para todas las barras
// ============================================================================

std::map<int, std::vector<ResonancePoint>>
FrequencyScanner::detectAllResonances() const {
    return m_resonancePoints;
}

// ============================================================================
// Calcular Q factor
// ============================================================================

double FrequencyScanner::calculateQFactor(
    const std::vector<FrequencyScanPoint>& scanResult, size_t peakIdx) const {

    if (peakIdx >= scanResult.size()) return 0.0;

    double peakMag = scanResult[peakIdx].magnitude;
    double peakFreq = scanResult[peakIdx].frequency;

    if (peakMag < 1e-12) return 0.0;

    // Encontrar ancho de banda a -3dB: |Z| = peakMag / sqrt(2)
    double halfPowerMag = peakMag / std::sqrt(2.0);

    // Buscar frecuencias de corte inferior y superior
    double f_lower = 0.0, f_upper = 0.0;

    // Buscar hacia abajo
    for (int i = static_cast<int>(peakIdx); i >= 0; --i) {
        if (scanResult[i].magnitude <= halfPowerMag) {
            f_lower = scanResult[i].frequency;
            break;
        }
    }
    if (f_lower == 0.0 && peakIdx > 0) {
        f_lower = scanResult[0].frequency;
    }

    // Buscar hacia arriba
    for (size_t i = peakIdx; i < scanResult.size(); ++i) {
        if (scanResult[i].magnitude <= halfPowerMag) {
            f_upper = scanResult[i].frequency;
            break;
        }
    }
    if (f_upper == 0.0 && peakIdx + 1 < scanResult.size()) {
        f_upper = scanResult.back().frequency;
    }

    double bandwidth = f_upper - f_lower;
    if (bandwidth < 1e-6) return 0.0;

    // Q = f_res / (f_upper - f_lower)
    return peakFreq / bandwidth;
}

// ============================================================================
// Estimacion de ancho de banda
// ============================================================================

std::pair<double, double> FrequencyScanner::estimateBandwidth(
    const std::vector<FrequencyScanPoint>& scanResult, size_t peakIdx) const {

    double halfPowerMag = scanResult[peakIdx].magnitude / std::sqrt(2.0);
    double f_lower = scanResult.front().frequency;
    double f_upper = scanResult.back().frequency;

    // Buscar cruce inferior
    for (int i = static_cast<int>(peakIdx); i >= 0; --i) {
        if (scanResult[i].magnitude <= halfPowerMag) {
            // Interpolacion lineal
            if (i + 1 <= static_cast<int>(peakIdx) && i + 1 < static_cast<int>(scanResult.size())) {
                double m1 = scanResult[i].magnitude;
                double m2 = scanResult[i + 1].magnitude;
                double f1 = scanResult[i].frequency;
                double f2 = scanResult[i + 1].frequency;
                if (m2 != m1) {
                    f_lower = f1 + (halfPowerMag - m1) * (f2 - f1) / (m2 - m1);
                }
            } else {
                f_lower = scanResult[i].frequency;
            }
            break;
        }
    }

    // Buscar cruce superior
    for (size_t i = peakIdx; i < scanResult.size(); ++i) {
        if (scanResult[i].magnitude <= halfPowerMag) {
            if (i > 0 && i - 1 >= peakIdx) {
                double m1 = scanResult[i - 1].magnitude;
                double m2 = scanResult[i].magnitude;
                double f1 = scanResult[i - 1].frequency;
                double f2 = scanResult[i].frequency;
                if (m2 != m1) {
                    f_upper = f1 + (halfPowerMag - m1) * (f2 - f1) / (m2 - m1);
                }
            } else {
                f_upper = scanResult[i].frequency;
            }
            break;
        }
    }

    return {f_lower, f_upper};
}

// ============================================================================
// Plot de impedancia
// ============================================================================

std::vector<std::pair<double, double>> FrequencyScanner::plotImpedance(int busId) const {
    std::vector<std::pair<double, double>> plotData;

    auto it = m_scanResults.find(busId);
    if (it == m_scanResults.end()) {
        return plotData;
    }

    plotData.reserve(it->second.size());
    for (const auto& point : it->second) {
        plotData.emplace_back(point.frequency, point.magnitude);
    }

    return plotData;
}

// ============================================================================
// Getters
// ============================================================================

const std::vector<FrequencyScanPoint>&
FrequencyScanner::getScanResult(int busId) const {
    static const std::vector<FrequencyScanPoint> empty;
    auto it = m_scanResults.find(busId);
    if (it != m_scanResults.end()) {
        return it->second;
    }
    return empty;
}

bool FrequencyScanner::hasScanResults() const {
    return !m_scanResults.empty();
}

} // namespace powsys365
