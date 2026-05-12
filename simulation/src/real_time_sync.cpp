#include "powsy365/simulation/real_time_sync.h"
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>

namespace powsys365 {

// ============================================================================
// PlaybackState string helper
// ============================================================================
std::string playbackStateToString(PlaybackState state) {
    switch (state) {
        case PlaybackState::STOPPED: return "STOPPED";
        case PlaybackState::PLAYING: return "PLAYING";
        case PlaybackState::PAUSED: return "PAUSED";
        case PlaybackState::STEPPING: return "STEPPING";
        case PlaybackState::REWINDING: return "REWINDING";
        default: return "UNKNOWN";
    }
}

// ============================================================================
// RealTimeSync
// ============================================================================
RealTimeSync::RealTimeSync() {
    m_wallClockStart = std::chrono::steady_clock::now();
    m_lastSyncTime = m_wallClockStart;
    m_stats.startWallClockTime = std::chrono::system_clock::now();
}

RealTimeSync::~RealTimeSync() {
    shutdown();
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
void RealTimeSync::setConfig(const RealTimeSyncConfig& config) {
    m_config = config;
    m_speedFactor.store(config.speedFactor);
}

RealTimeSyncConfig RealTimeSync::getConfig() const {
    return m_config;
}

// ---------------------------------------------------------------------------
// Playback control
// ---------------------------------------------------------------------------
void RealTimeSync::play() {
    if (m_state.load() == PlaybackState::PAUSED) {
        // Account for paused time
        auto now = std::chrono::steady_clock::now();
        m_totalPausedSec += std::chrono::duration<double>(now - m_pauseStartTime).count();
    }
    m_state.store(PlaybackState::PLAYING);
    m_wallClockStart = std::chrono::steady_clock::now();
}

void RealTimeSync::pause() {
    if (m_state.load() == PlaybackState::PLAYING) {
        m_state.store(PlaybackState::PAUSED);
        m_pauseStartTime = std::chrono::steady_clock::now();
    }
}

void RealTimeSync::step(int numSteps) {
    m_pendingSteps.store(numSteps);
    m_state.store(PlaybackState::STEPPING);
}

void RealTimeSync::stop() {
    m_state.store(PlaybackState::STOPPED);
    m_simulationTime.store(0.0);
}

void RealTimeSync::rewind(double targetTimeSec) {
    m_rewindTarget.store(targetTimeSec);
    m_state.store(PlaybackState::REWINDING);
}

void RealTimeSync::setSpeedFactor(double factor) {
    m_speedFactor.store(std::clamp(factor, 0.01, 100.0));
}

PlaybackState RealTimeSync::getState() const {
    return m_state.load();
}

// ---------------------------------------------------------------------------
// Time queries
// ---------------------------------------------------------------------------
double RealTimeSync::getSimulationTime() const {
    return m_simulationTime.load();
}

double RealTimeSync::getWallClockTime() const {
    auto elapsed = std::chrono::steady_clock::now() - m_wallClockStart;
    return std::chrono::duration<double>(elapsed).count();
}

double RealTimeSync::getDriftMs() const {
    return m_currentDriftMs;
}

// ---------------------------------------------------------------------------
// Snapshot / Restore
// ---------------------------------------------------------------------------
std::string RealTimeSync::takeSnapshot(const std::string& name, const std::string& description) {
    std::lock_guard<std::mutex> lock(m_snapshotsMutex);

    // Check max snapshots
    if (m_snapshots.size() >= static_cast<size_t>(m_config.maxSnapshots)) {
        // Remove oldest snapshot
        m_snapshots.erase(m_snapshots.begin());
    }

    SimulationSnapshot snapshot;
    snapshot.snapshotId = generateSnapshotId();
    snapshot.name = name;
    snapshot.description = description;
    snapshot.simulationTimeSec = m_simulationTime.load();
    snapshot.wallClockTime = std::chrono::system_clock::now();

    // Capture state via callback
    if (m_stateCaptureCb) {
        snapshot.serializedData = m_stateCaptureCb();
    }
    if (m_stateVectorCb) {
        snapshot.stateVector = m_stateVectorCb();
    }

    m_snapshots.push_back(snapshot);
    return snapshot.snapshotId;
}

bool RealTimeSync::restoreSnapshot(const std::string& snapshotId) {
    std::lock_guard<std::mutex> lock(m_snapshotsMutex);

    for (const auto& snap : m_snapshots) {
        if (snap.snapshotId == snapshotId) {
            m_simulationTime.store(snap.simulationTimeSec);

            if (m_stateRestoreCb && !snap.serializedData.empty()) {
                m_stateRestoreCb(snap.serializedData);
            }
            if (m_restoreVectorCb && !snap.stateVector.empty()) {
                m_restoreVectorCb(snap.stateVector);
            }

            return true;
        }
    }
    return false;
}

bool RealTimeSync::deleteSnapshot(const std::string& snapshotId) {
    std::lock_guard<std::mutex> lock(m_snapshotsMutex);
    auto it = std::remove_if(m_snapshots.begin(), m_snapshots.end(),
        [&snapshotId](const SimulationSnapshot& s) { return s.snapshotId == snapshotId; });
    if (it != m_snapshots.end()) {
        m_snapshots.erase(it, m_snapshots.end());
        return true;
    }
    return false;
}

std::vector<SimulationSnapshot> RealTimeSync::getSnapshots() const {
    std::lock_guard<std::mutex> lock(m_snapshotsMutex);
    return m_snapshots;
}

std::optional<SimulationSnapshot> RealTimeSync::getSnapshot(const std::string& snapshotId) const {
    std::lock_guard<std::mutex> lock(m_snapshotsMutex);
    for (const auto& snap : m_snapshots) {
        if (snap.snapshotId == snapshotId) {
            return snap;
        }
    }
    return std::nullopt;
}

void RealTimeSync::clearAllSnapshots() {
    std::lock_guard<std::mutex> lock(m_snapshotsMutex);
    m_snapshots.clear();
}

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------
void RealTimeSync::setStateCaptureCallback(std::function<std::vector<uint8_t>()> callback) {
    m_stateCaptureCb = callback;
}

void RealTimeSync::setStateRestoreCallback(std::function<void(const std::vector<uint8_t>&)> callback) {
    m_stateRestoreCb = callback;
}

void RealTimeSync::setStepCallback(std::function<void(double dt)> callback) {
    m_stepCb = callback;
}

void RealTimeSync::setTimeCallback(std::function<double()> callback) {
    m_timeCb = callback;
}

void RealTimeSync::setStateVectorCallback(std::function<std::vector<double>()> callback) {
    m_stateVectorCb = callback;
}

void RealTimeSync::setRestoreVectorCallback(std::function<void(const std::vector<double>&)> callback) {
    m_restoreVectorCb = callback;
}

// ---------------------------------------------------------------------------
// Synchronization
// ---------------------------------------------------------------------------
void RealTimeSync::synchronize() {
    calculateDrift();
    adjustTiming();
}

double RealTimeSync::getAdjustedStepSize() const {
    double speed = m_speedFactor.load();
    return m_config.stepSizeSec * speed;
}

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------
RealTimeSyncStats RealTimeSync::getStats() const {
    RealTimeSyncStats stats = m_stats;
    stats.currentSimulationTime = m_simulationTime.load();
    stats.driftMs = m_currentDriftMs;
    stats.effectiveSpeedFactor = m_speedFactor.load();
    stats.totalPausedTimeSec = m_totalPausedSec;
    return stats;
}

void RealTimeSync::resetStats() {
    m_stats = RealTimeSyncStats{};
    m_stats.startWallClockTime = std::chrono::system_clock::now();
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------
void RealTimeSync::start() {
    if (m_running.load()) return;
    m_running.store(true);
    m_state.store(PlaybackState::PLAYING);
    m_syncThread = std::thread(&RealTimeSync::syncLoop, this);
}

void RealTimeSync::shutdown() {
    m_running.store(false);
    if (m_syncThread.joinable()) {
        m_syncThread.join();
    }
    m_state.store(PlaybackState::STOPPED);
}

bool RealTimeSync::isRunning() const {
    return m_running.load();
}

// ---------------------------------------------------------------------------
// Private methods
// ---------------------------------------------------------------------------
void RealTimeSync::syncLoop() {
    m_wallClockStart = std::chrono::steady_clock::now();
    m_lastSyncTime = m_wallClockStart;

    while (m_running.load()) {
        auto loopStart = std::chrono::steady_clock::now();

        switch (m_state.load()) {
            case PlaybackState::PLAYING: {
                performStep();
                synchronize();
                break;
            }
            case PlaybackState::PAUSED: {
                doPause();
                break;
            }
            case PlaybackState::STEPPING: {
                int steps = m_pendingSteps.exchange(0);
                for (int i = 0; i < steps; ++i) {
                    performStep();
                }
                m_state.store(PlaybackState::PAUSED);
                break;
            }
            case PlaybackState::REWINDING: {
                doRewind();
                m_state.store(PlaybackState::PAUSED);
                break;
            }
            case PlaybackState::STOPPED: {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                break;
            }
        }

        // Control loop frequency
        auto loopEnd = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(loopEnd - loopStart).count();
        int targetIntervalMs = m_config.syncIntervalMs;
        if (elapsed < targetIntervalMs) {
            std::this_thread::sleep_for(std::chrono::milliseconds(targetIntervalMs - static_cast<int>(elapsed)));
        }
    }
}

void RealTimeSync::performStep() {
    double dt = getAdjustedStepSize();
    double currentSimTime = m_simulationTime.load();

    auto stepStart = std::chrono::steady_clock::now();

    if (m_stepCb) {
        m_stepCb(dt);
    }

    // Update simulation time
    double newTime = currentSimTime + dt;
    if (m_timeCb) {
        newTime = m_timeCb();
    }
    m_simulationTime.store(newTime);

    auto stepEnd = std::chrono::steady_clock::now();
    double stepMs = std::chrono::duration<double, std::milli>(stepEnd - stepStart).count();

    m_stats.totalSteps++;
    m_stats.averageStepTimeMs = (m_stats.averageStepTimeMs * (m_stats.totalSteps - 1) + stepMs) / m_stats.totalSteps;
}

void RealTimeSync::doPause() {
    // Just wait while paused
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

void RealTimeSync::doRewind() {
    double target = m_rewindTarget.load();
    m_simulationTime.store(target);

    // Find nearest snapshot and restore
    std::lock_guard<std::mutex> lock(m_snapshotsMutex);
    const SimulationSnapshot* bestSnap = nullptr;
    double bestDiff = std::numeric_limits<double>::max();

    for (const auto& snap : m_snapshots) {
        if (snap.simulationTimeSec <= target) {
            double diff = target - snap.simulationTimeSec;
            if (diff < bestDiff) {
                bestDiff = diff;
                bestSnap = &snap;
            }
        }
    }

    if (bestSnap) {
        if (m_stateRestoreCb && !bestSnap->serializedData.empty()) {
            m_stateRestoreCb(bestSnap->serializedData);
        }
        if (m_restoreVectorCb && !bestSnap->stateVector.empty()) {
            m_restoreVectorCb(bestSnap->stateVector);
        }
    }
}

void RealTimeSync::calculateDrift() {
    double speed = m_speedFactor.load();
    double expectedWallTime = m_simulationTime.load() / speed;
    double actualWallTime = getWallClockTime() - m_totalPausedSec;

    m_currentDriftMs = (actualWallTime - expectedWallTime) * 1000.0;

    // Update stats
    m_stats.driftMs = m_currentDriftMs;
    m_stats.maxDriftMs = std::max(m_stats.maxDriftMs, std::abs(m_currentDriftMs));

    // Running average
    static double driftAccum = 0;
    static uint64_t driftCount = 0;
    driftAccum += std::abs(m_currentDriftMs);
    driftCount++;
    if (driftCount % 100 == 0) {
        m_stats.averageDriftMs = driftAccum / driftCount;
    }
}

void RealTimeSync::adjustTiming() {
    if (!m_config.adaptiveSync) return;

    double driftMs = m_currentDriftMs;
    double maxDrift = m_config.maxDriftMs;

    if (std::abs(driftMs) > maxDrift) {
        // Adjust speed factor to compensate
        double adjustment = 1.0 + (driftMs > 0 ? -0.05 : 0.05);
        double newSpeed = m_speedFactor.load() * adjustment;
        m_speedFactor.store(std::clamp(newSpeed, 0.1, 10.0));
        m_stats.syncAdjustments++;
    }
}

std::string RealTimeSync::generateSnapshotId() {
    uint64_t id = m_snapshotIdCounter.fetch_add(1);
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    std::ostringstream oss;
    oss << "SNAP-" << ms << "-" << id;
    return oss.str();
}

} // namespace powsys365
