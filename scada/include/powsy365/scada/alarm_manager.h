#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <mutex>
#include <atomic>
#include <chrono>
#include <thread>
#include <condition_variable>
#include <queue>
#include <set>
#include <sqlite3.h>
#include <libpq-fe.h>

namespace powsys365 {

// ---------------------------------------------------------------------------
// Alarm severity levels
// ---------------------------------------------------------------------------
enum class AlarmSeverity : int {
    INFO = 1,
    WARNING = 2,
    CRITICAL = 3
};

std::string alarmSeverityToString(AlarmSeverity s);
AlarmSeverity alarmSeverityFromString(const std::string& s);

// ---------------------------------------------------------------------------
// Alarm states
// ---------------------------------------------------------------------------
enum class AlarmState {
    ACTIVE_UNACK,      // Active, not acknowledged
    ACTIVE_ACK,        // Active, acknowledged
    INACTIVE_UNACK,    // Cleared, not acknowledged
    INACTIVE_ACK,      // Cleared, acknowledged (RTN - Return To Normal)
    SUPPRESSED         // Temporarily suppressed
};

std::string alarmStateToString(AlarmState s);

// ---------------------------------------------------------------------------
// Alarm condition types for electrical systems
// ---------------------------------------------------------------------------
enum class AlarmConditionType {
    OVERVOLTAGE,           // V > Vmax
    UNDERVOLTAGE,          // V < Vmin
    OVERCURRENT,           // I > Imax
    OVERLOAD,              // Loading > 100%
    FREQUENCY_HIGH,        // f > fmax
    FREQUENCY_LOW,         // f < fmin
    POWER_FACTOR_LOW,      // cos(phi) < threshold
    VOLTAGE_UNBALANCE,     // V_unbalance > threshold
    CURRENT_UNBALANCE,     // I_unbalance > threshold
    HARMONIC_DISTORTION,   // THD > threshold
    BREAKER_TRIP,          // Breaker opened unexpectedly
    COMMUNICATION_LOST,    // Protocol communication failure
    PROTECTION_OPERATED,   // Protection relay operated
    GENERATOR_TRIP,        // Generator offline
    TRANSFORMER_OVERTEMP,  // Transformer temperature high
    CUSTOM                 // User-defined condition
};

std::string alarmConditionTypeToString(AlarmConditionType t);

// ---------------------------------------------------------------------------
// Alarm rule definition
// ---------------------------------------------------------------------------
struct AlarmRule {
    std::string ruleId;
    std::string name;
    std::string description;
    AlarmConditionType conditionType;
    AlarmSeverity severity;
    std::string measurementPointId; // which measurement to check
    double thresholdHigh = 0.0;
    double thresholdLow = 0.0;
    double deadband = 0.0;          // hysteresis to avoid flapping
    int delayMs = 0;                // activation delay
    bool enabled = true;
    std::string actionScript;       // optional: script/command to execute
    std::vector<std::string> notifyUsers; // user IDs to notify
    std::set<int> activeHours;      // hours when rule is active (empty = always)
    std::set<int> activeDays;       // days of week (0=Sunday, 6=Saturday)
};

// ---------------------------------------------------------------------------
// Suppression schedule
// ---------------------------------------------------------------------------
struct SuppressionSchedule {
    std::string scheduleId;
    std::string ruleId;
    std::chrono::system_clock::time_point startTime;
    std::chrono::system_clock::time_point endTime;
    std::string reason;
};

// ---------------------------------------------------------------------------
// Individual alarm instance
// ---------------------------------------------------------------------------
struct Alarm {
    std::string alarmId;
    std::string ruleId;
    std::string ruleName;
    AlarmSeverity severity;
    AlarmConditionType conditionType;
    AlarmState state;
    std::string description;
    std::string measurementPointId;
    double measuredValue = 0.0;
    double thresholdValue = 0.0;
    std::string unit;
    std::chrono::system_clock::time_point triggerTime;
    std::chrono::system_clock::time_point acknowledgeTime;
    std::chrono::system_clock::time_point clearTime;
    std::string acknowledgedBy;
    std::string clearedBy;
    std::string notes;
    int occurrenceCount = 1;
};

// ---------------------------------------------------------------------------
// Alarm notification
// ---------------------------------------------------------------------------
enum class AlarmNotificationType {
    SOUND,
    VISUAL,
    EMAIL,
    SMS,
    LOG_ONLY
};

struct AlarmNotification {
    std::string notificationId;
    std::string alarmId;
    AlarmNotificationType type;
    std::string recipient;
    std::string message;
    bool delivered = false;
    std::chrono::system_clock::time_point sentTime;
    std::chrono::system_clock::time_point deliveredTime;
};

// ---------------------------------------------------------------------------
// Alarm statistics
// ---------------------------------------------------------------------------
struct AlarmStats {
    uint64_t totalAlarms = 0;
    uint64_t activeAlarms = 0;
    uint64_t acknowledgedAlarms = 0;
    uint64_t criticalAlarms = 0;
    uint64_t warningAlarms = 0;
    uint64_t infoAlarms = 0;
    uint64_t suppressedAlarms = 0;
    std::map<AlarmConditionType, uint64_t> alarmsByType;
    double avgAcknowledgeTimeSec = 0.0;
    double avgClearTimeSec = 0.0;
};

// ---------------------------------------------------------------------------
// Database configuration
// ---------------------------------------------------------------------------
enum class AlarmDatabaseType {
    SQLITE,
    POSTGRESQL
};

struct AlarmDatabaseConfig {
    AlarmDatabaseType type = AlarmDatabaseType::SQLITE;
    std::string sqlitePath = "alarms.db";
    std::string pgHost = "localhost";
    int pgPort = 5432;
    std::string pgDatabase = "powsys365";
    std::string pgUser = "powsys";
    std::string pgPassword = "";
    int maxRetentionDays = 365;
};

// ---------------------------------------------------------------------------
// Alarm Manager
// ---------------------------------------------------------------------------
class AlarmManager {
public:
    AlarmManager();
    ~AlarmManager();

