#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <functional>
#include <thread>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <chrono>
#include <cstdint>

namespace powsys365 {

// ---------------------------------------------------------------------------
// Protocol identifiers
// ---------------------------------------------------------------------------
enum class ProtocolType {
    IEC61850_MMS,
    IEC61850_GOOSE,
    IEC61850_SV,
    DNP3_SERIAL,
    DNP3_TCP,
    DNP3_UDP,
    MODBUS_TCP,
    MODBUS_RTU,
    OPC_UA,
    UNKNOWN
};

std::string protocolTypeToString(ProtocolType pt);

// ---------------------------------------------------------------------------
// Message structure for routing
// ---------------------------------------------------------------------------
struct ProtocolMessage {
    std::string sourceId;
    std::string destinationId;
    ProtocolType protocol;
    std::vector<uint8_t> payload;
    std::chrono::system_clock::time_point timestamp;
    uint32_t sequenceNumber = 0;
    bool requiresAck = false;
    int retryCount = 0;
    std::chrono::milliseconds timeout{5000};
};

// ---------------------------------------------------------------------------
// Client registration info
// ---------------------------------------------------------------------------
struct ProtocolClient {
    std::string clientId;
    std::string name;
    ProtocolType protocol;
    std::string busId;        // logical bus / line group
    std::string endpoint;     // IP:port or serial device
    bool isActive = false;
    std::chrono::milliseconds responseTimeout{5000};
    int maxRetries = 3;
    std::chrono::system_clock::time_point lastActivity;

    // Callback for received messages
    std::function<void(const ProtocolMessage&)> onMessageReceived;
    // Callback for connection state changes
    std::function<void(bool connected)> onConnectionChanged;
};

// ---------------------------------------------------------------------------
// Routing table entry
// ---------------------------------------------------------------------------
struct RouteEntry {
    std::string routeId;
    std::string sourceBus;
    std::string destinationBus;
    ProtocolType protocol;
    bool enabled = true;
    int priority = 5; // 1=highest, 10=lowest
};

// ---------------------------------------------------------------------------
// Protocol statistics
// ---------------------------------------------------------------------------
struct ProtocolStats {
    uint64_t messagesSent = 0;
    uint64_t messagesReceived = 0;
    uint64_t messagesDropped = 0;
    uint64_t bytesTransferred = 0;
    uint64_t retryCount = 0;
    uint64_t timeoutCount = 0;
    uint64_t errorCount = 0;
    double avgResponseTimeMs = 0.0;
    std::chrono::system_clock::time_point lastReset;
};

// ---------------------------------------------------------------------------
// Main Protocol Gateway
// ---------------------------------------------------------------------------
class ProtocolGateway {
public:
    ProtocolGateway();
    ~ProtocolGateway();

    // Lifecycle
    bool start();
    void stop();
    bool isRunning() const;

    // Client management
    bool registerClient(const ProtocolClient& client);
    void unregisterClient(const std::string& clientId);
    std::shared_ptr<ProtocolClient> getClient(const std::string& clientId) const;
    std::vector<std::shared_ptr<ProtocolClient>> getClientsByProtocol(ProtocolType protocol) const;
    std::vector<std::shared_ptr<ProtocolClient>> getClientsByBus(const std::string& busId) const;
    std::vector<std::shared_ptr<ProtocolClient>> getAllClients() const;

    // Routing
    bool addRoute(const RouteEntry& route);
    void removeRoute(const std::string& routeId);
    std::vector<RouteEntry> getRoutes() const;

    // Message sending
    bool sendMessage(const ProtocolMessage& message);
    bool sendMessageToBus(const std::string& busId, const ProtocolMessage& message);
    bool sendMessageToProtocol(ProtocolType protocol, const ProtocolMessage& message);
    bool broadcastMessage(const ProtocolMessage& message);

    // Message receiving (async dispatch)
    void dispatchIncomingMessage(const ProtocolMessage& message);

    // Retry and timeout
    void setDefaultTimeout(std::chrono::milliseconds timeout);
    void setDefaultMaxRetries(int retries);
    std::chrono::milliseconds getDefaultTimeout() const;
    int getDefaultMaxRetries() const;

    // Statistics
    ProtocolStats getStats(ProtocolType protocol) const;
    ProtocolStats getStats(const std::string& clientId) const;
    void resetStats(ProtocolType protocol);
    void resetStats(const std::string& clientId);

    // Connection health check
    bool checkClientHealth(const std::string& clientId) const;
    std::vector<std::string> getUnhealthyClients() const;

    // Callbacks for global events
    void setOnMessageReceived(std::function<void(const ProtocolMessage&)> callback);
    void setOnClientConnected(std::function<void(const std::string&)> callback);
    void setOnClientDisconnected(std::function<void(const std::string&)> callback);
    void setOnError(std::function<void(const std::string&, const std::string&)> callback);

    // Health monitoring
    void setHealthCheckInterval(std::chrono::milliseconds interval);

private:
    void messageDispatcherLoop();
    void healthMonitorLoop();
    void processRetryQueue();
    bool routeMessage(const ProtocolMessage& message);

    // Internal state
    std::atomic<bool> m_running{false};
    std::thread m_dispatcherThread;
    std::thread m_healthThread;

    // Thread-safe collections
    mutable std::mutex m_clientsMutex;
    std::map<std::string, std::shared_ptr<ProtocolClient>> m_clients;

    mutable std::mutex m_routesMutex;
    std::vector<RouteEntry> m_routes;

    // Message queues
    std::mutex m_incomingMutex;
    std::condition_variable m_incomingCv;
    std::queue<ProtocolMessage> m_incomingQueue;

    std::mutex m_retryMutex;
    std::vector<ProtocolMessage> m_retryQueue;

    // Stats
    mutable std::mutex m_statsMutex;
    std::map<ProtocolType, ProtocolStats> m_protocolStats;
    std::map<std::string, ProtocolStats> m_clientStats;

    // Configuration
    std::chrono::milliseconds m_defaultTimeout{5000};
    int m_defaultMaxRetries = 3;
    std::chrono::milliseconds m_healthCheckInterval{30000};

    // Callbacks
    std::function<void(const ProtocolMessage&)> m_onMessageReceived;
    std::function<void(const std::string&)> m_onClientConnected;
    std::function<void(const std::string&)> m_onClientDisconnected;
    std::function<void(const std::string&, const std::string&)> m_onError;
};

} // namespace powsys365
