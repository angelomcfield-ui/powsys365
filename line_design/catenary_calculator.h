#pragma once

#include <cmath>
#include <vector>
#include <stdexcept>

namespace powsys365::linedesign {

/**
 * @brief Catenary geometry and sag-tension calculations
 *        for overhead transmission lines.
 *
 * Implements Edwards & Pender formulas, temperature adjustment
 * via conductor thermal expansion, and clearance analysis.
 */
class CatenaryCalculator {
public:
    /**
     * @brief Single point on the catenary curve.
     */
    struct CatenaryPoint {
        double x;       /**< Horizontal coordinate from left support [m] */
        double y;       /**< Elevation above left support [m]            */
        double slope;   /**< dy/dx at this point [m/m]                 */
    };

    /**
     * @brief 2-D profile of the catenary for a single span.
     */
    struct CatenaryProfile {
        double spanLength;          /**< Horizontal span [m]            */
        double sag;                 /**< Maximum sag [m]                */
        double tensionHorizontal;   /**< Horizontal tension [N]         */
        double tensionMax;          /**< Maximum tension (at support)   */
        double weightPerMeter;      /**< Conductor weight [N/m]         */
        double length;              /**< Actual conductor length [m]    */
        double averageHeight;       /**< Average height above supports  */
        std::vector<CatenaryPoint> points; /**< Discretized profile     */
    };

    /**
     * @brief Temperature-dependent material properties.
     */
    struct MaterialProperties {
        double alpha;      /**< Thermal expansion coefficient [1/°C]    */
        double E;          /**< Young's modulus [Pa]                     */
        double A;          /**< Cross-sectional area [m²]                */
        double beta;       /**< Stress-strain coefficient [1/Pa]         */
        double refTemp;    /**< Reference temperature [°C]               */
    };

    /**
     * @brief Ground / terrain profile for clearance calculation.
     */
    struct GroundProfile {
        double spanLength;
        std::vector<CatenaryPoint> points; /**< (x, y) terrain points   */
    };

    // ------------------------------------------------------------------
    //  Core Edwards & Pender calculations
    // ------------------------------------------------------------------

    /**
     * @brief Calculate sag from horizontal tension (Edwards & Pender).
     *
     * @param span   Horizontal span length [m].
     * @param tension Horizontal tension in conductor [N].
     * @param weight  Weight per unit length [N/m].
     * @return Maximum sag at mid-span [m].
     */
    static double calculateSag(double span, double tension, double weight);

    /**
     * @brief Calculate horizontal tension from known sag.
     *
     * @param sag    Maximum sag at mid-span [m].
     * @param span   Horizontal span length [m].
     * @param weight Weight per unit length [N/m].
     * @return Horizontal tension [N].
     */
    static double calculateTension(double sag, double span, double weight);

    /**
     * @brief Calculate vertical clearance at every point along span.
     *
     * @param catenary Catenary profile.
     * @param ground   Ground / terrain profile.
     * @return Minimum clearance and its position.
     */
    static std::pair<double, double>
    calculateClearance(const CatenaryProfile& catenary,
                       const GroundProfile&   ground);

    /**
     * @brief Adjust tension for temperature change using
     *        the full stress-strain - thermal expansion relationship.
     *
     * @param tensionRef  Tension at reference temperature [N].
     * @param tempRef     Reference temperature [°C].
     * @param tempNew     New temperature [°C].
     * @param span        Span length [m].
     * @param weight      Weight per unit length [N/m].
     * @param mat         Material properties.
     * @return Adjusted horizontal tension at new temperature [N].
     */
    static double temperatureAdjustTension(double tensionRef,
                                           double tempRef,
                                           double tempNew,
                                           double span,
                                           double weight,
                                           const MaterialProperties& mat);

    /**
     * @brief Adjust sag for temperature change.
     *
     * @param sagRef   Sag at reference temperature [m].
     * @param tempRef  Reference temperature [°C].
     * @param tempNew  New temperature [°C].
     * @param span     Span length [m].
     * @param weight   Weight per unit length [N/m].
     * @param mat      Material properties.
     * @return Sag at new temperature [m].
     */
    static double temperatureAdjustSag(double sagRef,
                                       double tempRef,
                                       double tempNew,
                                       double span,
                                       double weight,
                                       const MaterialProperties& mat);

    // ------------------------------------------------------------------
    //  Full profile generation
    // ------------------------------------------------------------------

    /**
     * @brief Generate a complete catenary profile for a level span.
     *
     * @param span     Horizontal span length [m].
     * @param tension  Horizontal tension [N].
     * @param weight   Weight per unit length [N/m].
     * @param numPoints Number of discretization points (>=2).
     * @return Populated CatenaryProfile.
     */
    static CatenaryProfile generateProfile(double span,
                                           double tension,
                                           double weight,
                                           int    numPoints = 101);

    /**
     * @brief Generate catenary profile for an inclined span
     *        (supports at different elevations).
     *
     * @param span     Horizontal span length [m].
     * @param hDiff    Elevation difference (right - left) [m].
     * @param tension  Horizontal tension [N].
     * @param weight   Weight per unit length [N/m].
     * @param numPoints Number of discretization points (>=2).
     * @return Populated CatenaryProfile.
     */
    static CatenaryProfile generateInclinedProfile(double span,
                                                   double hDiff,
                                                   double tension,
                                                   double weight,
                                                   int    numPoints = 101);

    /**
     * @brief Conductor length for a given span and tension.
     */
    static double conductorLength(double span, double tension, double weight);

    /**
     * @brief Maximum tension (at the higher support for inclined spans).
     */
    static double maxTension(double span, double tension, double weight);

private:
    /**
     * @brief Iterative solver for stress-strain - thermal expansion
     *        equilibrium (Edwards & Pender core equation).
     */
    static double solveTensionIteration(double tensionInitial,
                                        double tempRef,
                                        double tempNew,
                                        double span,
                                        double weight,
                                        const MaterialProperties& mat);

    /**
     * @brief Hyperbolic helper: cosh(a) - 1, stable for small a.
     */
    static double coshm1_stable(double a);

    /**
     * @brief Hyperbolic helper: sinh(a)/a, stable for small a.
     */
    static double sinhOverA_stable(double a);
};

} // namespace powsys365::linedesign
