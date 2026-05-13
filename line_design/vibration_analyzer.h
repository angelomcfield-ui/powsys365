#pragma once

#include <cmath>
#include <vector>
#include <utility>
#include <string>

namespace powsys365::linedesign {

/**
 * @brief Overhead conductor vibration analysis.
 *
 * Covers:
 *  – Aeolian (vortex-shedding) vibration
 *  – Wake-induced oscillation on bundled conductors
 *  – Galloping (iced conductor) analysis
 *  – Stockbridge damper placement optimisation
 */
class VibrationAnalyzer {
public:
    /**
     * @brief Conductor mechanical properties.
     */
    struct ConductorProps {
        double diameter;          /**< Outer diameter [m]                */
        double weightPerMeter;    /**< Unit weight [N/m]                 */
        double tension;           /**< Working tension [N]               */
        double E;                 /**< Young's modulus [Pa]              */
        double I_moment;          /**< Area moment of inertia [m⁴]       */
        double spanLength;        /**< Span length [m]                   */
        double dampingRatio;      /**< Internal damping ratio (0–1)      */
    };

    /**
     * @brief Wind characteristics.
     */
    struct WindProps {
        double speed;             /**< Wind speed [m/s]                  */
        double angle;             /**< Angle to conductor [deg]          */
        double turbulence;        /**< Turbulence intensity (0–1)        */
    };

    /**
     * @brief Aeolian vibration result.
     */
    struct AeolianResult {
        double amplitude;         /**< Peak-to-peak amplitude [m]        */
        double frequency;         /**< Vortex shedding frequency [Hz]    */
        double stress;            /**< Bending stress at clamp [MPa]     */
        double fatigueLife;       /**< Estimated fatigue life [hours]    */
        bool   exceedsLimit;      /**< True if amplitude > safe limit    */
        double safeAmplitude;     /**< Allowable peak-to-peak [m]        */
    };

    /**
     * @brief Wake-induced oscillation result.
     */
    struct WakeInducedResult {
        double amplitude;         /**< Peak amplitude [m]                */
        double frequency;         /**< Oscillation frequency [Hz]        */
        double spacingCritical;   /**< Critical sub-conductor spacing [m]*/
        bool   unstable;          /**< True if wake galloping predicted  */
    };

    /**
     * @brief Galloping analysis result.
     */
    struct GallopingResult {
        double amplitude;         /**< Estimated galloping amplitude [m] */
        double frequency;         /**< Galloping frequency [Hz]          */
        double minClearance;      /**< Minimum electrical clearance [m]  */
        bool   dangerous;         /**< True if flashover risk exists     */
        double denHartog;         /**< Den Hartog criterion value        */
        double neymanCriterion;   /**< Neyman & Rawlins criterion        */
    };

    /**
     * @brief Recommended damper location and sizing.
     */
    struct DamperSpec {
        double distanceFromClamp; /**< From suspension clamp [m]         */
        double mass;              /**< Damper mass [kg]                  */
        double stiffness;         /**< Messenger cable stiffness [N/m]   */
        double dampingCoeff;      /**< Equivalent damping [N·s/m]        */
        double frequencyRangeLow; /**< Lower freq. limit [Hz]            */
        double frequencyRangeHigh;/**< Upper freq. limit [Hz]            */
        double requiredDampers;   /**< Number of dampers recommended     */
    };

    // ------------------------------------------------------------------
    //  Aeolian (vortex-shedding) vibration
    // ------------------------------------------------------------------

    /**
     * @brief Aeolian vibration amplitude and stress analysis.
     *
     * @param conductor Conductor mechanical data.
     * @param wind      Wind conditions.
     * @return AeolianResult with amplitude, frequency, stress.
     */
    static AeolianResult aeolianVibration(const ConductorProps& conductor,
                                          const WindProps&      wind);

    /**
     * @brief Vortex shedding frequency (Strouhal relation).
     *
     * @param windSpeed  [m/s].
     * @param diameter   Conductor diameter [m].
     * @return Frequency [Hz].
     */
    static double vortexSheddingFrequency(double windSpeed,
                                          double diameter);

    /**
     * @brief Peak-to-peak amplitude (Diana & Falco model).
     *
     * @param conductor Conductor data.
     * @param windSpeed [m/s].
     * @return Peak-to-peak amplitude [m].
     */
    static double aeolianAmplitude(const ConductorProps& conductor,
                                   double                windSpeed);

