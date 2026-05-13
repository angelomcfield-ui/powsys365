#include "vibration_analyzer.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace powsys365::linedesign {

/* ================================================================
   Natural frequency of a vibrating conductor span
   ================================================================ */

/**
 * @brief Fundamental (and higher-mode) natural frequency.
 *
 *   f_n = (n / 2L) · √(T / μ)
 *
 * where μ = mass per unit length [kg/m], T = tension [N],
 * L = span length [m], n = mode number.
 */
double
VibrationAnalyzer::naturalFrequency(const ConductorProps& conductor,
                                    int                   mode)
{
    const double mu = conductor.weightPerMeter / 9.80665; // N/m → kg/m
    if (mu <= 0.0 || conductor.tension <= 0.0 || conductor.spanLength <= 0.0)
        return 0.0;
    return (static_cast<double>(mode) / (2.0 * conductor.spanLength))
           * std::sqrt(conductor.tension / mu);
}

/**
 * @brief Logarithmic decrement from internal damping.
 *
 *   δ = 2π · ζ  (for small damping ratio ζ)
 */
double
VibrationAnalyzer::logarithmicDecrement(const ConductorProps& conductor)
{
    return 2.0 * M_PI * conductor.dampingRatio;
}

/* ================================================================
   Aeolian (vortex-shedding) vibration
   ================================================================ */

/**
 * @brief Vortex shedding frequency via Strouhal relation.
 *
 *   f_s = S · v / D
 *
 * S ≈ 0.185 for a smooth cylinder.
 */
double
VibrationAnalyzer::vortexSheddingFrequency(double windSpeed,
                                           double diameter)
{
    if (diameter <= 0.0) return 0.0;
    return STROUHAL * windSpeed / diameter;
}

/**
 * @brief Aeolian vibration amplitude (Diana & Falco model).
 *
 *   A_pp = C_L · ρ_air · v² · D / (2 · k · ζ)
 *
 * where C_L ≈ 0.15 (lift coeff), k = equivalent stiffness.
 * Simplified to a power-law fit validated against field data.
 */
double
VibrationAnalyzer::aeolianAmplitude(const ConductorProps& conductor,
                                    double                windSpeed)
{
    const double f_s = vortexSheddingFrequency(windSpeed, conductor.diameter);
    const double f_n = naturalFrequency(conductor, 1);
    if (f_n <= 0.0) return 0.0;

    // Lock-in amplification factor (resonance when f_s ≈ f_n)
    const double freqRatio = f_s / f_n;
    const double lockIn    = 1.0 / (1.0 + 100.0 * (freqRatio - 1.0) * (freqRatio - 1.0));

    // Equivalent stiffness per unit length
    const double k_eq = M_PI * M_PI * M_PI * conductor.E * conductor.I_moment
                        / (conductor.spanLength * conductor.spanLength * conductor.spanLength);

    // Lift force per unit length (simplified)
    constexpr double rho_air = 1.225; // kg/m³
    constexpr double C_L = 0.15;
    const double F_L = 0.5 * rho_air * windSpeed * windSpeed * conductor.diameter * C_L;

    // Amplitude = F_L / (k_eq · ζ)  with lock-in amplification
    const double zeta = std::max(conductor.dampingRatio, 0.001);
    double amplitude = F_L / (k_eq * zeta) * lockIn;

    // Physical limits: cap at ~conductor diameter (clashing limit)
    const double maxAmp = 0.5 * conductor.diameter;
    return std::min(std::max(amplitude, 0.0), maxAmp);
}

/**
 * @brief Bending stress at suspension clamp (CIGRE SC22/WG11).
 *
 *   σ = E · D · y'' / 2
 *
 * where y'' ≈ (2π·f)² · A / v_w²  with v_w = wave speed.
 */
double
VibrationAnalyzer::bendingStressAtClamp(const ConductorProps& conductor,
                                        double                amplitude,
                                        double                frequency)
{
    const double mu = conductor.weightPerMeter / 9.80665;
    if (mu <= 0.0) return 0.0;
    const double v_w = std::sqrt(conductor.tension / mu); // wave speed
    if (v_w <= 0.0) return 0.0;

    // Curvature at clamp ≈ (2πf)² · A / v_w²
    const double curvature = (2.0 * M_PI * frequency) * (2.0 * M_PI * frequency)
                             * amplitude / (v_w * v_w);

    // Bending stress σ = E · c · κ  where c = D/2
    const double c = conductor.diameter / 2.0;
    return conductor.E * c * curvature;
}

/**
 * @brief Safe amplitude limit per IEC 60868.
 *
 * Empirical: A_safe = D / 2  for short spans,
 * modified by fatigue considerations for long spans.
 */
double
VibrationAnalyzer::safeAmplitudeLimit(const ConductorProps& conductor)
{
    const double base = 0.4 * conductor.diameter;
    // Reduce for long spans
    const double spanFactor = std::min(1.0, 400.0 / conductor.spanLength);
    return base * spanFactor;
}

/**
 * @brief Complete aeolian vibration analysis.
 */
