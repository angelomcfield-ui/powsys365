#include "event_queue.h"

#include <sstream>
#include <algorithm>
#include <cmath>
#include <chrono>

namespace powsys365::simulation {

// ============================================================================
// Event Implementation
// ============================================================================

Event::Event(double t, EventType tp, std::string d, EventPriority p)
    : time(t), type(tp), data(std::move(d)), priority(p) {}

Event::Event(double t, EventType tp, std::string d, EventPriority p,
             std::string src, uint32_t srcId)
    : time(t), type(tp), data(std::move(d)), priority(p),
      source(std::move(src)), sourceId(srcId) {}

bool Event::operator>(const Event& other) const {
    // Lower priority value = higher priority
    if (static_cast<uint32_t>(priority) != static_cast<uint32_t>(other.priority)) {
        return static_cast<uint32_t>(priority) > static_cast<uint32_t>(other.priority);
    }
    // Earlier time = higher priority
    if (time != other.time) {
        return time > other.time;
    }
    // Lower sequence = higher priority (FIFO for same time/priority)
    return sequence > other.sequence;
}

bool Event::operator<(const Event& other) const {
    if (static_cast<uint32_t>(priority) != static_cast<uint32_t>(other.priority)) {
        return static_cast<uint32_t>(priority) < static_cast<uint32_t>(other.priority);
    }
    if (time != other.time) {
        return time < other.time;
    }
    return sequence < other.sequence;
}

bool Event::operator==(const Event& other) const {
    return time == other.time &&
           type == other.type &&
           priority == other.priority &&
           sequence == other.sequence &&
           data == other.data &&
           source == other.source &&
           sourceId == other.sourceId;
}

const char* Event::typeToString(EventType t) {
    switch (t) {
        case EventType::SetValue:         return "SetValue";
        case EventType::GetValue:         return "GetValue";
        case EventType::StepSizeChange:   return "StepSizeChange";
        case EventType::StepRequest:      return "StepRequest";
        case EventType::Terminate:        return "Terminate";
        case EventType::Reset:            return "Reset";
        case EventType::ConnectionUpdate: return "ConnectionUpdate";
        case EventType::ParameterUpdate:  return "ParameterUpdate";
        case EventType::StateEvent:       return "StateEvent";
        case EventType::TimeEvent:        return "TimeEvent";
        case EventType::InputEvent:       return "InputEvent";
        case EventType::OutputEvent:      return "OutputEvent";
        case EventType::Custom:           return "Custom";
        default:                          return "Unknown";
    }
}

const char* Event::priorityToString(EventPriority p) {
    switch (p) {
        case EventPriority::Critical:    return "Critical";
        case EventPriority::High:        return "High";
        case EventPriority::Normal:      return "Normal";
        case EventPriority::Low:         return "Low";
        case EventPriority::Background:  return "Background";
        default:                         return "Unknown";
    }
}

std::string Event::toString() const {
    std::ostringstream oss;
    oss << "Event{time=" << time
        << ", type=" << typeToString(type)
        << ", priority=" << priorityToString(priority)
        << ", seq=" << sequence;
    if (!data.empty()) {
        oss << ", data='" << data << "'";
    }
    if (!source.empty()) {
        oss << ", source='" << source << "'";
    }
    oss << "}";
    return oss.str();
}

// ============================================================================
// EventQueue Implementation
// ============================================================================

EventQueue::EventQueue() = default;

EventQueue::EventQueue(size_t maxSize) : m_maxSize(maxSize) {}

EventQueue::EventQueue(EventQueue&& other) noexcept {
    std::lock_guard<std::mutex> lock(other.m_mutex);
    m_queue = std::move(other.m_queue);
    m_maxSize = other.m_maxSize;
    m_blocking = other.m_blocking;
    m_enabled = other.m_enabled;
    m_sequence = other.m_sequence;
    m_totalPushed = other.m_totalPushed;
    m_totalPopped = other.m_totalPopped;
    m_totalDropped = other.m_totalDropped;
    m_totalWaitTime = other.m_totalWaitTime;
    m_pushCallback = std::move(other.m_pushCallback);
    m_popCallback = std::move(other.m_popCallback);
    m_dropCallback = std::move(other.m_dropCallback);
}

EventQueue& EventQueue::operator=(EventQueue&& other) noexcept {
    if (this != &other) {
        std::lock_guard<std::mutex> lock1(m_mutex);
        std::lock_guard<std::mutex> lock2(other.m_mutex);
        m_queue = std::move(other.m_queue);
        m_maxSize = other.m_maxSize;
        m_blocking = other.m_blocking;
        m_enabled = other.m_enabled;
        m_sequence = other.m_sequence;
        m_totalPushed = other.m_totalPushed;
        m_totalPopped = other.m_totalPopped;
        m_totalDropped = other.m_totalDropped;
        m_totalWaitTime = other.m_totalWaitTime;
        m_pushCallback = std::move(other.m_pushCallback);
        m_popCallback = std::move(other.m_popCallback);
        m_dropCallback = std::move(other.m_dropCallback);
    }
    return *this;
}

// ---------------------------------------------------------------------------
// Core Operations
// ---------------------------------------------------------------------------

bool EventQueue::push(const Event& event) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_enabled) return false;

    if (m_queue.size() >= m_maxSize) {
        m_totalDropped++;
        if (m_dropCallback) m_dropCallback(event);
        return false;
    }

    Event e = event;
    e.sequence = m_sequence++;

    auto pushStart = std::chrono::steady_clock::now();
    m_queue.push(e);
    auto pushEnd = std::chrono::steady_clock::now();
    m_totalWaitTime += std::chrono::duration<double>(pushEnd - pushStart).count();

    m_totalPushed++;

    if (m_pushCallback) m_pushCallback(e);

    m_condVar.notify_one();
    return true;
}