    /**
     * @brief Bending stress at the suspension clamp
     *        (CIGRE SC22/WG11 simplified model).
     *
     * @param conductor Conductor data.
     * @param amplitude Peak-to-peak amplitude [m].
     * @param frequency Vibration frequency [Hz].
     * @return Bending stress [Pa].
     */
    static double bendingStressAtClamp(const ConductorProps& conductor,
                                       double                amplitude,
                                       double                frequency);

    /**
     * @brief Safe peak-to-peak amplitude limit per IEC 60868.
     */
    static double safeAmplitudeLimit(const ConductorProps& conductor);

    // ------------------------------------------------------------------
    //  Wake-induced oscillation (bundle conductors)
    // ------------------------------------------------------------------

    /**
     * @brief Wake-induced vibration on twin-/multi-bundle conductors.
     *
     * @param conductor  Conductor data (single sub-conductor).
     * @param wind       Wind conditions.
     * @param spacing    Sub-conductor spacing [m].
     * @param numSub     Number of sub-conductors.
     * @return WakeInducedResult.
     */
    static WakeInducedResult wakeInducedVibration(
        const ConductorProps& conductor,
        const WindProps&      wind,
        double                spacing,
        int                   numSub);

    /**
     * @brief Critical wind speed for wake instability.
     */
    static double criticalWindSpeed(double spacing,
                                    double diameter);

    /**
     * @brief Check wake-induced instability criterion.
     */
    static bool isWakeUnstable(const ConductorProps& conductor,
                               const WindProps&      wind,
                               double                spacing);

    // ------------------------------------------------------------------
    //  Galloping analysis
    // ------------------------------------------------------------------

    /**
     * @brief Galloping amplitude estimation for iced conductors.
     *
     * @param conductor  Conductor data.
     * @param wind       Wind conditions.
     * @param iceThickness Ice accretion thickness [m].
     * @param iceDensity   Ice density [kg/m³].
     * @param phaseSpacing Phase-to-phase spacing [m].
     * @return GallopingResult.
     */
    static GallopingResult gallopingAnalysis(const ConductorProps& conductor,
                                             const WindProps&      wind,
                                             double                iceThickness,
                                             double                iceDensity,
                                             double                phaseSpacing);

    /**
     * @brief Den Hartog instability criterion.
     *
     * @param conductor  Conductor data.
     * @param wind       Wind conditions.
     * @param iceThickness [m].
     * @return Den Hartog number (>0 → unstable).
     */
    static double denHartogCriterion(const ConductorProps& conductor,
                                     const WindProps&      wind,
                                     double                iceThickness);

    /**
     * @brief Neyman & Rawlins galloping criterion.
     */
    static double neymanCriterion(const ConductorProps& conductor,
                                  const WindProps&      wind);

    /**
     * @brief Maximum galloping amplitude (empirical CIGRE model).
     */
    static double gallopingAmplitude(const ConductorProps& conductor,
                                     const WindProps&      wind,
                                     double                iceThickness);

    // ------------------------------------------------------------------
    //  Damper placement
    // ------------------------------------------------------------------

    /**
     * @brief Recommend Stockbridge damper placement and sizing.
     *
     * @param conductor   Conductor data.
     * @param windRangeLow  Lowest expected wind speed [m/s].
     * @param windRangeHigh Highest expected wind speed [m/s].
     * @return DamperSpec.
     */
    static DamperSpec damperPlacement(const ConductorProps& conductor,
                                      double                windRangeLow  = 1.0,
                                      double                windRangeHigh = 10.0);

    /**
     * @brief Optimal damper attachment distance from suspension clamp.
     *
     * @param conductor  Conductor data.
     * @param targetFreq Target frequency to damp [Hz].
     * @return Distance [m].
     */
    static double optimalDamperDistance(const ConductorProps& conductor,
                                        double                targetFreq);

    /**
     * @brief Anti-galloping device recommendation (detuning pendulum,
     *        interphase spacer, etc.).
     *
     * @param conductor    Conductor data.
     * @param gallopingRes Galloping analysis result.
     * @return Human-readable recommendation string.
     */
    static std::string antiGallopingRecommendation(
        const ConductorProps& conductor,
        const GallopingResult& gallopingRes);

private:
    /**
     * @brief Strouhal number for smooth cylinder.
     */
    static constexpr double STROUHAL = 0.185;

    /**
     * @brief Natural frequency of a vibrating conductor span.
     */
    static double naturalFrequency(const ConductorProps& conductor,
                                   int                   mode = 1);

    /**
     * @brief Logarithmic decrement from internal damping.
     */
    static double logarithmicDecrement(const ConductorProps& conductor);
};

} // namespace powsys365::linedesign
