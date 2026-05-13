/**
 * @file thd_calculator.h
 * @brief Calculador de THD (Total Harmonic Distortion) para POWSYS365
 * @author POWSYS365 Team
 * @copyright 2025 XNOX L.L.C.
 * @version 1.0.0
 *
 * Calcula THDv, THDi, TDD y verifica cumplimiento IEEE 519 / IEC 61000-3-6.
 *
 * Formulas:
 *   THD_V = sqrt(sum(V_h^2) / V_1^2) * 100%,  h=2..50
 *   THD_I = sqrt(sum(I_h^2) / I_1^2) * 100%,  h=2..50
 *   TDD   = sqrt(sum(I_h^2) / I_L^2) * 100%,  h=2..50
 */

#pragma once

#include "harmonic_load_flow.h"
#include <Eigen/Dense>
#include <vector>
#include <complex>
#include <map>

namespace powsys365 {

/**
 * @brief Componente armonico individual.
 */
struct HarmonicComponent {
    int order = 0;                    ///< Orden armonico h
    double magnitude = 0.0;           ///< Magnitud [pu o V o A]
    double angle = 0.0;               ///< Angulo de fase [rad]
    double distortionPercent = 0.0;   ///< Distorsion individual DI_h [%]
};

/**
 * @brief Resultado de cumplimiento IEEE 519.
 */
struct IEEE519Compliance {
    bool compliant = true;            ///< Cumple el estandar
    double thdVoltageLimit = 5.0;     ///< Limite THDv [%]
    double thdCurrentLimit = 8.0;     ///< Limite THDi [%]
    double actualTHDv = 0.0;          ///< THDv medido [%]
    double actualTHDi = 0.0;          ///< THDi medido [%]
    std::vector<int> violatingOrders; ///< Ordenes que exceden limites individuales
    int voltageLevel = 1;             ///< 0=LV, 1=MV, 2=HV
};

/**
 * @brief Calculador de THD y distorsion armonica total.
 */
class THDCalculator {
public:
    using Complex = std::complex<double>;
    using VectorXcd = Eigen::VectorXcd;

    /**
     * @brief Constructor.
     */
    THDCalculator();
    ~THDCalculator();

    /**
     * @brief Establece los resultados del flujo de carga armonico.
     */
    void setHarmonicResults(const HarmonicLoadFlow::HarmonicLFResults& results);

    /**
     * @brief Calcula el THD de tension para una barra.
     *
     * THDv = sqrt(sum(|V_h|^2) / |V_1|^2) * 100%, h=2..50
     *
     * @param busId Indice de la barra.
     * @return THDv en porcentaje.
     */
    double calculateTHDv(int busId) const;

    /**
     * @brief Calcula el THD de corriente para una barra.
     *
     * THDi = sqrt(sum(|I_h|^2) / |I_1|^2) * 100%, h=2..50
     *
     * @param busId Indice de la barra.
     * @return THDi en porcentaje.
     */
    double calculateTHDi(int busId) const;

    /**
     * @brief Calcula el TDD (Total Demand Distortion) para una barra.
     *
     * TDD = sqrt(sum(|I_h|^2) / |I_L|^2) * 100%, h=2..50
     *
     * Donde I_L es la corriente de carga maxima (demanda).
     *
     * @param busId Indice de la barra.
     * @param maxLoadCurrent Corriente de carga maxima [A] (default: usa fundamental).
     * @return TDD en porcentaje.
     */
    double calculateTDD(int busId, double maxLoadCurrent = 0.0) const;

    /**
     * @brief Calcula THDv para todas las barras.
     * @return Vector de THDv por barra [%].
     */
    Eigen::VectorXd calculateTHDvAllBuses() const;

    /**
     * @brief Calcula THDi para todas las barras.
     * @return Vector de THDi por barra [%].
     */
    Eigen::VectorXd calculateTHDiAllBuses() const;

    /**
     * @brief Obtiene el espectro armonico completo para una barra.
     *
     * @param busId Indice de la barra.
     * @return Vector de componentes armonicos (orden, magnitud, angulo, DI%).
     */
    std::vector<HarmonicComponent> getHarmonicSpectrum(int busId) const;

    /**
     * @brief Calcula la distorsion individual para un orden armonico.
     *
     * DI_h = (|V_h| / |V_1|) * 100%
     *
     * @param busId Indice de la barra.
     * @param harmonicOrder Orden armonico h.
     * @return Distorsion individual [%].
     */
    double calculateIndividualDistortion(int busId, int harmonicOrder) const;

    /**
     * @brief Verifica cumplimiento con IEEE 519.
     *
     * Tabla de limites IEEE 519:
     * - V <= 69 kV:  THDv <= 5.0%, Vh <= 3.0%
     * - 69 < V <= 161 kV: THDv <= 3.0%, Vh <= 1.5%
     * - V > 161 kV: THDv <= 1.5%, Vh <= 1.0%
     *
     * @param busId Indice de la barra.
     * @param voltageLevel 0=LV (<=69kV), 1=MV (69-161kV), 2=HV (>161kV).
     * @return Estructura de cumplimiento.
     */
    IEEE519Compliance checkIEEE519Compliance(int busId, int voltageLevel = 1) const;

    /**
     * @brief Verifica cumplimiento para todas las barras.
     * @return true si TODAS las barras cumplen.
     */
    bool checkIEEE519AllBuses(int voltageLevel = 1) const;

    /**
     * @brief Verifica cumplimiento con IEC 61000-3-6.
     *
     * Limites:
     * - MV (1-35 kV): THDv <= 6.5%
     * - HV (>35 kV):  THDv <= 3.0%
     *
     * @param busId Indice de la barra.
     * @param voltageLevel 1=MV, 2=HV.
     * @return true si cumple.
     */
    bool checkIEC61000_3_6(int busId, int voltageLevel = 1) const;

    /**
     * @brief Verifica cumplimiento con EN 50160.
     *
     * Limites:
     * - THDv <= 8.0% (LV)
     * - Vh individual <= 5.0%
     *
     * @param busId Indice de la barra.
     * @return true si cumple.
     */
    bool checkEN50160(int busId) const;

    /**
     * @brief Obtiene los limites de IEEE 519 para un nivel de tension.
     * @return Par (limite_THDv, limite_THDi).
     */
    static std::pair<double, double> getIEEE519Limits(int voltageLevel);

    /**
     * @brief Obtiene los limites de distorsion individual por IEEE 519.
     * @return Mapa de orden armonico a limite [%].
     */
    static std::map<int, double> getIEEE519IndividualLimits(int voltageLevel);

private:
    HarmonicLoadFlow::HarmonicLFResults m_results;

    /**
     * @brief Obtiene la tension fundamental de una barra.
     */
    double getFundamentalVoltage(int busId) const;

    /**
     * @brief Obtiene la corriente fundamental de una barra.
     */
    double getFundamentalCurrent(int busId) const;
};

} // namespace powsys365
