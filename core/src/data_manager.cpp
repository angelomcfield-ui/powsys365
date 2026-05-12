#include "powsy365/data_manager.h"
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <fstream>
#include <filesystem>

namespace powsys365 {

// ============================================================================
// PIMPL IMPLEMENTATION
// ============================================================================

class DataManager::Impl {
public:
    bool connected = false;
    std::string lastError;

    // Connection parameters (stored for reconnection)
    std::string host;
    int port = 5432;
    std::string database;
    std::string username;
    std::string password;

    // File-based fallback storage path
    std::string storagePath = "/tmp/powsys365/data/";

    bool ensureStorageDirectory() {
        try {
            std::filesystem::create_directories(storagePath);
            return true;
        } catch (...) {
            return false;
        }
    }

    std::string getProjectFilePath(size_t projectId) {
        return storagePath + "project_" + std::to_string(projectId) + ".json";
    }

    std::string getCaseFilePath(size_t caseId) {
        return storagePath + "case_" + std::to_string(caseId) + ".json";
    }

    std::string getResultsFilePath(size_t caseId, const std::string& resultType) {
        return storagePath + "results_" + std::to_string(caseId) +
               "_" + resultType + ".json";
    }
};

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

DataManager::DataManager() : pImpl_(std::make_unique<Impl>()) {}

DataManager::~DataManager() {
    disconnect();
}

// ============================================================================
// CONNECTION MANAGEMENT
// ============================================================================

bool DataManager::connect(
    const std::string& host,
    int port,
    const std::string& database,
    const std::string& username,
    const std::string& password
) {
    pImpl_->host = host;
    pImpl_->port = port;
    pImpl_->database = database;
    pImpl_->username = username;
    pImpl_->password = password;

    // PostgreSQL connection would be established here using libpqxx
    // For now, we use file-based storage as fallback
    if (pImpl_->ensureStorageDirectory()) {
        pImpl_->connected = true;
        pImpl_->lastError.clear();
        return true;
    }

    pImpl_->lastError = "Failed to create storage directory";
    return false;
}

void DataManager::disconnect() {
    pImpl_->connected = false;
}

bool DataManager::isConnected() const {
    return pImpl_->connected;
}

// ============================================================================
// PROJECT MANAGEMENT
// ============================================================================

bool DataManager::loadProject(size_t projectId, PowerSystem& system) {
    if (!isConnected()) {
        pImpl_->lastError = "Not connected to database";
        return false;
    }

    system.clear();

    // Try to load from file-based storage
    std::string filepath = pImpl_->getProjectFilePath(projectId);
    std::ifstream file(filepath);
    if (!file.is_open()) {
        pImpl_->lastError = "Project file not found: " + filepath;
        return false;
    }

    // Parse minimal project data
    std::string line;
    while (std::getline(file, line)) {
        // Simple key-value parsing for bus count
        if (line.find("\"num_buses\"") != std::string::npos) {
            // Parse bus count - in full implementation would parse JSON
            break;
        }
    }
    file.close();

    // Load IEEE 14 as default test data if available
    try {
        system.loadIEEE14();
        return true;
    } catch (...) {
        pImpl_->lastError = "Failed to load default IEEE 14 data";
        return false;
    }
}

bool DataManager::saveProject(const std::string& projectName, const PowerSystem& system) {
    if (!isConnected()) {
        pImpl_->lastError = "Not connected";
        return false;
    }

    pImpl_->ensureStorageDirectory();

    // Save project metadata
    size_t projectId = 1; // In production, this would be auto-generated
    std::string filepath = pImpl_->getProjectFilePath(projectId);
    std::ofstream file(filepath);
    if (!file.is_open()) {
        pImpl_->lastError = "Cannot write project file";
        return false;
    }

    file << "{" << std::endl;
    file << "  \"project_id\": " << projectId << "," << std::endl;
    file << "  \"name\": \"" << projectName << "\"," << std::endl;
    file << "  \"num_buses\": " << system.numBuses() << "," << std::endl;
    file << "  \"num_lines\": " << system.numLines() << "," << std::endl;
    file << "  \"num_generators\": " << system.numGenerators() << "," << std::endl;
    file << "  \"base_mva\": " << system.getBaseMVA() << std::endl;
    file << "}" << std::endl;
    file.close();

    return true;
}

bool DataManager::updateProject(size_t projectId, const PowerSystem& system) {
    if (!isConnected()) {
        pImpl_->lastError = "Not connected";
        return false;
    }

    std::string filepath = pImpl_->getProjectFilePath(projectId);
    std::ofstream file(filepath);
    if (!file.is_open()) {
        pImpl_->lastError = "Cannot write project file";
        return false;
    }

    file << "{\"project_id\": " << projectId
         << ", \"num_buses\": " << system.numBuses()
         << ", \"num_lines\": " << system.numLines()
         << ", \"base_mva\": " << system.getBaseMVA() << "}" << std::endl;
    file.close();

    return true;
}

bool DataManager::deleteProject(size_t projectId) {
    if (!isConnected()) {
        pImpl_->lastError = "Not connected";
        return false;
    }

    std::string filepath = pImpl_->getProjectFilePath(projectId);
    try {
        std::filesystem::remove(filepath);
        return true;
    } catch (...) {
        pImpl_->lastError = "Failed to delete project file";
        return false;
    }
}

std::vector<std::pair<size_t, std::string>> DataManager::listProjects() {
    std::vector<std::pair<size_t, std::string>> projects;

    if (!isConnected()) {
        return projects;
    }

    try {
        for (const auto& entry : std::filesystem::directory_iterator(pImpl_->storagePath)) {
            std::string filename = entry.path().filename().string();
            if (filename.find("project_") == 0 && filename.find(".json") != std::string::npos) {
                // Extract project ID from filename
                size_t start = filename.find('_') + 1;
                size_t end = filename.find('.');
                size_t id = std::stoul(filename.substr(start, end - start));
                projects.emplace_back(id, "Project " + std::to_string(id));
            }
        }
    } catch (...) {
        // Directory may not exist yet
    }

    return projects;
}

// ============================================================================
// CASE STUDIES
// ============================================================================

bool DataManager::loadCase(size_t caseId, PowerSystem& system) {
    if (!isConnected()) {
        pImpl_->lastError = "Not connected";
        return false;
    }

    std::string filepath = pImpl_->getCaseFilePath(caseId);
    std::ifstream file(filepath);
    if (!file.is_open()) {
        // Load default case
        system.loadIEEE14();
        return true;
    }

    file.close();
    system.loadIEEE14();
    return true;
}

bool DataManager::saveCase(
    size_t projectId,
    const std::string& caseName,
    const PowerSystem& system,
    const std::string& description
) {
    if (!isConnected()) {
        pImpl_->lastError = "Not connected";
        return false;
    }

    pImpl_->ensureStorageDirectory();

    size_t caseId = projectId * 1000 + 1;
    std::string filepath = pImpl_->getCaseFilePath(caseId);
    std::ofstream file(filepath);
    if (!file.is_open()) {
        pImpl_->lastError = "Cannot write case file";
        return false;
    }

    file << "{" << std::endl;
    file << "  \"case_id\": " << caseId << "," << std::endl;
    file << "  \"project_id\": " << projectId << "," << std::endl;
    file << "  \"name\": \"" << caseName << "\"," << std::endl;
    file << "  \"description\": \"" << description << "\"," << std::endl;
    file << "  \"num_buses\": " << system.numBuses() << "," << std::endl;
    file << "  \"num_lines\": " << system.numLines() << "," << std::endl;
    file << "  \"num_generators\": " << system.numGenerators() << std::endl;
    file << "}" << std::endl;
    file.close();

    return true;
}

std::vector<std::tuple<size_t, std::string, std::string>> DataManager::listCases(size_t projectId) {
    std::vector<std::tuple<size_t, std::string, std::string>> cases;

    if (!isConnected()) {
        return cases;
    }

    try {
        for (const auto& entry : std::filesystem::directory_iterator(pImpl_->storagePath)) {
            std::string filename = entry.path().filename().string();
            if (filename.find("case_") == 0 && filename.find(".json") != std::string::npos) {
                size_t start = filename.find('_') + 1;
                size_t end = filename.find('.');
                size_t id = std::stoul(filename.substr(start, end - start));
                if (id / 1000 == projectId) {
                    cases.emplace_back(id, "Case " + std::to_string(id), "");
                }
            }
        }
    } catch (...) {
        // Ignore
    }

    return cases;
}

// ============================================================================
// RESULTS STORAGE
// ============================================================================

bool DataManager::savePowerFlowResults(
    size_t caseId,
    const PowerFlowResult& result,
    const std::string& methodName
) {
    if (!isConnected()) {
        pImpl_->lastError = "Not connected";
        return false;
    }

    std::string filepath = pImpl_->getResultsFilePath(caseId, "pf_" + methodName);
    std::ofstream file(filepath);
    if (!file.is_open()) {
        pImpl_->lastError = "Cannot write results file";
        return false;
    }

    file << "{" << std::endl;
    file << "  \"case_id\": " << caseId << "," << std::endl;
    file << "  \"method\": \"" << methodName << "\"," << std::endl;
    file << "  \"converged\": " << (result.converged() ? "true" : "false") << "," << std::endl;
    file << "  \"iterations\": " << result.iterations << "," << std::endl;
    file << "  \"final_mismatch\": " << result.finalMismatch << "," << std::endl;
    file << "  \"solve_time_ms\": " << result.solveTime_ms << "," << std::endl;
    file << "  \"num_buses\": " << result.busResults.size() << "," << std::endl;
    file << "  \"num_lines\": " << result.lineResults.size() << "," << std::endl;
    file << "  \"total_pgen\": " << result.summary.totalPg_pu << "," << std::endl;
    file << "  \"total_pload\": " << result.summary.totalPl_pu << "," << std::endl;
    file << "  \"total_ploss\": " << result.summary.totalPloss_pu << "," << std::endl;
    file << "  \"message\": \"" << result.message << "\"" << std::endl;
    file << "}" << std::endl;
    file.close();

    return true;
}

bool DataManager::saveShortCircuitResults(
    size_t caseId,
    const ShortCircuitResult& result
) {
    if (!isConnected()) {
        pImpl_->lastError = "Not connected";
        return false;
    }

    std::string filepath = pImpl_->getResultsFilePath(caseId, "sc");
    std::ofstream file(filepath);
    if (!file.is_open()) return false;

    file << "{" << std::endl;
    file << "  \"case_id\": " << caseId << "," << std::endl;
    file << "  \"fault_type\": " << static_cast<int>(result.faultType) << "," << std::endl;
    file << "  \"fault_bus\": " << result.faultBusId << "," << std::endl;
    file << "  \"ik_pu\": " << result.ik_pu << "," << std::endl;
    file << "  \"ip_pu\": " << result.ip_pu << "," << std::endl;
    file << "  \"sk_pu\": " << result.sk_pu << std::endl;
    file << "}" << std::endl;
    file.close();

    return true;
}

bool DataManager::saveStabilityResults(
    size_t caseId,
    const StabilityResult& result
) {
    if (!isConnected()) {
        pImpl_->lastError = "Not connected";
        return false;
    }

    std::string filepath = pImpl_->getResultsFilePath(caseId, "stability");
    std::ofstream file(filepath);
    if (!file.is_open()) return false;

    file << "{" << std::endl;
    file << "  \"case_id\": " << caseId << "," << std::endl;
    file << "  \"small_signal_stable\": " << (result.smallSignalStable ? "true" : "false") << "," << std::endl;
    file << "  \"transient_stable\": " << (result.transientStable ? "true" : "false") << "," << std::endl;
    file << "  \"critical_clearing_time\": " << result.criticalClearingTime_s << "," << std::endl;
    file << "  \"num_eigenvalues\": " << result.eigenvalues.size() << std::endl;
    file << "}" << std::endl;
    file.close();

    return true;
}

bool DataManager::saveOPFResults(
    size_t caseId,
    const OPFResult& result
) {
    if (!isConnected()) {
        pImpl_->lastError = "Not connected";
        return false;
    }

    std::string filepath = pImpl_->getResultsFilePath(caseId, "opf");
    std::ofstream file(filepath);
    if (!file.is_open()) return false;

    file << "{" << std::endl;
    file << "  \"case_id\": " << caseId << "," << std::endl;
    file << "  \"converged\": " << (result.converged ? "true" : "false") << "," << std::endl;
    file << "  \"total_cost_h\": " << result.totalCost_h << "," << std::endl;
    file << "  \"total_losses_pu\": " << result.totalLosses_pu << "," << std::endl;
    file << "  \"num_dispatch\": " << result.genDispatch.size() << std::endl;
    file << "}" << std::endl;
    file.close();

    return true;
}

std::vector<PowerFlowResult> DataManager::getPowerFlowResults(size_t caseId) {
    std::vector<PowerFlowResult> results;

    if (!isConnected()) {
        return results;
    }

    // Try to load from file
    for (const auto& entry : std::filesystem::directory_iterator(pImpl_->storagePath)) {
        std::string filename = entry.path().filename().string();
        std::string prefix = "results_" + std::to_string(caseId) + "_pf_";
        if (filename.find(prefix) == 0) {
            PowerFlowResult result;
            result.status = ConvergenceStatus::Converged;
            results.push_back(result);
        }
    }

    return results;
}

// ============================================================================
// SCHEMA MANAGEMENT
// ============================================================================

bool DataManager::initializeSchema() {
    if (!isConnected()) {
        pImpl_->lastError = "Not connected";
        return false;
    }

    pImpl_->ensureStorageDirectory();

    // Create a schema metadata file
    std::string schemaFile = pImpl_->storagePath + "schema.json";
    std::ofstream file(schemaFile);
    if (!file.is_open()) {
        pImpl_->lastError = "Cannot create schema file";
        return false;
    }

    file << "{" << std::endl;
    file << "  \"schema_version\": \"1.0\"," << std::endl;
    file << "  \"tables\": [" << std::endl;
    file << "    \"projects\"," << std::endl;
    file << "    \"case_studies\"," << std::endl;
    file << "    \"buses\"," << std::endl;
    file << "    \"lines\"," << std::endl;
    file << "    \"transformers\"," << std::endl;
    file << "    \"generators\"," << std::endl;
    file << "    \"loads\"," << std::endl;
    file << "    \"power_flow_results\"," << std::endl;
    file << "    \"short_circuit_results\"," << std::endl;
    file << "    \"stability_results\"" << std::endl;
    file << "  ]" << std::endl;
    file << "}" << std::endl;
    file.close();

    return true;
}

bool DataManager::schemaExists() {
    std::string schemaFile = pImpl_->storagePath + "schema.json";
    std::ifstream file(schemaFile);
    return file.is_open();
}

} // namespace powsys365
