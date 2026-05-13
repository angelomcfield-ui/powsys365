/**
 * @file filter_designer.h
 * @brief Disenador de filtros armonicos para POWSYS365
 * @author POWSYS365 Team
 * @copyright 2025 XNOX L.L.C.
 * @version 1.0.0
 *
 * Disena filtros pasivos (sintonizado, amortiguado, tipo C/doble sintonizado)
 * y activos, con calculo de respuesta en frecuencia y atenuacion.
 */

#pragma once

#include <complex>
#include <vector>
#include <map>
#include <string>

namespace powsys365 {

/**
 * @brief Diseno de filtro pasivo.
 */
struct FilterDesign {
    int busId = 0;                ///< Barra de instalacion
    int targetHarmonic = 0;       ///< Orden armonico de sintonia
    double L = 0.0;               ///< Inductancia [H]
    double C = 0.0;               ///< Capacitancia [F]
    double R = 0.0;               ///< Resistencia [ohm]
    double Q = 0.0;               ///< Factor de calidad
    double tunedFrequency = 0.0;  ///< Frecuencia de sintonia [Hz]
    double ratedVoltage = 0.0;    ///< Tension nominal [V]
    double ratedCurrent = 0.0;    ///< Corriente nominal [A]
    double reactivePower = 0.0;   ///< Potencia reactiva [MVAr]
    enum Type {
        TUNED,                    ///< Filtro sintonizado simple (serie L-C)
        DAMPED_SECOND_ORDER,      ///< Filtro amortiguado 2o orden (L-C // R)
        DAMPED_THIRD_ORDER,       ///< Filtro amortiguado 3er orden (L-C con R der.)
        TYPE_C,                   ///< Filtro tipo C (doble sintonizado)
        DETUNED,                  ///< Filtro desintonizado
        ACTIVE                    ///< Filtro activo
    };
    Type type = TUNED;
    double dampingFactor = 0.0;   ///< Factor de amortiguamiento (para filtros amortiguados)
    double bandwidth = 0.0;       ///< Ancho de banda [Hz] (para filtros activos)
    double attenuation = 0.0;     ///< Atenuacion requerida [dB] (para filtros activos)

    /**
     * @brief Valida el diseno del filtro.
     */
    bool isValid() const {
        return (L > 0.0 && C > 0.0 && R >= 0.0 && Q > 0.0 &&
                targetHarmonic > 0 && tunedFrequency > 0.0);
    }
};

/**
 * @brief Punto de la respuesta en frecuencia de un filtro.
 */
struct FilterResponsePoint {
    double frequency = 0.0;               ///< Frecuencia [Hz]
    std::complex<double> impedance;        ///< Impedancia del filtro [ohm]
    double magnitude = 0.0;               ///< |Z| [ohm]
    double attenuationDb = 0.0;           ///< Atenuacion [dB]
    double phase = 0.0;                   ///< Fase [rad]
};

/**
 * @brief Disenador de filtros armonicos pasivos y activos.
 */
class FilterDesigner {
public:
    /**
     * @brief Constructor.
     */
    FilterDesigner();
    ~FilterDesigner();

    /**
     * @brief Disena un filtro pasivo sintonizado simple.
     *
     * Circuito: serie L-C conectado a la barra.
     * Frecuencia de sintonia: fn = 1 / (2*pi*sqrt(L*C))
     * Factor de calidad: Q = omega_h * L / R
     *
     * Formulas:
     *   C = Qc / (Vn^2 * omega_1)
     *   L = 1 / (omega_h^2 * C)
     *   R = omega_h * L / Q
     *
     * @param harmonicOrder Orden armonico de sintonia.
     * @param Q Factor de calidad deseado.
     * @param reactivePower Potencia reactiva del filtro [MVAr].
     * @param fn Frecuencia fundamental [Hz].
     * @param Vn Tension nominal [V].
     * @return Diseno del filtro.
     */
    FilterDesign designPassiveFilter(int harmonicOrder,
                                      double Q,
                                      double reactivePower,
                                      double fn = 50.0,
                                      double Vn = 13800.0);

    /**
     * @brief Disena un filtro amortiguado de segundo orden.
     *
     * Circuito: serie L-C con resistencia en paralelo al capacitor.
     * Proporciona atenuacion en banda ancha con Q controlado por R.
     *
     * @param harmonicOrder Orden armonico de sintonia.
     * @param Q Factor de calidad deseado.
     * @param reactivePower Potencia reactiva [MVAr].
     * @param fn Frecuencia fundamental [Hz].
     * @param Vn Tension nominal [V].
     * @param dampingFactor Factor de amortiguamiento adicional.
     * @return Diseno del filtro.
     */
    FilterDesign designDampedFilter(int harmonicOrder,
                                     double Q,
                                     double reactivePower,
                                     double fn = 50.0,
                                     double Vn = 13800.0,
                                     double dampingFactor = 1.0);

