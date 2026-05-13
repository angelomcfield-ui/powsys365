#include "post_import_validator.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <queue>
#include <stack>
#include <numeric>

namespace powsys365::io {

// ============================================================================
// Main validate – runs all sub-validations
// ============================================================================

std::vector<ImportError> PostImportValidator::validate(const PowerSystemData& data) const {
    std::vector<ImportError> allErrors;

    auto conn = validateConnectivity(data);
    allErrors.insert(allErrors.end(), conn.begin(), conn.end());

    auto cons = validateConsistency(data);
    allErrors.insert(allErrors.end(), cons.begin(), cons.end());

    auto elec = validateElectrical(data);
    allErrors.insert(allErrors.end(), elec.begin(), elec.end());

    auto topo = validateTopology(data);
    allErrors.insert(allErrors.end(), topo.begin(), topo.end());

    return allErrors;
}

// ============================================================================
// Connectivity validation
// ============================================================================

std::vector<ImportError> PostImportValidator::validateConnectivity(
    const PowerSystemData& data) const {
    std::vector<ImportError> errs;

    if (data.buses.empty()) {
        errs.push_back({Severity::Warning, "NO_BUSES", "No buses in the network"});
        return errs;
    }

    // Build bus ID set and adjacency list
    std::set<int64_t> busSet;
    for (const auto& b : data.buses) busSet.insert(b.id);

    std::map<int64_t, std::vector<int64_t>> adj;
    for (const auto& b : data.buses) adj[b.id] = {};

    // Add branch connections
    for (const auto& br : data.branches) {
        if (br.status == 1) {
            adj[br.fromBus].push_back(br.toBus);
            adj[br.toBus].push_back(br.fromBus);
        }
    }
    // Add transformer connections
    for (const auto& t : data.transformers) {
        if (t.status == 1) {
            adj[t.fromBus].push_back(t.toBus);
            adj[t.toBus].push_back(t.fromBus);
            if (t.tertBus != 0) {
                adj[t.fromBus].push_back(t.tertBus);
                adj[t.tertBus].push_back(t.fromBus);
                adj[t.toBus].push_back(t.tertBus);
                adj[t.tertBus].push_back(t.toBus);
            }
        }
    }

    // Check that all branch endpoints exist
    for (const auto& br : data.branches) {
        if (busSet.find(br.fromBus) == busSet.end()) {
            errs.push_back({Severity::Error, "MISSING_FROM_BUS",
                "Branch from-bus " + std::to_string(br.fromBus) +
                " does not exist (circuitId=" + br.circuitId + ")"});
        }
        if (busSet.find(br.toBus) == busSet.end()) {
            errs.push_back({Severity::Error, "MISSING_TO_BUS",
                "Branch to-bus " + std::to_string(br.toBus) +
                " does not exist (circuitId=" + br.circuitId + ")"});
        }
    }

    // Check transformer endpoints
    for (const auto& t : data.transformers) {
        if (busSet.find(t.fromBus) == busSet.end()) {
            errs.push_back({Severity::Error, "MISSING_XFMR_BUS",
                "Transformer from-bus " + std::to_string(t.fromBus) +
                " does not exist"});
        }
        if (busSet.find(t.toBus) == busSet.end()) {
            errs.push_back({Severity::Error, "MISSING_XFMR_BUS",
                "Transformer to-bus " + std::to_string(t.toBus) +
                " does not exist"});
        }
        if (t.tertBus != 0 && busSet.find(t.tertBus) == busSet.end()) {
            errs.push_back({Severity::Error, "MISSING_XFMR_BUS",
                "Transformer tertiary bus " + std::to_string(t.tertBus) +
                " does not exist"});
        }
    }

    // Check generator bus references
    for (const auto& g : data.generators) {
        if (busSet.find(g.busId) == busSet.end()) {
            errs.push_back({Severity::Error, "MISSING_GEN_BUS",
                "Generator at bus " + std::to_string(g.busId) +
                " (id=" + g.id + ") references non-existent bus"});
        }
    }

    // Check load bus references
    for (const auto& ld : data.loads) {
        if (busSet.find(ld.busId) == busSet.end()) {
            errs.push_back({Severity::Error, "MISSING_LOAD_BUS",
                "Load at bus " + std::to_string(ld.busId) +
                " (id=" + ld.id + ") references non-existent bus"});
        }
    }

    // Check shunt bus references
    for (const auto& s : data.shunts) {
        if (busSet.find(s.busId) == busSet.end()) {
            errs.push_back({Severity::Error, "MISSING_SHUNT_BUS",
                "Shunt at bus " + std::to_string(s.busId) +
                " (id=" + s.id + ") references non-existent bus"});
        }
    }

    // Find islands
    auto islands = findIslands(adj, data.buses);
    if (islands.empty() && !data.buses.empty()) {
        errs.push_back({Severity::Warning, "DISCONNECTED",
            "All buses appear disconnected"});
    } else if (islands.size() > 1) {
        errs.push_back({Severity::Warning, "MULTIPLE_ISLANDS",
            "Network has " + std::to_string(islands.size()) +
            " disconnected islands (expected 1 for single-system)"});
    }

    // Check for isolated buses
    for (const auto& b : data.buses) {
        if (adj.count(b.id) && adj.at(b.id).empty()) {
            // Check if bus has any generators or loads attached
            bool hasGen = false, hasLoad = false;
            for (const auto& g : data.generators) if (g.busId == b.id) { hasGen = true; break; }
            for (const auto& ld : data.loads) if (ld.busId == b.id) { hasLoad = true; break; }
            if (!hasGen && !hasLoad) {
                errs.push_back({Severity::Warning, "ISOLATED_BUS",
                    "Bus " + b.name + " (" + std::to_string(b.id) +
                    ") is completely isolated with no generation or load"});
            }
        }
    }

    return errs;
}

void PostImportValidator::dfsConnectivity(
    int64_t busId,
    const std::map<int64_t, std::vector<int64_t>>& adj,
    std::set<int64_t>& visited) const {
    std::stack<int64_t> stack;
    stack.push(busId);
    while (!stack.empty()) {
        int64_t curr = stack.top();
        stack.pop();
        if (visited.count(curr)) continue;
        visited.insert(curr);
        auto it = adj.find(curr);
        if (it != adj.end()) {
            for (int64_t neighbor : it->second) {
                if (!visited.count(neighbor)) stack.push(neighbor);
            }
        }
    }
}

std::vector<std::set<int64_t>> PostImportValidator::findIslands(
    const std::map<int64_t, std::vector<int64_t>>& adj,
    const std::vector<Bus>& buses) const {
    std::vector<std::set<int64_t>> islands;
    std::set<int64_t> visited;

    for (const auto& b : buses) {
        if (visited.count(b.id)) continue;
        std::set<int64_t> island;
        dfsConnectivity(b.id, adj, island);
        if (!island.empty()) islands.push_back(island);
        visited.insert(island.begin(), island.end());
    }
    return islands;
}

// ============================================================================
// Consistency validation
// ============================================================================

std::vector<ImportError> PostImportValidator::validateConsistency(
    const PowerSystemData& data) const {
    std::vector<ImportError> errs;

    // Duplicate bus IDs
    std::map<int64_t, int> busIdCounts;
    for (const auto& b : data.buses) busIdCounts[b.id]++;
    for (const auto& [id, count] : busIdCounts) {
        if (count > 1) {
            errs.push_back({Severity::Error, "DUP_BUS_ID",
                "Bus ID " + std::to_string(id) + " appears " + std::to_string(count) +
                " times"});
        }
    }

    // Non-positive base voltages
    for (const auto& b : data.buses) {
        if (b.baseVoltage_kV <= 0) {
            errs.push_back({Severity::Warning, "ZERO_BASEKV",
                "Bus " + b.name + " (" + std::to_string(b.id) +
                ") has base voltage <= 0: " + std::to_string(b.baseVoltage_kV)});
        }
    }

    // Negative ratings
    for (const auto& br : data.branches) {
        if (br.rateA_MVA < 0) {
            errs.push_back({Severity::Warning, "NEG_RATE",
                "Branch " + std::to_string(br.fromBus) + "-" + std::to_string(br.toBus) +
                " has negative rateA: " + std::to_string(br.rateA_MVA)});
        }
    }

    // Invalid status values
    for (const auto& br : data.branches) {
        if (br.status != 0 && br.status != 1) {
            errs.push_back({Severity::Warning, "INVALID_STATUS",
                "Branch " + std::to_string(br.fromBus) + "-" + std::to_string(br.toBus) +
                " has invalid status: " + std::to_string(br.status)});
        }
    }
    for (const auto& g : data.generators) {
        if (g.status != 0 && g.status != 1) {
            errs.push_back({Severity::Warning, "INVALID_STATUS",
                "Generator at bus " + std::to_string(g.busId) +
                " has invalid status: " + std::to_string(g.status)});
        }
    }

    return errs;
}

// ============================================================================
// Electrical validation
// ============================================================================

std::vector<ImportError> PostImportValidator::validateElectrical(
    const PowerSystemData& data) const {
    std::vector<ImportError> errs;

    for (const auto& br : data.branches) {
        // Check for zero-impedance branches (potential numerical issues)
        if (std::abs(br.r_pu) < 1e-10 && std::abs(br.x_pu) < 1e-10) {
            errs.push_back({Severity::Warning, "ZERO_IMPEDANCE",
                "Branch " + std::to_string(br.fromBus) + "-" + std::to_string(br.toBus) +
                " has near-zero impedance (R=" + std::to_string(br.r_pu) +
                ", X=" + std::to_string(br.x_pu) + ")"});
        }
        // Check for negative resistance
        if (br.r_pu < 0) {
            errs.push_back({Severity::Warning, "NEG_RESISTANCE",
                "Branch " + std::to_string(br.fromBus) + "-" + std::to_string(br.toBus) +
                " has negative resistance: " + std::to_string(br.r_pu)});
        }
        // Check for unreasonably large reactance
        if (std::abs(br.x_pu) > 10.0) {
            errs.push_back({Severity::Warning, "LARGE_REACTANCE",
                "Branch " + std::to_string(br.fromBus) + "-" + std::to_string(br.toBus) +
                " has very large reactance: " + std::to_string(br.x_pu)});
        }
    }

    // Transformer validation
    for (const auto& t : data.transformers) {
        if (t.windV1_kV <= 0 || t.windV2_kV <= 0) {
            errs.push_back({Severity::Warning, "ZERO_WINDV",
                "Transformer " + std::to_string(t.fromBus) + "-" + std::to_string(t.toBus) +
                " has zero/negative winding voltage"});
        }
        if (t.rateA_MVA <= 0) {
            errs.push_back({Severity::Warning, "ZERO_RATING",
                "Transformer " + std::to_string(t.fromBus) + "-" + std::to_string(t.toBus) +
                " has zero/negative rating"});
        }
    }

    // Generator validation
    for (const auto& g : data.generators) {
        if (g.pMax_MW < g.pMin_MW) {
            errs.push_back({Severity::Warning, "INVALID_P_LIMITS",
                "Generator at bus " + std::to_string(g.busId) +
                " has Pmax < Pmin (" + std::to_string(g.pMax_MW) + " < " +
                std::to_string(g.pMin_MW) + ")"});
        }
        if (g.qMax_Mvar < g.qMin_Mvar) {
            errs.push_back({Severity::Warning, "INVALID_Q_LIMITS",
                "Generator at bus " + std::to_string(g.busId) +
                " has Qmax < Qmin (" + std::to_string(g.qMax_Mvar) + " < " +
                std::to_string(g.qMin_Mvar) + ")"});
        }
        if (g.vSet_pu <= 0) {
            errs.push_back({Severity::Warning, "INVALID_VSET",
                "Generator at bus " + std::to_string(g.busId) +
                " has non-positive voltage setpoint: " + std::to_string(g.vSet_pu)});
        }
    }

    return errs;
}

// ============================================================================
// Topology validation
// ============================================================================

std::vector<ImportError> PostImportValidator::validateTopology(
    const PowerSystemData& data) const {
    std::vector<ImportError> errs;

    // Self-loops
    for (const auto& br : data.branches) {
        if (br.fromBus == br.toBus && br.fromBus != 0) {
            errs.push_back({Severity::Warning, "SELF_LOOP",
                "Branch " + std::to_string(br.fromBus) + "-" + std::to_string(br.toBus) +
                " is a self-loop (fromBus == toBus)"});
        }
    }

    // Parallel branches check
    std::map<std::pair<int64_t, int64_t>, int> parallelCounts;
    std::map<std::pair<int64_t, int64_t>, std::vector<std::string>> parallelCkts;
    for (const auto& br : data.branches) {
        auto key = std::minmax(br.fromBus, br.toBus);
        parallelCounts[key]++;
        parallelCkts[key].push_back(br.circuitId);
    }
    for (const auto& [key, count] : parallelCounts) {
        if (count > 1) {
            // Check for duplicate circuit IDs
            std::set<std::string> cktSet;
            bool dupCkt = false;
            for (const auto& ckt : parallelCkts[key]) {
                if (!cktSet.insert(ckt).second) dupCkt = true;
            }
            if (dupCkt) {
                errs.push_back({Severity::Warning, "DUP_CIRCUIT_ID",
                    "Parallel branches between bus " + std::to_string(key.first) +
                    " and " + std::to_string(key.second) +
                    " have duplicate circuit IDs"});
            }
        }
    }

    // Transformer three-winding consistency
    for (const auto& t : data.transformers) {
        if (t.tertBus != 0) {
            if (t.fromBus == t.tertBus || t.toBus == t.tertBus) {
                errs.push_back({Severity::Warning, "DUP_WINDING",
                    "Three-winding transformer has duplicate bus on tertiary winding"});
            }
            if (t.windV3_kV <= 0) {
                errs.push_back({Severity::Warning, "ZERO_TERTV",
                    "Three-winding transformer has zero/negative tertiary voltage"});
            }
        }
    }

    return errs;
}

// ============================================================================
// Report generation
// ============================================================================

std::string PostImportValidator::generateReport(
    const PowerSystemData& data,
    const std::vector<ImportError>& errors) const {
    std::ostringstream oss;

    // Count by severity
    int nFatal = 0, nError = 0, nWarning = 0, nInfo = 0;
    for (const auto& e : errors) {
        switch (e.severity) {
            case Severity::Fatal:   nFatal++;   break;
            case Severity::Error:   nError++;   break;
            case Severity::Warning: nWarning++; break;
            case Severity::Info:    nInfo++;    break;
        }
    }

    oss << "=================================================================\n";
    oss << "           POWSYS365 POST-IMPORT VALIDATION REPORT              \n";
    oss << "=================================================================\n\n";

    oss << "NETWORK SUMMARY:\n";
    oss << "  Buses:         " << data.buses.size() << "\n";
    oss << "  Branches:      " << data.branches.size() << "\n";
    oss << "  Transformers:  " << data.transformers.size() << "\n";
    oss << "  Generators:    " << data.generators.size() << "\n";
    oss << "  Loads:         " << data.loads.size() << "\n";
    oss << "  Shunts:        " << data.shunts.size() << "\n";
    oss << "  Total elements: " << data.totalElements() << "\n\n";

    oss << "VALIDATION RESULTS:\n";
    oss << "  Fatal:   " << nFatal << "\n";
    oss << "  Error:   " << nError << "\n";
    oss << "  Warning: " << nWarning << "\n";
    oss << "  Info:    " << nInfo << "\n";
    oss << "  Total:   " << errors.size() << "\n\n";

    if (!errors.empty()) {
        oss << "DETAILED FINDINGS:\n";
        oss << "-----------------------------------------------------------------\n";
        for (const auto& e : errors) {
            oss << e.toString() << "\n";
        }
        oss << "-----------------------------------------------------------------\n";
    }

    if (nFatal == 0 && nError == 0) {
        if (nWarning == 0) {
            oss << "\nSTATUS: PASSED (no issues found)\n";
        } else {
            oss << "\nSTATUS: PASSED WITH WARNINGS\n";
        }
    } else {
        oss << "\nSTATUS: FAILED (" << (nFatal + nError) << " issues require attention)\n";
    }

    // Connectivity summary
    std::set<int64_t> busSet;
    for (const auto& b : data.buses) busSet.insert(b.id);
    std::map<int64_t, std::vector<int64_t>> adj;
    for (const auto& b : data.buses) adj[b.id] = {};
    for (const auto& br : data.branches) {
        if (br.status == 1) {
            adj[br.fromBus].push_back(br.toBus);
            adj[br.toBus].push_back(br.fromBus);
        }
    }
    for (const auto& t : data.transformers) {
        if (t.status == 1) {
            adj[t.fromBus].push_back(t.toBus);
            adj[t.toBus].push_back(t.fromBus);
        }
    }
    auto islands = findIslands(adj, data.buses);
    oss << "\nTOPOLOGY:\n";
    oss << "  Connected islands: " << islands.size() << "\n";
    for (std::size_t i = 0; i < islands.size(); ++i) {
        oss << "    Island " << (i + 1) << ": " << islands[i].size() << " buses\n";
    }

    oss << "\n=================================================================\n";
    return oss.str();
}

} // namespace powsys365::io
