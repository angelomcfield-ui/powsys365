#pragma once

#include "import_types.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>

namespace powsys365::io {

// ============================================================================
// PostImportValidator – comprehensive validation of imported data
// ============================================================================

class PostImportValidator {
public:
    PostImportValidator() = default;
    ~PostImportValidator() = default;

    /**
     * Run all validations on imported data.
     * Returns a list of errors/warnings found.
     */
    std::vector<ImportError> validate(const PowerSystemData& data) const;

    /**
     * Validate network connectivity:
     * - All elements reference existing buses
     * - No isolated buses (unless intentional)
     * - Network is fully connected or has valid islands
     */
    std::vector<ImportError> validateConnectivity(const PowerSystemData& data) const;

    /**
     * Validate data consistency:
     * - No duplicate IDs
     * - Voltage levels are positive
     * - Ratings are non-negative
     * - Status values are valid (0 or 1)
     */
    std::vector<ImportError> validateConsistency(const PowerSystemData& data) const;

    /**
     * Validate electrical parameters:
     * - Branch impedances are reasonable
     * - Transformer ratings match voltage levels
     * - Generator setpoints are within limits
     */
    std::vector<ImportError> validateElectrical(const PowerSystemData& data) const;

    /**
     * Validate topology:
     * - No self-loops in branches
     * - No parallel branches with same ID
     * - Transformer windings are consistent
     */
    std::vector<ImportError> validateTopology(const PowerSystemData& data) const;

    /**
     * Generate a human-readable validation report.
     */
    std::string generateReport(const PowerSystemData& data,
                                const std::vector<ImportError>& errors) const;

private:
    // ---- Connectivity helpers ---------------------------------------------
    void dfsConnectivity(int64_t busId,
                         const std::map<int64_t, std::vector<int64_t>>& adj,
                         std::set<int64_t>& visited) const;

    std::vector<std::set<int64_t>> findIslands(
        const std::map<int64_t, std::vector<int64_t>>& adj,
        const std::vector<Bus>& buses) const;

    // ---- Check helpers ----------------------------------------------------
    bool busExists(int64_t busId, const std::set<int64_t>& busSet) const;
    bool isReasonableImpedance(double r, double x) const;
    bool isReasonableVoltage(double v) const;
};

} // namespace powsys365::io
