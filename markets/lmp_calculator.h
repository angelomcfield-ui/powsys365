/**
 * @file lmp_calculator.h
 * @brief Calculador de LMP (Locational Marginal Price) para POWSYS365
 * @author POWSYS365 Team
 * @copyright 2025 XNOX L.L.C.
 * @version 1.0.0
 *
 * Calcula precios nodales LMP con descomposicion en componentes:
 *   LMP = lambda (energia) + mu (congestion) + gamma (perdidas)
 *
 * El LMP en un nodo i se define como el costo incremental de suministrar
 * un MW adicional de demanda en ese nodo:
 *   LMP_i = lambda + mu * dG/dP_i + lambda * dL/dP_i
 *
 * Donde:
 * - lambda = multiplicador de Lagrange del balance de potencia
 * - mu = multiplicadores de las restricciones de flujo en lineas
 * - G = restricciones de capacidad de lineas
 * - L = perdidas del sistema
 */

#pragma once

#include <Eigen/Dense>
#include <Eigen/Sparse>
#include <vector>
#include <map>
#include <tuple>
#include <string>

namespace powsys365 {

/**
 * @brief Resultados de OPF necesarios para calculo de LMP.
 */
struct OPFInputData {
    Eigen::VectorXd busVoltages;              ///< Tensiones de barra [pu]
    Eigen::VectorXd busAngles;                ///< Angulos de barra [rad]
    Eigen::VectorXd generatorPower;           ///< Potencias de generadores [MW]
    Eigen::VectorXd generatorCosts;           ///< Costos marginales [USD/MWh]
    Eigen::VectorXd lineFlows;                ///< Flujos en lineas [MW]
    Eigen::VectorXd lineLimits;               ///< Limites de lineas [MW]
    Eigen::VectorXd lineFromBus;              ///< Barra origen de cada linea
    Eigen::VectorXd lineToBus;                ///< Barra destino de cada linea
    Eigen::VectorXd lineResistance;           ///< Resistencia de cada linea [pu]
    Eigen::VectorXd lineReactance;            ///< Reactancia de cada linea [pu]
    double lambdaEnergy = 0.0;                ///< Componente de energia [USD/MWh]
    Eigen::VectorXd lambdaCongestion;         ///< Multiplicadores de congestion
    Eigen::VectorXd lambdaLosses;             ///< Sensibilidad a perdidas
    double totalGeneration = 0.0;             ///< Generacion total [MW]
    double totalDemand = 0.0;                 ///< Demanda total [MW]
    double totalLosses = 0.0;                 ///< Perdidas totales [MW]
    int slackBus = 0;                         ///< Barra de referencia
};

/**
 * @brief Resultado LMP para una barra.
 */
struct LMPResult {
    int busId = 0;                            ///< ID de la barra
    double lmp = 0.0;                         ///< LMP total [USD/MWh]
    double energyComponent = 0.0;             ///< Componente de energia (lambda)
    double congestionComponent = 0.0;         ///< Componente de congestion (mu)
    double lossesComponent = 0.0;             ///< Componente de perdidas (gamma)
    double marginalCost = 0.0;                ///< Costo marginal de generacion
    bool isReferenceBus = false;              ///< Es barra de referencia
};

/**
 * @brief Calculador de precios nodales LMP.
 *
 * Implementa el calculo de LMP via descomposicion en tres componentes
 * basado en la solucion dual del OPF:
 *
 * LMP = lambda * 1^T + mu^T * dG/dP + nu^T * dL/dP
 *
 * donde lambda, mu, nu son los multiplicadores de Lagrange.
 */
class LMPricingCalculator {
public:
    /**
     * @brief Constructor.
     */
    LMPricingCalculator();
    ~LMPricingCalculator();

    /**
     * @brief Establece los datos de entrada del OPF.
     */
    void setOPFData(const OPFInputData& data);

    /**
     * @brief Calcula el LMP para una barra especifica.
     *
     * LMP_i = lambda + congestion_i + losses_i
     *
     * @param busId ID de la barra (indice).
     * @return Resultado LMP con descomposicion completa.
     */
    LMPResult calculateLMP(int busId) const;

    /**
     * @brief Calcula LMP para todas las barras.
     * @return Vector de resultados LMP por barra.
     */
    std::vector<LMPResult> calculateForAllBuses() const;

