#include "catenary_calculator.h"
#include <algorithm>
#include <numeric>
#include <limits>

namespace powsys365::linedesign {

/* ================================================================
   Hyperbolic helpers (stable for small arguments)
   ================================================================ */

/**
 * @brief Numerically stable cosh(a) - 1 using series for |a| < 0.1.
 *
 * For small arguments direct cosh(a)-1 loses precision (cancellation).
 * Taylor: cosh(a)-1 = a²/2! + a⁴/4! + a⁶/6! + …
 */
double CatenaryCalculator::coshm1_stable(double a)
{
    const double a2 = a * a;
    if (std::abs(a) < 0.1) {
        return a2 * (0.5 + a2 * (1.0 / 24.0
                                 + a2 * (1.0 / 720.0
                                         + a2 * (1.0 / 40320.0))));
    }
    return std::cosh(a) - 1.0;
}

/**
 * @brief Numerically stable sinh(a)/a for small |a|.
 *
 * Taylor: sinh(a)/a = 1 + a²/3! + a⁴/5! + a⁶/7! + …
 */
double CatenaryCalculator::sinhOverA_stable(double a)
{
    const double a2 = a * a;
    if (std::abs(a) < 0.1) {
        return 1.0 + a2 * (1.0 / 6.0
                           + a2 * (1.0 / 120.0
                                   + a2 * (1.0 / 5040.0
                                           + a2 / 362880.0)));
    }
    return std::sinh(a) / a;
}

/* ================================================================
   Edwards & Pender sag-tension core formulas
   ================================================================ */

/**
 * @brief Calculate sag from horizontal tension.
 *
 * Edwards & Pender exact catenary:
 *   sag = (H/w) * [ cosh( w·span / (2·H) ) – 1 ]
 *
 * where H = horizontal tension [N], w = weight [N/m].
 */
double
CatenaryCalculator::calculateSag(double span, double tension, double weight)
{
    if (span    <= 0.0) throw std::invalid_argument("span must be > 0");
    if (tension <= 0.0) throw std::invalid_argument("tension must be > 0");
    if (weight  <= 0.0) throw std::invalid_argument("weight must be > 0");

    const double a = weight * span / (2.0 * tension);
    return (tension / weight) * coshm1_stable(a);
}

/**
 * @brief Calculate horizontal tension from sag.
 *
 * Invert the catenary relation via Newton-Raphson:
 *   f(H) = (H/w)·coshm1( w·span/(2H) ) – sag = 0
 *
 * Initial guess from parabolic approximation:
 *   H₀ = w·span² / (8·sag)
 */
double
CatenaryCalculator::calculateTension(double sag, double span, double weight)
{
    if (sag  <= 0.0) throw std::invalid_argument("sag must be > 0");
    if (span <= 0.0) throw std::invalid_argument("span must be > 0");
    if (weight <= 0.0) throw std::invalid_argument("weight must be > 0");

    // Parabolic initial guess
    double H = weight * span * span / (8.0 * sag);

    // Newton-Raphson to refine
    for (int iter = 0; iter < 20; ++iter) {
        const double wh  = weight * span / (2.0 * H);
        const double c1  = coshm1_stable(wh);
        const double f   = (H / weight) * c1 - sag;

        // Derivative: df/dH = c1/w – (span/2H)·sinh(wh)
        const double sh  = std::sinh(wh);
        const double df  = c1 / weight - (span / (2.0 * H)) * sh;

        if (std::abs(df) < 1e-18) break;
        const double dH = f / df;
        H -= dH;
        if (H <= 0.0) H = weight * span * 0.01; // guard
        if (std::abs(dH) < 1e-9) break;
    }
    return H;
}

/**
 * @brief Calculate vertical clearance between catenary and ground.
 *
 * For every x along the span, find clearance = catenary_y(x) – ground_y(x).
 * Returns (minimum clearance, position x where it occurs).
 */
std::pair<double, double>
CatenaryCalculator::calculateClearance(const CatenaryProfile& catenary,
                                       const GroundProfile&   ground)
{
    if (catenary.points.empty() || ground.points.empty())
        throw std::invalid_argument("empty profile");

    double minClearance = std::numeric_limits<double>::max();
    double minX         = 0.0;

    // Linear interpolation of ground at each catenary point
    for (const auto& cp : catenary.points) {
        // Find ground segment containing cp.x
        double gY = ground.points.back().y; // default to last
        for (size_t i = 1; i < ground.points.size(); ++i) {
            const auto& g0 = ground.points[i - 1];
            const auto& g1 = ground.points[i];
            if (cp.x >= g0.x && cp.x <= g1.x) {
                const double t = (cp.x - g0.x) / (g1.x - g0.x + 1e-12);
                gY = g0.y + t * (g1.y - g0.y);
                break;
            }
        }
        const double clearance = cp.y - gY;
        if (clearance < minClearance) {
            minClearance = clearance;
            minX         = cp.x;
        }
    }
    return {minClearance, minX};
}

/**
 * @brief Full catenary profile for a level span.
 *
 * Discretizes the exact catenary:
 *   y(x) = (H/w) * [ cosh( w·(x – span/2) / H ) – cosh( w·span / (2H) ) ]
 * with the vertex shifted so y(0)=y(span)=0.
 */
CatenaryCalculator::CatenaryProfile
CatenaryCalculator::generateProfile(double span,
                                    double tension,
                                    double weight,
                                    int    numPoints)
{
    if (numPoints < 2) throw std::invalid_argument("numPoints >= 2");

    CatenaryProfile prof;
    prof.spanLength        = span;
    prof.tensionHorizontal = tension;
    prof.weightPerMeter    = weight;
    prof.sag               = calculateSag(span, tension, weight);
    prof.length            = conductorLength(span, tension, weight);
    prof.tensionMax        = maxTension(span, tension, weight);

    prof.points.reserve(numPoints);
    const double H  = tension;
    const double w  = weight;
    const double c  = H / w;                // catenary constant
    const double a  = w * span / (2.0 * H); // argument at support

    for (int i = 0; i < numPoints; ++i) {
        const double frac = static_cast<double>(i) / (numPoints - 1);
        const double x    = frac * span;
        const double xi   = x - span / 2.0;   // offset from vertex
        const double arg  = w * xi / H;
        CatenaryPoint pt;
        pt.x     = x;
        pt.y     = c * (std::cosh(arg) - std::cosh(a));
        // dy/dx = sinh(w·xi/H)
        pt.slope = std::sinh(arg);
        prof.points.push_back(pt);
        prof.averageHeight += pt.y;
    }
    prof.averageHeight /= numPoints;
    return prof;
}

/**
 * @brief Catenary profile for an inclined span (unequal support heights).
 *
 * The inclined catenary is transformed by adding a linear term:
 *   y_inclined(x) = y_level(x) + hDiff * (x / span)
 *
 * Sag is measured vertically from the chord line.
 */
CatenaryCalculator::CatenaryProfile
CatenaryCalculator::generateInclinedProfile(double span,
                                            double hDiff,
                                            double tension,
                                            double weight,
                                            int    numPoints)
{
    CatenaryProfile prof = generateProfile(span, tension, weight, numPoints);

    // Add linear height component and recompute sag relative to chord
    double yMax = -std::numeric_limits<double>::max();
    double yMin =  std::numeric_limits<double>::max();
    for (auto& pt : prof.points) {
        pt.y += hDiff * (pt.x / span);
        if (pt.y > yMax) yMax = pt.y;
        if (pt.y < yMin) yMin = pt.y;
    }
    prof.sag = yMax - yMin; // vertical sag relative to chord
    // Adjust conductor length for inclined span
    prof.length = conductorLength(span, tension, weight)
                  * std::sqrt(1.0 + (hDiff / span) * (hDiff / span));

    // Max tension at higher support
    const double slopeSupport = hDiff / span;
    prof.tensionMax = tension * std::sqrt(1.0 + slopeSupport * slopeSupport)
                      + weight * span * 0.5;
    return prof;
}

/**
 * @brief Actual conductor length for a given span and tension.
 *
 * L = (2H/w)·sinh( w·span / (2H) )
 */
double
CatenaryCalculator::conductorLength(double span, double tension, double weight)
{
    const double a = weight * span / (2.0 * tension);
    return (2.0 * tension / weight) * std::sinh(a);
}

/**
 * @brief Maximum tension at the support.
 *
 * For level span: T_max = H (since slope=0 at midspan).
 * This returns the support tension including vertical component.
 */
double
CatenaryCalculator::maxTension(double span, double tension, double weight)
{
    const double verticalComp = weight * span * 0.5;
    return std::sqrt(tension * tension + verticalComp * verticalComp);
}

/* ================================================================
   Temperature adjustment (Edwards & Pender)
   ================================================================ */

/**
 * @brief Adjust tension for temperature change.
 *
 * Core equation (Edwards & Pender, stress-strain + thermal):
 *   L_ref – L(T) + α·ΔT·L_ref = strain_integral
 *
 * Solved iteratively for the new tension H₂.
 */
double
CatenaryCalculator::temperatureAdjustTension(double tensionRef,
                                             double tempRef,
                                             double tempNew,
                                             double span,
                                             double weight,
                                             const MaterialProperties& mat)
{
    if (mat.E   <= 0.0) throw std::invalid_argument("E must be > 0");
    if (mat.A   <= 0.0) throw std::invalid_argument("A must be > 0");
    if (mat.alpha <= 0.0) throw std::invalid_argument("alpha must be > 0");

    const double H1 = tensionRef;
    const double dT = tempNew - tempRef;

    // Thermal expansion length change
    const double dL_thermal = mat.alpha * dT * span;

    return solveTensionIteration(H1, tempRef, tempNew, span, weight, mat);
}

/**
 * @brief Adjust sag for temperature change.
 *
 * Computes the adjusted tension first, then derives sag.
 */
double
CatenaryCalculator::temperatureAdjustSag(double sagRef,
                                          double tempRef,
                                          double tempNew,
                                          double span,
                                          double weight,
                                          const MaterialProperties& mat)
{
    const double H1 = calculateTension(sagRef, span, weight);
    const double H2 = temperatureAdjustTension(H1, tempRef, tempNew,
                                                span, weight, mat);
    return calculateSag(span, H2, weight);
}

/**
 * @brief Iterative solver for the stress-strain equilibrium.
 *
 * The governing equation for tension at new temperature:
 *   (H₂²·L_e)/(2·E·A) – (H₂·span) + (w²·span³)/(24·H₂)
 *   = (H₁²·L_e)/(2·E·A) – (H₁·span) + (w²·span³)/(24·H₁)
 *     + mat.alpha·ΔT·span
 *
 * where L_e ≈ span (elastic length).
 *
 * Solved via Newton-Raphson.
 */
double
CatenaryCalculator::solveTensionIteration(double tensionInitial,
                                          double tempRef,
                                          double tempNew,
                                          double span,
                                          double weight,
                                          const MaterialProperties& mat)
{
    const double H1    = tensionInitial;
    const double dT    = tempNew - tempRef;
    const double E     = mat.E;
    const double A     = mat.A;
    const double alpha = mat.alpha;
    const double w     = weight;

    // RHS constant term (all evaluated at reference state)
    const double L_e    = span; // elastic length ≈ span for small sags
    const double rhs_const = (H1 * H1 * L_e) / (2.0 * E * A)
                           - H1 * span
                           + (w * w * span * span * span) / (24.0 * H1)
                           + alpha * dT * span;

    // Initial guess: use linear thermal expansion estimate
    double H2 = H1 * (1.0 - alpha * dT * E * A / H1 * 0.5);
    if (H2 <= 1.0) H2 = H1;

    for (int iter = 0; iter < 50; ++iter) {
        const double H2sq = H2 * H2;
        const double term1 = (H2sq * L_e) / (2.0 * E * A);
        const double term2 = H2 * span;
        const double term3 = (w * w * span * span * span) / (24.0 * H2);

        const double f  = term1 - term2 + term3 - rhs_const;

        // df/dH2
        const double df = (H2 * L_e) / (E * A)
                        - span
                        - (w * w * span * span * span) / (24.0 * H2sq);

        if (std::abs(df) < 1e-18) break;
        const double dH = f / df;
        H2 -= dH;
        if (H2 <= 0.0) H2 = 1.0;
        if (std::abs(dH) < 1e-9) break;
    }
    return H2;
}

} // namespace powsys365::linedesign
