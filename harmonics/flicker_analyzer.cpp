/**
 * @file flicker_analyzer.cpp
 * @brief Implementacion del analizador de flicker para POWSYS365
 * @author POWSYS365 Team
 * @copyright 2025 XNOX L.L.C.
 * @version 1.0.0
 */

#include "flicker_analyzer.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <sstream>
#include <iomanip>

namespace powsys365 {

// ============================================================================
// Modelo de lampara de referencia IEC 61000-4-15
// ============================================================================

ReferenceLampModel::ReferenceLampModel() {
    initializeFilter(1000.0);  // Default 1 kHz
}

ReferenceLampModel::~ReferenceLampModel() = default;

void ReferenceLampModel::initializeFilter(double samplingRate) {
    // Filtro paso-banda IEC 61000-4-15 centrado en 8.8 Hz
    // Funcion de transferencia analógica:
    // H(s) = k * omega1 * s / (s^2 + 2*lambda*omega1*s + omega1^2)
    //
    // Discretizacion bilineal (Tustin):
    // s = (2/T) * (z-1)/(z+1)

    double T = 1.0 / samplingRate;
    double c = 2.0 / T;

    double omega1_sq = OMEGA1 * OMEGA1;
    double two_lambda_omega1 = 2.0 * LAMBDA * OMEGA1;

    // Coeficientes del filtro digital: y[n] = (b0*x[n] + b1*x[n-1] + b2*x[n-2]
    //                                          - a1*y[n-1] - a2*y[n-2]) / a0
    double a0 = c * c + two_lambda_omega1 * c + omega1_sq;
    m_a1 = 2.0 * (omega1_sq - c * c) / a0;
    m_a2 = (c * c - two_lambda_omega1 * c + omega1_sq) / a0;
    m_b0 = K_GAIN * OMEGA1 * c / a0;
    m_b1 = 0.0;  // Numerador impar
    m_b2 = -K_GAIN * OMEGA1 * c / a0;
}

void ReferenceLampModel::reset() {
    m_x1 = 0.0;
    m_x2 = 0.0;
}

double ReferenceLampModel::filterSample(double input) {
    // Forma de espacio de estados simplificada
    // Estado: x1[n] = -a1*x1[n-1] - a2*x2[n-1] + input[n]
    //         x2[n] = x1[n-1]
    // Salida:  y[n] = b0*x1[n] + b1*x2[n] + b2*x2[n-1]

    double x1_new = -m_a1 * m_x1 - m_a2 * m_x2 + input;
    double output = m_b0 * x1_new + m_b1 * m_x1 + m_b2 * m_x2;

    m_x2 = m_x1;
    m_x1 = x1_new;

    return output;
}

std::vector<double> ReferenceLampModel::process(
    const std::vector<double>& voltageFluctuations, double samplingRate) {

    if (voltageFluctuations.empty()) return {};

    initializeFilter(samplingRate);
    reset();

    std::vector<double> output;
    output.reserve(voltageFluctuations.size());

    for (double v : voltageFluctuations) {
        output.push_back(filterSample(v));
    }

    return output;
}

// ============================================================================
// FlickerAnalyzer - Constructor
// ============================================================================

FlickerAnalyzer::FlickerAnalyzer() = default;
FlickerAnalyzer::~FlickerAnalyzer() = default;

// ============================================================================
// Calcular Pinst (sensacion de flicker instantanea)
// ============================================================================

std::vector<double> FlickerAnalyzer::calculatePinst(
    const std::vector<double>& voltageFluctuations, double samplingRate) {

    if (voltageFluctuations.empty()) return {};

    // Paso 1: Aplicar modelo de lampara de referencia
    std::vector<double> lampResponse = m_lampModel.process(
        voltageFluctuations, samplingRate);

    // Paso 2: Bloque cuadratico (simula respuesta no lineal del ojo)
    // Pinst = 2 * |lampResponse|^2 (simplificado del modelo IEC)
    std::vector<double> pinst;
    pinst.reserve(lampResponse.size());

    for (double r : lampResponse) {
        // La sensacion de flicker es proporcional al cuadrado de la respuesta
        // con un factor de escala que normaliza a Pst = 1 para umbral perceptible
        double p = 2.0 * r * r;
        pinst.push_back(p);
    }

    return pinst;
}

// ============================================================================
// Calcular percentil
// ============================================================================

double FlickerAnalyzer::calculatePercentile(const std::vector<double>& data,
    double percentile) const {

    if (data.empty()) return 0.0;

    std::vector<double> sorted = data;
    std::sort(sorted.begin(), sorted.end());

    // Metodo de interpolacion lineal
    double rank = (percentile / 100.0) * (sorted.size() - 1);
    size_t lowerIdx = static_cast<size_t>(std::floor(rank));
    size_t upperIdx = static_cast<size_t>(std::ceil(rank));

    if (lowerIdx >= sorted.size()) lowerIdx = sorted.size() - 1;
    if (upperIdx >= sorted.size()) upperIdx = sorted.size() - 1;

    double frac = rank - std::floor(rank);

    return sorted[lowerIdx] + frac * (sorted[upperIdx] - sorted[lowerIdx]);
}

// ============================================================================
// Calcular Pst
// ============================================================================

double FlickerAnalyzer::calculatePst(const std::vector<double>& pInstSeries) const {
    if (pInstSeries.empty()) return 0.0;

    // Formula de percentiles IEC 61000-4-15:
    // Pst = sqrt(0.0314*P0.1 + 0.0525*P1 + 0.0657*P3 + 0.28*P10 + 0.08*P50)
    //
    // Donde P0.1, P1, P3, P10, P50 son percentiles de la CDF de Pinst

    double p0_1 = calculatePercentile(pInstSeries, 0.1);
    double p1 = calculatePercentile(pInstSeries, 1.0);
    double p3 = calculatePercentile(pInstSeries, 3.0);
    double p10 = calculatePercentile(pInstSeries, 10.0);
    double p50 = calculatePercentile(pInstSeries, 50.0);

    // Aplicar formula IEC 61000-4-15
    double pst_sq = 0.0314 * p0_1 +
                     0.0525 * p1 +
                     0.0657 * p3 +
                     0.2800 * p10 +
                     0.0800 * p50;

    return std::sqrt(pst_sq);
}

// ============================================================================
// Calcular Plt
// ============================================================================

double FlickerAnalyzer::calculatePlt(const std::vector<double>& pstValues) const {
    if (pstValues.empty()) return 0.0;

    // Plt = cubic_mean(Pst_1, Pst_2, ..., Pst_N)
    //     = ( (1/N) * sum(Pst_i^3) )^(1/3)
    //
    // Periodo tipico: 12 intervalos Pst de 10 min = 2 horas

    double sumCubes = 0.0;
    for (double pst : pstValues) {
        sumCubes += pst * pst * pst;
    }

    double meanCube = sumCubes / static_cast<double>(pstValues.size());

    return std::cbrt(meanCube);
}

// ============================================================================
// Analisis completo de flicker
// ============================================================================

FlickerResult FlickerAnalyzer::analyzeFlicker(
    const std::vector<double>& voltageFluctuations,
    double samplingRate, double observationTime) {

    FlickerResult result;

    if (voltageFluctuations.empty()) {
        return result;
    }

    // Paso 1: Calcular Pinst
    result.pInstTimeSeries = calculatePinst(voltageFluctuations, samplingRate);

    if (result.pInstTimeSeries.empty()) {
        return result;
    }

    // Pinst maximo
    result.pInst = *std::max_element(result.pInstTimeSeries.begin(),
                                      result.pInstTimeSeries.end());

    // Percentiles de Pinst
    result.p50 = calculatePercentile(result.pInstTimeSeries, 50.0);
    result.p1 = calculatePercentile(result.pInstTimeSeries, 1.0);
    result.p0_1 = calculatePercentile(result.pInstTimeSeries, 0.1);

    // Paso 2: Calcular Pst
    result.pst = calculatePst(result.pInstTimeSeries);

    // Paso 3: Simular Plt (usamos el mismo Pst si solo tenemos un intervalo)
    // Para multiples intervalos de 10 min, usaríamos calculatePlt
    if (observationTime >= 7200.0 && result.pst > 0.0) {
        // Simular ~12 intervalos Pst para 2 horas
        int numIntervals = static_cast<int>(observationTime / 600.0);
        std::vector<double> pstValues(numIntervals, result.pst);
        result.plt = calculatePlt(pstValues);
    } else {
        // Plt ≈ Pst para observaciones cortas
        result.plt = result.pst * 0.85;  // Factor tipico
    }

    // Evaluacion de cumplimiento
    result.pstCompliant = assessPstCompliance(result.pst);
    result.pltCompliant = assessPltCompliance(result.plt);

    return result;
}

// ============================================================================
// Evaluacion de cumplimiento
// ============================================================================

bool FlickerAnalyzer::assessPstCompliance(double pst) const {
    // IEC 61000-3-3: Pst <= 1.0
    // IEEE 1453: Pst <= 1.0 (tipico)
    return (pst <= 1.0);
}

bool FlickerAnalyzer::assessPltCompliance(double plt) const {
    // IEC 61000-3-3: Plt <= 0.8
    // IEEE 1453: Plt <= 0.8
    return (plt <= 0.8);
}

bool FlickerAnalyzer::assessIEC61000Compliance(const FlickerResult& result) const {
    return result.pstCompliant && result.pltCompliant;
}

// ============================================================================
// Reporte
// ============================================================================

std::string FlickerAnalyzer::generateReport(const FlickerResult& result) const {
    std::stringstream report;

    report << "========================================\n";
    report << "REPORTE DE ANALISIS DE FLICKER\n";
    report << "========================================\n\n";

    report << "Parametros IEC 61000-4-15:\n";
    report << "  Pst (10 min): " << std::fixed << std::setprecision(3)
           << result.pst << "\n";
    report << "  Plt (2 hrs):  " << std::setprecision(3) << result.plt << "\n";
    report << "  Pinst max:    " << std::setprecision(4) << result.pInst << "\n";
    report << "  P50:          " << std::setprecision(4) << result.p50 << "\n";
    report << "  P1:           " << std::setprecision(4) << result.p1 << "\n";
    report << "  P0.1:         " << std::setprecision(4) << result.p0_1 << "\n\n";

    report << "Cumplimiento:\n";
    report << "  IEC 61000-3-3 Pst: "
           << (result.pstCompliant ? "CUMPLE (Pst <= 1.0)" : "NO CUMPLE")
           << "\n";
    report << "  IEC 61000-3-3 Plt: "
           << (result.pltCompliant ? "CUMPLE (Plt <= 0.8)" : "NO CUMPLE")
           << "\n";
    report << "  IEC 61000-3-3 General: "
           << (assessIEC61000Compliance(result) ? "CUMPLE" : "NO CUMPLE")
           << "\n";

    return report.str();
}

// ============================================================================
// Getter del modelo de lampara
// ============================================================================

ReferenceLampModel& FlickerAnalyzer::getLampModel() {
    return m_lampModel;
}

} // namespace powsys365
