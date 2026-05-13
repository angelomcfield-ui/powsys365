#pragma once

#include <vector>
#include <string>
#include <array>
#include <cmath>

namespace powsys365::linedesign {

/**
 * @brief Transmission tower / pylon structural design.
 *
 * Computes design loads (wind, ice, conductor tension), generates
 * lattice tower geometry, and performs mass optimisation.
 */
class TowerDesigner {
public:
    /**
     * @brief Wind load parameters.
     */
    struct WindLoad {
        double speed;           /**< Design wind speed [m/s]            */
        double pressure;        /**< Dynamic pressure q=0.5·ρ·v² [Pa]   */
        double gustFactor;      /**< Gust response factor Gf            */
        double shapeFactor;     /**< Drag/shape factor Cf               */
        double exposure;        /**< Exposure category (1=open…4=urban)  */
    };

    /**
     * @brief Ice load parameters.
     */
    struct IceLoad {
        double thickness;       /**< Radial ice thickness [m]           */
        double density;         /**< Ice density [kg/m³]                */
        double accretionFactor; /**< Accretion shape factor             */
    };

    /**
     * @brief Conductor attachment loads.
     */
    struct ConductorLoad {
        double weight;          /**< Vertical conductor weight [N]      */
        double tension;         /**< Horizontal conductor tension [N]   */
        double windLoad;        /**< Wind on conductor span [N]         */
        double iceLoad;         /**< Ice weight on conductor [N]        */
        double sag;             /**< Sag at attachment [m]              */
        double attachmentHeight;/**< Height of attachment point [m]     */
        double spanLength;      /**< Wind/weight span [m]               */
        std::string phase;      /**< "A", "B", "C", "ground"            */
    };

    /**
     * @brief Combined load case per ASCE 74 / IEC 60826.
     */
    struct LoadCase {
        int                     id;
        std::string             name;         /**< e.g. "NESC Heavy"      */
        double                  loadFactor;   /**< Safety multiplier      */
        WindLoad                wind;
        IceLoad                 ice;
        std::vector<ConductorLoad> conductors;
    };

    /**
     * @brief Force resultant at a node.
     */
    struct NodeForce {
        int    nodeId;
        double Fx, Fy, Fz;      /**< Force components [N]              */
        double Mx, My, Mz;      /**< Moment components [N·m]           */
    };

    /**
     * @brief Tower geometry node.
     */
    struct TowerNode {
        int    id;
        double x, y, z;         /**< Coordinates [m]                   */
        std::string type;       /**< "leg", "bracing", "platform"       */
    };

    /**
     * @brief Tower member (lattice element).
     */
    struct TowerMember {
        int    id;
        int    nodeI;
        int    nodeJ;
        std::string section;    /**< e.g. "L76×76×6", "φ20"             */
        double area;            /**< Cross-sectional area [m²]           */
        double length;          /**< Member length [m]                   */
        double slenderness;     /**< Slenderness ratio λ                 */
        double capacity;        /**< Axial capacity [N]                  */
        double utilization;     /**< Actual / capacity                   */
        bool   exceedsCapacity; /**< True if utilization > 1.0           */
    };

    /**
     * @brief Complete tower design model.
     */
    struct TowerModel {
        double towerHeight;
        double baseWidth;
        double topWidth;
        std::vector<TowerNode>  nodes;
        std::vector<TowerMember> members;
        double totalMass;       /**< Steel mass [kg]                     */
        double maxStressRatio;  /**< Max member utilization              */
    };

    // ------------------------------------------------------------------
    //  Load calculation
    // ------------------------------------------------------------------

    /**
     * @brief Calculate all nodal forces for a given load case.
     *
     * @param wind       Wind load parameters.
     * @param ice        Ice load parameters.
     * @param conductors Conductor attachment data.
     * @return Per-node force resultants.
     */
    static std::vector<NodeForce> calculateLoads(
        const WindLoad&                wind,
        const IceLoad&                 ice,
        const std::vector<ConductorLoad>& conductors);

    /**
     * @brief Wind pressure on tower body (ASCE 74).
     *
     * @param windSpeed   [m/s].
     * @param height      Above ground [m].
     * @param exposure    Category 1–4.
     * @return Pressure [Pa].
     */
    static double windPressure(double windSpeed,
                               double height,
                               int    exposure);

    /**
     * @brief Transverse wind load on a conductor span.
     *
     * @param windSpeed    [m/s].
     * @param spanLength   [m].
     * @param conductorDia [m].
     * @param iceThickness [m].
     * @param dragCoeff    [-].
     * @return Wind force [N].
     */
    static double conductorWindLoad(double windSpeed,
                                    double spanLength,
                                    double conductorDia,
                                    double iceThickness,
                                    double dragCoeff);

    /**
     * @brief Vertical ice load on a conductor span.
     */
    static double conductorIceLoad(double spanLength,
                                   double conductorDia,
                                   double iceThickness,
                                   double iceDensity);

    /**
     * @brief NESC / IEC load combination factor.
     */
    static double loadCombinationFactor(const std::string& code,
                                        const std::string& loadCase);

    // ------------------------------------------------------------------
    //  Tower geometry
    // ------------------------------------------------------------------

    /**
     * @brief Generate a self-supporting lattice tower geometry.
     *
     * @param height       Total tower height [m].
     * @param baseWidth    Base face width [m].
     * @param topWidth     Top face width [m].
     * @param numPanels    Number of body panels.
     * @param groundWireH  Ground-wire peak height above top [m].
     * @param phaseHeights Conductor attachment heights [m].
     * @return TowerModel.
     */
    static TowerModel designGeometry(
        double              height,
        double              baseWidth,
        double              topWidth,
        int                 numPanels,
        double              groundWireH,
        const std::vector<double>& phaseHeights);

    /**
     * @brief Design a guyed mast tower geometry.
     */
    static TowerModel designGuyedMast(
        double              height,
        double              baseDiameter,
        int                 numGuys,
        double              guyHeight1,
        double              guyHeight2,
        const std::vector<double>& phaseHeights);

    // ------------------------------------------------------------------
    //  Optimisation
    // ------------------------------------------------------------------

    /**
     * @brief Optimise member sizes to minimise steel mass while
     *        keeping all utilizations ≤ unity.
     *
     * @param model       Initial tower model.
     * @param loadCases   Set of load cases to check.
     * @param steelGrade  Yield strength [Pa].
     * @return Optimised TowerModel.
     */
    static TowerModel optimizeStructure(
        const TowerModel&                model,
        const std::vector<LoadCase>&     loadCases,
        double                           steelGrade);

    /**
     * @brief Check tower against all load cases.
     *
     * @param model      Tower geometry.
     * @param loadCases  Design load cases.
     * @return Max utilization ratio across all cases.
     */
    static double checkTowerAgainstLoads(
        const TowerModel&             model,
        const std::vector<LoadCase>&  loadCases);

    /**
     * @brief Suggest lighter section for an over-designed member.
     */
    static std::string suggestLighterSection(double currentArea,
                                              double requiredArea);

private:
    /**
     * @brief Generate standard L-angle sections.
     */
    static std::vector<std::pair<std::string, double>> availableAngles();

    /**
     * @brief Member capacity per AISC LRFD / Eurocode 3.
     */
    static double memberCapacity(double area,
                                 double length,
                                 double E,
                                 double yield,
                                 bool   inCompression);
};

} // namespace powsys365::linedesign