    // Lifecycle
    bool initialize(const AlarmDatabaseConfig& config);
    void shutdown();
    bool isInitialized() const;

    // Rule management
    bool addRule(const AlarmRule& rule);
    bool updateRule(const AlarmRule& rule);
    bool deleteRule(const std::string& ruleId);
    bool enableRule(const std::string& ruleId, bool enable);
    std::optional<AlarmRule> getRule(const std::string& ruleId) const;
    std::vector<AlarmRule> getAllRules() const;
    std::vector<AlarmRule> getRulesByConditionType(AlarmConditionType type) const;

    // Evaluation
    void evaluateMeasurement(const std::string& pointId, double value,
                              const std::string& unit);
    void evaluateMeasurements(const std::vector<std::tuple<std::string, double, std::string>>& measurements);

    // Alarm actions
    bool acknowledgeAlarm(const std::string& alarmId, const std::string& user);
    bool resetAlarm(const std::string& alarmId, const std::string& user);
    bool suppressAlarm(const std::string& alarmId, const std::string& reason);
    bool unSuppressAlarm(const std::string& alarmId);
    bool addNotes(const std::string& alarmId, const std::string& notes);

    // Queries
    std::vector<Alarm> getActiveAlarms() const;
    std::vector<Alarm> getActiveAlarmsBySeverity(AlarmSeverity severity) const;
    std::vector<Alarm> getAlarmsByRule(const std::string& ruleId) const;
    std::vector<Alarm> getAlarmHistory(const std::chrono::system_clock::time_point& start,
                                        const std::chrono::system_clock::time_point& end) const;
    std::optional<Alarm> getAlarm(const std::string& alarmId) const;

    // Statistics
    AlarmStats getStatistics() const;
    AlarmStats getStatistics(const std::chrono::system_clock::time_point& start,
                              const std::chrono::system_clock::time_point& end) const;

    // Suppression schedules
    bool addSuppressionSchedule(const SuppressionSchedule& schedule);
    bool removeSuppressionSchedule(const std::string& scheduleId);
    std::vector<SuppressionSchedule> getActiveSuppressionSchedules() const;

    // Notifications
    void setNotificationCallback(AlarmNotificationType type,
                                   std::function<void(const Alarm&, const AlarmNotification&)> callback);
    std::vector<AlarmNotification> getPendingNotifications() const;
    std::vector<AlarmNotification> getNotificationsForAlarm(const std::string& alarmId) const;

    // Persistence
    bool exportToFile(const std::string& filePath) const;
    bool archiveOldAlarms(int olderThanDays);

    // Callbacks for UI integration
    void setOnAlarmTriggered(std::function<void(const Alarm&)> callback);
    void setOnAlarmAcknowledged(std::function<void(const Alarm&)> callback);
    void setOnAlarmCleared(std::function<void(const Alarm&)> callback);

private:
    void processEvaluationQueue();
    void notificationDispatcherLoop();
    void persistAlarm(const Alarm& alarm);
    void updateAlarmInDb(const Alarm& alarm);
    bool isRuleActiveNow(const AlarmRule& rule) const;
    bool isSuppressed(const std::string& ruleId) const;
    std::string generateAlarmId();
    void checkAndTriggerNotification(const Alarm& alarm);
    void deliverNotification(const AlarmNotification& notification);
    bool initSqlite();
    bool initPostgres();
    void createTablesIfNotExist();
    void cleanupOldRecords();

    // State
    std::atomic<bool> m_initialized{false};
    AlarmDatabaseConfig m_dbConfig;
    sqlite3* m_sqliteDb = nullptr;
    PGconn* m_pgConn = nullptr;

    // Rules
    mutable std::mutex m_rulesMutex;
    std::map<std::string, AlarmRule> m_rules;

    // Active alarms
    mutable std::mutex m_alarmsMutex;
    std::map<std::string, Alarm> m_activeAlarms;

    // Evaluation queue
    std::mutex m_evalMutex;
    std::condition_variable m_evalCv;
    std::queue<std::tuple<std::string, double, std::string>> m_evalQueue;
    std::thread m_evalThread;

    // Suppression
    mutable std::mutex m_suppressMutex;
    std::map<std::string, SuppressionSchedule> m_suppressionSchedules;

    // Notifications
    std::mutex m_notifMutex;
    std::vector<AlarmNotification> m_notifications;
    std::map<AlarmNotificationType, std::function<void(const Alarm&, const AlarmNotification&)>> m_notificationCallbacks;
    std::thread m_notifThread;
    std::atomic<bool> m_notifRunning{false};

    // Callbacks
    std::function<void(const Alarm&)> m_onAlarmTriggered;
    std::function<void(const Alarm&)> m_onAlarmAcknowledged;
    std::function<void(const Alarm&)> m_onAlarmCleared;

    // ID counter
    std::atomic<uint64_t> m_alarmIdCounter{1};
};

} // namespace powsys365
