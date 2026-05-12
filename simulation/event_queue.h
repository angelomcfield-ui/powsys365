#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <cstdint>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>

namespace powsys365::simulation {

// ============================================================================
// Event Type Enumeration
// ============================================================================
enum class EventType {
    Unknown = 0,
    SetValue,           // Set a variable value
    GetValue,           // Request a variable value
    StepSizeChange,     // Change simulation step size
    StepRequest,        // Request a simulation step
    Terminate,          // Request simulation termination
    Reset,              // Request simulation reset
    ConnectionUpdate,   // Update FMU connection
    ParameterUpdate,    // Update a parameter
    StateEvent,         // State-dependent event (zero crossing)
    TimeEvent,          // Time-based event
    InputEvent,         // External input change
    OutputEvent,        // Output request
    Custom              // User-defined event
};

// ============================================================================
// Event Priority
// ============================================================================
enum class EventPriority : uint32_t {
    Critical = 0,       // System-critical (e.g., termination)
    High = 1,           // Important (e.g., state events)
    Normal = 2,         // Standard priority
    Low = 3,            // Background tasks
    Background = 4      // Logging, monitoring
};

// ============================================================================
// Event Structure
// ============================================================================
struct Event {
    double time = 0.0;                          // Simulation time of event
    EventType type = EventType::Unknown;        // Event type
    std::string data;                           // Event data/payload (JSON or comma-separated)
    EventPriority priority = EventPriority::Normal;  // Event priority
    uint64_t sequence = 0;                      // Sequence number for FIFO ordering
    std::string source;                         // Source component name
    uint32_t sourceId = 0;                      // Source component ID

    // Constructors
    Event() = default;
    Event(double t, EventType tp, std::string d, EventPriority p = EventPriority::Normal);
    Event(double t, EventType tp, std::string d, EventPriority p,
          std::string src, uint32_t srcId);

    // Comparison for priority queue (lower priority value = higher priority)
    // Events are ordered by: priority ascending, then time ascending, then sequence ascending
    bool operator>(const Event& other) const;
    bool operator<(const Event& other) const;
    bool operator==(const Event& other) const;

    // Helpers
    std::string toString() const;
    static const char* typeToString(EventType t);
    static const char* priorityToString(EventPriority p);
};

// ============================================================================
// Event Queue (Thread-Safe Priority Queue)
// ============================================================================
class EventQueue {
public:
    EventQueue();
    explicit EventQueue(size_t maxSize);
    ~EventQueue() = default;

    // Disable copy, enable move
    EventQueue(const EventQueue&) = delete;
    EventQueue& operator=(const EventQueue&) = delete;
    EventQueue(EventQueue&&) noexcept;
    EventQueue& operator=(EventQueue&&) noexcept;

    // ------------------------------------------------------------------------
    // Core Operations
    // ------------------------------------------------------------------------

    // Push an event into the queue (thread-safe)
    bool push(const Event& event);
    bool push(Event&& event);
    bool push(double time, EventType type, const std::string& data,
              EventPriority priority = EventPriority::Normal);

    // Pop the highest priority event (thread-safe, blocking)
    Event pop();

    // Try to pop without blocking
    bool tryPop(Event& event);

    // Try to pop with timeout
    bool tryPop(Event& event, std::chrono::milliseconds timeout);

    // Peek at the highest priority event without removing
    Event peek() const;

    // Check if queue is empty
    bool empty() const;

    // Get current number of events
    size_t size() const;

    // Clear all events
    void clear();

    // ------------------------------------------------------------------------
    // Query Operations
    // ------------------------------------------------------------------------

    // Get all events with time <= specified time (removes them from queue)
    std::vector<Event> getEventsBefore(double time);

    // Get all events of a specific type (removes them from queue)
    std::vector<Event> getEventsByType(EventType type);

    // Get all events with priority <= specified priority (removes them from queue)
    std::vector<Event> getEventsByPriority(EventPriority maxPriority);

    // Check if any events exist before a given time
    bool hasEventsBefore(double time) const;

    // Get the time of the next event without removing it
    double nextEventTime() const;

    // Get the priority of the next event
    EventPriority nextEventPriority() const;

    // Count events by type
    size_t countByType(EventType type) const;

    // Count events before a given time
    size_t countBefore(double time) const;

    // ------------------------------------------------------------------------
    // Batch Operations
    // ------------------------------------------------------------------------

    // Push multiple events at once
    bool pushBatch(const std::vector<Event>& events);
    bool pushBatch(std::vector<Event>&& events);

    // ------------------------------------------------------------------------
    // Configuration
    // ------------------------------------------------------------------------

    void setMaxSize(size_t maxSize);
    size_t maxSize() const noexcept;

    // Set blocking behavior: if true, pop() blocks when queue is empty
    void setBlocking(bool blocking);
    bool isBlocking() const noexcept;

    // Enable/disable the queue
    void setEnabled(bool enabled);
    bool isEnabled() const noexcept;

    // ------------------------------------------------------------------------
    // Statistics
    // ------------------------------------------------------------------------

    uint64_t totalPushed() const noexcept;
    uint64_t totalPopped() const noexcept;
    uint64_t totalDropped() const noexcept;
    double averageWaitTime() const noexcept;

    // ------------------------------------------------------------------------
    // Callbacks
    // ------------------------------------------------------------------------

    // Called when an event is pushed
    void setPushCallback(std::function<void(const Event&)> callback);

    // Called when an event is popped
    void setPopCallback(std::function<void(const Event&)> callback);

    // Called when an event is dropped (queue full)
    void setDropCallback(std::function<void(const Event&)> callback);

private:
    using PriorityQueue = std::priority_queue<Event, std::vector<Event>, std::greater<>>;

    mutable std::mutex m_mutex;
    mutable std::condition_variable m_condVar;
    PriorityQueue m_queue;

    size_t m_maxSize = 100000;
    bool m_blocking = true;
    bool m_enabled = true;
    uint64_t m_sequence = 0;

    // Statistics
    uint64_t m_totalPushed = 0;
    uint64_t m_totalPopped = 0;
    uint64_t m_totalDropped = 0;
    double m_totalWaitTime = 0.0;

    // Callbacks
    std::function<void(const Event&)> m_pushCallback;
    std::function<void(const Event&)> m_popCallback;
    std::function<void(const Event&)> m_dropCallback;

    // Internal: pop without locking (caller must hold lock)
    Event popUnsafe();
};

// ============================================================================
// Event Queue Factory / Manager
// ============================================================================
class EventQueueManager {
public:
    static EventQueueManager& instance();

    // Get or create a named queue
    std::shared_ptr<EventQueue> getQueue(const std::string& name);

    // Remove a queue
    void removeQueue(const std::string& name);

    // Check if a queue exists
    bool hasQueue(const std::string& name) const;

    // List all queue names
    std::vector<std::string> listQueues() const;

    // Clear all queues
    void clearAll();

private:
    EventQueueManager() = default;
    ~EventQueueManager() = default;

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, std::shared_ptr<EventQueue>> m_queues;
};

} // namespace powsys365::simulation
