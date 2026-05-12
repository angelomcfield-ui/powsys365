#pragma once
#include "../../commons/types.h"
#include "power_system.h"
#include <string>
#include <vector>
#include <memory>

namespace powsys365 {

/**
 * DataManager - Persistent storage interface for power system projects.
 *
 * Provides database connectivity for loading and saving:
 * - Projects (collections of systems)
 * - Case studies (individual power system snapshots)
 * - Power flow results
 * - Short circuit results
 * - Stability analysis results
 *
 * The database schema is managed externally. This class provides
 * the C++ interface for data operations.
 */
class DataManager {
public:
    DataManager();
    ~DataManager();

    // ========================================================================
    // CONNECTION MANAGEMENT
    // ========================================================================

    /**
     * Connect to the PostgreSQL database.
     * @param host Database server hostname
     * @param port Database server port
     * @param database Database name
     * @param username Database user
     * @param password Database password
     * @return true if connection successful
     */
    bool connect(
        const std::string& host,
        int port,
        const std::string& database,
        const std::string& username,
        const std::string& password
    );

    /** Disconnect from database. */
    void disconnect();

    /** Check if database connection is active. */
    bool isConnected() const;

    // ========================================================================
    // PROJECT MANAGEMENT
    // ========================================================================

    /** Load a complete project including all case studies. */
    bool loadProject(size_t projectId, PowerSystem& system);

    /** Save current system as a new project. */
    bool saveProject(const std::string& projectName, const PowerSystem& system);

    /** Update existing project. */
    bool updateProject(size_t projectId, const PowerSystem& system);

    /** Delete a project and all associated data. */
    bool deleteProject(size_t projectId);

    /** List all available projects. */
    std::vector<std::pair<size_t, std::string>> listProjects();

    // ========================================================================
    // CASE STUDIES
    // ========================================================================

    /** Load a specific case study into the power system. */
    bool loadCase(size_t caseId, PowerSystem& system);

    /** Save current system state as a case study. */
    bool saveCase(
        size_t projectId,
        const std::string& caseName,
        const PowerSystem& system,
        const std::string& description = ""
    );

    /** List case studies for a project. */
    std::vector<std::tuple<size_t, std::string, std::string>> listCases(size_t projectId);

    // ========================================================================
    // RESULTS STORAGE
    // ========================================================================

    /** Save power flow results. */
    bool savePowerFlowResults(
        size_t caseId,
        const PowerFlowResult& result,
        const std::string& methodName
    );

    /** Save short circuit results. */
    bool saveShortCircuitResults(
        size_t caseId,
        const ShortCircuitResult& result
    );

    /** Save stability results. */
    bool saveStabilityResults(
        size_t caseId,
        const StabilityResult& result
    );

    /** Save OPF results. */
    bool saveOPFResults(
        size_t caseId,
        const OPFResult& result
    );

    /** Retrieve power flow results for a case. */
    std::vector<PowerFlowResult> getPowerFlowResults(size_t caseId);

    // ========================================================================
    // SCHEMA MANAGEMENT
    // ========================================================================

    /** Initialize database schema (creates tables if not exist). */
    bool initializeSchema();

    /** Check if schema is initialized. */
    bool schemaExists();

private:
    class Impl;
    std::unique_ptr<Impl> pImpl_;  // PIMPL idiom for DB dependency isolation
};

} // namespace powsys365