    /**
     * @brief Descompone el LMP en sus tres componentes.
     *
     * @param busId ID de la barra.
     * @return Tupla (energy, congestion, losses) en USD/MWh.
     */
    std::tuple<double, double, double> decomposeLMP(int busId) const;

    /**
     * @brief Calcula el componente de energia (lambda).
     *
     * LMP_energy = lambda (multiplicador de Lagrange de balance)
     *
     * Este componente es identico en todos los nodos cuando no hay congestion.
     *
     * @return Componente de energia [USD/MWh].
     */
    double calculateEnergyComponent() const;

    /**
     * @brief Calcula el componente de congestion para una barra.
     *
     * LMP_congestion = sum(mu_k * PTDF_ki) para todas las lineas k
     *
     * Donde PTDF_ki es el factor de distribucion de transferencia de
     * potencia de la linea k respecto a la barra i.
     *
     * @param busId ID de la barra.
     * @return Componente de congestion [USD/MWh].
     */
    double calculateCongestionComponent(int busId) const;

    /**
     * @brief Calcula el componente de perdidas para una barra.
     *
     * LMP_losses = lambda * dL/dP_i
     *
     * Donde dL/dP_i es la sensibilidad de las perdidas totales a
     * cambios en la inyeccion en la barra i.
     *
     * @param busId ID de la barra.
     * @return Componente de perdidas [USD/MWh].
     */
    double calculateLossesComponent(int busId) const;

    /**
     * @brief Calcula los PTDF (Power Transfer Distribution Factors).
     *
     * PTDF_ki = (X_k * (e_i - e_j)^T * B'^(-1)) / X_k
     *         = fila i de B'^(-1) evaluada en los nodos de la linea k
     *
     * @return Matriz PTDF [linea x barra].
     */
    Eigen::SparseMatrix<double> calculatePTDF() const;

    /**
     * @brief Calcula los LODF (Line Outage Distribution Factors).
     *
     * LODF_kl = PTDF_kl / (1 - PTDF_ll)
     *
     * @return Matriz LODF [linea_monitorada x linea_fallada].
     */
    Eigen::SparseMatrix<double> calculateLODF() const;

    /**
     * @brief Calcula los GSF (Generation Shift Factors).
     *
     * GSF_ki = PTDF_ki - PTDF_k_ref
     *
     * @return Matriz GSF [linea x barra].
     */
    Eigen::SparseMatrix<double> calculateGSF() const;

    /**
     * @brief Identifica nodos con congestion.
     *
     * Un nodo esta congestionado si su LMP difiere del promedio
     * por mas del umbral especificado.
     *
     * @param threshold Umbral de desviacion [USD/MWh].
     * @return Mapa de busId a desviacion de LMP.
     */
    std::map<int, double> identifyCongestedNodes(double threshold = 1.0) const;

    /**
     * @brief Identifica lineas congestionadas.
     * @return Vector de indices de lineas congestionadas.
     */
    std::vector<int> identifyCongestedLines() const;

    /**
     * @brief Calcula el FTR (Financial Transmission Right) entre dos nodos.
     *
     * FTR = (LMP_sink - LMP_source) * cantidad
     *
     * @param sourceBus Barra origen.
     * @param sinkBus Barra destino.
     * @param amount Cantidad de FTR [MW].
     * @return Valor del FTR [USD/h].
     */
    double calculateFTR(int sourceBus, int sinkBus, double amount) const;

    /**
     * @brief Genera reporte de LMP.
     */
    std::string generateLMPReport() const;

    /**
     * @brief Obtiene el LMP promedio del sistema.
     */
    double getAverageLMP() const;

    /**
     * @brief Obtiene el LMP maximo.
     */
    double getMaxLMP() const;

    /**
     * @brief Obtiene el LMP minimo.
     */
    double getMinLMP() const;

private:
    OPFInputData m_opfData;
    mutable std::vector<LMPResult> m_cachedResults;

    /**
     * @brief Calcula la matriz de susceptancia reducida B'.
     */
    Eigen::SparseMatrix<double> buildBPrimeMatrix() const;

    /**
     * @brief Calcula la sensibilidad de perdidas respecto a potencia.
     */
    double lossSensitivityFactor(int busId) const;

    /**
     * @brief Verifica que los datos OPF sean validos.
     */
    bool validateOPFData() const;
};

} // namespace powsys365
