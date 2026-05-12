#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace powsys365 {

// ---------------------------------------------------------------------------
// Playback state
// ---------------------------------------------------------------------------
enum class PlaybackState {
    STOPPED,
    PLAYING,
    PAUSED,
    STEPPING,
    REWINDING
};

std::string playbackStateToString(PlaybackState state);

// ---------------------------------------------------------------------------
// Snapshot of simulation state
// ---------------------------------------------------------------------------
struct SimulationSnapshot {
    std::string snapshotId;
    std::string name;
    std::string description;
    double simulationTimeSec = 0.0;
    std::chrono::system_clock::time_point wallClockTime;
    std::vector<double> stateVector;
    std::vector<double> algebraicVector;
    std::vector<uint8_t> serializedData; // opaque serialized state
    std::map<std::string, double> parameters;
};

// ---------------------------------------------------------------------------
// Sync configuration
// ---------------------------------------------------------------------------
struct RealTimeSyncConfig {
    double speedFactor = 1.0;       // 1.0 = real-time, 2.0 = 2x faster, 0.5 = half speed
    double stepSizeSec = 0.001;     // simulation step size
    bool adaptiveSync = true;       // adjust to maintain sync
    int syncIntervalMs = 10;        // how often to sync (ms)
    double maxDriftMs = 5.0;        // max allowed drift before correction
    bool enableSnapshots = true;    // enable snapshot/restore
    int maxSnapshots = 50;          // max number of snapshots to keep
};

// ---------------------------------------------------------------------------
// Sync statistics
// ---------------------------------------------------------------------------
struct RealTimeSyncStats {
    double currentSimulationTime = 0.0;
    double currentWallClockTime = 0.0;
    double driftMs = 0.0;
    double averageDriftMs = 0.0;
    double maxDriftMs = 0.0;
    uint64_t totalSteps = 0;
    uint64_t syncAdjustments = 0;
    double effectiveSpeedFactor = 1.0;
    double averageStepTimeMs = 0.0;
    std::chrono::system_clock::time_point startWallClockTime;
    double totalPausedTimeSec = 0.0;
};

// ---------------------------------------------------------------------------
// Real-Time Synchronizer
// ---------------------------------------------------------------------------
class RealTimeSync {
public:
    RealTimeSync();
    ~RealTimeSync();

    // Configuration
    void setConfig(const RealTimeSyncConfig& config);
    RealTimeSyncConfig getConfig() const;

    // Playback control
    void play();
    void pause();
    void step(int numSteps = 1);
    void stop();
    void rewind(double targetTimeSec);
    void setSpeedFactor(double factor);
    PlaybackState getState() const;

    // Time queries
    double getSimulationTime() const;
    double getWallClockTime() const;
    double getDriftMs() const;

    // Snapshot / Restore
    std::string takeSnapshot(const std::string& name, const std::string& description = "");
    bool restoreSnapshot(const std::string& snapshotId);
    bool deleteSnapshot(const std::string& snapshotId);
    std::vector<SimulationSnapshot> getSnapshots() const;
    std::optional<SimulationSnapshot> getSnapshot(const std::string& snapshotId) const;
    void clearAllSnapshots();

    // State capture/restore callbacks (for integration with simulator)
    void setStateCaptureCallback(std::function<std::vector<uint8_t>()> callback);
    void setStateRestoreCallback(std::function<void(const std::vector<uint8_t>&)> callback);
    void setStepCallback(std::function<void(double dt)> callback);
    void setTimeCallback(std::function<double()> callback);
    void setStateVectorCallback(std::function<std::vector<double>()> callback);
    void setRestoreVectorCallback(std::function<void(const std::vector<double>&)> callback);

    // Synchronization
    void synchronize();
    double getAdjustedStepSize() const;

    // Statistics
    RealTimeSyncStats getStats() const;
    void resetStats();

    // Main loop (runs in own thread when started)
    void start();
    void shutdown();
    bool isRunning() const;

private:
    void syncLoop();
    void performStep();
    void doPause();
    void doRewind();
    void calculateDrift();
    void adjustTiming();
    std::string generateSnapshotId();

    RealTimeSyncConfig m_config;
    RealTimeSyncStats m_stats;

    // Playback state
    std::atomic<PlaybackState> m_state{PlaybackState::STOPPED};
    std::atomic<double> m_simulationTime{0.0};
    std::atomic<double> m_speedFactor{1.0};

    // Step control
    std::atomic<int> m_pendingSteps{0};
    std::atomic<double> m_rewindTarget{0.0};

    // Snapshots
    mutable std::mutex m_snapshotsMutex;
    std::vector<SimulationSnapshot> m_snapshots;

    // Timing
    std::chrono::steady_clock::time_point m_wallClockStart;
    std::chrono::steady_clock::time_point m_lastSyncTime;
    std::chrono::steady_clock::time_point m_pauseStartTime;
    double m_totalPausedSec = 0.0;
    double m_currentDriftMs = 0.0;

    // Callbacks
    std::function<std::vector<uint8_t>()> m_stateCaptureCb;
    std::function<void(const std::vector<uint8_t>&)> m_stateRestoreCb;
    std::function<void(double dt)> m_stepCb;
    std::function<double()> m_timeCb;
    std::function<std::vector<double>()> m_stateVectorCb;
    std::function<void(const std::vector<double>&)> m_restoreVectorCb;

    // Threading
    std::atomic<bool> m_running{false};
    std::thread m_syncThread;

    // ID counter
    std::atomic<uint64_t> m_snapshotIdCounter{1};
};

} // namespace powsys365