bool EventQueue::push(Event&& event) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_enabled) return false;

    if (m_queue.size() >= m_maxSize) {
        m_totalDropped++;
        if (m_dropCallback) m_dropCallback(event);
        return false;
    }

    event.sequence = m_sequence++;

    auto pushStart = std::chrono::steady_clock::now();
    m_queue.push(std::move(event));
    auto pushEnd = std::chrono::steady_clock::now();
    m_totalWaitTime += std::chrono::duration<double>(pushEnd - pushStart).count();

    m_totalPushed++;

    if (m_pushCallback) m_pushCallback(m_queue.top());

    m_condVar.notify_one();
    return true;
}

bool EventQueue::push(double time, EventType type, const std::string& data,
                       EventPriority priority) {
    return push(Event(time, type, data, priority));
}

Event EventQueue::pop() {
    std::unique_lock<std::mutex> lock(m_mutex);

    if (m_blocking) {
        m_condVar.wait(lock, [this] { return !m_queue.empty() || !m_enabled; });
    }

    if (m_queue.empty()) {
        return Event(); // Return empty event
    }

    return popUnsafe();
}

bool EventQueue::tryPop(Event& event) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_queue.empty()) {
        return false;
    }

    event = popUnsafe();
    return true;
}

bool EventQueue::tryPop(Event& event, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(m_mutex);

    bool hasEvent = m_condVar.wait_for(lock, timeout,
        [this] { return !m_queue.empty() || !m_enabled; });

    if (!hasEvent || m_queue.empty()) {
        return false;
    }

    event = popUnsafe();
    return true;
}

Event EventQueue::peek() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_queue.empty()) {
        return Event();
    }

    return m_queue.top();
}

Event EventQueue::popUnsafe() {
    Event event = m_queue.top();
    m_queue.pop();
    m_totalPopped++;

    if (m_popCallback) m_popCallback(event);

    return event;
}

bool EventQueue::empty() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_queue.empty();
}

size_t EventQueue::size() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_queue.size();
}

void EventQueue::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Clear the priority queue by swapping with empty
    PriorityQueue empty;
    m_queue.swap(empty);
}

// ---------------------------------------------------------------------------
// Query Operations
// ---------------------------------------------------------------------------

std::vector<Event> EventQueue::getEventsBefore(double time) {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<Event> result;
    std::vector<Event> remaining;

    while (!m_queue.empty()) {
        Event e = m_queue.top();
        if (e.time <= time) {
            result.push_back(e);
            m_queue.pop();
            m_totalPopped++;
            if (m_popCallback) m_popCallback(e);
        } else {
            remaining.push_back(e);
            m_queue.pop();
        }
    }

    // Put remaining events back
    for (auto& e : remaining) {
        m_queue.push(std::move(e));
    }

    return result;
}

std::vector<Event> EventQueue::getEventsByType(EventType type) {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<Event> result;
    std::vector<Event> remaining;

    while (!m_queue.empty()) {
        Event e = m_queue.top();
        m_queue.pop();
        if (e.type == type) {
            result.push_back(e);
            m_totalPopped++;
            if (m_popCallback) m_popCallback(e);
        } else {
            remaining.push_back(e);
        }
    }

    for (auto& e : remaining) {
        m_queue.push(std::move(e));
    }

    return result;
}

std::vector<Event> EventQueue::getEventsByPriority(EventPriority maxPriority) {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<Event> result;
    std::vector<Event> remaining;

    while (!m_queue.empty()) {
        Event e = m_queue.top();
        m_queue.pop();
        if (static_cast<uint32_t>(e.priority) <= static_cast<uint32_t>(maxPriority)) {
            result.push_back(e);
            m_totalPopped++;
            if (m_popCallback) m_popCallback(e);
        } else {
            remaining.push_back(e);
        }
    }

    for (auto& e : remaining) {
        m_queue.push(std::move(e));
    }

    return result;
}

