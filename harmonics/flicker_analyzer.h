/**
 * @file flicker_analyzer.h
 * @brief Analizador de flicker (parpadeo luminico) para POWSYS365
 * @author POWSYS365 Team
 * @copyright 2025 XNOX L.L.C.
 * @version 1.0.0
 *
 * Calcula Pst (short-term flicker severity), Plt (long-term flicker severity)
 * usando el modelo de lampara de referencia IEC 61000-4-15.
 *
 * Segun IEC 61000-3-3 y IEEE 1453:
 * - Pst <= 1.0 (general)
 * - Plt <= 0.8 (general)
 */

#pragma once

#include <vector>
#include <complex>
#include <string>

namespace powsys365 {

/**
 * @brief Resultado del analisis de flicker.
 */
struct FlickerResult {
    double pst = 0.0;             ///< Short-term flicker severity (10 min)
    double plt = 0.0;             ///< Long-term flicker severity (2 horas)
    double pInst = 0.0;           ///< Flicker instantaneo maximo
    double p50 = 0.0;             ///< Percentil 50 de Pinst
    double p1 = 0.0;              ///< Percentil 1 de Pinst
    double p0_1 = 0.0;            ///< Percentil 0.1 de Pinst
    bool pstCompliant = true;     ///< Cumple Pst <= 1.0
    bool pltCompliant = true;     ///< Cumple Plt <= 0.8
    std::vector<double> pInstTimeSeries; ///< Serie temporal de Pinst
};

/**
 * @brief Modelo de lampara de referencia IEC 61000-4-15.
 *
 * La respuesta de la lampara modela la sensibilidad del ojo humano
 * a las fluctuaciones de tension. La funcion de transferencia es:
 *
 * H(s) = k * omega1 * s / (s^2 + 2*lambda*omega1*s + omega1^2)
 *
 * Donde:
 * - omega1 = 2*pi*8.8 rad/s (frecuencia de resonancia del filtro)
 * - lambda = 0.25 (coeficiente de amortiguamiento)
 * - k = 1.74802 (ganancia)
 */
class ReferenceLampModel {
public:
    ReferenceLampModel();
    ~ReferenceLampModel();

    /**
     * @brief Procesa una serie de fluctuaciones de tension.
     *
     * Aplica el modelo de lampara de referencia IEC 61000-4-15
     * a una serie de muestras de fluctuacion de tension.
     *
     * @param voltageFluctuations Serie de deltaV/V (fluctuacion relativa).
     * @param samplingRate Tasa de muestreo [Hz].
     * @return Serie de sensacion de flicker (dimensionless).
     */
    std::vector<double> process(const std::vector<double>& voltageFluctuations,
                                 double samplingRate);

    /**
     * @brief Aplica el filtro de respuesta de lampara a una muestra.
     *
     * Filtro paso-banda centrado en ~8.8 Hz que modela la sensibilidad
     * del ojo humano a fluctuaciones de luz.
     *
     * @param input Muestra de entrada (fluctuacion de tension).
     * @return Salida filtrada (sensacion de flicker).
     */
    double filterSample(double input);

    /**
     * @brief Reinicia el estado interno del filtro.
     */
    void reset();

private:
    // Parametros del modelo IEC 61000-4-15
    static constexpr double OMEGA1 = 2.0 * 3.14159265358979323846 * 8.8;  // 8.8 Hz
    static constexpr double LAMBDA = 0.25;
    static constexpr double K_GAIN = 1.74802;

    // Estado del filtro (forma de espacio de estados)
    double m_x1 = 0.0;  // Estado 1
    double m_x2 = 0.0;  // Estado 2

    // Coeficientes del filtro digital (bilineal/Tustin)
    double m_a1, m_a2, m_b0, m_b1, m_b2;

    /**
     * @brief Inicializa los coeficientes del filtro digital.
     */
    void initializeFilter(double samplingRate);
};

/**
 * @brief Analizador completo de flicker segun IEC 61000.
 */
class FlickerAnalyzer {
public:
    /**
     * @brief Constructor.
     */
    FlickerAnalyzer();
    ~FlickerAnalyzer();

    /**
     * @brief Calcula Pst (short-term flicker severity).
     *
     * Pst se calcula a partir de una serie temporal de sensacion de flicker
     * usando la formula de percentiles de la IEC 61000-4-15:
     *
     * Pst = sqrt(0.0314*P0.1 + 0.0525*P1s + 0.0657*P3s + 0.28*P10s + 0.08*P50s)
     *
     * Donde los percentiles se calculan sobre la distribucion acumulada
     * de la sensacion de flicker instantanea.
     *
     * @param pInstSeries Serie temporal de sensacion de flicker instantanea.
     * @return Pst (periodo tipico: 10 minutos).
     */
    double calculatePst(const std::vector<double>& pInstSeries) const;

    /**
     * @brief Calcula Plt (long-term flicker severity).
     *
     * Plt = cubic_mean(Pst_1, Pst_2, ..., Pst_N)^(1/3)
     *
     * Donde N es el numero de intervalos Pst (tipicamente 12 para 2 horas).
     *
     * @param pstValues Vector de valores Pst.
     * @return Plt (periodo tipico: 2 horas).
     */
    double calculatePlt(const std::vector<double>& pstValues) const;

    /**
     * @brief Analiza flicker desde fluctuaciones de tension.
     *
     * Pipeline completo:
     * 1. Aplicar modelo de lampara de referencia
     * 2. Calcular sensacion de flicker instantanea
     * 3. Calcular percentiles
     * 4. Calcular Pst
     *
     * @param voltageFluctuations Serie temporal de deltaV/V [%].
     * @param samplingRate Tasa de muestreo [Hz].
     * @param observationTime Tiempo de observacion [s] (default: 600s = 10min).
     * @return Resultado del analisis de flicker.
     */
    FlickerResult analyzeFlicker(const std::vector<double>& voltageFluctuations,
                                  double samplingRate = 1000.0,
                                  double observationTime = 600.0);

    /**
     * @brief Evalua cumplimiento con IEC 61000-3-3.
     *
     * Limites:
     * - Pst <= 1.0 (condiciones normales)
     * - Plt <= 0.8 (condiciones normales)
     *
     * @param result Resultado del analisis de flicker.
     * @return true si cumple.
     */
    bool assessIEC61000Compliance(const FlickerResult& result) const;

    /**
     * @brief Evalua cumplimiento Pst individual.
     */
    bool assessPstCompliance(double pst) const;

    /**
     * @brief Evalua cumplimiento Plt individual.
     */
    bool assessPltCompliance(double plt) const;

    /**
     * @brief Calcula la sensacion de flicker instantanea (Pinst).
     *
     * Pinst se obtiene pasando las fluctuaciones por el modelo de lampara
     * y luego por el bloque de cuadratica (simulando la respuesta no lineal).
     *
     * @param voltageFluctuations Serie de fluctuaciones deltaV/V.
     * @param samplingRate Tasa de muestreo [Hz].
     * @return Serie de Pinst.
     */
    std::vector<double> calculatePinst(const std::vector<double>& voltageFluctuations,
                                        double samplingRate);

    /**
     * @brief Genera reporte de analisis de flicker.
     */
    std::string generateReport(const FlickerResult& result) const;

    /**
     * @brief Obtiene el modelo de lampara de referencia.
     */
    ReferenceLampModel& getLampModel();

private:
    ReferenceLampModel m_lampModel;

    /**
     * @brief Calcula percentiles de una distribucion.
     */
    double calculatePercentile(const std::vector<double>& sortedData,
                                double percentile) const;
};

} // namespace powsys365