VibrationAnalyzer::AeolianResult
VibrationAnalyzer::aeolianVibration(const ConductorProps& conductor,
                                    const WindProps&      wind)
{
    AeolianResult res;
    res.frequency  = vortexSheddingFrequency(wind.speed, conductor.diameter);
    res.amplitude  = aeolianAmplitude(conductor, wind.speed);
    res.stress     = bendingStressAtClamp(conductor, res.amplitude, res.frequency);
    res.safeAmplitude = safeAmplitudeLimit(conductor);
    res.exceedsLimit  = (res.amplitude > res.safeAmplitude);

    // Fatigue life estimate (simplified Miner's rule)
    // N_fatigue = C / σ^m  with typical C=1e12, m=4 for aluminium
    constexpr double C_fat = 1.0e12;
    constexpr double m_fat = 4.0;
    if (res.stress > 1e4) {
        const double sigma_MPa = res.stress / 1.0e6;
        res.fatigueLife = C_fat / std::pow(sigma_MPa, m_fat); // cycles
        res.fatigueLife /= std::max(res.frequency, 1.0);      // → hours
        res.fatigueLife = std::min(res.fatigueLife, 1.0e6);
    } else {
        res.fatigueLife = 1.0e6; // essentially infinite
    }
    return res;
}

/* ================================================================
   Wake-induced oscillation (bundle conductors)
   ================================================================ */

/**
 * @brief Critical wind speed for wake instability.
 *
 *   v_crit = f · s / S   (when vortex shedding matches spacing)
 */
double
VibrationAnalyzer::criticalWindSpeed(double spacing, double diameter)
{
    if (diameter <= 0.0) return 0.0;
    return spacing * 1.0 / (STROUHAL * diameter); // simplified
}

/**
 * @brief Check wake-induced instability criterion.
 */
bool
VibrationAnalyzer::isWakeUnstable(const ConductorProps& conductor,
                                  const WindProps&      wind,
                                  double                spacing)
{
    const double v_crit = criticalWindSpeed(spacing, conductor.diameter);
    const double f_n    = naturalFrequency(conductor, 1);

    // Unstable if wind speed is near critical and leeward conductor
    // lies in the wake of windward one
    const double wakeWidth = 1.2 * spacing; // approximate wake width
    return (wind.speed > v_crit * 0.5 && wind.speed < v_crit * 2.0)
           && (spacing < 20.0 * conductor.diameter);
}

/**
 * @brief Wake-induced vibration analysis.
 */
VibrationAnalyzer::WakeInducedResult
VibrationAnalyzer::wakeInducedVibration(const ConductorProps& conductor,
                                        const WindProps&      wind,
                                        double                spacing,
                                        int                   numSub)
{
    WakeInducedResult res;
    res.spacingCritical = 15.0 * conductor.diameter;
    res.frequency = naturalFrequency(conductor, 1);
    res.unstable = isWakeUnstable(conductor, wind, spacing);

    if (res.unstable) {
        // Amplitude estimate: sub-conductor galloping in wake
        const double wakeDeflection = 0.1 * spacing
            * std::exp(-0.05 * (wind.speed - criticalWindSpeed(spacing, conductor.diameter))
                       * (wind.speed - criticalWindSpeed(spacing, conductor.diameter)));
        res.amplitude = std::min(wakeDeflection, 0.3 * spacing);
    } else {
        res.amplitude = 0.0;
    }
    return res;
}

/* ================================================================
   Galloping analysis
   ================================================================ */

/**
 * @brief Den Hartog instability criterion.
 *
 *   DH = dC_L/dα – C_D
 *
 * If DH > 0 the conductor is aerodynamically unstable.
 * Simplified model for D-section ice accretion.
 */
double
VibrationAnalyzer::denHartogCriterion(const ConductorProps& conductor,
                                      const WindProps&      wind,
                                      double                iceThickness)
{
    (void)wind; // used in more detailed models
    if (iceThickness <= 0.0) return -1.0; // no ice → stable

    const double D = conductor.diameter;
    const double t = iceThickness;

    // Simplified D-section derivatives for thin ice
    const double aspect = t / D;
    const double dCL_dalpha = 2.0 * aspect * (1.0 + 3.0 * aspect);
    const double C_D        = 1.2 + 0.8 * aspect; // drag of D-section

    return dCL_dalpha - C_D;
}

/**
 * @brief Neyman & Rawlins criterion.
 *
 *   NR = (v² · D) / (T/μ · f_n) · factor
 *
 * If NR > threshold (~50) → galloping likely.
 */
double
VibrationAnalyzer::neymanCriterion(const ConductorProps& conductor,
                                   const WindProps&      wind)
{
    const double mu = conductor.weightPerMeter / 9.80665;
    const double f_n = naturalFrequency(conductor, 1);
    if (f_n <= 0.0 || conductor.tension <= 0.0 || mu <= 0.0)
        return 0.0;

    const double v_w = std::sqrt(conductor.tension / mu);
    return (wind.speed * wind.speed * conductor.diameter)
           / (v_w * f_n * conductor.spanLength) * 1000.0;
}

