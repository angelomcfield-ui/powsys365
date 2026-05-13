#include "tower_designer.h"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <stdexcept>

namespace powsys365::linedesign {

/* ================================================================
   Wind pressure (ASCE 74 / IEC 60826)
   ================================================================ */

/**
 * @brief Wind pressure on a tower body.
 *
 *   q_z = 0.5 · ρ · (v · k_z · G_f)²
 *
 * where k_z is the height exposure coefficient.
 */
double
TowerDesigner::windPressure(double windSpeed, double height, int exposure)
{
    constexpr double rho = 1.225; // kg/m³

    // ASCE 7 exposure coefficients (simplified)
    double alpha, zg;
    switch (exposure) {
        case 1: alpha = 9.5;  zg = 274.3; break; // Open terrain
        case 2: alpha = 7.0;  zg = 365.8; break; // Rough open
        case 3: alpha = 9.5;  zg = 213.4; break; // Suburban
        case 4: alpha = 7.0;  zg = 152.4; break; // Urban
        default: alpha = 9.5; zg = 274.3; break;
    }

    // Height factor k_z
    double k_z = 2.01 * std::pow(std::min(height, zg) / 10.0, 1.0 / alpha);
    if (height > zg) k_z *= std::sqrt(height / zg); // simplified extension

    // Dynamic pressure
    double q = 0.5 * rho * windSpeed * windSpeed * k_z * k_z;
    return q;
}

/**
 * @brief Transverse wind load on a conductor span.
 *
 *   F_w = q · C_d · D_e · L
 *
 * where D_e = D + 2·t_ice is the effective diameter.
 */
double
TowerDesigner::conductorWindLoad(double windSpeed,
                                 double spanLength,
                                 double conductorDia,
                                 double iceThickness,
                                 double dragCoeff)
{
    constexpr double rho = 1.225;
    double q = 0.5 * rho * windSpeed * windSpeed;
    double D_eff = conductorDia + 2.0 * iceThickness;
    return q * dragCoeff * D_eff * spanLength;
}

/**
 * @brief Vertical ice load on a conductor span.
 *
 *   F_ice = ρ_ice · π · t · (D + t) · L · g
 */
double
TowerDesigner::conductorIceLoad(double spanLength,
                                double conductorDia,
                                double iceThickness,
                                double iceDensity)
{
    if (iceThickness <= 0.0) return 0.0;
    constexpr double g = 9.80665;
    double area = M_PI * iceThickness * (conductorDia + iceThickness);
    return iceDensity * area * spanLength * g;
}

/**
 * @brief NESC / IEC load combination factor.
 */
double
TowerDesigner::loadCombinationFactor(const std::string& code,
                                     const std::string& loadCase)
{
    if (code == "NESC") {
        if (loadCase == "Heavy")  return 2.5;  // NESC Rule 250B
        if (loadCase == "Medium") return 2.2;
        if (loadCase == "Light")  return 1.75;
        if (loadCase == "Extreme")return 1.0;
        return 2.5;
    }
    if (code == "IEC") {
        if (loadCase == "I")   return 1.35;
        if (loadCase == "II")  return 1.5;
        if (loadCase == "III") return 1.0;
        return 1.35;
    }
    if (code == "ASCE") {
        return 1.5; // ASCE 74 default
    }
    return 1.0;
}

/* ================================================================
   Load calculation
   ================================================================ */

std::vector<TowerDesigner::NodeForce>
TowerDesigner::calculateLoads(
    const WindLoad&                wind,
    const IceLoad&                 ice,
    const std::vector<ConductorLoad>& conductors)
{
    std::vector<NodeForce> forces;

    for (const auto& cond : conductors) {
        NodeForce nf;
        nf.nodeId = 0; // Will be assigned by caller based on geometry

        // Vertical: weight + ice
        double vertical = cond.weight + cond.iceLoad;

        // Horizontal transverse: wind on conductor
        double transverse = cond.windLoad;

        // Horizontal longitudinal: tension component (simplified)
        double longitudinal = 0.0;
        if (cond.sag > 0.0) {
            // Angle from horizontal
            double angle = std::atan(2.0 * cond.sag / cond.spanLength);
            longitudinal = cond.tension * std::cos(angle);
        }

        nf.Fx = transverse;    // transverse wind
        nf.Fy = longitudinal;  // along line direction
        nf.Fz = -vertical;     // downward gravity
        nf.Mx = 0.0;
        nf.My = transverse * cond.attachmentHeight; // overturning moment
        nf.Mz = 0.0;

        // Scale by load factor
        nf.Fx *= wind.gustFactor;
        nf.Fz *= (1.0 + ice.accretionFactor);

        forces.push_back(nf);
    }

    return forces;
}

/* ================================================================
   Tower geometry design
   ================================================================ */

TowerDesigner::TowerModel
TowerDesigner::designGeometry(
    double              height,
    double              baseWidth,
    double              topWidth,
    int                 numPanels,
    double              groundWireH,
    const std::vector<double>& phaseHeights)
{
    TowerModel model;
    model.towerHeight = height;
    model.baseWidth   = baseWidth;
    model.topWidth    = topWidth;

    // Generate nodes for a 4-legged lattice tower
    double panelH = height / numPanels;
    int nodeId = 1;

    // Body nodes (4 corners per level)
    std::vector<std::array<int, 4>> levelNodes;
    for (int level = 0; level <= numPanels; ++level) {
        double z = level * panelH;
        double frac = (numPanels > 0) ? static_cast<double>(level) / numPanels : 0.0;
        double w = baseWidth + (topWidth - baseWidth) * frac;
        double halfW = w * 0.5;

        std::array<int, 4> corners;
        // Corner 1: (+x, +y)
        TowerNode n1{nodeId,  halfW,  halfW, z, (level == 0) ? "leg" : "bracing"};
        model.nodes.push_back(n1); corners[0] = nodeId++;
        // Corner 2: (-x, +y)
        TowerNode n2{nodeId, -halfW,  halfW, z, (level == 0) ? "leg" : "bracing"};
        model.nodes.push_back(n2); corners[1] = nodeId++;
        // Corner 3: (-x, -y)
        TowerNode n3{nodeId, -halfW, -halfW, z, (level == 0) ? "leg" : "bracing"};
        model.nodes.push_back(n3); corners[2] = nodeId++;
        // Corner 4: (+x, -y)
        TowerNode n4{nodeId,  halfW, -halfW, z, (level == 0) ? "leg" : "bracing"};
        model.nodes.push_back(n4); corners[3] = nodeId++;

        levelNodes.push_back(corners);
    }

    // Ground-wire peak nodes (X-brace at top)
    double peakZ = height + groundWireH;
    std::array<int, 4> peakCorners;
    double peakHalf = topWidth * 0.25;
    for (int i = 0; i < 4; ++i) {
        double px = (i == 0 || i == 3) ? peakHalf : -peakHalf;
        double py = (i == 0 || i == 1) ? peakHalf : -peakHalf;
        TowerNode pn{nodeId, px, py, peakZ, "platform"};
        model.nodes.push_back(pn);
        peakCorners[i] = nodeId++;
    }

    // Phase attachment nodes
    std::vector<int> phaseNodeIds;
    for (double ph : phaseHeights) {
        // Find closest level
        int level = static_cast<int>(std::round(ph / panelH));
        level = std::max(0, std::min(level, numPanels));
        double frac = (numPanels > 0) ? static_cast<double>(level) / numPanels : 0.0;
        double w = baseWidth + (topWidth - baseWidth) * frac;

        // Two phase attachment points per phase (left and right of tower face)
        TowerNode pn1{nodeId,  w * 0.6, 0.0, ph, "platform"};
        model.nodes.push_back(pn1);
        phaseNodeIds.push_back(nodeId++);

        TowerNode pn2{nodeId, -w * 0.6, 0.0, ph, "platform"};
        model.nodes.push_back(pn2);
        phaseNodeIds.push_back(nodeId++);
    }

    // Generate members (legs, horizontal bracing, diagonal bracing)
    int memberId = 1;
    auto addMember = [&](int nI, int nJ, const std::string& sec) {
        TowerNode nodeI{0,0,0,0}, nodeJ{0,0,0,0};
        for (const auto& n : model.nodes) {
            if (n.id == nI) nodeI = n;
            if (n.id == nJ) nodeJ = n;
        }
        double dx = nodeJ.x - nodeI.x;
        double dy = nodeJ.y - nodeI.y;
        double dz = nodeJ.z - nodeI.z;
        double L  = std::sqrt(dx*dx + dy*dy + dz*dz);

        TowerMember m;
        m.id      = memberId++;
        m.nodeI   = nI;
        m.nodeJ   = nJ;
        m.section = sec;
        m.area    = (sec == "leg") ? 0.004 : 0.0015; // m², typical
        m.length  = L;
        m.slenderness = L / std::sqrt(m.area / M_PI); // simplified
        m.capacity    = 250.0e6 * m.area; // 250 MPa steel
        m.utilization = 0.0;
        m.exceedsCapacity = false;
        model.members.push_back(m);
    };

    // Leg members (vertical along corners)
    for (int level = 0; level < numPanels; ++level) {
        for (int c = 0; c < 4; ++c) {
            addMember(levelNodes[level][c], levelNodes[level + 1][c], "leg");
        }
    }

    // Peak legs
    for (int c = 0; c < 4; ++c) {
        addMember(levelNodes[numPanels][c], peakCorners[c], "leg");
    }

    // Horizontal bracing at each level
    for (int level = 0; level <= numPanels; ++level) {
        for (int c = 0; c < 4; ++c) {
            int next = (c + 1) % 4;
            addMember(levelNodes[level][c], levelNodes[level][next], "L76×76×6");
        }
    }

    // Diagonal bracing (X-pattern in each panel face)
    for (int level = 0; level < numPanels; ++level) {
        for (int c = 0; c < 4; ++c) {
            int next = (c + 1) % 4;
            // One diagonal per face (alternating direction)
            if (level % 2 == 0) {
                addMember(levelNodes[level][c], levelNodes[level + 1][next], "L64×64×5");
            } else {
                addMember(levelNodes[level][next], levelNodes[level + 1][c], "L64×64×5");
            }
        }
    }

    // Peak cross-bracing
    addMember(peakCorners[0], peakCorners[2], "L50×50×4");
    addMember(peakCorners[1], peakCorners[3], "L50×50×4");

    // Connect phase nodes to tower
    for (size_t pi = 0; pi < phaseNodeIds.size(); ++pi) {
        int pn = phaseNodeIds[pi];
        TowerNode pNode{0,0,0,0};
        for (const auto& n : model.nodes) {
            if (n.id == pn) { pNode = n; break; }
        }
        // Find nearest body node
        double minDist = 1e9;
        int nearestNode = -1;
        for (const auto& n : model.nodes) {
            if (n.type == "leg" || n.type == "bracing") {
                double d = std::sqrt((n.x-pNode.x)*(n.x-pNode.x)
                                   + (n.y-pNode.y)*(n.y-pNode.y)
                                   + (n.z-pNode.z)*(n.z-pNode.z));
                if (d < minDist) { minDist = d; nearestNode = n.id; }
            }
        }
        if (nearestNode > 0) {
            addMember(pn, nearestNode, "L64×64×5");
        }
    }

    // Estimate total steel mass
    constexpr double rho_steel = 7850.0; // kg/m³
    model.totalMass = 0.0;
    for (const auto& m : model.members) {
        model.totalMass += m.area * m.length * rho_steel;
    }
    model.maxStressRatio = 0.0;

    return model;
}

/**
 * @brief Guyed mast geometry.
 */
TowerDesigner::TowerModel
TowerDesigner::designGuyedMast(
    double              height,
    double              baseDiameter,
    int                 numGuys,
    double              guyHeight1,
    double              guyHeight2,
    const std::vector<double>& phaseHeights)
{
    (void)numGuys; // used in detailed design
    TowerModel model;
    model.towerHeight = height;
    model.baseWidth   = baseDiameter;
    model.topWidth    = baseDiameter * 0.3;

    int nodeId = 1;
    int numSegments = 10;
    double segH = height / numSegments;

    // Central mast nodes
    std::vector<int> mastNodes;
    for (int i = 0; i <= numSegments; ++i) {
        double z = i * segH;
        double frac = static_cast<double>(i) / numSegments;
        double dia = baseDiameter * (1.0 - 0.7 * frac);
        TowerNode n{nodeId, 0.0, 0.0, z, (i == 0) ? "leg" : "bracing"};
        model.nodes.push_back(n);
        mastNodes.push_back(nodeId++);
    }

    // Guy anchor nodes
    double anchorDist = height * 0.6;
    std::vector<int> guyAnchorNodes;
    for (int g = 0; g < 4; ++g) {
        double angle = g * M_PI / 2.0;
        TowerNode an{nodeId, anchorDist * std::cos(angle),
                     anchorDist * std::sin(angle), 0.0, "leg"};
        model.nodes.push_back(an);
        guyAnchorNodes.push_back(nodeId++);
    }

    // Guy attachment nodes on mast
    int guy1Node = mastNodes[static_cast<int>(guyHeight1 / segH)];
    int guy2Node = mastNodes[static_cast<int>(guyHeight2 / segH)];

    // Phase attachment nodes
    for (double ph : phaseHeights) {
        TowerNode pn{nodeId, baseDiameter * 0.6, 0.0, ph, "platform"};
        model.nodes.push_back(pn);
        nodeId++;
    }

    // Generate members
    int memberId = 1;
    auto addMember = [&](int nI, int nJ, const std::string& sec) {
        TowerNode nodeI{0,0,0,0}, nodeJ{0,0,0,0};
        for (const auto& n : model.nodes) {
            if (n.id == nI) nodeI = n;
            if (n.id == nJ) nodeJ = n;
        }
        double L = std::sqrt((nodeJ.x-nodeI.x)*(nodeJ.x-nodeI.x)
                           + (nodeJ.y-nodeI.y)*(nodeJ.y-nodeI.y)
                           + (nodeJ.z-nodeI.z)*(nodeJ.z-nodeI.z));
        TowerMember m{memberId++, nI, nJ, sec, 0.003, L, L/0.06,
                      250.0e6 * 0.003, 0.0, false};
        model.members.push_back(m);
    };

    // Mast segments
    for (int i = 0; i < numSegments; ++i) {
        addMember(mastNodes[i], mastNodes[i+1], "leg");
    }

    // Guy wires
    for (int g = 0; g < 4; ++g) {
        addMember(guy1Node, guyAnchorNodes[g], "φ16");
        addMember(guy2Node, guyAnchorNodes[g], "φ16");
    }

    // Mass estimate
    constexpr double rho_steel = 7850.0;
    model.totalMass = 0.0;
    for (const auto& m : model.members) {
        model.totalMass += m.area * m.length * rho_steel;
    }
    model.maxStressRatio = 0.0;

    return model;
}

/* ================================================================
   Optimisation
   ================================================================ */

TowerDesigner::TowerModel
TowerDesigner::optimizeStructure(
    const TowerModel&            model,
    const std::vector<LoadCase>& loadCases,
    double                       steelGrade)
{
    TowerModel opt = model;
    auto sections = availableAngles();

    bool improved = true;
    int maxIter = 20;
    while (improved && maxIter-- > 0) {
        improved = false;
        double maxUtil = checkTowerAgainstLoads(opt, loadCases);

        if (maxUtil > 1.0) {
            // Under-designed: upgrade critical members
            for (auto& m : opt.members) {
                if (m.utilization > 0.95) {
                    // Find next larger section
                    for (const auto& sec : sections) {
                        if (sec.second > m.area) {
                            m.area = sec.second;
                            m.section = sec.first;
                            m.capacity = steelGrade * sec.second;
                            improved = true;
                            break;
                        }
                    }
                }
            }
        } else if (maxUtil < 0.5) {
            // Over-designed: downgrade non-critical members
            for (auto& m : opt.members) {
                if (m.utilization < 0.3 && m.section != "L50×50×4") {
                    // Find smaller section
                    for (auto it = sections.rbegin(); it != sections.rend(); ++it) {
                        if (it->second < m.area) {
                            double oldArea = m.area;
                            m.area = it->second;
                            m.section = it->first;
                            m.capacity = steelGrade * it->second;
                            // Verify it still passes
                            double newUtil = checkTowerAgainstLoads(opt, loadCases);
                            if (newUtil <= 1.0) {
                                improved = true;
                                break;
                            }
                            // Revert
                            m.area = oldArea;
                            for (const auto& sec : sections) {
                                if (sec.second == oldArea) {
                                    m.section = sec.first;
                                    break;
                                }
                            }
                            m.capacity = steelGrade * oldArea;
                            break;
                        }
                    }
                }
            }
        }
    }

    // Recalculate mass
    constexpr double rho_steel = 7850.0;
    opt.totalMass = 0.0;
    for (const auto& m : opt.members) {
        opt.totalMass += m.area * m.length * rho_steel;
    }

    return opt;
}

double
TowerDesigner::checkTowerAgainstLoads(
    const TowerModel&            model,
    const std::vector<LoadCase>& loadCases)
{
    double globalMaxUtil = 0.0;

    // Mutable copy to store utilization
    TowerModel& m = const_cast<TowerModel&>(model);

    for (const auto& lc : loadCases) {
        auto forces = calculateLoads(lc.wind, lc.ice, lc.conductors);

        for (auto& member : m.members) {
            // Find connected node forces
            double axialForce = 0.0;
            for (const auto& f : forces) {
                // Simplified: sum vertical and horizontal at each end
                axialForce += std::sqrt(f.Fx*f.Fx + f.Fy*f.Fy + f.Fz*f.Fz);
            }
            axialForce *= lc.loadFactor;

            // Compare against capacity
            double util = axialForce / member.capacity;
            member.utilization = util;
            member.exceedsCapacity = (util > 1.0);
            globalMaxUtil = std::max(globalMaxUtil, util);
        }
    }

    m.maxStressRatio = globalMaxUtil;
    return globalMaxUtil;
}

std::string
TowerDesigner::suggestLighterSection(double currentArea,
                                      double requiredArea)
{
    auto sections = availableAngles();
    for (auto it = sections.rbegin(); it != sections.rend(); ++it) {
        if (it->second >= requiredArea && it->second < currentArea) {
            return it->first;
        }
    }
    return "No lighter section available";
}

/* ================================================================
   Private helpers
   ================================================================ */

std::vector<std::pair<std::string, double>>
TowerDesigner::availableAngles()
{
    return {
        {"L50×50×4",  0.000384},
        {"L64×64×5",  0.000613},
        {"L76×76×6",  0.000879},
        {"L89×89×6",  0.001035},
        {"L102×102×7", 0.001381},
        {"L127×127×9", 0.002177},
        {"L152×152×11",0.003180},
        {"L178×178×13",0.004401},
        {"L203×203×16",0.006197}
    };
}

double
TowerDesigner::memberCapacity(double area,
                              double length,
                              double E,
                              double yield,
                              bool   inCompression)
{
    double radius = std::sqrt(area / M_PI);
    double lambda = length / radius; // slenderness

    if (inCompression) {
        // Euler buckling stress
        double sigma_euler = M_PI * M_PI * E / (lambda * lambda);
        // AISC interaction: min(yield, euler) with factor
        double sigma_cr = (sigma_euler < yield) ? sigma_euler : yield;
        if (lambda > 200.0) {
            sigma_cr *= (1.0 - 0.5 * (lambda / 200.0 - 1.0));
        }
        return sigma_cr * area;
    }
    // Tension
    return yield * area;
}

} // namespace powsys365::linedesign
