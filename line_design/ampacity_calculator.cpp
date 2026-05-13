#include "ampacity_calculator.h"
#include <algorithm>
#include <numeric>
#include <cmath>

namespace powsys365::linedesign {

/* ================================================================
   Air properties as function of film temperature and altitude
   ================================================================ */

AmpacityCalculator::AirProperties
AmpacityCalculator::airProperties(double T_film, double altitude)
{
    // ISA temperature lapse: T = 15.0 – 0.0065·h [°C]
    const double T_amb = 15.0 - 0.0065 * altitude; // unused here, kept for doc
    (void)T_amb;

    // Pressure at altitude (barometric)
    const double p0   = 101325.0;               // Pa at sea level
    const double p    = p0 * std::exp(-altitude / 8500.0);

    // Density ρ = p / (R_specific · T_K)
    const double T_K  = T_film + 273.15;
    const double R    = 287.05;                 // J/(kg·K) for air
    const double rho  = p / (R * T_K);

    // Sutherland formula for dynamic viscosity
    // μ = μ_ref · (T/T_ref)^(3/2) · (T_ref + S)/(T + S)
    const double mu_ref = 1.716e-5;             // Pa·s at 273.15 K
    const double T_ref  = 273.15;
    const double S      = 110.4;                // Sutherland const for air
    const double mu     = mu_ref * std::pow(T_K / T_ref, 1.5)
                          * (T_ref + S) / (T_K + S);

    // Thermal conductivity (power-law fit)
    const double k      = 0.0241 + 7.7e-5 * T_film; // W/(m·°C)

    return {rho, mu, k};
}

/* ================================================================
   IEEE 738 convective cooling
   ================================================================ */

/**
 * @brief Convective heat-transfer coefficient [W/(m²·°C)].
 *
 * IEEE 738 uses:
 *   h_c = N_u · k / D
 *   N_u = C · Re^n  (McAdams correlation)
 */
double
AmpacityCalculator::convectiveCoefficient(double Tc,
                                            const ConductorProperties& c,
                                            const WeatherConditions&   w)
{
    const double Re = reynoldsNumber(Tc, c, w);
    const double Nu = nusseltNumber(Re);
    const double T_film = 0.5 * (Tc + w.ambientTemp);
    const AirProperties air = airProperties(T_film, w.altitude);
    return Nu * air.k / c.diameter;
}

/**
 * @brief Nusselt number for forced convection over a cylinder.
 *
 * IEEE 738 / McAdams:
 *   Nu = 0.65·Re^0.2 + 0.23·Re^0.61   (Re < 4000)
 *   Nu = 0.023·Re^0.8                 (Re ≥ 4000)
 */
double
AmpacityCalculator::nusseltNumber(double Re)
{
    if (Re <= 0.0) return 0.0;
    if (Re < 4000.0) {
        return 0.65 * std::pow(Re, 0.2) + 0.23 * std::pow(Re, 0.61);
    }
    return 0.023 * std::pow(Re, 0.8);
}

/**
 * @brief Reynolds number for airflow over conductor.
 *
 *   Re = ρ · v · D / μ
 *
 * Corrected for wind angle and film temperature.
 */
double
AmpacityCalculator::reynoldsNumber(double Tc,
                                   const ConductorProperties& c,
                                   const WeatherConditions&   w)
{
    const double T_film = 0.5 * (Tc + w.ambientTemp);
    const AirProperties air = airProperties(T_film, w.altitude);
    const double v_eff = w.windSpeed * std::sin(w.windAngle * M_PI / 180.0);
    return air.rho * std::max(v_eff, 0.01) * c.diameter / air.mu;
}

/**
 * @brief Convective heat loss Qc [W/m].
 *
 *   Qc = h_c · (Tc – Tamb) · π · D   (natural + forced)
 *
 * For low wind: add natural convection term.
 */
double
AmpacityCalculator::convectiveCooling(double Tc,
                                      const ConductorProperties& c,
                                      const WeatherConditions&   w)
{
    const double dT = Tc - w.ambientTemp;
    if (dT <= 0.0) return 0.0;

    const double h_c = convectiveCoefficient(Tc, c, w);
    double Qc = h_c * dT * M_PI;  // per unit length (Qc per m = h·dT·π·D/D)
    // IEEE 738 uses per unit surface area; below is per metre length:
    Qc = h_c * dT * M_PI * c.diameter;

    // Natural convection when wind is very low
    if (w.windSpeed < 0.5) {
        const double T_film = 0.5 * (Tc + w.ambientTemp);
        const AirProperties air = airProperties(T_film, w.altitude);
        // Grashof number
        const double beta = 1.0 / (T_film + 273.15);
        const double Gr   = 9.81 * beta * dT
                            * std::pow(c.diameter, 3.0)
                            / (air.mu / air.rho * air.mu / air.rho);
        const double Nu_nc = 0.55 * std::pow(Gr, 0.25); // simplified
        const double h_nc  = Nu_nc * air.k / c.diameter;
        Qc = std::max(Qc, h_nc * dT * M_PI * c.diameter);
    }
    return Qc;
}

/**
 * @brief Radiative heat loss Qr [W/m].
 *
 * Stefan-Boltzmann:
 *   Qr = π · D · ε · σ · [ (Tc+273)⁴ – (Tamb+273)⁴ ]
 */
double
AmpacityCalculator::radiativeCooling(double Tc,
                                     const ConductorProperties& c,
                                     const WeatherConditions&   w)
{
    constexpr double sigma = 5.670374e-8; // W/(m²·K⁴)
    const double Tc_K = Tc + 273.15;
    const double Ta_K = w.ambientTemp + 273.15;
    if (Tc_K <= Ta_K) return 0.0;
    return M_PI * c.diameter * c.emissivity * sigma
           * (Tc_K * Tc_K * Tc_K * Tc_K - Ta_K * Ta_K * Ta_K * Ta_K);
}

/**
 * @brief Solar heat gain Qs [W/m].
 *
 * IEEE 738:
 *   Qs = α · D · Q_sol · sin(θ_solar)
 *
 * where θ_solar = solar elevation angle.
 */
double
AmpacityCalculator::solarHeating(const ConductorProperties& c,
                                 const WeatherConditions&   w)
{
    const double elev = solarElevation(w.dayOfYear, w.hour, w.latitude);
    const double sin_elev = std::sin(elev * M_PI / 180.0);
    if (sin_elev <= 0.0) return 0.0; // night

    // Adjust solar radiation for atmosphere type
    double Qsol = w.solarRadiation;
    if (w.atmosphere == "clear")      Qsol *= 1.0;
    else if (w.atmosphere == "industrial") Qsol *= 0.85;
    else if (w.atmosphere == "marine")     Qsol *= 0.90;

    return c.absortivity * c.diameter * Qsol * sin_elev;
}

/**
 * @brief AC resistance at temperature Tc [Ω/m].
 *
 *   R(Tc) = R(Tref) · [ 1 + αR · (Tc – Tref) ]
 */
double
AmpacityCalculator::acResistance(double Tc, const ConductorProperties& c)
{
    return c.R_ac_25 * (1.0 + c.alpha_R * (Tc - c.T_ref));
}

/**
 * @brief Ohmic heating I²R(Tc) [W/m].
 */
double
AmpacityCalculator::ohmicHeating(double I,
                                 double Tc,
                                 const ConductorProperties& c)
{
    return I * I * acResistance(Tc, c);
}

/* ================================================================
   Solar geometry helpers
   ================================================================ */

/**
 * @brief Solar declination (approximate) [deg].
 *
 *   δ = 23.45 · sin( 360/365 · (284 + N) )   (Cooper equation)
 */
double
AmpacityCalculator::solarDeclination(int dayOfYear)
{
    return 23.45 * std::sin(M_PI / 180.0 * (360.0 / 365.0 * (284.0 + dayOfYear)));
}

/**
 * @brief Solar elevation angle [deg].
 */
double
AmpacityCalculator::solarElevation(int dayOfYear,
                                   int hour,
                                   double latitude)
{
    const double delta = solarDeclination(dayOfYear) * M_PI / 180.0;
    const double phi   = latitude * M_PI / 180.0;
    // Hour angle: 15° per hour from solar noon
    const double omega = (hour - 12.0) * 15.0 * M_PI / 180.0;
    const double sinElev = std::sin(phi) * std::sin(delta)
                         + std::cos(phi) * std::cos(delta) * std::cos(omega);
    return std::asin(std::max(-1.0, std::min(1.0, sinElev))) * 180.0 / M_PI;
}

/* ================================================================
   Equilibrium solver (steady-state temperature)
   ================================================================ */

/**
 * @brief Solve the heat balance for conductor temperature.
 *
 *   Qs + I²R(Tc) = Qc(Tc) + Qr(Tc)
 *
 * Newton-Raphson on Tc.
 */
double
AmpacityCalculator::solveEquilibrium(double I,
                                     const ConductorProperties& c,
                                     const WeatherConditions&   w)
{
    double Tc = w.ambientTemp + 30.0; // initial guess
    for (int iter = 0; iter < 50; ++iter) {
        const double Qs  = solarHeating(c, w);
        const double Qc  = convectiveCooling(Tc, c, w);
        const double Qr  = radiativeCooling(Tc, c, w);
        const double Qi  = ohmicHeating(I, Tc, c);

        const double f   = Qs + Qi - Qc - Qr;

        // Numerical derivative df/dTc
        const double dT  = 0.5;
        const double Qc2 = convectiveCooling(Tc + dT, c, w);
        const double Qr2 = radiativeCooling(Tc + dT, c, w);
        const double Qi2 = ohmicHeating(I, Tc + dT, c);
        const double df  = ((Qi2 - Qc2 - Qr2) - (Qi - Qc - Qr)) / dT;

        if (std::abs(df) < 1e-12) break;
        const double dTc = f / df;
        Tc -= dTc;
        if (Tc < w.ambientTemp) Tc = w.ambientTemp;
        if (std::abs(dTc) < 1e-4) break;
    }
    return Tc;
}

/* ================================================================
   Main steady-state ampacity
   ================================================================ */

AmpacityCalculator::AmpacityResult
AmpacityCalculator::calculateAmpacity(const ConductorProperties& conductor,
                                      const WeatherConditions&   weather)
{
    AmpacityResult res;

    // --- Solar heating ---
    res.solarHeating = solarHeating(conductor, weather);

    // --- Steady-state: find I such that Tc = T_max ---
    res.I_max_steady = ampacityAtTemperature(conductor.T_max,
                                              conductor, weather);

    // --- Temperature at rated current ---
    res.T_ss = solveEquilibrium(res.I_max_steady, conductor, weather);

    // --- Heat balance at rated current ---
    res.conductiveCooling   = convectiveCooling(res.T_ss, conductor, weather);
    res.radiativeCooling    = radiativeCooling(res.T_ss, conductor, weather);
    res.ohmicHeating        = ohmicHeating(res.I_max_steady, res.T_ss, conductor);
    res.R_ac_op             = acResistance(res.T_ss, conductor);
    res.conductorTemp       = res.T_ss;

    // --- Transient overload (default: 15 min overload) ---
    TransientParams tp;
    tp.I_pre = res.I_max_steady * 0.8;
    tp.T_pre = solveEquilibrium(tp.I_pre, conductor, weather);
    tp.duration = 900.0; // 15 minutes
    res.I_max_transient = transientOverloadCurrent(conductor, weather, tp);
    res.timeToReachTmax = timeToReachTemperature(
        res.I_max_transient, tp.T_pre, conductor.T_max, conductor, weather);

    return res;
}

/**
 * @brief Calculate conductor temperature for a given current.
 */
double
AmpacityCalculator::conductorTemperature(
    double I,
    const ConductorProperties& conductor,
    const WeatherConditions&   weather)
{
    return solveEquilibrium(I, conductor, weather);
}

/**
 * @brief Find I_max such that Tc = T_limit.
 *
 * Binary search between 0 and a generous upper bound.
 */
double
AmpacityCalculator::ampacityAtTemperature(
    double T_limit,
    const ConductorProperties& conductor,
    const WeatherConditions&   weather)
{
    if (T_limit <= weather.ambientTemp) return 0.0;

    double I_low  = 0.0;
    double I_high = 10000.0; // generous upper bound [A]

    // Bracket the solution
    double T_high = solveEquilibrium(I_high, conductor, weather);
    while (T_high < T_limit && I_high < 1e7) {
        I_high *= 2.0;
        T_high  = solveEquilibrium(I_high, conductor, weather);
    }

    // Binary search
    for (int iter = 0; iter < 60; ++iter) {
        double I_mid  = 0.5 * (I_low + I_high);
        double T_mid  = solveEquilibrium(I_mid, conductor, weather);
        if (T_mid > T_limit) {
            I_high = I_mid;
        } else {
            I_low = I_mid;
        }
        if (I_high - I_low < 1e-4) break;
    }
    return 0.5 * (I_low + I_high);
}

/* ================================================================
   Transient overload
   ================================================================ */

/**
 * @brief Transient overload current: find I such that after
 *        the allowed duration, T reaches T_max.
 *
 * Uses the exponential heating approximation:
 *   T(t) = T_ss(I) – (T_ss(I) – T_pre)·exp(–t/τ)
 *
 * where τ = mCp / (Qc' + Qr' – I²·R')
 */
double
AmpacityCalculator::transientOverloadCurrent(
    const ConductorProperties& conductor,
    const WeatherConditions&   weather,
    const TransientParams&     trans)
{
    // Binary search for I
    double I_low  = trans.I_pre;
    double I_high = ampacityAtTemperature(conductor.T_max, conductor, weather) * 1.5;

    for (int iter = 0; iter < 50; ++iter) {
        double I = 0.5 * (I_low + I_high);
        double t = timeToReachTemperature(
            I, trans.T_pre, conductor.T_max, conductor, weather);
        if (t < trans.duration) {
            // Reaches Tmax too fast → reduce I
            I_high = I;
        } else {
            // Doesn't reach in time → increase I
            I_low = I;
        }
        if (I_high - I_low < 1e-4) break;
    }
    return 0.5 * (I_low + I_high);
}

/**
 * @brief Time for conductor to reach T_target from T_pre under current I.
 *
 * Linearised heat-capacity equation:
 *   dT/dt = [ Qs + I²R(T) – Qc(T) – Qr(T) ] / mCp
 *
 * Solved with adaptive sub-stepping.
 */
double
AmpacityCalculator::timeToReachTemperature(
    double I,
    double T_pre,
    double T_target,
    const ConductorProperties& conductor,
    const WeatherConditions&   weather)
{
    if (T_target <= T_pre) return 0.0;

    double T = T_pre;
    double t = 0.0;
    const double dt_base = 1.0; // base step 1 second

    while (T < T_target && t < 1e6) { // max 11.5 days
        double Qs  = solarHeating(conductor, weather);
        double Qc  = convectiveCooling(T, conductor, weather);
        double Qr  = radiativeCooling(T, conductor, weather);
        double Qi  = ohmicHeating(I, T, conductor);

        double dTdt = (Qs + Qi - Qc - Qr) / conductor.mCp;
        if (dTdt <= 0.0) {
            // Won't reach target (steady-state below target)
            return 1e9;
        }

        // Adaptive step: limit temperature increment
        double dt = std::min(dt_base, 5.0 / dTdt);
        double dT = dTdt * dt;
        if (T + dT > T_target) {
            dt = (T_target - T) / dTdt;
            t += dt;
            break;
        }
        T += dT;
        t += dt;
    }
    return t;
}

/**
 * @brief Transient temperature curve.
 */
std::vector<double>
AmpacityCalculator::transientTemperatureCurve(
    double I,
    double T_pre,
    const std::vector<double>& times,
    const ConductorProperties& conductor,
    const WeatherConditions&   weather)
{
    std::vector<double> temps;
    temps.reserve(times.size());
    double T = T_pre;
    size_t idx = 0;
    double t_acc = 0.0;

    for (double t_target : times) {
        double dt_needed = t_target - t_acc;
        const double dt_step = 1.0; // 1-second sub-step
        double dt_rem = dt_needed;

        while (dt_rem > 1e-9) {
            double dt = std::min(dt_step, dt_rem);
            double Qs = solarHeating(conductor, weather);
            double Qc = convectiveCooling(T, conductor, weather);
            double Qr = radiativeCooling(T, conductor, weather);
            double Qi = ohmicHeating(I, T, conductor);
            double dTdt = (Qs + Qi - Qc - Qr) / conductor.mCp;
            T += dTdt * dt;
            if (T < weather.ambientTemp) T = weather.ambientTemp;
            dt_rem -= dt;
        }
        t_acc = t_target;
        temps.push_back(T);
    }
    return temps;
}

} // namespace powsys365::linedesign