bool EventQueue::hasEventsBefore(double time) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_queue.empty()) return false;

    // We need to scan without modifying
    // Copy top elements to check
    PriorityQueue temp = m_queue;
    while (!temp.empty()) {
        if (temp.top().time <= time) return true;
        temp.pop();
    }
    return false;
}

double EventQueue::nextEventTime() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_queue.empty()) {
        return std::numeric_limits<double>::infinity();
    }

    return m_queue.top().time;
}

EventPriority EventQueue::nextEventPriority() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_queue.empty()) {
        return EventPriority::Background;
    }

    return m_queue.top().priority;
}

size_t EventQueue::countByType(EventType type) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    size_t count = 0;
    PriorityQueue temp = m_queue;
    while (!temp.empty()) {
        if (temp.top().type == type) count++;
        temp.pop();
    }
    return count;
}

size_t EventQueue::countBefore(double time) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    size_t count = 0;
    PriorityQueue temp = m_queue;
    while (!temp.empty()) {
        if (temp.top().time <= time) count++;
        temp.pop();
    }
    return count;
}

// ---------------------------------------------------------------------------
// Batch Operations
// ---------------------------------------------------------------------------

bool EventQueue::pushBatch(const std::vector<Event>& events) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_enabled) return false;

    bool allPushed = true;
    for (const auto& event : events) {
        if (m_queue.size() >= m_maxSize) {
            m_totalDropped++;
            if (m_dropCallback) m_dropCallback(event);
            allPushed = false;
            continue;
        }

        Event e = event;
        e.sequence = m_sequence++;
        m_queue.push(e);
        m_totalPushed++;
        if (m_pushCallback) m_pushCallback(e);
    }

    m_condVar.notify_all();
    return allPushed;
}

bool EventQueue::pushBatch(std::vector<Event>&& events) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_enabled) return false;

    bool allPushed = true;
    for (auto& event : events) {
        if (m_queue.size() >= m_maxSize) {
            m_totalDropped++;
            if (m_dropCallback) m_dropCallback(event);
            allPushed = false;
            continue;
        }

        event.sequence = m_sequence++;
        m_queue.push(std::move(event));
        m_totalPushed++;
        if (m_pushCallback) m_pushCallback(m_queue.top());
    }

    m_condVar.notify_all();
    return allPushed;
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void EventQueue::setMaxSize(size_t maxSize) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_maxSize = maxSize;
}

size_t EventQueue::maxSize() const noexcept {
    return m_maxSize;
}

void EventQueue::setBlocking(bool blocking) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_blocking = blocking;
    if (!blocking) {
        m_condVar.notify_all();
    }
}

bool EventQueue::isBlocking() const noexcept {
    return m_blocking;
}

void EventQueue::setEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_enabled = enabled;
    if (!enabled) {
        m_condVar.notify_all();
    }
}

bool EventQueue::isEnabled() const noexcept {
    return m_enabled;
}

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------

uint64_t EventQueue::totalPushed() const noexcept {
    return m_totalPushed;
}

uint64_t EventQueue::totalPopped() const noexcept {
    return m_totalPopped;
}

uint64_t EventQueue::totalDropped() const noexcept {
    return m_totalDropped;
}

double EventQueue::averageWaitTime() const noexcept {
    if (m_totalPushed == 0) return 0.0;
    return m_totalWaitTime / static_cast<double>(m_totalPushed);
}

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------

void EventQueue::setPushCallback(std::function<void(const Event&)> callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pushCallback = std::move(callback);
}

void EventQueue::setPopCallback(std::function<void(const Event&)> callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_popCallback = std::move(callback);
}

void EventQueue::setDropCallback(std::function<void(const Event&)> callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_dropCallback = std::move(callback);
}

// ============================================================================
// EventQueueManager Implementation
// ============================================================================

EventQueueManager& EventQueueManager::instance() {
    static EventQueueManager instance;
    return instance;
}

std::shared_ptr<EventQueue> EventQueueManager::getQueue(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_queues.find(name);
    if (it != m_queues.end()) {
        return it->second;
    }

    auto queue = std::make_shared<EventQueue>();
    m_queues[name] = queue;
    return queue;
}

void EventQueueManager::removeQueue(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queues.erase(name);
}

bool EventQueueManager::hasQueue(const std::string& name) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_queues.find(name) != m_queues.end();
}

std::vector<std::string> EventQueueManager::listQueues() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<std::string> names;
    names.reserve(m_queues.size());
    for (const auto& [name, _] : m_queues) {
        names.push_back(name);
    }
    return names;
}

void EventQueueManager::clearAll() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queues.clear();
}

} // namespace powsys365::simulation
