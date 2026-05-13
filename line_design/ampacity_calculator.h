#pragma once

#include <cmath>
#include <stdexcept>
#include <string>
#include <optional>

namespace powsys365::linedesign {

/**
 * @brief IEEE 738-2012 standard ampacity calculator.
 *
 * Steady-state and transient thermal rating of overhead
 * bare conductors including solar heating, convective cooling,
 * radiative cooling, and ohmic heating (I²R).
 */
class AmpacityCalculator {
public:
    /**
     * @brief Conductor electrical and thermal properties.
     */
    struct ConductorProperties {
        double diameter;        /**< Outer diameter [m]                     */
        double area;            /**< Cross-sectional area [m²]              */
        double R_ac_25;         /**< AC resistance at 25 °C [Ω/m]           */
        double alpha_R;         /**< Resistance temp. coefficient [1/°C]    */
        double absortivity;     /**< Solar absorptivity (0–1)               */
        double emissivity;      /**< Thermal emissivity (0–1)               */
        double T_max;           /**< Maximum allowable temp [°C]            */
        double T_ref;           /**< Reference temp for R_ac [°C]           */
        double mCp;             /**< Specific heat × mass [J/(m·°C)]        */
    };

    /**
     * @brief Environmental / weather conditions.
     */
    struct WeatherConditions {
        double ambientTemp;     /**< Air temperature [°C]                   */
        double windSpeed;       /**< Perpendicular wind speed [m/s]         */
        double windAngle;       /**< Angle wind-to-conductor [deg]          */
        double solarRadiation;  /**< Solar radiation flux [W/m²]            */
        double altitude;        /**< Elevation above sea level [m]          */
        double latitude;        /**< Latitude [deg]                         */
        int    dayOfYear;       /**< 1 – 366                                */
        int    hour;            /**< 0 – 23                                 */
        std::string atmosphere; /**< "clear", "industrial", "marine"        */
        double humidity;        /**< Relative humidity (0–1)                */
    };

    /**
     * @brief Result bundle for ampacity calculation.
     */
    struct AmpacityResult {
        double I_max_steady;        /**< Steady-state ampacity [A]          */
        double I_max_transient;     /**< Short-time overload ampacity [A]   */
        double conductorTemp;       /**< Operating temperature [°C]         */
        double solarHeating;        /**< Solar heat input [W/m]             */
        double convectiveCooling;   /**< Convective heat loss [W/m]         */
        double radiativeCooling;    /**< Radiative heat loss [W/m]          */
        double ohmicHeating;        /**< I²R heat generation [W/m]          */
        double T_ss;                /**< Steady-state conductor temp [°C]   */
        double R_ac_op;             /**< AC resistance at operating temp    */
        double timeToReachTmax;     /**< Transient: seconds to reach T_max  */
    };

    /**
     * @brief Transient overload parameters.
     */
    struct TransientParams {
        double I_pre;           /**< Pre-fault current [A]                  */
        double T_pre;           /**< Pre-fault conductor temperature [°C]   */
        double duration;        /**< Allowed overload duration [s]          */
    };

    // ------------------------------------------------------------------
    //  Steady-state ampacity (IEEE 738 eq. 1)
    // ------------------------------------------------------------------

    /**
     * @brief Calculate steady-state thermal rating.
     *
     * @param conductor Conductor physical properties.
     * @param weather   Environmental conditions.
     * @return AmpacityResult with all heat-balance components.
     */
    static AmpacityResult calculateAmpacity(
        const ConductorProperties& conductor,
        const WeatherConditions&   weather);

    /**
     * @brief Calculate conductor temperature for a given current.
     *
     * @param I          Current [A].
     * @param conductor  Conductor physical properties.
     * @param weather    Environmental conditions.
     * @return Steady-state conductor temperature [°C].
     */
    static double conductorTemperature(
        double I,
        const ConductorProperties& conductor,
        const WeatherConditions&   weather);

    /**
     * @brief Calculate maximum permissible current given a
     *        target maximum conductor temperature.
     *
     * @param T_limit    Maximum allowed conductor temperature [°C].
     * @param conductor  Conductor physical properties.
     * @param weather    Environmental conditions.
     * @return Current rating [A].
     */
    static double ampacityAtTemperature(
        double T_limit,
        const ConductorProperties& conductor,
        const WeatherConditions&   weather);

