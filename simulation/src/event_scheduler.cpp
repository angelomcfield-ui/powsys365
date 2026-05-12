#include "powsy365/simulation/event_scheduler.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <chrono>

namespace powsys365 {

// ============================================================================
// String helpers
// ============================================================================
std::string eventTypeToString(SimulationEventType type) {
    switch (type) {
        case SimulationEventType::FAULT: return "FAULT";
        case SimulationEventType::FAULT_CLEAR: return "FAULT_CLEAR";
        case SimulationEventType::BREAKER_OPEN: return "BREAKER_OPEN";
        case SimulationEventType::BREAKER_CLOSE: return "BREAKER_CLOSE";
        case SimulationEventType::LOAD_CHANGE: return "LOAD_CHANGE";
        case SimulationEventType::GENERATOR_TRIP: return "GENERATOR_TRIP";
        case SimulationEventType::GENERATOR_RESTORE: return "GENERATOR_RESTORE";
        case SimulationEventType::LINE_TRIP: return "LINE_TRIP";
        case SimulationEventType::LINE_RESTORE: return "LINE_RESTORE";
        case SimulationEventType::TRANSFORMER_TAP_CHANGE: return "TRANSFORMER_TAP_CHANGE";
        case SimulationEventType::SHUNT_SWITCHING: return "SHUNT_SWITCHING";
        case SimulationEventType::MOTOR_STARTING: return "MOTOR_STARTING";
        case SimulationEventType::SETPOINT_CHANGE: return "SETPOINT_CHANGE";
        case SimulationEventType::CUSTOM: return "CUSTOM";
        default: return "UNKNOWN";
    }
}

// ============================================================================
// EventScheduler
// ============================================================================
EventScheduler::EventScheduler() {
    m_stats.startTime = std::chrono::steady_clock::now();
}

EventScheduler::~EventScheduler() {
    stop();
}

void EventScheduler::start() {
    if (m_running.load()) return;
    m_running.store(true);
    m_schedulerThread = std::thread(&EventScheduler::schedulerLoop, this);
}

void EventScheduler::stop() {
    m_running.store(false);
    m_queueCv.notify_all();
    if (m_schedulerThread.joinable()) {
        m_schedulerThread.join();
    }
}

bool EventScheduler::isRunning() const {
    return m_running.load();
}

// ---------------------------------------------------------------------------
// Event scheduling
// ---------------------------------------------------------------------------
uint64_t EventScheduler::scheduleEvent(const SimulationEvent& event) {
    uint64_t eventId = generateEventId();

    SimulationEvent evt = event;
    evt.eventId = eventId;
    evt.scheduledTime = std::chrono::steady_clock::now();
    if (evt.simulationTimeSec <= 0.0) {
        evt.simulationTimeSec = m_currentSimulationTime.load();
    }

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_eventQueue.push(evt);
    }
    {
        std::lock_guard<std::mutex> lock(m_registryMutex);
        m_eventRegistry[eventId] = evt;
    }
    {
        std::lock_guard<std::mutex> lock(m_statsMutex);
        m_stats.totalEventsScheduled++;
        m_stats.eventsByType[evt.type]++;
    }

    m_queueCv.notify_one();
    return eventId;
}

uint64_t EventScheduler::scheduleEvent(double simulationTimeSec, SimulationEventType type,
                                         const std::string& targetElementId,
                                         const std::string& name,
                                         const std::string& description,
                                         const std::map<std::string, double>& params,
                                         int priority) {
    SimulationEvent event;
    event.type = type;
    event.simulationTimeSec = simulationTimeSec;
    event.targetElementId = targetElementId;
    event.name = name;
    event.description = description;
    event.parameters = params;
    event.priority = priority;
    return scheduleEvent(event);
}

uint64_t EventScheduler::schedulePeriodicEvent(double startTimeSec, double periodSec,
                                                  SimulationEventType type,
                                                  const std::string& targetElementId,
                                                  const std::string& name,
                                                  int maxRepetitions,
                                                  int priority) {
    SimulationEvent event;
    event.type = type;
    event.simulationTimeSec = startTimeSec;
    event.targetElementId = targetElementId;
    event.name = name;
    event.periodic = true;
    event.periodSec = periodSec;
    event.maxRepetitions = maxRepetitions;
    event.repetitionCount = 0;
    event.priority = priority;

    uint64_t id = scheduleEvent(event);

    std::lock_guard<std::mutex> lock(m_statsMutex);
    m_stats.totalPeriodicEvents++;

    return id;
}

