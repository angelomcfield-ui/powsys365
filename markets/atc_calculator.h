/**
 * @file atc_calculator.h
 * @brief Calculador de ATC/TTC para POWSYS365
 * @author POWSYS365 Team
 * @copyright 2025 XNOX L.L.C.
 * @version 1.0.0
 *
 * Calcula Available Transfer Capability (ATC), Total Transfer
 * Capability (TTC), Transfer Reliability Margin (TRM), y
 * Capacity Benefit Margin (CBM) con consideracion N-1.
 *
 * Formulas:
 *   ATC = TTC - TRM - CBM - ETC
 *   TTC = min(C_i) sobre todas las contingencias
 *   TRM = 5-10% de TTC (margen de confiabilidad)
 *   CBM = 5% de TTC (margen de beneficio por capacidad)
 */

#pragma once

#include "lmp_calculator.h"
#include <Eigen/Dense>
#include <vector>
#include <map>
#include <string>

namespace powsys365 {

/**
 * @brief Resultado de ATC para un par de barras/areas.
 */
struct ATCResult {
    int fromBus = 0;              ///< Barra/area origen
    int toBus = 0;                ///< Barra/area destino
    double ttc = 0.0;             ///< Total Transfer Capability [MW]
    double trm = 0.0;             ///< Transfer Reliability Margin [MW]
    double cbm = 0.0;             ///< Capacity Benefit Margin [MW]
    double etc = 0.0;             ///< Existing Transmission Commitments [MW]
    double atc = 0.0;             ///< Available Transfer Capability [MW]
    double baseCaseFlow = 0.0;    ///< Flujo en caso base [MW]
    bool n1Secure = true;         ///< Es seguro bajo contingencias N-1
    std::vector<std::pair<int, double>> limitingContingencies; ///< Contingencias limitantes (id, flujo)
    std::vector<std::pair<int, double>> limitingLines;         ///< Lineas limitantes (id, margen)
};

/**
 * @brief Calculador de ATC/TTC con consideracion N-1.
 *
 * Implementa el calculo completo de capacidad de transferencia
 * disponible entre areas o nodos del sistema, incluyendo:
 * - TTC: capacidad maxima antes de violaciones
 * - TRM: margen de confiabilidad para incertidumbre
 * - CBM: margen para beneficio por capacidad
 * - ATC = TTC - TRM - CBM - ETC
 * - Verificacion de seguridad N-1
 */
class ATCCalculator {
public:
    /**
     * @brief Constructor.
     */
    ATCCalculator();
    ~ATCCalculator();

    /**
     * @brief Establece los datos de entrada del sistema.
     */
    void setSystemData(const OPFInputData& data);

    /**
     * @brief Calcula el ATC entre dos barras.
     *
     * Pipeline:
     * 1. Calcular TTC (maxima transferencia sin violaciones)
     * 2. Calcular TRM (margen de confiabilidad)
     * 3. Calcular CBM (margen de beneficio)
     * 4. Obtener ETC (compromisos existentes)
     * 5. ATC = TTC - TRM - CBM - ETC
     *
     * @param fromBus Barra/area origen.
     * @param toBus Barra/area destino.
     * @param trmPercent Porcentaje TRM (default: 10%).
     * @param cbmPercent Porcentaje CBM (default: 5%).
     * @return Resultado ATC completo.
     */
    ATCResult calculateATC(int fromBus, int toBus,
                           double trmPercent = 0.10,
                           double cbmPercent = 0.05);

    /**
     * @brief Calcula TTC (Total Transfer Capability).
     *
     * TTC es la maxima transferencia de potencia posible entre
     * dos puntos del sistema sin violar limites de seguridad.
     *
     * Se calcula como el minimo margen sobre todas las lineas:
     * TTC = min(limit_k - |flow_k|) para todas las lineas k
     *
     * @param fromBus Barra origen.
     * @param toBus Barra destino.
     * @return TTC [MW].
     */
    double calculateTTC(int fromBus, int toBus) const;

    /**
     * @brief Calcula TTC considerando contingencias N-1.
     *
     * Para cada contingencia (falla de una linea), recalcula los
     * flujos y determina el TTC resultante. El TTC final es el
     * minimo sobre todas las contingencias.
     *
     * @param fromBus Barra origen.
     * @param toBus Barra destino.
     * @return TTC bajo contingencias N-1 [MW].
     */
    double calculateTTC_N1(int fromBus, int toBus) const;

    /**
     * @brief Calcula TRM (Transfer Reliability Margin).
     *
     * TRM = porcentaje * TTC (tipicamente 5-10%)
     *
     * Cubre la incertidumbre en las condiciones del sistema
     * y las variaciones de carga/generacion.
     *
     * @param ttc Total Transfer Capability [MW].
     * @param percent Porcentaje (default: 0.10 = 10%).
     * @return TRM [MW].
     */
    double calculateTRM(double ttc, double percent = 0.10) const;

    /**
     * @brief Calcula CBM (Capacity Benefit Margin).
     *
     * CBM = porcentaje * TTC (tipicamente 5%)
     *
     * Margen reservado para obtener beneficios de mercado
     * mediante importacion de energia de areas vecinas.
     *
     * @param ttc Total Transfer Capability [MW].
     * @param percent Porcentaje (default: 0.05 = 5%).
     * @return CBM [MW].
     */
    double calculateCBM(double ttc, double percent = 0.05) const;

    /**
     * @brief Estima ETC (Existing Transmission Commitments).
     *
     * ETC representa los compromisos de transmision existentes
     * que ya estan asignados en el mercado.
     *
     * @return ETC [MW].
     */
    double calculateETC() const;

    /**
     * @brief Calcula ATC para todos los pares de barras.
     * @return Mapa de (fromBus, toBus) a resultado ATC.
     */
    std::map<std::pair<int, int>, ATCResult> calculateATCForAllPairs();

    /**
     * @brief Verifica seguridad N-1 para un par de barras.
     *
     * Para cada linea, simula su falla y verifica que ninguna
     * otra linea exceda su limite de capacidad.
     *
     * @param fromBus Barra origen.
     * @param toBus Barra destino.
     * @param transferAmount Cantidad de transferencia [MW].
     * @return true si es seguro bajo todas las contingencias N-1.
     */
    bool checkN1Security(int fromBus, int toBus,
                          double transferAmount) const;

    /**
     * @brief Simula una contingencia (falla de linea).
     *
     * Aplica LODF para recalcular flujos post-contingencia:
     * flow_post_k = flow_pre_k + LODF_kl * flow_pre_l
     *
     * @param lineOut Linea que falla.
     * @return Vector de flujos post-contingencia.
     */
    Eigen::VectorXd simulateContingency(int lineOut) const;

    /**
     * @brief Genera reporte de ATC.
     */
    std::string generateATCReport() const;

    /**
     * @brief Obtiene los resultados ATC almacenados.
     */
    const std::map<std::pair<int, int>, ATCResult>& getResults() const;

private:
    OPFInputData m_opfData;
    std::map<std::pair<int, int>, ATCResult> m_atcResults;

    /**
     * @brief Calcula LODF para una contingencia especifica.
     */
    Eigen::VectorXd calculateLODFVector(int lineOut) const;

    /**
     * @brief Verifica que las lineas no excedan sus limites.
     */
    bool checkLineLimits(const Eigen::VectorXd& flows,
                          std::vector<std::pair<int, double>>& violations) const;
};

} // namespace powsys365
