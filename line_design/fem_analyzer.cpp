#include "fem_analyzer.h"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <cstring>

namespace powsys365::linedesign {

/* ================================================================
   Model assembly
   ================================================================ */

void
FEMAnalyzer::addNode(const Node& node)
{
    // Check ID uniqueness
    for (const auto& n : nodes_) {
        if (n.id == node.id)
            throw std::invalid_argument("duplicate node ID");
    }
    nodes_.push_back(node);
}

void
FEMAnalyzer::addTrussElement(const TrussElement& elem)
{
    for (const auto& e : trussElems_) {
        if (e.id == elem.id)
            throw std::invalid_argument("duplicate truss element ID");
    }
    // Validate node references
    bool hasI = false, hasJ = false;
    for (const auto& n : nodes_) {
        if (n.id == elem.nodeI) hasI = true;
        if (n.id == elem.nodeJ) hasJ = true;
    }
    if (!hasI || !hasJ)
        throw std::invalid_argument("truss element references unknown node");
    trussElems_.push_back(elem);
}

void
FEMAnalyzer::addBeamElement(const BeamElement& elem)
{
    for (const auto& e : beamElems_) {
        if (e.id == elem.id)
            throw std::invalid_argument("duplicate beam element ID");
    }
    bool hasI = false, hasJ = false;
    for (const auto& n : nodes_) {
        if (n.id == elem.nodeI) hasI = true;
        if (n.id == elem.nodeJ) hasJ = true;
    }
    if (!hasI || !hasJ)
        throw std::invalid_argument("beam element references unknown node");
    beamElems_.push_back(elem);
}

void
FEMAnalyzer::addLoadCase(const LoadCase& lc)
{
    for (const auto& existing : loadCases_) {
        if (existing.id == lc.id)
            throw std::invalid_argument("duplicate load case ID");
    }
    loadCases_.push_back(lc);
}

void
FEMAnalyzer::clearModel()
{
    nodes_.clear();
    trussElems_.clear();
    beamElems_.clear();
    loadCases_.clear();
}

size_t
FEMAnalyzer::nodeCount() const { return nodes_.size(); }

size_t
FEMAnalyzer::elementCount() const { return trussElems_.size() + beamElems_.size(); }

/**
 * @brief Build DOF map: each of 6 DOF per node gets an active index
 *        or -1 if fixed by boundary condition.
 */
std::vector<int>
FEMAnalyzer::buildDOFMap() const
{
    std::vector<int> map(nodes_.size() * 6, -1);
    int activeIdx = 0;
    for (size_t ni = 0; ni < nodes_.size(); ++ni) {
        for (int d = 0; d < 6; ++d) {
            if (!nodes_[ni].fixed[d]) {
                map[ni * 6 + d] = activeIdx++;
            }
        }
    }
    return map;
}

int
FEMAnalyzer::activeDOFs() const
{
    int count = 0;
    for (const auto& n : nodes_) {
        for (int d = 0; d < 6; ++d) {
            if (!n.fixed[d]) ++count;
        }
    }
    return count;
}

/* ================================================================
   Truss element local stiffness (2×2 axial)
   ================================================================ */

void
FEMAnalyzer::trussLocalStiffness(const TrussElement& elem,
                                 const Node&         nI,
                                 const Node&         nJ,
                                 double              kLocal[4])
{
    const double dx = nJ.x - nI.x;
    const double dy = nJ.y - nI.y;
    const double dz = nJ.z - nI.z;
    const double L  = std::sqrt(dx*dx + dy*dy + dz*dz);
    if (L < 1e-12) {
        std::fill(kLocal, kLocal + 4, 0.0);
        return;
    }
    const double c = dx / L;
    const double s_y = dy / L;
    const double s_z = dz / L;
    const double EA_L = elem.E * elem.A / L;

    // 2×2 stiffness in local coords, then transform
    // k = EA/L · [  c²    cs ]
    //             [  cs    s² ]
    // Expanded to 2 DOF (axial only along element direction)
    kLocal[0] = EA_L;
    kLocal[1] = -EA_L;
    kLocal[2] = -EA_L;
    kLocal[3] = EA_L;
    (void)c; (void)s_y; (void)s_z; // direction cosines used in global assembly
}

/* ================================================================
   Beam element local stiffness (12×12 Euler-Bernoulli)
   ================================================================ */

void
FEMAnalyzer::beamLocalStiffness(const BeamElement& elem,
                                const Node&        nI,
                                const Node&        nJ,
                                double             kLocal[144])
{
    std::fill(kLocal, kLocal + 144, 0.0);

    const double dx = nJ.x - nI.x;
    const double dy = nJ.y - nI.y;
    const double dz = nJ.z - nI.z;
    const double L  = std::sqrt(dx*dx + dy*dy + dz*dz);
    if (L < 1e-12) return;

    const double EA   = elem.E * elem.A;
    const double EI_y = elem.E * elem.Iy;
    const double EI_z = elem.E * elem.Iz;
    const double GJ   = elem.G * elem.J;

    // Standard 12×12 Euler-Bernoulli beam stiffness matrix
    // Row/col ordering: [u1,v1,w1,θx1,θy1,θz1, u2,v2,w2,θx2,θy2,θz2]
    auto K = [&](int r, int c) -> double& { return kLocal[r * 12 + c]; };

    // Axial (DOF 0, 6)
    K(0, 0)   =  EA / L;   K(0, 6)  = -EA / L;
    K(6, 0)   = -EA / L;   K(6, 6)  =  EA / L;

    // Torsion (DOF 3, 9)
    K(3, 3)   =  GJ / L;   K(3, 9)  = -GJ / L;
    K(9, 3)   = -GJ / L;   K(9, 9)  =  GJ / L;

    // Bending in local y-z plane (DOF 1,5,7,11 for v, θz)
    const double EIz_L  = EI_z / L;
    const double EIz_L2 = EIz_L / L;
    const double EIz_L3 = EIz_L2 / L;

    K(1, 1)   =  12.0 * EIz_L3;  K(1, 5)  =   6.0 * EIz_L2;
    K(1, 7)   = -12.0 * EIz_L3;  K(1, 11) =   6.0 * EIz_L2;

    K(5, 1)   =   6.0 * EIz_L2;  K(5, 5)  =   4.0 * EIz_L;
    K(5, 7)   =  -6.0 * EIz_L2;  K(5, 11) =   2.0 * EIz_L;

    K(7, 1)   = -12.0 * EIz_L3;  K(7, 5)  =  -6.0 * EIz_L2;
    K(7, 7)   =  12.0 * EIz_L3;  K(7, 11) =  -6.0 * EIz_L2;

    K(11, 1)  =   6.0 * EIz_L2;  K(11, 5) =   2.0 * EIz_L;
    K(11, 7)  =  -6.0 * EIz_L2;  K(11, 11)=   4.0 * EIz_L;

    // Bending in local x-z plane (DOF 2,4,8,10 for w, θy)
    const double EIy_L  = EI_y / L;
    const double EIy_L2 = EIy_L / L;
    const double EIy_L3 = EIy_L2 / L;

    K(2, 2)   =  12.0 * EIy_L3;  K(2, 4)  =  -6.0 * EIy_L2;
    K(2, 8)   = -12.0 * EIy_L3;  K(2, 10) =  -6.0 * EIy_L2;

    K(4, 2)   =  -6.0 * EIy_L2;  K(4, 4)  =   4.0 * EIy_L;
    K(4, 8)   =   6.0 * EIy_L2;  K(4, 10) =   2.0 * EIy_L;

    K(8, 2)   = -12.0 * EIy_L3;  K(8, 4)  =   6.0 * EIy_L2;
    K(8, 8)   =  12.0 * EIy_L3;  K(8, 10) =   6.0 * EIy_L2;

    K(10, 2)  =  -6.0 * EIy_L2;  K(10, 4) =   2.0 * EIy_L;
    K(10, 8)  =   6.0 * EIy_L2;  K(10, 10)=   4.0 * EIy_L;
}

/* ================================================================
   Global stiffness assembly (skyline storage)
   ================================================================ */

/**
 * @brief Assemble the global stiffness matrix in skyline format.
 *
 * For each element, compute local stiffness, then add contributions
 * to global K using the DOF map.
 */
void
FEMAnalyzer::assembleStiffness(std::vector<double>& K,
                               std::vector<int>&    idiag)
{
    const auto dofMap = buildDOFMap();
    const int ndof = activeDOFs();

    // Determine skyline profile
    std::vector<int> colHeight(ndof, 0);

    auto updateProfile = [&](int nodeId, int localDof) {
        int globalDof = -1;
        for (size_t ni = 0; ni < nodes_.size(); ++ni) {
            if (nodes_[ni].id == nodeId) {
                globalDof = dofMap[ni * 6 + localDof];
                break;
            }
        }
        return globalDof;
    };

    // Compute column heights from element connectivity
    for (const auto& elem : trussElems_) {
        int gI[6] = {-1}, gJ[6] = {-1};
        for (size_t ni = 0; ni < nodes_.size(); ++ni) {
            if (nodes_[ni].id == elem.nodeI) {
                for (int d = 0; d < 6; ++d) gI[d] = dofMap[ni * 6 + d];
            }
            if (nodes_[ni].id == elem.nodeJ) {
                for (int d = 0; d < 6; ++d) gJ[d] = dofMap[ni * 6 + d];
            }
        }
        std::vector<int> dofs;
        for (int d = 0; d < 3; ++d) { // truss has only translation DOFs
            if (gI[d] >= 0) dofs.push_back(gI[d]);
            if (gJ[d] >= 0) dofs.push_back(gJ[d]);
        }
        for (size_t i = 0; i < dofs.size(); ++i) {
            for (size_t j = 0; j < dofs.size(); ++j) {
                if (dofs[i] >= dofs[j]) {
                    colHeight[dofs[i]] = std::max(colHeight[dofs[i]],
                                                   dofs[i] - dofs[j]);
                }
            }
        }
    }

    for (const auto& elem : beamElems_) {
        int gI[6] = {-1}, gJ[6] = {-1};
        for (size_t ni = 0; ni < nodes_.size(); ++ni) {
            if (nodes_[ni].id == elem.nodeI) {
                for (int d = 0; d < 6; ++d) gI[d] = dofMap[ni * 6 + d];
            }
            if (nodes_[ni].id == elem.nodeJ) {
                for (int d = 0; d < 6; ++d) gJ[d] = dofMap[ni * 6 + d];
            }
        }
        std::vector<int> dofs;
        for (int d = 0; d < 6; ++d) {
            if (gI[d] >= 0) dofs.push_back(gI[d]);
            if (gJ[d] >= 0) dofs.push_back(gJ[d]);
        }
        for (size_t i = 0; i < dofs.size(); ++i) {
            for (size_t j = 0; j < dofs.size(); ++j) {
                if (dofs[i] >= dofs[j]) {
                    colHeight[dofs[i]] = std::max(colHeight[dofs[i]],
                                                   dofs[i] - dofs[j]);
                }
            }
        }
    }

    // Build idiag (diagonal addresses in packed storage)
    idiag.resize(ndof);
    int addr = 0;
    for (int i = 0; i < ndof; ++i) {
        addr += colHeight[i] + 1;
        idiag[i] = addr;
    }

    // Allocate and zero K
    K.assign(addr, 0.0);

    // Assemble element contributions
    auto addToK = [&](int row, int col, double val) {
        if (row < col) std::swap(row, col); // store upper triangle
        if (row < 0 || col < 0) return;
        int height = (row == 0) ? 0 : (idiag[row] - idiag[row - 1] - 1);
        int colStart = row - height;
        if (col < colStart) return; // outside profile
        int idx = idiag[row] - (row - col);
        K[idx] += val;
    };

    // Truss elements
    double kLocTruss[4];
    for (const auto& elem : trussElems_) {
        Node nI{0,0,0,0}, nJ{0,0,0,0};
        for (const auto& n : nodes_) {
            if (n.id == elem.nodeI) nI = n;
            if (n.id == elem.nodeJ) nJ = n;
        }
        trussLocalStiffness(elem, nI, nJ, kLocTruss);

        int gI[6] = {-1}, gJ[6] = {-1};
        for (size_t ni = 0; ni < nodes_.size(); ++ni) {
            if (nodes_[ni].id == elem.nodeI) {
                for (int d = 0; d < 6; ++d) gI[d] = dofMap[ni * 6 + d];
            }
            if (nodes_[ni].id == elem.nodeJ) {
                for (int d = 0; d < 6; ++d) gJ[d] = dofMap[ni * 6 + d];
            }
        }

        // Truss contributes only to translational DOFs 0,1,2
        // kLocTruss is 2×2 for [u1, u2]
        // Need to transform to global coordinates
        const double dx = nJ.x - nI.x;
        const double dy = nJ.y - nI.y;
        const double dz = nJ.z - nI.z;
        const double L  = std::sqrt(dx*dx + dy*dy + dz*dz);
        if (L < 1e-12) continue;
        const double cx = dx / L, cy = dy / L, cz = dz / L;

        // Direction cosine matrix for 3D transformation
        // K_global = T^T · k_local · T
        const double dirs[2][3] = {{cx, cy, cz}, {-cx, -cy, -cz}};
        const int globalIdx[2] = {gI[0], gJ[0]}; // translation DOF 0 (x-dir)
        (void)globalIdx;

        // Actually map to 3 translational DOFs per node
        int dofs[6];
        dofs[0] = gI[0]; dofs[1] = gI[1]; dofs[2] = gI[2];
        dofs[3] = gJ[0]; dofs[4] = gJ[1]; dofs[5] = gJ[2];

        for (int i = 0; i < 6; ++i) {
            for (int j = 0; j < 6; ++j) {
                int ni_idx = i / 3; // 0 or 1
                int nj_idx = j / 3;
                int di = i % 3;
                int dj = j % 3;
                double val = kLocTruss[ni_idx * 2 + nj_idx] * dirs[ni_idx][di] * dirs[nj_idx][dj];
                addToK(dofs[i], dofs[j], val);
            }
        }
    }

    // Beam elements
    double kLocBeam[144];
    for (const auto& elem : beamElems_) {
        Node nI{0,0,0,0}, nJ{0,0,0,0};
        for (const auto& n : nodes_) {
            if (n.id == elem.nodeI) nI = n;
            if (n.id == elem.nodeJ) nJ = n;
        }
        beamLocalStiffness(elem, nI, nJ, kLocBeam);

        int gI[6] = {-1}, gJ[6] = {-1};
        for (size_t ni = 0; ni < nodes_.size(); ++ni) {
            if (nodes_[ni].id == elem.nodeI) {
                for (int d = 0; d < 6; ++d) gI[d] = dofMap[ni * 6 + d];
            }
            if (nodes_[ni].id == elem.nodeJ) {
                for (int d = 0; d < 6; ++d) gJ[d] = dofMap[ni * 6 + d];
            }
        }

        int dofs[12];
        for (int d = 0; d < 6; ++d) { dofs[d] = gI[d]; dofs[d+6] = gJ[d]; }

        for (int i = 0; i < 12; ++i) {
            for (int j = 0; j < 12; ++j) {
                addToK(dofs[i], dofs[j], kLocBeam[i * 12 + j]);
            }
        }
    }
}

/* ================================================================
   Load vector assembly
   ================================================================ */

std::vector<double>
FEMAnalyzer::assembleLoadVector(int loadCaseId)
{
    const auto dofMap = buildDOFMap();
    const int ndof = activeDOFs();
    std::vector<double> F(ndof, 0.0);

    // Find load case
    const LoadCase* lc = nullptr;
    for (const auto& l : loadCases_) {
        if (l.id == loadCaseId) { lc = &l; break; }
    }
    if (!lc) throw std::invalid_argument("load case not found");

    for (const auto& nl : lc->nodeLoads) {
        int nodeId = nl.first;
        const auto& vals = nl.second;
        // Find node index
        for (size_t ni = 0; ni < nodes_.size(); ++ni) {
            if (nodes_[ni].id == nodeId) {
                for (int d = 0; d < 6; ++d) {
                    int gdof = dofMap[ni * 6 + d];
                    if (gdof >= 0) F[gdof] += vals[d];
                }
                break;
            }
        }
    }
    return F;
}

/* ================================================================
   Skyline solver (Cholesky LDL^T decomposition)
   ================================================================ */

void
FEMAnalyzer::solveSkyline(const std::vector<double>& K,
                          const std::vector<int>&    idiag,
                          std::vector<double>&       rhs)
{
    const int n = static_cast<int>(idiag.size());
    if (n == 0) return;

    // Copy K since we need to factor it
    std::vector<double> A = K;

    // LDL^T factorization
    for (int j = 0; j < n; ++j) {
        int jStart = (j == 0) ? 0 : idiag[j - 1] + 1;
        int jLen   = idiag[j] - jStart;
        int jHeight = jLen;

        for (int i = j - jHeight; i < j; ++i) {
            if (i < 0) continue;
            int iStart = (i == 0) ? 0 : idiag[i - 1] + 1;
            int iLen   = idiag[i] - iStart;

            // D(j) · L(i,j) = K(i,j) – Σ L(i,m)·D(m)·L(j,m)
            double sum = 0.0;
            int mStart = std::max(j - jHeight, i - iLen);
            for (int m = mStart; m < i; ++m) {
                int idx_im = idiag[i] - (i - m);
                int idx_jm = idiag[j] - (j - m);
                if (idx_im >= 0 && idx_jm >= 0)
                    sum += A[idx_im] * A[idx_jm];
            }
            int idx_ij = idiag[j] - (j - i);
            int idx_ii = idiag[i];
            if (std::abs(A[idx_ii]) < 1e-18) {
                throw std::runtime_error("singular matrix in FEM solve");
            }
            A[idx_ij] = (A[idx_ij] - sum) / A[idx_ii];
        }

        // Diagonal entry D(j)
        double sum = 0.0;
        for (int m = j - jHeight; m < j; ++m) {
            if (m < 0) continue;
            int idx_jm = idiag[j] - (j - m);
            int idx_mm = idiag[m];
            sum += A[idx_jm] * A[idx_jm] * A[idx_mm];
        }
        int idx_jj = idiag[j];
        A[idx_jj] -= sum;
    }

    // Forward substitution: L·y = rhs
    for (int i = 0; i < n; ++i) {
        int iStart = (i == 0) ? 0 : idiag[i - 1] + 1;
        int iLen   = idiag[i] - iStart;
        for (int j = i - iLen; j < i; ++j) {
            if (j < 0) continue;
            rhs[i] -= A[idiag[i] - (i - j)] * rhs[j];
        }
    }

    // Diagonal scaling: D·z = y
    for (int i = 0; i < n; ++i) {
        if (std::abs(A[idiag[i]]) < 1e-18) {
            throw std::runtime_error("zero diagonal in FEM solve");
        }
        rhs[i] /= A[idiag[i]];
    }

    // Back substitution: L^T·u = z
    for (int i = n - 1; i >= 0; --i) {
        int iStart = (i == 0) ? 0 : idiag[i - 1] + 1;
        int iLen   = idiag[i] - iStart;
        for (int j = i - 1; j >= i - iLen; --j) {
            if (j < 0) break;
            rhs[j] -= A[idiag[i] - (i - j)] * rhs[i];
        }
    }
}

/* ================================================================
   Main linear solver
   ================================================================ */

FEMAnalyzer::Solution
FEMAnalyzer::solveStructure(int loadCaseId)
{
    Solution sol;
    sol.converged = false;
    sol.iterations = 0;

    if (nodes_.empty()) {
        sol.residualNorm = 0.0;
        return sol;
    }

    // Assemble K
    std::vector<double> K;
    std::vector<int> idiag;
    assembleStiffness(K, idiag);

    // Assemble load vector
    std::vector<double> F = assembleLoadVector(loadCaseId);

    // Solve
    std::vector<double> u = F;
    solveSkyline(K, idiag, u);

    sol.converged = true;
    sol.residualNorm = 0.0;

    // Map displacements back to nodes
    const auto dofMap = buildDOFMap();
    for (size_t ni = 0; ni < nodes_.size(); ++ni) {
        Displacement d;
        d.nodeId = nodes_[ni].id;
        int g[6];
        for (int i = 0; i < 6; ++i) g[i] = dofMap[ni * 6 + i];
        d.dx = (g[0] >= 0) ? u[g[0]] : 0.0;
        d.dy = (g[1] >= 0) ? u[g[1]] : 0.0;
        d.dz = (g[2] >= 0) ? u[g[2]] : 0.0;
        d.rx = (g[3] >= 0) ? u[g[3]] : 0.0;
        d.ry = (g[4] >= 0) ? u[g[4]] : 0.0;
        d.rz = (g[5] >= 0) ? u[g[5]] : 0.0;
        sol.displacements.push_back(d);
    }

    // Compute stresses and deformations
    sol.stresses    = stressAnalysis(sol, 250.0e6); // default yield 250 MPa
    sol.deformations= deformationAnalysis(sol);
    return sol;
}

/* ================================================================
   Non-linear solver (Newton-Raphson with geometric stiffness)
   ================================================================ */

FEMAnalyzer::Solution
FEMAnalyzer::solveNonLinear(int loadCaseId,
                            int    maxIterations,
                            double tolerance)
{
    Solution sol;
    // Start with linear solution
    sol = solveStructure(loadCaseId);
    if (!sol.converged) return sol;

    // Newton-Raphson iterations with updated geometry
    for (int iter = 0; iter < maxIterations; ++iter) {
        // Update nodal coordinates with current displacements
        auto nodesOrig = nodes_;
        for (const auto& disp : sol.displacements) {
            for (auto& n : nodes_) {
                if (n.id == disp.nodeId) {
                    n.x += disp.dx;
                    n.y += disp.dy;
                    n.z += disp.dz;
                    break;
                }
            }
        }

        // Re-assemble and solve
        std::vector<double> K;
        std::vector<int> idiag;
        try {
            assembleStiffness(K, idiag);
        } catch (...) {
            nodes_ = nodesOrig;
            break;
        }

        std::vector<double> F = assembleLoadVector(loadCaseId);
        std::vector<double> du = F;
        try {
            solveSkyline(K, idiag, du);
        } catch (...) {
            nodes_ = nodesOrig;
            break;
        }

        // Check convergence
        double maxDu = 0.0;
        for (double v : du) maxDu = std::max(maxDu, std::abs(v));

        // Update displacements
        const auto dofMap = buildDOFMap();
        for (size_t ni = 0; ni < nodes_.size(); ++ni) {
            for (auto& disp : sol.displacements) {
                if (disp.nodeId == nodes_[ni].id) {
                    int g[6];
                    for (int i = 0; i < 6; ++i) g[i] = dofMap[ni * 6 + i];
                    if (g[0] >= 0) disp.dx += du[g[0]];
                    if (g[1] >= 0) disp.dy += du[g[1]];
                    if (g[2] >= 0) disp.dz += du[g[2]];
                    if (g[3] >= 0) disp.rx += du[g[3]];
                    if (g[4] >= 0) disp.ry += du[g[4]];
                    if (g[5] >= 0) disp.rz += du[g[5]];
                    break;
                }
            }
        }

        nodes_ = nodesOrig; // restore original geometry for next iteration
        sol.iterations = iter + 1;

        if (maxDu < tolerance) {
            sol.converged = true;
            sol.residualNorm = maxDu;
            break;
        }
    }

    // Final stress/deformation recovery
    sol.stresses     = stressAnalysis(sol, 250.0e6);
    sol.deformations = deformationAnalysis(sol);
    return sol;
}

/* ================================================================
   Post-processing
   ================================================================ */

std::vector<FEMAnalyzer::ElementStress>
FEMAnalyzer::stressAnalysis(const Solution& sol, double yieldStrength)
{
    std::vector<ElementStress> results;

    for (const auto& elem : trussElems_) {
        // Find nodes
        Node nI{0,0,0,0}, nJ{0,0,0,0};
        for (const auto& n : nodes_) {
            if (n.id == elem.nodeI) nI = n;
            if (n.id == elem.nodeJ) nJ = n;
        }

        const double dx = nJ.x - nI.x;
        const double dy = nJ.y - nI.y;
        const double dz = nJ.z - nI.z;
        const double L  = std::sqrt(dx*dx + dy*dy + dz*dz);
        if (L < 1e-12) continue;

        // Find displacements
        Displacement dI{0,0,0,0,0,0,0}, dJ{0,0,0,0,0,0,0};
        for (const auto& d : sol.displacements) {
            if (d.nodeId == elem.nodeI) dI = d;
            if (d.nodeId == elem.nodeJ) dJ = d;
        }

        // Axial strain = (Δu · e) / L
        const double du = dJ.dx - dI.dx;
        const double dv = dJ.dy - dI.dy;
        const double dw = dJ.dz - dI.dz;
        const double axialStrain = (du*dx + dv*dy + dw*dz) / (L*L);

        ElementStress es;
        es.elementId    = elem.id;
        es.axialStress  = elem.E * axialStrain;
        es.bendingStressY = 0.0;
        es.bendingStressZ = 0.0;
        es.vonMises     = std::abs(es.axialStress);
        es.safetyFactor = (es.vonMises > 1e-12) ? yieldStrength / es.vonMises : 1e9;
        es.exceedsYield = (es.vonMises > yieldStrength);
        results.push_back(es);
    }

    for (const auto& elem : beamElems_) {
        Node nI{0,0,0,0}, nJ{0,0,0,0};
        for (const auto& n : nodes_) {
            if (n.id == elem.nodeI) nI = n;
            if (n.id == elem.nodeJ) nJ = n;
        }
        const double dx = nJ.x - nI.x;
        const double dy = nJ.y - nI.y;
        const double dz = nJ.z - nI.z;
        const double L  = std::sqrt(dx*dx + dy*dy + dz*dz);
        if (L < 1e-12) continue;

        Displacement dI{0,0,0,0,0,0,0}, dJ{0,0,0,0,0,0,0};
        for (const auto& d : sol.displacements) {
            if (d.nodeId == elem.nodeI) dI = d;
            if (d.nodeId == elem.nodeJ) dJ = d;
        }

        const double du = dJ.dx - dI.dx;
        const double axialStrain = (du*dx) / (L*L);
        const double axialStress = elem.E * axialStrain;

        // Bending stress from transverse displacements (simplified)
        const double M_y = elem.E * elem.Iy * 6.0 * std::abs(dJ.dz - dI.dz) / (L*L);
        const double M_z = elem.E * elem.Iz * 6.0 * std::abs(dJ.dy - dI.dy) / (L*L);
        const double sigma_bend_y = M_y * std::sqrt(elem.Iy / M_PI) * 2.0; // approx
        const double sigma_bend_z = M_z * std::sqrt(elem.Iz / M_PI) * 2.0;
        (void)sigma_bend_y; (void)sigma_bend_z;

        ElementStress es;
        es.elementId      = elem.id;
        es.axialStress    = axialStress;
        es.bendingStressY = M_y / (elem.Iy / (std::sqrt(elem.Iy)*0.5 + 1e-12));
        es.bendingStressZ = M_z / (elem.Iz / (std::sqrt(elem.Iz)*0.5 + 1e-12));

        // Von Mises for beam: σ_vm = √(σ_ax² + 3τ²)  simplified
        const double sigma_total = std::abs(axialStress)
                                 + std::abs(es.bendingStressY)
                                 + std::abs(es.bendingStressZ);
        es.vonMises     = sigma_total;
        es.safetyFactor = (es.vonMises > 1e-12) ? yieldStrength / es.vonMises : 1e9;
        es.exceedsYield = (es.vonMises > yieldStrength);
        results.push_back(es);
    }

    return results;
}

std::vector<FEMAnalyzer::ElementDeformation>
FEMAnalyzer::deformationAnalysis(const Solution& sol)
{
    std::vector<ElementDeformation> results;

    for (const auto& elem : trussElems_) {
        Node nI{0,0,0,0}, nJ{0,0,0,0};
        for (const auto& n : nodes_) {
            if (n.id == elem.nodeI) nI = n;
            if (n.id == elem.nodeJ) nJ = n;
        }
        Displacement dI{0,0,0,0,0,0,0}, dJ{0,0,0,0,0,0,0};
        for (const auto& d : sol.displacements) {
            if (d.nodeId == elem.nodeI) dI = d;
            if (d.nodeId == elem.nodeJ) dJ = d;
        }
        const double du = dJ.dx - dI.dx;
        const double dv = dJ.dy - dI.dy;
        const double dw = dJ.dz - dI.dz;
        const double L0 = std::sqrt((nJ.x-nI.x)*(nJ.x-nI.x)
                                  + (nJ.y-nI.y)*(nJ.y-nI.y)
                                  + (nJ.z-nI.z)*(nJ.z-nI.z));
        const double L1 = std::sqrt((nJ.x+dJ.dx-nI.x-dI.dx)*(nJ.x+dJ.dx-nI.x-dI.dx)
                                  + (nJ.y+dJ.dy-nI.y-dI.dy)*(nJ.y+dJ.dy-nI.y-dI.dy)
                                  + (nJ.z+dJ.dz-nI.z-dI.dz)*(nJ.z+dJ.dz-nI.z-dI.dz));
        ElementDeformation ed;
        ed.elementId     = elem.id;
        ed.axialStrain   = (L0 > 1e-12) ? (L1 - L0) / L0 : 0.0;
        ed.curvatureY    = 0.0;
        ed.curvatureZ    = 0.0;
        ed.totalDeflection = std::sqrt(du*du + dv*dv + dw*dw);
        results.push_back(ed);
    }

    for (const auto& elem : beamElems_) {
        Node nI{0,0,0,0}, nJ{0,0,0,0};
        for (const auto& n : nodes_) {
            if (n.id == elem.nodeI) nI = n;
            if (n.id == elem.nodeJ) nJ = n;
        }
        Displacement dI{0,0,0,0,0,0,0}, dJ{0,0,0,0,0,0,0};
        for (const auto& d : sol.displacements) {
            if (d.nodeId == elem.nodeI) dI = d;
            if (d.nodeId == elem.nodeJ) dJ = d;
        }
        const double L = std::sqrt((nJ.x-nI.x)*(nJ.x-nI.x)
                                 + (nJ.y-nI.y)*(nJ.y-nI.y)
                                 + (nJ.z-nI.z)*(nJ.z-nI.z));
        ElementDeformation ed;
        ed.elementId     = elem.id;
        ed.axialStrain   = (dJ.dx - dI.dx) / std::max(L, 1e-12);
        ed.curvatureY    = (dJ.rz - dI.rz) / std::max(L, 1e-12);
        ed.curvatureZ    = (dJ.ry - dI.ry) / std::max(L, 1e-12);
        ed.totalDeflection = std::sqrt((dJ.dx-dI.dx)*(dJ.dx-dI.dx)
                                     + (dJ.dy-dI.dy)*(dJ.dy-dI.dy)
                                     + (dJ.dz-dI.dz)*(dJ.dz-dI.dz));
        results.push_back(ed);
    }

    return results;
}

/* ================================================================
   Modal analysis (subspace iteration for lowest eigenvalues)
   ================================================================ */
std::pair<std::vector<double>, std::vector<std::vector<double>>>
FEMAnalyzer::modalAnalysis(int numModes)
{
    // Assemble K
    std::vector<double> K;
    std::vector<int> idiag;
    assembleStiffness(K, idiag);
    const int ndof = activeDOFs();

    numModes = std::min(numModes, ndof);

    // Lumped mass matrix (diagonal)
    std::vector<double> M(ndof, 0.0);
    const auto dofMap = buildDOFMap();
    for (size_t ni = 0; ni < nodes_.size(); ++ni) {
        // Estimate mass from connected elements
        double nodeMass = 0.0;
        for (const auto& elem : trussElems_) {
            if (elem.nodeI == nodes_[ni].id || elem.nodeJ == nodes_[ni].id) {
                Node nI{0,0,0,0}, nJ{0,0,0,0};
                for (const auto& n : nodes_) {
                    if (n.id == elem.nodeI) nI = n;
                    if (n.id == elem.nodeJ) nJ = n;
                }
                const double L = std::sqrt((nJ.x-nI.x)*(nJ.x-nI.x)
                                         + (nJ.y-nI.y)*(nJ.y-nI.y)
                                         + (nJ.z-nI.z)*(nJ.z-nI.z));
                const double rho = 7850.0; // steel density kg/m³
                nodeMass += 0.5 * rho * elem.A * L;
            }
        }
        if (nodeMass < 0.1) nodeMass = 1.0; // minimum mass
        for (int d = 0; d < 3; ++d) {
            int gdof = dofMap[ni * 6 + d];
            if (gdof >= 0) M[gdof] = nodeMass;
        }
    }

    // Inverse iteration for lowest modes
    std::vector<std::vector<double>> eigenVectors(numModes, std::vector<double>(ndof, 0.0));
    std::vector<double> eigenValues(numModes, 0.0);

    // Random initial guesses
    for (int m = 0; m < numModes; ++m) {
        for (int i = 0; i < ndof; ++i) {
            eigenVectors[m][i] = std::sin((m + 1) * (i + 1) * M_PI / (ndof + 1.0));
        }
    }

    // Gram-Schmidt + inverse iteration
    for (int iter = 0; iter < 30; ++iter) {
        for (int m = 0; m < numModes; ++m) {
            // Solve K·x = M·φ_m  (inverse iteration step)
            std::vector<double> rhs(ndof, 0.0);
            for (int i = 0; i < ndof; ++i) {
                rhs[i] = M[i] * eigenVectors[m][i];
            }
            std::vector<double> x = rhs;
            solveSkyline(K, idiag, x);

            // Rayleigh quotient for eigenvalue
            double num = 0.0, den = 0.0;
            for (int i = 0; i < ndof; ++i) {
                num += x[i] * M[i] * eigenVectors[m][i];
                den += x[i] * M[i] * x[i];
            }
            eigenValues[m] = (den > 1e-12) ? num / den : 0.0;

            // Normalize
            double norm = 0.0;
            for (int i = 0; i < ndof; ++i) {
                eigenVectors[m][i] = x[i];
                norm += M[i] * x[i] * x[i];
            }
            norm = std::sqrt(norm);
            if (norm > 1e-12) {
                for (int i = 0; i < ndof; ++i) eigenVectors[m][i] /= norm;
            }

            // Gram-Schmidt orthogonalisation against lower modes
            for (int k = 0; k < m; ++k) {
                double proj = 0.0;
                for (int i = 0; i < ndof; ++i) {
                    proj += M[i] * eigenVectors[m][i] * eigenVectors[k][i];
                }
                for (int i = 0; i < ndof; ++i) {
                    eigenVectors[m][i] -= proj * eigenVectors[k][i];
                }
            }
        }
    }

    // Convert eigenvalues to frequencies: ω = √(1/λ), f = ω/(2π)
    std::vector<double> frequencies;
    for (int m = 0; m < numModes; ++m) {
        if (eigenValues[m] > 1e-12) {
            frequencies.push_back(1.0 / (2.0 * M_PI * std::sqrt(eigenValues[m])));
        } else {
            frequencies.push_back(0.0);
        }
    }

    return {frequencies, eigenVectors};
}

} // namespace powsys365::linedesign