uint64_t EventScheduler::scheduleRelativeEvent(double offsetSec, SimulationEventType type,
                                                  const std::string& targetElementId,
                                                  const std::string& name,
                                                  const std::map<std::string, double>& params,
                                                  int priority) {
    double simTime = m_currentSimulationTime.load() + offsetSec;
    return scheduleEvent(simTime, type, targetElementId, name, "", params, priority);
}

// ---------------------------------------------------------------------------
// Event management
// ---------------------------------------------------------------------------
bool EventScheduler::cancelEvent(uint64_t eventId) {
    std::lock_guard<std::mutex> lock(m_registryMutex);
    auto it = m_eventRegistry.find(eventId);
    if (it != m_eventRegistry.end()) {
        it->second.cancelled = true;
        std::lock_guard<std::mutex> statsLock(m_statsMutex);
        m_stats.totalEventsCancelled++;
        return true;
    }
    return false;
}

bool EventScheduler::rescheduleEvent(uint64_t eventId, double newSimulationTimeSec) {
    std::lock_guard<std::mutex> lock(m_registryMutex);
    auto it = m_eventRegistry.find(eventId);
    if (it != m_eventRegistry.end() && !it->second.executed && !it->second.cancelled) {
        it->second.simulationTimeSec = newSimulationTimeSec;
        return true;
    }
    return false;
}