    /**
     * @brief Disena un filtro tipo C (doble sintonizado).
     *
     * Circuito: dos ramas L-C en paralelo, sintonizadas en dos
     * frecuencias diferentes para cubrir multiples armonicos.
     *
     * @param harmonicOrder1 Primer orden de sintonia.
     * @param harmonicOrder2 Segundo orden de sintonia.
     * @param Q Factor de calidad.
     * @param reactivePower Potencia reactiva [MVAr].
     * @param fn Frecuencia fundamental [Hz].
     * @param Vn Tension nominal [V].
     * @return Diseno del filtro tipo C.
     */
    FilterDesign designTypeCFilter(int harmonicOrder1,
                                    int harmonicOrder2,
                                    double Q,
                                    double reactivePower,
                                    double fn = 50.0,
                                    double Vn = 13800.0);

    /**
     * @brief Disena un filtro desintonizado (detuned).
     *
     * El filtro se sintoniza ligeramente por debajo del armonico
     * objetivo para evitar resonancia paralelo con la red.
     *
     * Factor de desintonizacion: p = 0.95 (sintonia en 0.95*h*fn)
     *
     * @param harmonicOrder Orden armonico objetivo.
     * @param Q Factor de calidad.
     * @param reactivePower Potencia reactiva [MVAr].
     * @param fn Frecuencia fundamental [Hz].
     * @param Vn Tension nominal [V].
     * @param detuneFactor Factor de desintonia (0.9-0.98 tipico).
     * @return Diseno del filtro desintonizado.
     */
    FilterDesign designDetunedFilter(int harmonicOrder,
                                      double Q,
                                      double reactivePower,
                                      double fn = 50.0,
                                      double Vn = 13800.0,
                                      double detuneFactor = 0.95);

    /**
     * @brief Disena un filtro activo.
     *
     * El filtro activo inyecta corrientes armonicas compensadoras:
     * I_comp(h) = -I_load(h)
     *
     * @param bandwidth Ancho de banda de compensacion [Hz].
     * @param attenuation Atenuacion requerida [dB].
     * @param harmonicOrders Ordenes armonicos a compensar.
     * @param Vn Tension nominal [V].
     * @return Diseno del filtro activo.
     */
    FilterDesign designActiveFilter(double bandwidth,
                                     double attenuation,
                                     const std::vector<int>& harmonicOrders,
                                     double Vn = 400.0);

    /**
     * @brief Calcula la respuesta en frecuencia del filtro.
     *
     * Evalua Z_filter(f) para f = f_min .. f_max con 'steps' puntos.
     *
     * @param filter Diseno del filtro.
     * @param f_min Frecuencia minima [Hz].
     * @param f_max Frecuencia maxima [Hz].
     * @param steps Numero de puntos.
     * @return Vector de puntos (frecuencia, impedancia, atenuacion).
     */
    std::vector<FilterResponsePoint> calculateFilterResponse(
        const FilterDesign& filter,
        double f_min = 50.0,
        double f_max = 2500.0,
        int steps = 500) const;

    /**
     * @brief Calcula la atenuacion del filtro a una frecuencia especifica.
     *
     * Atenuacion [dB] = 20*log10(|Z_system| / |Z_system + Z_filter|)
     *
     * @param filter Diseno del filtro.
     * @param frequency Frecuencia [Hz].
     * @param systemImpedance Impedancia del sistema a esa frecuencia [ohm].
     * @return Atenuacion en dB.
     */
    double calculateAttenuation(const FilterDesign& filter,
                                 double frequency,
                                 std::complex<double> systemImpedance) const;

    /**
     * @brief Calcula la impedancia del filtro a una frecuencia.
     *
     * @param filter Diseno del filtro.
     * @param frequency Frecuencia [Hz].
     * @return Impedancia compleja [ohm].
     */
    std::complex<double> calculateFilterImpedance(const FilterDesign& filter,
                                                    double frequency) const;

    /**
     * @brief Verifica que el filtro no cause resonancia en otros ordenes.
     *
     * @param filter Diseno del filtro.
     * @param maxOrder Orden armonico maximo a verificar.
     * @param systemImpedance Funcion de impedancia del sistema vs frecuencia.
     * @return true si no hay resonancias indeseadas.
     */
    bool verifyNoUnwantedResonance(const FilterDesign& filter,
                                    int maxOrder = 50,
                                    double maxSystemImpedance = 10.0) const;

    /**
     * @brief Genera reporte del diseno del filtro.
     */
    std::string generateFilterReport(const FilterDesign& filter) const;

private:
    /**
     * @brief Calcula los valores L, C, R para un filtro sintonizado.
     */
    void computeLCR(FilterDesign& filter,
                    int harmonicOrder, double Q, double reactivePowerVar,
                    double fn, double Vn);
};

} // namespace powsys365