/**
 * @brief Maximum galloping amplitude (CIGRE empirical model).
 *
 *   A_max = C · v · D · √t_ice / f_n
 *
 * with C ≈ 0.5 for typical configurations.
 */
double
VibrationAnalyzer::gallopingAmplitude(const ConductorProps& conductor,
                                      const WindProps&      wind,
                                      double                iceThickness)
{
    if (iceThickness <= 0.0) return 0.0;

    const double f_n = naturalFrequency(conductor, 1);
    if (f_n <= 0.0) return 0.0;

    constexpr double C_gallop = 0.5;
    double amp = C_gallop * wind.speed * conductor.diameter
                 * std::sqrt(iceThickness) / f_n;

    // Cap at span/10 (physical limit)
    return std::min(amp, conductor.spanLength / 10.0);
}

/**
 * @brief Complete galloping analysis.
 */
VibrationAnalyzer::GallopingResult
VibrationAnalyzer::gallopingAnalysis(const ConductorProps& conductor,
                                     const WindProps&      wind,
                                     double                iceThickness,
                                     double                iceDensity,
                                     double                phaseSpacing)
{
    (void)iceDensity; // available for more detailed models
    GallopingResult res;
    res.denHartog    = denHartogCriterion(conductor, wind, iceThickness);
    res.neymanCriterion = neymanCriterion(conductor, wind);
    res.frequency    = naturalFrequency(conductor, 1);
    res.amplitude    = gallopingAmplitude(conductor, wind, iceThickness);

    // Minimum electrical clearance during galloping
    res.minClearance = phaseSpacing - 2.0 * res.amplitude;
    res.dangerous    = (res.denHartog > 0.0)
                       && (res.minClearance < 1.5) // < 1.5 m clearance
                       && (res.amplitude > 0.5 * conductor.diameter);

    return res;
}

/* ================================================================
   Stockbridge damper placement
   ================================================================ */

/**
 * @brief Optimal damper distance from suspension clamp.
 *
 *   L = (1 / 4f) · √(T/μ)
 *
 * This places the damper at the antinode of the target frequency.
 */
double
VibrationAnalyzer::optimalDamperDistance(const ConductorProps& conductor,
                                         double                targetFreq)
{
    const double mu = conductor.weightPerMeter / 9.80665;
    if (mu <= 0.0 || targetFreq <= 0.0) return 0.0;
    const double v_w = std::sqrt(conductor.tension / mu);
    return v_w / (4.0 * targetFreq);
}

/**
 * @brief Recommend Stockbridge damper placement and sizing.
 */
VibrationAnalyzer::DamperSpec
VibrationAnalyzer::damperPlacement(const ConductorProps& conductor,
                                   double                windRangeLow,
                                   double                windRangeHigh)
{
    DamperSpec spec;

    // Frequency range corresponding to wind range
    spec.frequencyRangeLow  = vortexSheddingFrequency(windRangeLow,  conductor.diameter);
    spec.frequencyRangeHigh = vortexSheddingFrequency(windRangeHigh, conductor.diameter);

    // Optimal distance for mid-range frequency
    const double f_mid = 0.5 * (spec.frequencyRangeLow + spec.frequencyRangeHigh);
    spec.distanceFromClamp = optimalDamperDistance(conductor, f_mid);

    // Damper sizing: mass ≈ conductor mass over 1–2 wavelengths
    const double mu = conductor.weightPerMeter / 9.80665;
    const double lambda = std::sqrt(conductor.tension / mu) / f_mid;
    spec.mass = mu * lambda * 0.3; // ~30% of span mass in one wavelength

    // Messenger cable stiffness (typical range)
    spec.stiffness = 10000.0 + 5000.0 * conductor.diameter * 1000.0; // N/m

    // Equivalent damping coefficient
    spec.dampingCoeff = 2.0 * spec.mass * 2.0 * M_PI * f_mid * 0.15; // 15% damping

    // Number of dampers: one per span end for long spans, one total for short
    spec.requiredDampers = (conductor.spanLength > 300.0) ? 2 : 1;

    return spec;
}

/**
 * @brief Anti-galloping device recommendation.
 */
std::string
VibrationAnalyzer::antiGallopingRecommendation(
    const ConductorProps&  conductor,
    const GallopingResult& gallopingRes)
{
    (void)conductor;
    std::ostringstream oss;
    if (!gallopingRes.dangerous) {
        oss << "No anti-galloping devices required.";
        return oss.str();
    }

    oss << "Galloping risk detected. Recommendations:\n";
    if (gallopingRes.denHartog > 0.5) {
        oss << "  – Install inter-phase spacers (rigid or flexible).\n";
        oss << "  – Apply detuning pendulums at mid-span.\n";
    } else {
        oss << "  – Monitor conditions; consider temporary line derating.\n";
    }
    if (gallopingRes.amplitude > 2.0) {
        oss << "  – Increase phase spacing or install twisted pair spacers.\n";
    }
    oss << "  – Install galloping monitors for real-time alerts.\n";
    return oss.str();
}

} // namespace powsys365::linedesign