std::optional<SimulationEvent> EventScheduler::getEvent(uint64_t eventId) const {
    std::lock_guard<std::mutex> lock(m_registryMutex);
    auto it = m_eventRegistry.find(eventId);
    if (it != m_eventRegistry.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<SimulationEvent> EventScheduler::getPendingEvents() const {
    std::lock_guard<std::mutex> lock(m_registryMutex);
    std::vector<SimulationEvent> result;
    for (const auto& [id, event] : m_eventRegistry) {
        (void)id;
        if (!event.executed && !event.cancelled) {
            result.push_back(event);
        }
    }
    return result;
}

std::vector<SimulationEvent> EventScheduler::getExecutedEvents() const {
    std::lock_guard<std::mutex> lock(m_historyMutex);
    return m_executedEvents;
}

std::vector<SimulationEvent> EventScheduler::getFilteredEvents(const EventFilter& filter) const {
    std::vector<SimulationEvent> result;

    std::lock_guard<std::mutex> lock(m_registryMutex);
    for (const auto& [id, event] : m_eventRegistry) {
        (void)id;
        // Filter by type
        if (!filter.types.empty() && filter.types.find(event.type) == filter.types.end()) {
            continue;
        }
        // Filter by target element
        if (!filter.targetElements.empty() &&
            filter.targetElements.find(event.targetElementId) == filter.targetElements.end()) {
            continue;
        }
        // Filter by time range
        if (event.simulationTimeSec < filter.timeRangeStartSec) continue;
        if (filter.timeRangeEndSec >= 0 && event.simulationTimeSec > filter.timeRangeEndSec) continue;
        // Filter by execution state
        if (event.executed && !filter.includeExecuted) continue;
        if (!event.executed && !event.cancelled && !filter.includePending) continue;
        if (event.cancelled && !filter.includeCancelled) continue;

        result.push_back(event);
    }

    return result;
}

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------
void EventScheduler::setEventCallback(SimulationEventType type, EventCallback callback) {
    std::lock_guard<std::mutex> lock(m_callbacksMutex);
    m_callbacks[type] = callback;
}

void EventScheduler::setGlobalCallback(EventCallback callback) {
    std::lock_guard<std::mutex> lock(m_callbacksMutex);
    m_globalCallback = callback;
}

void EventScheduler::setPrecondition(SimulationEventType type, EventPrecondition precondition) {
    std::lock_guard<std::mutex> lock(m_callbacksMutex);
    m_preconditions[type] = precondition;
}

void EventScheduler::removeCallback(SimulationEventType type) {
    std::lock_guard<std::mutex> lock(m_callbacksMutex);
    m_callbacks.erase(type);
    m_preconditions.erase(type);
}

// ---------------------------------------------------------------------------
// Clock sync
// ---------------------------------------------------------------------------
void EventScheduler::setSimulationTime(double currentSimTimeSec) {
    m_currentSimulationTime.store(currentSimTimeSec);
}

double EventScheduler::getSimulationTime() const {
    return m_currentSimulationTime.load();
}

void EventScheduler::setRealTimeMode(bool realTime) {
    m_realTimeMode.store(realTime);
}

bool EventScheduler::isRealTimeMode() const {
    return m_realTimeMode.load();
}

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------
EventSchedulerStats EventScheduler::getStats() const {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    EventSchedulerStats stats = m_stats;
    stats.eventsInQueue = m_eventQueue.size();
    return stats;
}

void EventScheduler::resetStats() {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    m_stats = EventSchedulerStats{};
    m_stats.startTime = std::chrono::steady_clock::now();
}

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------
void EventScheduler::clearAllEvents() {
    std::lock_guard<std::mutex> qLock(m_queueMutex);
    while (!m_eventQueue.empty()) m_eventQueue.pop();

    std::lock_guard<std::mutex> rLock(m_registryMutex);
    m_eventRegistry.clear();

    std::lock_guard<std::mutex> hLock(m_historyMutex);
    m_executedEvents.clear();
}

void EventScheduler::clearExecutedEvents() {
    std::lock_guard<std::mutex> lock(m_historyMutex);
    m_executedEvents.clear();
}

size_t EventScheduler::getQueueSize() const {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    return m_eventQueue.size();
}

// ---------------------------------------------------------------------------
// Random event generation
// ---------------------------------------------------------------------------
uint64_t EventScheduler::scheduleRandomEvent(double timeMinSec, double timeMaxSec,
                                                SimulationEventType type,
                                                const std::string& targetElementId,
                                                const std::string& name) {
    std::uniform_real_distribution<double> dist(timeMinSec, timeMaxSec);
    double randomTime = dist(m_randomGen);
    return scheduleEvent(randomTime, type, targetElementId, name, "Random event");
}

// ---------------------------------------------------------------------------
// Private methods
// ---------------------------------------------------------------------------
void EventScheduler::schedulerLoop() {
    while (m_running.load()) {
        std::unique_lock<std::mutex> lock(m_queueMutex);
        m_queueCv.wait_for(lock, std::chrono::milliseconds(10), [this] {
            return !m_eventQueue.empty() || !m_running.load();
        });

        if (!m_running.load()) break;

        // Check if top event should be triggered
        while (!m_eventQueue.empty()) {
            SimulationEvent event = m_eventQueue.top();

            double simTime = m_currentSimulationTime.load();
            if (event.simulationTimeSec > simTime) break;

            m_eventQueue.pop();
            lock.unlock();

            if (!event.cancelled && !event.executed) {
                // Check preconditions
                bool shouldExecute = true;
                {
                    std::lock_guard<std::mutex> cbLock(m_callbacksMutex);
                    auto preIt = m_preconditions.find(event.type);
                    if (preIt != m_preconditions.end()) {
                        shouldExecute = preIt->second(event);
                    }
                }

                if (shouldExecute) {
                    event.executed = true;
                    event.triggerTime = std::chrono::steady_clock::now();
                    executeEvent(event);
                }

                // Update registry
                {
                    std::lock_guard<std::mutex> rLock(m_registryMutex);
                    m_eventRegistry[event.eventId] = event;
                }

                // Record in history
                {
                    std::lock_guard<std::mutex> hLock(m_historyMutex);
                    m_executedEvents.push_back(event);
                }

                // Update stats
                {
                    std::lock_guard<std::mutex> sLock(m_statsMutex);
                    m_stats.totalEventsExecuted++;
                }

                // Handle periodic events
                if (event.periodic) {
                    handlePeriodicEvent(event);
                }
            }

            lock.lock();
        }
    }
}

void EventScheduler::executeEvent(const SimulationEvent& event) {
    // Call type-specific callback
    {
        std::lock_guard<std::mutex> lock(m_callbacksMutex);
        auto it = m_callbacks.find(event.type);
        if (it != m_callbacks.end() && it->second) {
            it->second(event);
        }
        // Call global callback
        if (m_globalCallback) {
            m_globalCallback(event);
        }
    }
}

uint64_t EventScheduler::generateEventId() {
    return m_nextEventId.fetch_add(1);
}

void EventScheduler::handlePeriodicEvent(const SimulationEvent& parent) {
    if (parent.maxRepetitions > 0 && parent.repetitionCount >= parent.maxRepetitions) {
        return;
    }

    SimulationEvent next = parent;
    next.eventId = 0;
    next.parentEventId = parent.eventId;
    next.simulationTimeSec = parent.simulationTimeSec + parent.periodSec;
    next.executed = false;
    next.cancelled = false;
    next.repetitionCount = parent.repetitionCount + 1;
    next.triggerTime = {};

    scheduleEvent(next);
}

} // namespace powsys365
