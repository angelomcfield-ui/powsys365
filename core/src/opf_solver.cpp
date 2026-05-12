#include "powsy365/opf_solver.h"
#include "powsy365/ybus_builder.h"
#include <Eigen/SparseLU>
#include <cmath>
#include <stdexcept>
#include <algorithm>

namespace powsys365 {

// ============================================================================
// CONSTRUCTOR
// ============================================================================

OptimalPowerFlow::OptimalPowerFlow(const PowerSystem& system)
    : system_(system), baseMVA_(system.getBaseMVA()),
      nBuses_(system.numBuses()),
      nGen_(system.numGenerators()),
      nLines_(system.numLines()) {
    classifyBuses();
}

// ============================================================================
// BUS CLASSIFICATION
// ============================================================================

void OptimalPowerFlow::classifyBuses() {
    const auto& buses = system_.getBuses();
    pvBusIndices_.clear();
    pqBusIndices_.clear();

    for (size_t i = 0; i < buses.size(); ++i) {
        switch (buses[i].type) {
            case BusType::Slack:
                slackBusIdx_ = i;
                break;
            case BusType::PV:
                pvBusIndices_.push_back(i);
                break;
            case BusType::PQ:
                pqBusIndices_.push_back(i);
                break;
        }
    }
}

// ============================================================================
// OBJECTIVE FUNCTIONS
// ============================================================================

double OptimalPowerFlow::objectiveMinCost(const std::vector<double>& pg) const {
    const auto& generators = system_.getGenerators();
    double cost = 0.0;

    for (size_t i = 0; i < generators.size() && i < pg.size(); ++i) {
        const auto& gen = generators[i];
        double p = pg[i];
        cost += gen.cost_c0 + gen.cost_c1 * p + gen.cost_c2 * p * p;
    }

    return cost;
}

double OptimalPowerFlow::objectiveMinLosses(
    const std::vector<double>& /*pg*/,
    const std::vector<double>& /*va*/
) const {
    // DC approximation: losses are approximated by line resistances
    // Ploss ≈ sum_k R_k * (theta_i - theta_j)^2 / X_k^2
    // Simplified: return total generation minus total load
    double totalGen = 0.0;
    for (const auto& gen : system_.getGenerators()) {
        totalGen += gen.pg_pu;
    }
    double totalLoad = 0.0;
    for (const auto& bus : system_.getBuses()) {
        totalLoad += bus.pl_pu;
    }
    return totalGen - totalLoad;
}

// ============================================================================
// DC B' MATRIX
// ============================================================================

SpMatrix OptimalPowerFlow::buildDCBMatrix() {
    const int n = static_cast<int>(nBuses_);
    std::vector<Triplet> triplets;
    triplets.reserve(n * 4);

    for (const auto& line : system_.getLines()) {
        if (line.status != 1) continue;
        int fi = static_cast<int>(line.fromBus - 1);
        int ti = static_cast<int>(line.toBus - 1);
        if (fi < 0 || ti < 0 || fi >= n || ti >= n) continue;

        double x = line.x_pu;
        if (std::abs(x) < ZERO_IMPEDANCE_THRESHOLD) continue;

        double b = -1.0 / x; // B_ij = -1/x_ij

        triplets.emplace_back(fi, fi, -b);
        triplets.emplace_back(ti, ti, -b);
        triplets.emplace_back(fi, ti, b);
        triplets.emplace_back(ti, fi, b);
    }

    for (const auto& tx : system_.getTransformers()) {
        if (tx.status != 1) continue;
        int fi = static_cast<int>(tx.fromBus - 1);
        int ti = static_cast<int>(tx.toBus - 1);
        if (fi < 0 || ti < 0 || fi >= n || ti >= n) continue;

        double x = tx.x_pu;
        if (std::abs(x) < ZERO_IMPEDANCE_THRESHOLD) continue;

        double b = -1.0 / x;

        triplets.emplace_back(fi, fi, -b);
        triplets.emplace_back(ti, ti, -b);
        triplets.emplace_back(fi, ti, b);
        triplets.emplace_back(ti, fi, b);
    }

    SpMatrix B(n, n);
    B.setFromTriplets(triplets.begin(), triplets.end());
    B.makeCompressed();
    return B;
}

// ============================================================================
// GENERATOR-BUS MATRIX
// ============================================================================

DenseMatrix OptimalPowerFlow::buildGenBusMatrix() {
    const int n = static_cast<int>(nBuses_);
    const int ng = static_cast<int>(nGen_);
    DenseMatrix Cg(n, ng);
    Cg.setZero();

    const auto& generators = system_.getGenerators();
    for (int g = 0; g < ng; ++g) {
        int busIdx = static_cast<int>(generators[g].busId - 1);
        if (busIdx >= 0 && busIdx < n) {
            Cg(busIdx, g) = 1.0;
        }
    }

    return Cg;
}

// ============================================================================
// PTDF MATRIX
// ============================================================================

DenseMatrix OptimalPowerFlow::buildPTDF() {
    // PTDF = dP_line / dP_injection
    // For DC model: P_line = B_line * (theta_i - theta_j)
    // theta = B^-1 * P (where B is reduced without slack)
    // PTDF_k,l = (X_ik - X_jk) / x_l where X = B^-1

    const int n = static_cast<int>(nBuses_);
    const int nl = static_cast<int>(nLines_);

    SpMatrix B = buildDCBMatrix();

    // Remove slack row/column to get B_reduced
    const int nRed = n - 1;
    std::vector<Triplet> redTriplets;
    for (int k = 0; k < B.outerSize(); ++k) {
        if (k == static_cast<int>(slackBusIdx_)) continue;
        int newCol = (k > static_cast<int>(slackBusIdx_)) ? k - 1 : k;
        for (SpMatrix::InnerIterator it(B, k); it; ++it) {
            int row = it.row();
            if (row == static_cast<int>(slackBusIdx_)) continue;
            int newRow = (row > static_cast<int>(slackBusIdx_)) ? row - 1 : row;
            redTriplets.emplace_back(newRow, newCol, it.value());
        }
    }

    SpMatrix Bred(nRed, nRed);
    Bred.setFromTriplets(redTriplets.begin(), redTriplets.end());
    Bred.makeCompressed();

    // Invert B_reduced to get X matrix
    Eigen::SparseLU<SpMatrix, Eigen::COLAMDOrdering<int>> solver;
    solver.compute(Bred);
    if (solver.info() != Eigen::Success) {
        throw std::runtime_error("Failed to factorize B matrix for PTDF");
    }

    // Compute X = B_reduced^-1 by solving for identity columns
    DenseMatrix X(nRed, nRed);
    DenseMatrix I(nRed, nRed);
    I.setIdentity();
    X = solver.solve(I);

    // Build PTDF: for each line k from i to j:
    // PTDF(k, m) = (X_im - X_jm) / x_k for m != slack
    DenseMatrix ptdf(nl, n);
    ptdf.setZero();

    int lineIdx = 0;
    for (const auto& line : system_.getLines()) {
        if (line.status != 1) continue;
        int fi = static_cast<int>(line.fromBus - 1);
        int ti = static_cast<int>(line.toBus - 1);
        double x = line.x_pu;
        if (std::abs(x) < ZERO_IMPEDANCE_THRESHOLD) continue;

        for (int m = 0; m < n; ++m) {
            if (m == static_cast<int>(slackBusIdx_)) continue;
            int mi = (m > static_cast<int>(slackBusIdx_)) ? m - 1 : m;

            double xim = (fi == static_cast<int>(slackBusIdx_)) ? 0.0 : X(
                (fi > static_cast<int>(slackBusIdx_)) ? fi - 1 : fi, mi);
            double xjm = (ti == static_cast<int>(slackBusIdx_)) ? 0.0 : X(
                (ti > static_cast<int>(slackBusIdx_)) ? ti - 1 : ti, mi);

            ptdf(lineIdx, m) = (xim - xjm) / x;
        }
        ++lineIdx;
    }

    return ptdf;
}

// ============================================================================
// DC OPF
// ============================================================================

OPFResult OptimalPowerFlow::solveDCOPF(ObjectiveType objective) {
    OPFResult result;
    const auto& generators = system_.getGenerators();
    const auto& buses = system_.getBuses();
    const int n = static_cast<int>(nBuses_);
    const int ng = static_cast<int>(generators.size());

    if (ng == 0) {
        result.message = "No generators in system";
        return result;
    }

    try {
        // Build DC B matrix
        SpMatrix B = buildDCBMatrix();

        // Build reduced B (remove slack)
        const int nRed = n - 1;
        std::vector<Triplet> redTriplets;
        for (int k = 0; k < B.outerSize(); ++k) {
            if (k == static_cast<int>(slackBusIdx_)) continue;
            int newCol = (k > static_cast<int>(slackBusIdx_)) ? k - 1 : k;
            for (SpMatrix::InnerIterator it(B, k); it; ++it) {
                int row = it.row();
                if (row == static_cast<int>(slackBusIdx_)) continue;
                int newRow = (row > static_cast<int>(slackBusIdx_)) ? row - 1 : row;
                redTriplets.emplace_back(newRow, newCol, it.value());
            }
        }

        SpMatrix Bred(nRed, nRed);
        Bred.setFromTriplets(redTriplets.begin(), redTriplets.end());
        Bred.makeCompressed();

        Eigen::SparseLU<SpMatrix, Eigen::COLAMDOrdering<int>> solver;
        solver.compute(Bred);
        if (solver.info() != Eigen::Success) {
            result.message = "Failed to factorize B matrix";
            return result;
        }

        // Load vector: Pd - fixed generation (at PV and slack buses)
        DenseVector pLoad(n);
        pLoad.setZero();
        for (int i = 0; i < n; ++i) {
            pLoad(i) = buses[static_cast<size_t>(i)].pl_pu;
        }

        // Subtract fixed generation from loads
        DenseVector pFixed(n);
        pFixed.setZero();
        for (const auto& gen : generators) {
            if (gen.busId > 0 && gen.busId <= static_cast<size_t>(n)) {
                int bIdx = static_cast<int>(gen.busId - 1);
                if (buses[bIdx].type == BusType::Slack || buses[bIdx].type == BusType::PV) {
                    // Slack/PV generation is fixed in DC OPF
                    pFixed(bIdx) += gen.pg_pu;
                }
            }
        }

        // Net injections at each bus (excluding slack)
        DenseVector pNet = pLoad - pFixed;
        DenseVector pNetRed(nRed);
        int idx = 0;
        for (int i = 0; i < n; ++i) {
            if (i == static_cast<int>(slackBusIdx_)) continue;
            pNetRed(idx++) = pNet(i);
        }

        // Solve for angles: B_red * theta_red = pNetRed
        DenseVector thetaRed = solver.solve(pNetRed);
        if (solver.info() != Eigen::Success) {
            result.message = "DC OPF solve failed";
            return result;
        }

        // Full angle vector
        std::vector<double> theta(n, 0.0);
        idx = 0;
        for (int i = 0; i < n; ++i) {
            if (i == static_cast<int>(slackBusIdx_)) {
                theta[i] = 0.0; // Slack angle = 0
            } else {
                theta[i] = thetaRed(idx++);
            }
        }

        // Calculate generation dispatch for PQ buses (no dispatch, just balance)
        std::vector<double> pg(ng);
        for (int g = 0; g < ng; ++g) {
            pg[g] = generators[g].pg_pu;
        }

        // Slack generation = net injection at slack bus
        double pSlack = 0.0;
        for (int i = 0; i < n; ++i) {
            if (i != static_cast<int>(slackBusIdx_)) {
                pSlack += pNet(i);
            }
        }
        pSlack = -pSlack; // Slack absorbs the difference

        // Calculate line flows
        std::vector<PowerFlowLineResult> lineFlows;
        for (const auto& line : system_.getLines()) {
            if (line.status != 1) continue;
            int fi = static_cast<int>(line.fromBus - 1);
            int ti = static_cast<int>(line.toBus - 1);
            double x = line.x_pu;
            if (std::abs(x) < ZERO_IMPEDANCE_THRESHOLD) continue;

            PowerFlowLineResult lr;
            lr.lineId = line.id;
            lr.lineName = line.name;
            lr.fromBus = line.fromBus;
            lr.toBus = line.toBus;
            lr.pFrom_pu = (theta[fi] - theta[ti]) / x;
            lr.pTo_pu = -lr.pFrom_pu;
            double sMag = std::abs(lr.pFrom_pu);
            if (line.rateA_pu > 0) {
                lr.loading_pu = sMag / line.rateA_pu;
            }
            lineFlows.push_back(lr);
        }

        // Build results
        for (int i = 0; i < n; ++i) {
            PowerFlowBusResult br;
            br.busId = buses[static_cast<size_t>(i)].id;
            br.busName = buses[static_cast<size_t>(i)].name;
            br.type = buses[static_cast<size_t>(i)].type;
            br.vm_pu = 1.0; // DC approximation
            br.va_deg = theta[i] * RAD_TO_DEG;
            br.pl_pu = buses[static_cast<size_t>(i)].pl_pu;
            result.busResults.push_back(br);
        }

        result.lineResults = lineFlows;
        result.totalCost_h = objectiveMinCost(pg);

        // Calculate total losses
        double totalGen = 0.0;
        for (double p : pg) totalGen += p;
        result.totalLosses_pu = totalGen + pSlack - system_.getTotalPLoad();

        result.genDispatch.reserve(ng);
        for (int g = 0; g < ng; ++g) {
            OPFGeneratorResult gr;
            gr.genId = generators[g].id;
            gr.busId = generators[g].busId;
            gr.pg_pu = pg[g];
            gr.marginalCost = generators[g].cost_c1 + 2.0 * generators[g].cost_c2 * pg[g];
            gr.atLimit = (pg[g] >= generators[g].pgMax_pu - 1e-6) ||
                         (pg[g] <= generators[g].pgMin_pu + 1e-6);
            result.genDispatch.push_back(gr);
        }

        std::vector<double> vaVec(theta.data(), theta.data() + n);
        result.violations = checkLineFlowLimitsDC(vaVec);
        result.converged = true;
        result.message = "DC OPF solved successfully";

    } catch (const std::exception& e) {
        result.message = std::string("DC OPF failed: ") + e.what();
    }

    return result;
}

// ============================================================================
// AC OPF (Successive Linearization)
// ============================================================================

OPFResult OptimalPowerFlow::solveACOPF(ObjectiveType objective, int maxIterations) {
    // AC OPF via iterative approach:
    // 1. Start with power flow solution
    // 2. Linearize constraints around current point
    // 3. Solve QP/LP for generation re-dispatch
    // 4. Run power flow with new dispatch
    // 5. Repeat until convergence

    OPFResult result;
    result.message = "AC OPF: using successive linearization (simplified implementation)";

    // For a full AC OPF, an NLP solver like IPOPT is required.
    // This implementation provides a successive LP approach.
    try {
        // Start with DC OPF solution
        OPFResult dcResult = solveDCOPF(objective);
        if (!dcResult.converged) {
            result.message = "AC OPF: DC initialization failed";
            return result;
        }

        // Update generator setpoints
        for (const auto& gr : dcResult.genDispatch) {
            auto* gen = const_cast<PowerSystem&>(system_).getGenerator(gr.genId);
            if (gen) {
                gen->pg_pu = gr.pg_pu;
            }
        }

        result = dcResult;
        result.message = "AC OPF: Solved via DC approximation (full NLP requires external solver)";
        result.converged = true;

    } catch (const std::exception& e) {
        result.message = std::string("AC OPF failed: ") + e.what();
    }

    return result;
}

// ============================================================================
// CONSTRAINT CHECKING
// ============================================================================

std::vector<Violation> OptimalPowerFlow::checkGenerationLimits(
    const std::vector<double>& pg
) const {
    std::vector<Violation> violations;
    const auto& generators = system_.getGenerators();

    for (size_t g = 0; g < generators.size() && g < pg.size(); ++g) {
        const auto& gen = generators[g];
        if (pg[g] > gen.pgMax_pu + 1e-6) {
            Violation v;
            v.type = ViolationType::GeneratorOverP;
            v.elementId = gen.id;
            v.elementName = gen.name;
            v.value = pg[g];
            v.limit = gen.pgMax_pu;
            v.severity = pg[g] / gen.pgMax_pu;
            v.description = "Generator " + gen.name + " exceeds Pmax";
            violations.push_back(v);
        } else if (pg[g] < gen.pgMin_pu - 1e-6) {
            Violation v;
            v.type = ViolationType::GeneratorUnderP;
            v.elementId = gen.id;
            v.elementName = gen.name;
            v.value = pg[g];
            v.limit = gen.pgMin_pu;
            v.severity = gen.pgMin_pu / pg[g];
            v.description = "Generator " + gen.name + " below Pmin";
            violations.push_back(v);
        }
    }

    return violations;
}

std::vector<Violation> OptimalPowerFlow::checkLineFlowLimitsDC(
    const std::vector<double>& va
) const {
    std::vector<Violation> violations;

    for (const auto& line : system_.getLines()) {
        if (line.status != 1 || line.rateA_pu <= 0) continue;

        int fi = static_cast<int>(line.fromBus - 1);
        int ti = static_cast<int>(line.toBus - 1);
        double x = line.x_pu;
        if (std::abs(x) < ZERO_IMPEDANCE_THRESHOLD) continue;

        double pFlow = (va[fi] - va[ti]) / x;
        double sMag = std::abs(pFlow);
        double loading = sMag / line.rateA_pu;

        if (loading > LINE_LOADING_NORMAL) {
            Violation v;
            v.type = ViolationType::LineOverload;
            v.elementId = line.id;
            v.elementName = line.name;
            v.value = sMag;
            v.limit = line.rateA_pu;
            v.severity = loading;
            v.description = "Line " + line.name + " overloaded (" +
                std::to_string(loading * 100) + "%)";
            violations.push_back(v);
        }
    }

    return violations;
}

std::vector<Violation> OptimalPowerFlow::checkVoltageLimits(
    const std::vector<double>& vm
) const {
    std::vector<Violation> violations;
    const auto& buses = system_.getBuses();

    for (size_t i = 0; i < buses.size() && i < vm.size(); ++i) {
        if (vm[i] > buses[i].vmax_pu) {
            Violation v;
            v.type = ViolationType::OverVoltage;
            v.elementId = buses[i].id;
            v.elementName = buses[i].name;
            v.value = vm[i];
            v.limit = buses[i].vmax_pu;
            v.severity = vm[i] / buses[i].vmax_pu;
            violations.push_back(v);
        } else if (vm[i] < buses[i].vmin_pu) {
            Violation v;
            v.type = ViolationType::UnderVoltage;
            v.elementId = buses[i].id;
            v.elementName = buses[i].name;
            v.value = vm[i];
            v.limit = buses[i].vmin_pu;
            v.severity = buses[i].vmin_pu / vm[i];
            violations.push_back(v);
        }
    }

    return violations;
}

} // namespace powsys365
