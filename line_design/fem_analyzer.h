#pragma once

#include <vector>
#include <array>
#include <string>
#include <stdexcept>

namespace powsys365::linedesign {

/**
 * @brief 3-D Finite Element structural analysis for
 *        transmission line towers and conductor systems.
 *
 * Solves  K · u = F  for nodal displacements and recovers
 * element stresses and deformations. Supports truss and
 * beam elements, geometric non-linearity, and load combinations.
 */
class FEMAnalyzer {
public:
    /**
     * @brief 3-D Cartesian node.
     */
    struct Node {
        int    id;           /**< Node identifier                */
        double x, y, z;      /**< Coordinates [m]                */
        bool   fixed[6];     /**< BC flags: Tx,Ty,Tz,Rx,Ry,Rz    */
    };

    /**
     * @brief Truss element (2-node axial-only bar).
     */
    struct TrussElement {
        int    id;           /**< Element identifier             */
        int    nodeI;        /**< First node                     */
        int    nodeJ;        /**< Second node                    */
        double E;            /**< Young's modulus [Pa]           */
        double A;            /**< Cross-sectional area [m²]      */
    };

    /**
     * @brief Beam element (2-node, 12-DOF, Euler-Bernoulli).
     */
    struct BeamElement {
        int    id;           /**< Element identifier             */
        int    nodeI;        /**< First node                     */
        int    nodeJ;        /**< Second node                    */
        double E;            /**< Young's modulus [Pa]           */
        double A;            /**< Cross-sectional area [m²]      */
        double Iy;           /**< Moment of inertia y [m⁴]       */
        double Iz;           /**< Moment of inertia z [m⁴]       */
        double J;            /**< Torsional constant [m⁴]        */
        double G;            /**< Shear modulus [Pa]             */
    };

    /**
     * @brief Load case definition.
     */
    struct LoadCase {
        int                         id;        /**< Load case ID      */
        std::string                 name;      /**< Description       */
        std::vector<std::pair<int, std::array<double,6>>> nodeLoads;
                                               /**< (nodeId, [Fx,Fy,Fz,Mx,My,Mz]) */
    };

    /**
     * @brief Nodal displacement vector (6 DOF per node).
     */
    struct Displacement {
        int    nodeId;
        double dx, dy, dz;       /**< Translations [m]             */
        double rx, ry, rz;       /**< Rotations [rad]              */
    };

    /**
     * @brief Element stress result.
     */
    struct ElementStress {
        int    elementId;
        double axialStress;      /**< σ = F/A [Pa]                 */
        double bendingStressY;   /**< σ = M·z/Iy [Pa]              */
        double bendingStressZ;   /**< σ = M·y/Iz [Pa]              */
        double vonMises;         /**< Equivalent stress [Pa]       */
        double safetyFactor;     /**< Yield/σ_vm                   */
        bool   exceedsYield;     /**< True if σ_vm > yield         */
    };

    /**
     * @brief Element deformation result.
     */
    struct ElementDeformation {
        int    elementId;
        double axialStrain;      /**< ε_axial [-]                  */
        double curvatureY;       /**< κ_y [1/m]                    */
        double curvatureZ;       /**< κ_z [1/m]                    */
        double totalDeflection;  /**< Max deflection [m]           */
    };

    /**
     * @brief Complete solution output.
     */
    struct Solution {
        std::vector<Displacement>    displacements;
        std::vector<ElementStress>   stresses;
        std::vector<ElementDeformation> deformations;
        int                          iterations;
        bool                         converged;
        double                       residualNorm;
    };

    // ------------------------------------------------------------------
    //  Structure assembly
    // ------------------------------------------------------------------

    /**
     * @brief Add a node to the model.
     */
    void addNode(const Node& node);

    /**
     * @brief Add a truss element.
     */
    void addTrussElement(const TrussElement& elem);

    /**
     * @brief Add a beam element.
     */
    void addBeamElement(const BeamElement& elem);

    /**
     * @brief Define a load case.
     */
    void addLoadCase(const LoadCase& lc);

    /**
     * @brief Clear the entire model.
     */
    void clearModel();

    // ------------------------------------------------------------------
    //  Solvers
    // ------------------------------------------------------------------

    /**
     * @brief Linear static analysis: K·u = F.
     *
     * @param loadCaseId  Which load case to solve.
     * @return Solution with displacements, stresses, deformations.
     */
    Solution solveStructure(int loadCaseId);

    /**
     * @brief Non-linear (geometric) analysis with Newton-Raphson.
     *
     * @param loadCaseId       Load case ID.
     * @param maxIterations    Maximum NR iterations.
     * @param tolerance        Convergence tolerance.
     * @return Solution.
     */
    Solution solveNonLinear(int loadCaseId,
                            int    maxIterations = 50,
                            double tolerance     = 1e-6);

    // ------------------------------------------------------------------
    //  Post-processing
    // ------------------------------------------------------------------

    /**
     * @brief Recover element stresses from displacements.
     *
     * @param sol  Solution containing displacements.
     * @param yieldStrength Material yield strength [Pa].
     * @return Per-element stress data.
     */
    std::vector<ElementStress> stressAnalysis(
        const Solution& sol,
        double          yieldStrength);

    /**
     * @brief Recover element deformations from displacements.
     *
     * @param sol  Solution containing displacements.
     * @return Per-element deformation data.
     */
    std::vector<ElementDeformation> deformationAnalysis(
        const Solution& sol);

    /**
     * @brief Modal analysis – natural frequencies and mode shapes.
     *
     * @param numModes  Number of eigen-modes to extract.
     * @return Pair of (frequencies [Hz], mode-shape matrices).
     */
    std::pair<std::vector<double>, std::vector<std::vector<double>>>
    modalAnalysis(int numModes);

    // ------------------------------------------------------------------
    //  Helper queries
    // ------------------------------------------------------------------

    /** @brief Number of nodes. */
    size_t nodeCount() const;

    /** @brief Number of elements (truss + beam). */
    size_t elementCount() const;

    /** @brief Number of active DOFs after applying BCs. */
    int activeDOFs() const;

private:
    std::vector<Node>         nodes_;
    std::vector<TrussElement> trussElems_;
    std::vector<BeamElement>  beamElems_;
    std::vector<LoadCase>     loadCases_;

    /**
     * @brief Assemble global stiffness matrix (sparse skyline).
     */
    void assembleStiffness(std::vector<double>& K,
                           std::vector<int>&    idiag);

    /**
     * @brief Assemble load vector for a given load case.
     */
    std::vector<double> assembleLoadVector(int loadCaseId);

    /**
     * @brief Solve skyline symmetric positive-definite system.
     */
    static void solveSkyline(const std::vector<double>& K,
                             const std::vector<int>&    idiag,
                             std::vector<double>&       rhs);

    /**
     * @brief Build DOF map: global DOF → active DOF index (-1 if fixed).
     */
    std::vector<int> buildDOFMap() const;

    /**
     * @brief Truss element local stiffness 2×2.
     */
    static void trussLocalStiffness(const TrussElement& elem,
                                    const Node&         nI,
                                    const Node&         nJ,
                                    double              kLocal[4]);

    /**
     * @brief Beam element local stiffness 12×12 (Euler-Bernoulli).
     */
    static void beamLocalStiffness(const BeamElement& elem,
                                   const Node&        nI,
                                   const Node&        nJ,
                                   double             kLocal[144]);
};

} // namespace powsys365::linedesign
