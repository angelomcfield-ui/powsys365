/**
 * @file resonance_analyzer.h
 * @brief Analizador de resonancia para POWSYS365
 * @author POWSYS365 Team
 * @copyright 2025 XNOX L.L.C.
 * @version 1.0.0
 *
 * Identifica condiciones de resonancia, calcula factor Q,
 * y evalua riesgo basado en IEEE 519.
 */

#pragma once

#include "frequency_scan.h"
#include <vector>
#include <map>
#include <string>

namespace powsys365 {

/**
 * @brief Nivel de riesgo de resonancia.
 */
enum class ResonanceRisk {
    LOW,      ///< Riesgo bajo: Z_res bajo, lejos de armonicos caracteristicos
    MEDIUM,   ///< Riesgo medio: resonancia cerca de armonicos tipicos
    HIGH      ///< Riesgo alto: resonancia en orden armonico con fuentes
};

/**
 * @brief Resultado del analisis de una resonancia.
 */
struct ResonanceAnalysis {
    double frequency = 0.0;           ///< Frecuencia de resonancia [Hz]
    int harmonicOrder = 0;            ///< Orden armonico aproximado
    double qFactor = 0.0;             ///< Factor de calidad Q
    double impedanceMagnitude = 0.0;  ///< |Z_res| [pu]
    ResonanceRisk riskLevel;          ///< Nivel de riesgo
    std::string riskDescription;      ///< Descripcion del riesgo
    double proximityToSource = 0.0;   ///< Proximidad a fuente armonica [0,1]
    double voltageAmplification = 0.0; ///< Factor de amplificacion de tension
};

/**
 * @brief Analizador completo de condiciones de resonancia.
 *
 * Identifica puntos de resonancia a partir de frequency scans,
 * calcula el factor de calidad Q, evalua el riesgo segun IEEE 519,
 y determina la proximidad a fuentes armonicas.
 */
class ResonanceAnalyzer {
public:
    /**
     * @brief Constructor.
     */
    ResonanceAnalyzer();
    ~ResonanceAnalyzer();

    /**
     * @brief Ejecuta el analisis completo de resonancias.
     *
     * Combina frequency scan, deteccion de resonancias y evaluacion
     * de riesgo para todas las barras del sistema.
     *
     * @param scanner FrequencyScanner con resultados de scan.
     * @param harmonicOrders Ordenes armonicos con fuentes activas.
     * @return Mapa de busId a lista de analisis de resonancia.
     */
    std::map<int, std::vector<ResonanceAnalysis>> analyze(
        const FrequencyScanner& scanner,
        const std::vector<int>& harmonicOrders);

    /**
     * @brief Obtiene los puntos de resonancia detectados.
     *
     * Cada punto incluye: frecuencia, Q-factor, impedancia.
     *
     * @return Mapa de busId a lista de puntos de resonancia.
     */
    std::map<int, std::vector<ResonancePoint>> getResonancePoints() const;

    /**
     * @brief Evalua el riesgo de una resonancia.
     *
     * Basado en IEEE 519 y proximidad a fuentes armonicas:
     * - HIGH: Q > 30 y resonancia coincide con orden armonico activo
     * - MEDIUM: Q > 10 o proximidad < 1 orden a fuente activa
     * - LOW: Q <= 10 y lejos de fuentes armonicas
     *
     * @param resonance Punto de resonancia.
     * @param activeHarmonics Ordenes armonicos con fuentes activas.
     * @return Nivel de riesgo.
     */
    ResonanceRisk assessRisk(const ResonancePoint& resonance,
                              const std::vector<int>& activeHarmonics) const;

    /**
     * @brief Evalua riesgo con informacion completa.
     */
    ResonanceAnalysis assessRiskDetailed(const ResonancePoint& resonance,
                                          const std::vector<int>& activeHarmonics) const;

    /**
     * @brief Genera reporte de analisis de resonancia.
     */
    std::string generateReport() const;

    /**
     * @brief Obtiene el analisis completo para una barra.
     */
    std::vector<ResonanceAnalysis> getAnalysisForBus(int busId) const;

    /**
     * @brief Verifica si hay barras con riesgo HIGH.
     */
    bool hasHighRiskResonances() const;

    /**
     * @brief Obtiene el numero total de resonancias detectadas.
     */
    size_t getTotalResonanceCount() const;

    /**
     * @brief Obtiene el numero de resonancias por nivel de riesgo.
     */
    std::map<ResonanceRisk, size_t> getRiskDistribution() const;

private:
    std::map<int, std::vector<ResonancePoint>> m_resonancePoints;
    std::map<int, std::vector<ResonanceAnalysis>> m_analyses;

    /**
     * @brief Calcula la proximidad a la fuente armonica mas cercana.
     * @return Valor en [0,1] donde 1.0 = coincidencia exacta.
     */
    double calculateSourceProximity(int resonanceOrder,
                                     const std::vector<int>& activeHarmonics) const;

    /**
     * @brief Calcula el factor de amplificacion de tension esperado.
     */
    double calculateVoltageAmplification(double qFactor,
                                          double sourceImpedance) const;

    /**
     * @brief Convierte nivel de riesgo a string descriptivo.
     */
    static std::string riskToString(ResonanceRisk risk);
};

} // namespace powsys365