    // ------------------------------------------------------------------
    //  Transient overload
    // ------------------------------------------------------------------

    /**
     * @brief Calculate transient overload current capability.
     *
     * @param conductor  Conductor physical properties.
     * @param weather    Environmental conditions.
     * @param trans      Pre-fault state and allowed duration.
     * @return Maximum overload current [A].
     */
    static double transientOverloadCurrent(
        const ConductorProperties& conductor,
        const WeatherConditions&   weather,
        const TransientParams&     trans);

    /**
     * @brief Time for conductor to reach T_max from T_pre
     *        under overload current I.
     *
     * @param I          Overload current [A].
     * @param T_pre      Initial conductor temperature [°C].
     * @param T_max      Maximum temperature [°C].
     * @param conductor  Conductor physical properties.
     * @param weather    Environmental conditions.
     * @return Time [s].
     */
    static double timeToReachTemperature(
        double I,
        double T_pre,
        double T_max,
        const ConductorProperties& conductor,
        const WeatherConditions&   weather);

    /**
     * @brief Transient temperature rise curve.
     *
     * @param I          Current [A].
     * @param T_pre      Initial temperature [°C].
     * @param times      Time samples [s].
     * @param conductor  Conductor properties.
     * @param weather    Environmental conditions.
     * @return Temperature at each time sample [°C].
     */
    static std::vector<double> transientTemperatureCurve(
        double I,
        double T_pre,
        const std::vector<double>& times,
        const ConductorProperties& conductor,
        const WeatherConditions&   weather);

    // ------------------------------------------------------------------
    //  Individual heat-transfer terms (IEEE 738)
    // ------------------------------------------------------------------

    /** @brief Solar heat gain Qs [W/m] */
    static double solarHeating(const ConductorProperties& c,
                               const WeatherConditions&   w);

    /** @brief Convective heat loss Qc [W/m] */
    static double convectiveCooling(double Tc,
                                    const ConductorProperties& c,
                                    const WeatherConditions&   w);

    /** @brief Radiative heat loss Qr [W/m] */
    static double radiativeCooling(double Tc,
                                   const ConductorProperties& c,
                                   const WeatherConditions&   w);

    /** @brief Ohmic heat generation Qi = I²Rac(Tc) [W/m] */
    static double ohmicHeating(double I,
                               double Tc,
                               const ConductorProperties& c);

    /** @brief AC resistance at temperature Tc [Ω/m] */
    static double acResistance(double Tc, const ConductorProperties& c);

    /** @brief Convective heat-transfer coefficient [W/(m²·°C)] */
    static double convectiveCoefficient(double Tc,
                                        const ConductorProperties& c,
                                        const WeatherConditions&   w);

    /**
     * @brief Nusselt number for forced convection over cylinder
     *        (McAdams correlation used in IEEE 738).
     */
    static double nusseltNumber(double Re);

    /**
     * @brief Reynolds number for airflow over conductor.
     */
    static double reynoldsNumber(double Tc,
                                 const ConductorProperties& c,
                                 const WeatherConditions&   w);

    /**
     * @brief Air properties (density, viscosity, thermal conductivity)
     *        at film temperature.
     */
    struct AirProperties {
        double rho;   /**< Density [kg/m³]         */
        double mu;    /**< Dynamic viscosity [Pa·s]*/
        double k;     /**< Thermal conductivity [W/(m·°C)] */
    };
    static AirProperties airProperties(double T_film, double altitude);

private:
    /**
     * @brief Solar declination for day-of-year.
     */
    static double solarDeclination(int dayOfYear);

    /**
     * @brief Solar elevation angle.
     */
    static double solarElevation(int dayOfYear, int hour, double latitude);

    /**
     * @brief Heater equilibrium: Qs + I²R = Qc + Qr  → solve for Tc.
     */
    static double solveEquilibrium(double I,
                                   const ConductorProperties& c,
                                   const WeatherConditions&   w);
};

} // namespace powsys365::linedesign
