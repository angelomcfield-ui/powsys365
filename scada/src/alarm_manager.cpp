#include "powsy365/scada/alarm_manager.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <ctime>
#include <cstring>

namespace powsys365 {

// ============================================================================
// String helpers
// ============================================================================
std::string alarmSeverityToString(AlarmSeverity s) {
    switch (s) {
        case AlarmSeverity::INFO: return "INFO";
        case AlarmSeverity::WARNING: return "WARNING";
        case AlarmSeverity::CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

AlarmSeverity alarmSeverityFromString(const std::string& s) {
    if (s == "CRITICAL") return AlarmSeverity::CRITICAL;
    if (s == "WARNING") return AlarmSeverity::WARNING;
    return AlarmSeverity::INFO;
}

std::string alarmStateToString(AlarmState s) {
    switch (s) {
        case AlarmState::ACTIVE_UNACK: return "ACTIVE_UNACK";
        case AlarmState::ACTIVE_ACK: return "ACTIVE_ACK";
        case AlarmState::INACTIVE_UNACK: return "INACTIVE_UNACK";
        case AlarmState::INACTIVE_ACK: return "INACTIVE_ACK";
        case AlarmState::SUPPRESSED: return "SUPPRESSED";
        default: return "UNKNOWN";
    }
}

std::string alarmConditionTypeToString(AlarmConditionType t) {
    switch (t) {
        case AlarmConditionType::OVERVOLTAGE: return "OVERVOLTAGE";
        case AlarmConditionType::UNDERVOLTAGE: return "UNDERVOLTAGE";
        case AlarmConditionType::OVERCURRENT: return "OVERCURRENT";
        case AlarmConditionType::OVERLOAD: return "OVERLOAD";
        case AlarmConditionType::FREQUENCY_HIGH: return "FREQUENCY_HIGH";
        case AlarmConditionType::FREQUENCY_LOW: return "FREQUENCY_LOW";
        case AlarmConditionType::POWER_FACTOR_LOW: return "POWER_FACTOR_LOW";
        case AlarmConditionType::VOLTAGE_UNBALANCE: return "VOLTAGE_UNBALANCE";
        case AlarmConditionType::CURRENT_UNBALANCE: return "CURRENT_UNBALANCE";
        case AlarmConditionType::HARMONIC_DISTORTION: return "HARMONIC_DISTORTION";
        case AlarmConditionType::BREAKER_TRIP: return "BREAKER_TRIP";
        case AlarmConditionType::COMMUNICATION_LOST: return "COMMUNICATION_LOST";
        case AlarmConditionType::PROTECTION_OPERATED: return "PROTECTION_OPERATED";
        case AlarmConditionType::GENERATOR_TRIP: return "GENERATOR_TRIP";
        case AlarmConditionType::TRANSFORMER_OVERTEMP: return "TRANSFORMER_OVERTEMP";
        case AlarmConditionType::CUSTOM: return "CUSTOM";
        default: return "UNKNOWN";
    }
}

// ============================================================================
// AlarmManager
// ============================================================================
AlarmManager::AlarmManager() = default;

AlarmManager::~AlarmManager() {
    shutdown();
}

bool AlarmManager::initialize(const AlarmDatabaseConfig& config) {
    if (m_initialized.load()) return true;

    m_dbConfig = config;

    if (config.type == AlarmDatabaseType::SQLITE) {
        if (!initSqlite()) return false;
    } else {
        if (!initPostgres()) return false;
    }

    createTablesIfNotExist();

    // Start threads
    m_running.store(true);
    m_evalThread = std::thread(&AlarmManager::processEvaluationQueue, this);
    m_notifRunning.store(true);
    m_notifThread = std::thread(&AlarmManager::notificationDispatcherLoop, this);

    m_initialized.store(true);
    return true;
}

void AlarmManager::shutdown() {
    m_running.store(false);
    m_notifRunning.store(false);
    m_evalCv.notify_all();

    if (m_evalThread.joinable()) m_evalThread.join();
    if (m_notifThread.joinable()) m_notifThread.join();

    if (m_sqliteDb) {
        sqlite3_close(m_sqliteDb);
        m_sqliteDb = nullptr;
    }
    if (m_pgConn) {
        PQfinish(m_pgConn);
        m_pgConn = nullptr;
    }

    m_initialized.store(false);
}

bool AlarmManager::isInitialized() const {
    return m_initialized.load();
}

// ---------------------------------------------------------------------------
// Database initialization
// ---------------------------------------------------------------------------
bool AlarmManager::initSqlite() {
    int rc = sqlite3_open(m_dbConfig.sqlitePath.c_str(), &m_sqliteDb);
    if (rc != SQLITE_OK) {
        m_sqliteDb = nullptr;
        return false;
    }

    // Enable WAL mode for better concurrency
    sqlite3_exec(m_sqliteDb, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(m_sqliteDb, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);

    return true;
}

bool AlarmManager::initPostgres() {
    std::ostringstream connStr;
    connStr << "host=" << m_dbConfig.pgHost
            << " port=" << m_dbConfig.pgPort
            << " dbname=" << m_dbConfig.pgDatabase
            << " user=" << m_dbConfig.pgUser
            << " password=" << m_dbConfig.pgPassword;

    m_pgConn = PQconnectdb(connStr.str().c_str());
    if (PQstatus(m_pgConn) != CONNECTION_OK) {
        PQfinish(m_pgConn);
        m_pgConn = nullptr;
        return false;
    }
    return true;
}

void AlarmManager::createTablesIfNotExist() {
    const char* createAlarmsTable =
        "CREATE TABLE IF NOT EXISTS alarms ("
        "  alarm_id TEXT PRIMARY KEY,"
        "  rule_id TEXT NOT NULL,"
        "  rule_name TEXT,"
        "  severity INTEGER NOT NULL,"
        "  condition_type TEXT NOT NULL,"
        "  state TEXT NOT NULL,"
        "  description TEXT,"
        "  measurement_point_id TEXT,"
        "  measured_value REAL,"
        "  threshold_value REAL,"
        "  unit TEXT,"
        "  trigger_time INTEGER NOT NULL,"
        "  acknowledge_time INTEGER,"
        "  clear_time INTEGER,"
        "  acknowledged_by TEXT,"
        "  cleared_by TEXT,"
        "  notes TEXT,"
        "  occurrence_count INTEGER DEFAULT 1"
        ")";

    const char* createRulesTable =
        "CREATE TABLE IF NOT EXISTS alarm_rules ("
        "  rule_id TEXT PRIMARY KEY,"
        "  name TEXT NOT NULL,"
        "  description TEXT,"
        "  condition_type TEXT NOT NULL,"
        "  severity INTEGER NOT NULL,"
        "  measurement_point_id TEXT,"
        "  threshold_high REAL,"
        "  threshold_low REAL,"
        "  deadband REAL DEFAULT 0,"
        "  delay_ms INTEGER DEFAULT 0,"
        "  enabled INTEGER DEFAULT 1,"
        "  action_script TEXT,"
        "  notify_users TEXT,"
        "  active_hours TEXT,"
        "  active_days TEXT"
        ")";

    const char* createNotificationsTable =
        "CREATE TABLE IF NOT EXISTS alarm_notifications ("
        "  notification_id TEXT PRIMARY KEY,"
        "  alarm_id TEXT NOT NULL,"
        "  type TEXT NOT NULL,"
        "  recipient TEXT,"
        "  message TEXT,"
        "  delivered INTEGER DEFAULT 0,"
        "  sent_time INTEGER,"
        "  delivered_time INTEGER"
        ")";

    const char* createSuppressionTable =
        "CREATE TABLE IF NOT EXISTS suppression_schedules ("
        "  schedule_id TEXT PRIMARY KEY,"
        "  rule_id TEXT NOT NULL,"
        "  start_time INTEGER NOT NULL,"
        "  end_time INTEGER NOT NULL,"
        "  reason TEXT"
        ")";

    if (m_sqliteDb) {
        sqlite3_exec(m_sqliteDb, createAlarmsTable, nullptr, nullptr, nullptr);
        sqlite3_exec(m_sqliteDb, createRulesTable, nullptr, nullptr, nullptr);
        sqlite3_exec(m_sqliteDb, createNotificationsTable, nullptr, nullptr, nullptr);
        sqlite3_exec(m_sqliteDb, createSuppressionTable, nullptr, nullptr, nullptr);

        // Create indexes
        sqlite3_exec(m_sqliteDb,
            "CREATE INDEX IF NOT EXISTS idx_alarms_rule ON alarms(rule_id);", nullptr, nullptr, nullptr);
        sqlite3_exec(m_sqliteDb,
            "CREATE INDEX IF NOT EXISTS idx_alarms_state ON alarms(state);", nullptr, nullptr, nullptr);
        sqlite3_exec(m_sqliteDb,
            "CREATE INDEX IF NOT EXISTS idx_alarms_time ON alarms(trigger_time);", nullptr, nullptr, nullptr);
    } else if (m_pgConn) {
        PQexec(m_pgConn, createAlarmsTable);
        PQexec(m_pgConn, createRulesTable);
        PQexec(m_pgConn, createNotificationsTable);
        PQexec(m_pgConn, createSuppressionTable);
    }
}

// ---------------------------------------------------------------------------
// Rule management
// ---------------------------------------------------------------------------
bool AlarmManager::addRule(const AlarmRule& rule) {
    std::lock_guard<std::mutex> lock(m_rulesMutex);
    m_rules[rule.ruleId] = rule;

    // Persist to database
    if (m_sqliteDb) {
        std::ostringstream oss;
        oss << "INSERT OR REPLACE INTO alarm_rules VALUES ('"
            << rule.ruleId << "', '"
            << rule.name << "', '"
            << rule.description << "', '"
            << alarmConditionTypeToString(rule.conditionType) << "', "
            << static_cast<int>(rule.severity) << ", '"
            << rule.measurementPointId << "', "
            << rule.thresholdHigh << ", "
            << rule.thresholdLow << ", "
            << rule.deadband << ", "
            << rule.delayMs << ", "
            << (rule.enabled ? 1 : 0) << ", '"
            << rule.actionScript << "', '";

        for (const auto& u : rule.notifyUsers) {
            oss << u << ",";
        }
        oss << "', '";
        for (int h : rule.activeHours) {
            oss << h << ",";
        }
        oss << "', '";
        for (int d : rule.activeDays) {
            oss << d << ",";
        }
        oss << "')";

        sqlite3_exec(m_sqliteDb, oss.str().c_str(), nullptr, nullptr, nullptr);
    }

    return true;
}

bool AlarmManager::updateRule(const AlarmRule& rule) {
    return addRule(rule);
}

bool AlarmManager::deleteRule(const std::string& ruleId) {
    std::lock_guard<std::mutex> lock(m_rulesMutex);
    m_rules.erase(ruleId);

    if (m_sqliteDb) {
        std::ostringstream oss;
        oss << "DELETE FROM alarm_rules WHERE rule_id = '" << ruleId << "'";
        sqlite3_exec(m_sqliteDb, oss.str().c_str(), nullptr, nullptr, nullptr);
    }

    return true;
}

bool AlarmManager::enableRule(const std::string& ruleId, bool enable) {
    std::lock_guard<std::mutex> lock(m_rulesMutex);
    auto it = m_rules.find(ruleId);
    if (it != m_rules.end()) {
        it->second.enabled = enable;

        if (m_sqliteDb) {
            std::ostringstream oss;
            oss << "UPDATE alarm_rules SET enabled = " << (enable ? 1 : 0)
                << " WHERE rule_id = '" << ruleId << "'";
            sqlite3_exec(m_sqliteDb, oss.str().c_str(), nullptr, nullptr, nullptr);
        }
        return true;
    }
    return false;
}

std::optional<AlarmRule> AlarmManager::getRule(const std::string& ruleId) const {
    std::lock_guard<std::mutex> lock(m_rulesMutex);
    auto it = m_rules.find(ruleId);
    if (it != m_rules.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<AlarmRule> AlarmManager::getAllRules() const {
    std::lock_guard<std::mutex> lock(m_rulesMutex);
    std::vector<AlarmRule> result;
    for (const auto& [id, rule] : m_rules) {
        (void)id;
        result.push_back(rule);
    }
    return result;
}

std::vector<AlarmRule> AlarmManager::getRulesByConditionType(AlarmConditionType type) const {
    std::lock_guard<std::mutex> lock(m_rulesMutex);
    std::vector<AlarmRule> result;
    for (const auto& [id, rule] : m_rules) {
        (void)id;
        if (rule.conditionType == type) {
            result.push_back(rule);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Evaluation
// ---------------------------------------------------------------------------
void AlarmManager::evaluateMeasurement(const std::string& pointId, double value,
                                         const std::string& unit) {
    if (!m_initialized.load()) return;

    {
        std::lock_guard<std::mutex> lock(m_evalMutex);
        m_evalQueue.push(std::make_tuple(pointId, value, unit));
    }
    m_evalCv.notify_one();
}

void AlarmManager::evaluateMeasurements(
    const std::vector<std::tuple<std::string, double, std::string>>& measurements) {
    if (!m_initialized.load()) return;

    {
        std::lock_guard<std::mutex> lock(m_evalMutex);
        for (const auto& m : measurements) {
            m_evalQueue.push(m);
        }
    }
    m_evalCv.notify_one();
}

void AlarmManager::processEvaluationQueue() {
    while (m_running.load()) {
        std::unique_lock<std::mutex> lock(m_evalMutex);
        m_evalCv.wait_for(lock, std::chrono::milliseconds(100), [this] {
            return !m_evalQueue.empty() || !m_running.load();
        });

        if (!m_running.load()) break;

        while (!m_evalQueue.empty()) {
            auto [pointId, value, unit] = m_evalQueue.front();
            m_evalQueue.pop();
            lock.unlock();

            // Get rules for this measurement point
            std::vector<AlarmRule> rules;
            {
                std::lock_guard<std::mutex> rulesLock(m_rulesMutex);
                for (const auto& [id, rule] : m_rules) {
                    (void)id;
                    if (rule.measurementPointId == pointId && rule.enabled &&
                        isRuleActiveNow(rule) && !isSuppressed(rule.ruleId)) {
                        rules.push_back(rule);
                    }
                }
            }

            for (const auto& rule : rules) {
                bool conditionMet = false;
                double threshold = 0.0;
                std::string description;

                switch (rule.conditionType) {
                    case AlarmConditionType::OVERVOLTAGE:
                        if (rule.thresholdHigh > 0 && value > rule.thresholdHigh) {
                            conditionMet = true;
                            threshold = rule.thresholdHigh;
                            description = "Overvoltage detected: " + std::to_string(value) +
                                          unit + " > " + std::to_string(rule.thresholdHigh) + unit;
                        }
                        break;
                    case AlarmConditionType::UNDERVOLTAGE:
                        if (rule.thresholdLow > 0 && value < rule.thresholdLow) {
                            conditionMet = true;
                            threshold = rule.thresholdLow;
                            description = "Undervoltage detected: " + std::to_string(value) +
                                          unit + " < " + std::to_string(rule.thresholdLow) + unit;
                        }
                        break;
                    case AlarmConditionType::OVERCURRENT:
                        if (rule.thresholdHigh > 0 && value > rule.thresholdHigh) {
                            conditionMet = true;
                            threshold = rule.thresholdHigh;
                            description = "Overcurrent detected: " + std::to_string(value) +
                                          unit + " > " + std::to_string(rule.thresholdHigh) + unit;
                        }
                        break;
                    case AlarmConditionType::OVERLOAD:
                        if (rule.thresholdHigh > 0 && value > rule.thresholdHigh) {
                            conditionMet = true;
                            threshold = rule.thresholdHigh;
                            description = "Overload detected: " + std::to_string(value) +
                                          "% > " + std::to_string(rule.thresholdHigh) + "%";
                        }
                        break;
                    case AlarmConditionType::FREQUENCY_HIGH:
                        if (rule.thresholdHigh > 0 && value > rule.thresholdHigh) {
                            conditionMet = true;
                            threshold = rule.thresholdHigh;
                            description = "High frequency: " + std::to_string(value) +
                                          unit + " > " + std::to_string(rule.thresholdHigh) + unit;
                        }
                        break;
                    case AlarmConditionType::FREQUENCY_LOW:
                        if (rule.thresholdLow > 0 && value < rule.thresholdLow) {
                            conditionMet = true;
                            threshold = rule.thresholdLow;
                            description = "Low frequency: " + std::to_string(value) +
                                          unit + " < " + std::to_string(rule.thresholdLow) + unit;
                        }
                        break;
                    case AlarmConditionType::POWER_FACTOR_LOW:
                        if (rule.thresholdLow > 0 && value < rule.thresholdLow) {
                            conditionMet = true;
                            threshold = rule.thresholdLow;
                            description = "Low power factor: " + std::to_string(value) +
                                          " < " + std::to_string(rule.thresholdLow);
                        }
                        break;
                    default:
                        break;
                }

                if (conditionMet) {
                    // Check deadband - don't re-trigger if already active and within deadband
                    bool shouldTrigger = true;
                    {
                        std::lock_guard<std::mutex> alarmLock(m_alarmsMutex);
                        for (const auto& [aid, alarm] : m_activeAlarms) {
                            (void)aid;
                            if (alarm.ruleId == rule.ruleId && alarm.state == AlarmState::ACTIVE_UNACK) {
                                if (std::abs(value - alarm.measuredValue) < rule.deadband) {
                                    shouldTrigger = false;
                                } else {
                                    // Update existing alarm with new value
                                    auto& existingAlarm = const_cast<Alarm&>(alarm);
                                    existingAlarm.measuredValue = value;
                                    existingAlarm.occurrenceCount++;
                                    updateAlarmInDb(existingAlarm);
                                }
                                break;
                            }
                        }
                    }

                    if (shouldTrigger) {
                        Alarm alarm;
                        alarm.alarmId = generateAlarmId();
                        alarm.ruleId = rule.ruleId;
                        alarm.ruleName = rule.name;
                        alarm.severity = rule.severity;
                        alarm.conditionType = rule.conditionType;
                        alarm.state = AlarmState::ACTIVE_UNACK;
                        alarm.description = description;
                        alarm.measurementPointId = pointId;
                        alarm.measuredValue = value;
                        alarm.thresholdValue = threshold;
                        alarm.unit = unit;
                        alarm.triggerTime = std::chrono::system_clock::now();
                        alarm.occurrenceCount = 1;

                        {
                            std::lock_guard<std::mutex> alarmLock(m_alarmsMutex);
                            m_activeAlarms[alarm.alarmId] = alarm;
                        }

                        persistAlarm(alarm);
                        checkAndTriggerNotification(alarm);

                        if (m_onAlarmTriggered) {
                            m_onAlarmTriggered(alarm);
                        }
                    }
                } else {
                    // Check if alarm should be cleared
                    std::lock_guard<std::mutex> alarmLock(m_alarmsMutex);
                    for (auto& [aid, alarm] : m_activeAlarms) {
                        if (alarm.ruleId == rule.ruleId &&
                            (alarm.state == AlarmState::ACTIVE_UNACK ||
                             alarm.state == AlarmState::ACTIVE_ACK)) {
                            bool shouldClear = false;
                            if (rule.thresholdLow > 0 && value > rule.thresholdLow + rule.deadband &&
                                rule.conditionType == AlarmConditionType::UNDERVOLTAGE) {
                                shouldClear = true;
                            } else if (rule.thresholdHigh > 0 &&
                                       value < rule.thresholdHigh - rule.deadband &&
                                       rule.conditionType != AlarmConditionType::UNDERVOLTAGE &&
                                       rule.conditionType != AlarmConditionType::FREQUENCY_LOW &&
                                       rule.conditionType != AlarmConditionType::POWER_FACTOR_LOW) {
                                shouldClear = true;
                            }

                            if (shouldClear) {
                                alarm.state = (alarm.state == AlarmState::ACTIVE_ACK) ?
                                               AlarmState::INACTIVE_ACK : AlarmState::INACTIVE_UNACK;
                                alarm.clearTime = std::chrono::system_clock::now();
                                updateAlarmInDb(alarm);

                                if (m_onAlarmCleared) {
                                    m_onAlarmCleared(alarm);
                                }
                            }
                            break;
                        }
                    }
                }
            }

            lock.lock();
        }
    }
}

// ---------------------------------------------------------------------------
// Alarm actions
// ---------------------------------------------------------------------------
bool AlarmManager::acknowledgeAlarm(const std::string& alarmId, const std::string& user) {
    std::lock_guard<std::mutex> lock(m_alarmsMutex);
    auto it = m_activeAlarms.find(alarmId);
    if (it == m_activeAlarms.end()) return false;

    it->second.state = AlarmState::ACTIVE_ACK;
    it->second.acknowledgeTime = std::chrono::system_clock::now();
    it->second.acknowledgedBy = user;

    updateAlarmInDb(it->second);

    if (m_onAlarmAcknowledged) {
        m_onAlarmAcknowledged(it->second);
    }

    return true;
}

bool AlarmManager::resetAlarm(const std::string& alarmId, const std::string& user) {
    std::lock_guard<std::mutex> lock(m_alarmsMutex);
    auto it = m_activeAlarms.find(alarmId);
    if (it == m_activeAlarms.end()) return false;

    it->second.state = AlarmState::INACTIVE_ACK;
    it->second.clearTime = std::chrono::system_clock::now();
    it->second.clearedBy = user;

    updateAlarmInDb(it->second);

    if (m_onAlarmCleared) {
        m_onAlarmCleared(it->second);
    }

    return true;
}

bool AlarmManager::suppressAlarm(const std::string& alarmId, const std::string& reason) {
    std::lock_guard<std::mutex> lock(m_alarmsMutex);
    auto it = m_activeAlarms.find(alarmId);
    if (it == m_activeAlarms.end()) return false;

    it->second.state = AlarmState::SUPPRESSED;
    it->second.notes = reason;
    updateAlarmInDb(it->second);
    return true;
}

bool AlarmManager::unSuppressAlarm(const std::string& alarmId) {
    std::lock_guard<std::mutex> lock(m_alarmsMutex);
    auto it = m_activeAlarms.find(alarmId);
    if (it == m_activeAlarms.end()) return false;

    if (it->second.state == AlarmState::SUPPRESSED) {
        it->second.state = AlarmState::ACTIVE_UNACK;
        updateAlarmInDb(it->second);
        return true;
    }
    return false;
}

bool AlarmManager::addNotes(const std::string& alarmId, const std::string& notes) {
    std::lock_guard<std::mutex> lock(m_alarmsMutex);
    auto it = m_activeAlarms.find(alarmId);
    if (it == m_activeAlarms.end()) return false;

    it->second.notes += "; " + notes;
    updateAlarmInDb(it->second);
    return true;
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------
std::vector<Alarm> AlarmManager::getActiveAlarms() const {
    std::lock_guard<std::mutex> lock(m_alarmsMutex);
    std::vector<Alarm> result;
    for (const auto& [id, alarm] : m_activeAlarms) {
        (void)id;
        if (alarm.state == AlarmState::ACTIVE_UNACK ||
            alarm.state == AlarmState::ACTIVE_ACK ||
            alarm.state == AlarmState::SUPPRESSED) {
            result.push_back(alarm);
        }
    }
    return result;
}

std::vector<Alarm> AlarmManager::getActiveAlarmsBySeverity(AlarmSeverity severity) const {
    std::lock_guard<std::mutex> lock(m_alarmsMutex);
    std::vector<Alarm> result;
    for (const auto& [id, alarm] : m_activeAlarms) {
        (void)id;
        if (alarm.severity == severity &&
            (alarm.state == AlarmState::ACTIVE_UNACK ||
             alarm.state == AlarmState::ACTIVE_ACK)) {
            result.push_back(alarm);
        }
    }
    return result;
}

std::vector<Alarm> AlarmManager::getAlarmsByRule(const std::string& ruleId) const {
    std::lock_guard<std::mutex> lock(m_alarmsMutex);
    std::vector<Alarm> result;
    for (const auto& [id, alarm] : m_activeAlarms) {
        (void)id;
        if (alarm.ruleId == ruleId) {
            result.push_back(alarm);
        }
    }
    return result;
}

std::vector<Alarm> AlarmManager::getAlarmHistory(
    const std::chrono::system_clock::time_point& start,
    const std::chrono::system_clock::time_point& end) const {
    std::vector<Alarm> result;

    if (m_sqliteDb) {
        auto startSec = std::chrono::duration_cast<std::chrono::seconds>(
            start.time_since_epoch()).count();
        auto endSec = std::chrono::duration_cast<std::chrono::seconds>(
            end.time_since_epoch()).count();

        std::ostringstream oss;
        oss << "SELECT * FROM alarms WHERE trigger_time >= " << startSec
            << " AND trigger_time <= " << endSec
            << " ORDER BY trigger_time DESC";

        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(m_sqliteDb, oss.str().c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                Alarm alarm;
                alarm.alarmId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                alarm.ruleId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                alarm.ruleName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                alarm.severity = static_cast<AlarmSeverity>(sqlite3_column_int(stmt, 3));
                alarm.description = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
                alarm.measuredValue = sqlite3_column_double(stmt, 8);
                alarm.thresholdValue = sqlite3_column_double(stmt, 9);
                result.push_back(alarm);
            }
            sqlite3_finalize(stmt);
        }
    }

    return result;
}

std::optional<Alarm> AlarmManager::getAlarm(const std::string& alarmId) const {
    std::lock_guard<std::mutex> lock(m_alarmsMutex);
    auto it = m_activeAlarms.find(alarmId);
    if (it != m_activeAlarms.end()) {
        return it->second;
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------
AlarmStats AlarmManager::getStatistics() const {
    AlarmStats stats;
    std::lock_guard<std::mutex> lock(m_alarmsMutex);

    for (const auto& [id, alarm] : m_activeAlarms) {
        (void)id;
        stats.totalAlarms++;
        if (alarm.state == AlarmState::ACTIVE_UNACK || alarm.state == AlarmState::ACTIVE_ACK) {
            stats.activeAlarms++;
        }
        if (alarm.state == AlarmState::ACTIVE_ACK || alarm.state == AlarmState::INACTIVE_ACK) {
            stats.acknowledgedAlarms++;
        }
        if (alarm.state == AlarmState::SUPPRESSED) {
            stats.suppressedAlarms++;
        }
        switch (alarm.severity) {
            case AlarmSeverity::CRITICAL: stats.criticalAlarms++; break;
            case AlarmSeverity::WARNING: stats.warningAlarms++; break;
            case AlarmSeverity::INFO: stats.infoAlarms++; break;
        }
        stats.alarmsByType[alarm.conditionType]++;
    }

    return stats;
}

AlarmStats AlarmManager::getStatistics(
    const std::chrono::system_clock::time_point& start,
    const std::chrono::system_clock::time_point& end) const {
    auto alarms = getAlarmHistory(start, end);
    AlarmStats stats;
    for (const auto& alarm : alarms) {
        stats.totalAlarms++;
        switch (alarm.severity) {
            case AlarmSeverity::CRITICAL: stats.criticalAlarms++; break;
            case AlarmSeverity::WARNING: stats.warningAlarms++; break;
            case AlarmSeverity::INFO: stats.infoAlarms++; break;
        }
        stats.alarmsByType[alarm.conditionType]++;
    }
    return stats;
}

// ---------------------------------------------------------------------------
// Suppression schedules
// ---------------------------------------------------------------------------
bool AlarmManager::addSuppressionSchedule(const SuppressionSchedule& schedule) {
    std::lock_guard<std::mutex> lock(m_suppressMutex);
    m_suppressionSchedules[schedule.scheduleId] = schedule;

    if (m_sqliteDb) {
        auto startSec = std::chrono::duration_cast<std::chrono::seconds>(
            schedule.startTime.time_since_epoch()).count();
        auto endSec = std::chrono::duration_cast<std::chrono::seconds>(
            schedule.endTime.time_since_epoch()).count();

        std::ostringstream oss;
        oss << "INSERT OR REPLACE INTO suppression_schedules VALUES ('"
            << schedule.scheduleId << "', '"
            << schedule.ruleId << "', "
            << startSec << ", "
            << endSec << ", '"
            << schedule.reason << "')";
        sqlite3_exec(m_sqliteDb, oss.str().c_str(), nullptr, nullptr, nullptr);
    }

    return true;
}

bool AlarmManager::removeSuppressionSchedule(const std::string& scheduleId) {
    std::lock_guard<std::mutex> lock(m_suppressMutex);
    m_suppressionSchedules.erase(scheduleId);

    if (m_sqliteDb) {
        std::ostringstream oss;
        oss << "DELETE FROM suppression_schedules WHERE schedule_id = '"
            << scheduleId << "'";
        sqlite3_exec(m_sqliteDb, oss.str().c_str(), nullptr, nullptr, nullptr);
    }

    return true;
}

std::vector<SuppressionSchedule> AlarmManager::getActiveSuppressionSchedules() const {
    std::lock_guard<std::mutex> lock(m_suppressMutex);
    std::vector<SuppressionSchedule> result;
    auto now = std::chrono::system_clock::now();
    for (const auto& [id, sched] : m_suppressionSchedules) {
        (void)id;
        if (sched.startTime <= now && sched.endTime >= now) {
            result.push_back(sched);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Notifications
// ---------------------------------------------------------------------------
void AlarmManager::setNotificationCallback(AlarmNotificationType type,
                                               std::function<void(const Alarm&, const AlarmNotification&)> callback) {
    std::lock_guard<std::mutex> lock(m_notifMutex);
    m_notificationCallbacks[type] = callback;
}

std::vector<AlarmNotification> AlarmManager::getPendingNotifications() const {
    std::lock_guard<std::mutex> lock(m_notifMutex);
    std::vector<AlarmNotification> result;
    for (const auto& notif : m_notifications) {
        if (!notif.delivered) {
            result.push_back(notif);
        }
    }
    return result;
}

std::vector<AlarmNotification> AlarmManager::getNotificationsForAlarm(const std::string& alarmId) const {
    std::lock_guard<std::mutex> lock(m_notifMutex);
    std::vector<AlarmNotification> result;
    for (const auto& notif : m_notifications) {
        if (notif.alarmId == alarmId) {
            result.push_back(notif);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------
void AlarmManager::persistAlarm(const Alarm& alarm) {
    if (!m_sqliteDb) return;

    auto triggerSec = std::chrono::duration_cast<std::chrono::seconds>(
        alarm.triggerTime.time_since_epoch()).count();

    std::ostringstream oss;
    oss << "INSERT OR REPLACE INTO alarms VALUES ('"
        << alarm.alarmId << "', '"
        << alarm.ruleId << "', '"
        << alarm.ruleName << "', "
        << static_cast<int>(alarm.severity) << ", '"
        << alarmConditionTypeToString(alarm.conditionType) << "', '"
        << alarmStateToString(alarm.state) << "', '"
        << alarm.description << "', '"
        << alarm.measurementPointId << "', "
        << alarm.measuredValue << ", "
        << alarm.thresholdValue << ", '"
        << alarm.unit << "', "
        << triggerSec << ", NULL, NULL, '', '', '', "
        << alarm.occurrenceCount << ")";

    sqlite3_exec(m_sqliteDb, oss.str().c_str(), nullptr, nullptr, nullptr);
}

void AlarmManager::updateAlarmInDb(const Alarm& alarm) {
    if (!m_sqliteDb) return;

    auto ackSec = alarm.acknowledgeTime.time_since_epoch().count() > 0 ?
        std::chrono::duration_cast<std::chrono::seconds>(
            alarm.acknowledgeTime.time_since_epoch()).count() : 0;
    auto clearSec = alarm.clearTime.time_since_epoch().count() > 0 ?
        std::chrono::duration_cast<std::chrono::seconds>(
            alarm.clearTime.time_since_epoch()).count() : 0;

    std::ostringstream oss;
    oss << "UPDATE alarms SET state = '" << alarmStateToString(alarm.state) << "', "
        << "acknowledge_time = " << (ackSec > 0 ? std::to_string(ackSec) : "NULL") << ", "
        << "clear_time = " << (clearSec > 0 ? std::to_string(clearSec) : "NULL") << ", "
        << "acknowledged_by = '" << alarm.acknowledgedBy << "', "
        << "cleared_by = '" << alarm.clearedBy << "', "
        << "notes = '" << alarm.notes << "', "
        << "measured_value = " << alarm.measuredValue << ", "
        << "occurrence_count = " << alarm.occurrenceCount
        << " WHERE alarm_id = '" << alarm.alarmId << "'";

    sqlite3_exec(m_sqliteDb, oss.str().c_str(), nullptr, nullptr, nullptr);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
bool AlarmManager::isRuleActiveNow(const AlarmRule& rule) const {
    if (rule.activeHours.empty() && rule.activeDays.empty()) return true;

    auto now = std::chrono::system_clock::now();
    std::time_t nowT = std::chrono::system_clock::to_time_t(now);
    std::tm* nowTm = std::localtime(&nowT);

    if (!rule.activeDays.empty()) {
        if (rule.activeDays.find(nowTm->tm_wday) == rule.activeDays.end()) {
            return false;
        }
    }

    if (!rule.activeHours.empty()) {
        if (rule.activeHours.find(nowTm->tm_hour) == rule.activeHours.end()) {
            return false;
        }
    }

    return true;
}

bool AlarmManager::isSuppressed(const std::string& ruleId) const {
    std::lock_guard<std::mutex> lock(m_suppressMutex);
    auto now = std::chrono::system_clock::now();
    for (const auto& [id, sched] : m_suppressionSchedules) {
        (void)id;
        if (sched.ruleId == ruleId && sched.startTime <= now && sched.endTime >= now) {
            return true;
        }
    }
    return false;
}

std::string AlarmManager::generateAlarmId() {
    uint64_t id = m_alarmIdCounter.fetch_add(1);
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    std::ostringstream oss;
    oss << "ALM-" << ms << "-" << id;
    return oss.str();
}

void AlarmManager::checkAndTriggerNotification(const Alarm& alarm) {
    std::lock_guard<std::mutex> lock(m_notifMutex);

    // Always log
    AlarmNotification logNotif;
    logNotif.notificationId = "NOTIF-LOG-" + alarm.alarmId;
    logNotif.alarmId = alarm.alarmId;
    logNotif.type = AlarmNotificationType::LOG_ONLY;
    logNotif.message = alarm.description;
    logNotif.sentTime = std::chrono::system_clock::now();
    logNotif.delivered = true;
    m_notifications.push_back(logNotif);

    // Visual notification for all
    AlarmNotification visualNotif;
    visualNotif.notificationId = "NOTIF-VIS-" + alarm.alarmId;
    visualNotif.alarmId = alarm.alarmId;
    visualNotif.type = AlarmNotificationType::VISUAL;
    visualNotif.message = "[" + alarmSeverityToString(alarm.severity) + "] " +
                           alarm.ruleName + ": " + alarm.description;
    visualNotif.sentTime = std::chrono::system_clock::now();
    m_notifications.push_back(visualNotif);

    // Sound for critical/warning
    if (alarm.severity >= AlarmSeverity::WARNING) {
        AlarmNotification soundNotif;
        soundNotif.notificationId = "NOTIF-SND-" + alarm.alarmId;
        soundNotif.alarmId = alarm.alarmId;
        soundNotif.type = AlarmNotificationType::SOUND;
        soundNotif.sentTime = std::chrono::system_clock::now();
        m_notifications.push_back(soundNotif);
    }
}

void AlarmManager::notificationDispatcherLoop() {
    while (m_notifRunning.load()) {
        std::vector<AlarmNotification> pending;
        {
            std::lock_guard<std::mutex> lock(m_notifMutex);
            for (auto& notif : m_notifications) {
                if (!notif.delivered) {
                    pending.push_back(notif);
                }
            }
        }

        for (auto& notif : pending) {
            auto it = m_notificationCallbacks.find(notif.type);
            if (it != m_notificationCallbacks.end()) {
                // Find the alarm
                std::optional<Alarm> alarm;
                {
                    std::lock_guard<std::mutex> lock(m_alarmsMutex);
                    auto ait = m_activeAlarms.find(notif.alarmId);
                    if (ait != m_activeAlarms.end()) {
                        alarm = ait->second;
                    }
                }
                if (alarm.has_value()) {
                    it->second(alarm.value(), notif);
                }
            }
            notif.delivered = true;
        }

        // Cleanup old delivered notifications
        {
            std::lock_guard<std::mutex> lock(m_notifMutex);
            m_notifications.erase(
                std::remove_if(m_notifications.begin(), m_notifications.end(),
                    [](const AlarmNotification& n) {
                        return n.delivered &&
                            std::chrono::system_clock::now() - n.sentTime > std::chrono::hours(24);
                    }),
                m_notifications.end());
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

bool AlarmManager::exportToFile(const std::string& filePath) const {
    std::lock_guard<std::mutex> lock(m_alarmsMutex);

    FILE* fp = fopen(filePath.c_str(), "w");
    if (!fp) return false;

    fprintf(fp, "Alarm ID, Rule ID, Severity, Condition, State, Description, "
                "Point, Measured Value, Threshold, Unit, Trigger Time\n");

    for (const auto& [id, alarm] : m_activeAlarms) {
        (void)id;
        auto triggerT = std::chrono::system_clock::to_time_t(alarm.triggerTime);
        std::tm* triggerTm = std::localtime(&triggerT);
        char timeStr[64];
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", triggerTm);

        fprintf(fp, "%s, %s, %s, %s, %s, %s, %s, %.4f, %.4f, %s, %s\n",
                alarm.alarmId.c_str(),
                alarm.ruleId.c_str(),
                alarmSeverityToString(alarm.severity).c_str(),
                alarmConditionTypeToString(alarm.conditionType).c_str(),
                alarmStateToString(alarm.state).c_str(),
                alarm.description.c_str(),
                alarm.measurementPointId.c_str(),
                alarm.measuredValue,
                alarm.thresholdValue,
                alarm.unit.c_str(),
                timeStr);
    }

    fclose(fp);
    return true;
}

bool AlarmManager::archiveOldAlarms(int olderThanDays) {
    if (!m_sqliteDb) return false;

    auto cutoff = std::chrono::system_clock::now() - std::chrono::hours(24 * olderThanDays);
    auto cutoffSec = std::chrono::duration_cast<std::chrono::seconds>(
        cutoff.time_since_epoch()).count();

    std::ostringstream oss;
    oss << "DELETE FROM alarms WHERE trigger_time < " << cutoffSec;
    sqlite3_exec(m_sqliteDb, oss.str().c_str(), nullptr, nullptr, nullptr);

    return true;
}

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------
void AlarmManager::setOnAlarmTriggered(std::function<void(const Alarm&)> callback) {
    m_onAlarmTriggered = callback;
}

void AlarmManager::setOnAlarmAcknowledged(std::function<void(const Alarm&)> callback) {
    m_onAlarmAcknowledged = callback;
}

void AlarmManager::setOnAlarmCleared(std::function<void(const Alarm&)> callback) {
    m_onAlarmCleared = callback;
}

void AlarmManager::cleanupOldRecords() {
    if (m_dbConfig.maxRetentionDays > 0) {
        archiveOldAlarms(m_dbConfig.maxRetentionDays);
    }
}

} // namespace powsys365
