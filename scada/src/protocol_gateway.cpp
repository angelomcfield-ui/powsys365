#include "powsy365/scada/protocol_gateway.h"
#include <iostream>
#include <algorithm>
#include <cstring>

namespace powsys365 {

std::string protocolTypeToString(ProtocolType pt) {
    switch (pt) {
        case ProtocolType::IEC61850_MMS: return "IEC61850_MMS";
        case ProtocolType::IEC61850_GOOSE: return "IEC61850_GOOSE";
        case ProtocolType::IEC61850_SV: return "IEC61850_SV";
        case ProtocolType::DNP3_SERIAL: return "DNP3_SERIAL";
        case ProtocolType::DNP3_TCP: return "DNP3_TCP";
        case ProtocolType::DNP3_UDP: return "DNP3_UDP";
        case ProtocolType::MODBUS_TCP: return "MODBUS_TCP";
        case ProtocolType::MODBUS_RTU: return "MODBUS_RTU";
        case ProtocolType::OPC_UA: return "OPC_UA";
        default: return "UNKNOWN";
    }
}

// ============================================================================
// ProtocolGateway
// ============================================================================
ProtocolGateway::ProtocolGateway() {
}

ProtocolGateway::~ProtocolGateway() {
    stop();
}

bool ProtocolGateway::start() {
    if (m_running.load()) return true;

    m_running.store(true);
    m_dispatcherThread = std::thread(&ProtocolGateway::messageDispatcherLoop, this);
    m_healthThread = std::thread(&ProtocolGateway::healthMonitorLoop, this);

    return true;
}

void ProtocolGateway::stop() {
    m_running.store(false);

    m_incomingCv.notify_all();

    if (m_dispatcherThread.joinable()) {
        m_dispatcherThread.join();
    }
    if (m_healthThread.joinable()) {
        m_healthThread.join();
    }
}

bool ProtocolGateway::isRunning() const {
    return m_running.load();
}

// ---------------------------------------------------------------------------
// Client management
// ---------------------------------------------------------------------------
bool ProtocolGateway::registerClient(const ProtocolClient& client) {
    std::lock_guard<std::mutex> lock(m_clientsMutex);
    auto clientPtr = std::make_shared<ProtocolClient>(client);
    m_clients[client.clientId] = clientPtr;

    if (m_onClientConnected) {
        m_onClientConnected(client.clientId);
    }
    return true;
}

void ProtocolGateway::unregisterClient(const std::string& clientId) {
    std::lock_guard<std::mutex> lock(m_clientsMutex);
    auto it = m_clients.find(clientId);
    if (it != m_clients.end()) {
        it->second->isActive = false;
        m_clients.erase(it);
        if (m_onClientDisconnected) {
            m_onClientDisconnected(clientId);
        }
    }
}

std::shared_ptr<ProtocolClient> ProtocolGateway::getClient(const std::string& clientId) const {
    std::lock_guard<std::mutex> lock(m_clientsMutex);
    auto it = m_clients.find(clientId);
    if (it != m_clients.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<std::shared_ptr<ProtocolClient>> ProtocolGateway::getClientsByProtocol(ProtocolType protocol) const {
    std::lock_guard<std::mutex> lock(m_clientsMutex);
    std::vector<std::shared_ptr<ProtocolClient>> result;
    for (const auto& [id, client] : m_clients) {
        if (client->protocol == protocol) {
            result.push_back(client);
        }
    }
    return result;
}

std::vector<std::shared_ptr<ProtocolClient>> ProtocolGateway::getClientsByBus(const std::string& busId) const {
    std::lock_guard<std::mutex> lock(m_clientsMutex);
    std::vector<std::shared_ptr<ProtocolClient>> result;
    for (const auto& [id, client] : m_clients) {
        if (client->busId == busId) {
            result.push_back(client);
        }
    }
    return result;
}

std::vector<std::shared_ptr<ProtocolClient>> ProtocolGateway::getAllClients() const {
    std::lock_guard<std::mutex> lock(m_clientsMutex);
    std::vector<std::shared_ptr<ProtocolClient>> result;
    for (const auto& [id, client] : m_clients) {
        result.push_back(client);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Routing
// ---------------------------------------------------------------------------
bool ProtocolGateway::addRoute(const RouteEntry& route) {
    std::lock_guard<std::mutex> lock(m_routesMutex);
    m_routes.push_back(route);
    return true;
}

void ProtocolGateway::removeRoute(const std::string& routeId) {
    std::lock_guard<std::mutex> lock(m_routesMutex);
    m_routes.erase(std::remove_if(m_routes.begin(), m_routes.end(),
        [&routeId](const RouteEntry& r) { return r.routeId == routeId; }), m_routes.end());
}

std::vector<RouteEntry> ProtocolGateway::getRoutes() const {
    std::lock_guard<std::mutex> lock(m_routesMutex);
    return m_routes;
}

// ---------------------------------------------------------------------------
// Message sending
// ---------------------------------------------------------------------------
bool ProtocolGateway::sendMessage(const ProtocolMessage& message) {
    if (!m_running.load()) return false;

    // Update stats
    {
        std::lock_guard<std::mutex> lock(m_statsMutex);
        auto& stats = m_protocolStats[message.protocol];
        stats.messagesSent++;
        stats.bytesTransferred += message.payload.size();
    }

    return routeMessage(message);
}

bool ProtocolGateway::sendMessageToBus(const std::string& busId, const ProtocolMessage& message) {
    if (!m_running.load()) return false;

    std::lock_guard<std::mutex> lock(m_clientsMutex);
    bool sent = false;
    for (const auto& [id, client] : m_clients) {
        if (client->busId == busId && client->isActive) {
            if (client->onMessageReceived) {
                client->onMessageReceived(message);
                sent = true;
            }
        }
    }

    if (sent) {
        std::lock_guard<std::mutex> statsLock(m_statsMutex);
        auto& stats = m_protocolStats[message.protocol];
        stats.messagesSent++;
    }

    return sent;
}

bool ProtocolGateway::sendMessageToProtocol(ProtocolType protocol, const ProtocolMessage& message) {
    if (!m_running.load()) return false;

    std::lock_guard<std::mutex> lock(m_clientsMutex);
    bool sent = false;
    for (const auto& [id, client] : m_clients) {
        if (client->protocol == protocol && client->isActive) {
            if (client->onMessageReceived) {
                client->onMessageReceived(message);
                sent = true;
            }
        }
    }

    if (sent) {
        std::lock_guard<std::mutex> statsLock(m_statsMutex);
        auto& stats = m_protocolStats[protocol];
        stats.messagesSent++;
    }

    return sent;
}

bool ProtocolGateway::broadcastMessage(const ProtocolMessage& message) {
    if (!m_running.load()) return false;

    std::lock_guard<std::mutex> lock(m_clientsMutex);
    for (const auto& [id, client] : m_clients) {
        if (client->isActive && client->onMessageReceived) {
            client->onMessageReceived(message);
        }
    }

    std::lock_guard<std::mutex> statsLock(m_statsMutex);
    auto& stats = m_protocolStats[message.protocol];
    stats.messagesSent += m_clients.size();

    return !m_clients.empty();
}

// ---------------------------------------------------------------------------
// Message dispatch
// ---------------------------------------------------------------------------
void ProtocolGateway::dispatchIncomingMessage(const ProtocolMessage& message) {
    {
        std::lock_guard<std::mutex> lock(m_incomingMutex);
        m_incomingQueue.push(message);
    }
    m_incomingCv.notify_one();

    // Update stats
    std::lock_guard<std::mutex> statsLock(m_statsMutex);
    auto& stats = m_protocolStats[message.protocol];
    stats.messagesReceived++;
    stats.bytesTransferred += message.payload.size();
}

void ProtocolGateway::setDefaultTimeout(std::chrono::milliseconds timeout) {
    m_defaultTimeout = timeout;
}

void ProtocolGateway::setDefaultMaxRetries(int retries) {
    m_defaultMaxRetries = retries;
}

std::chrono::milliseconds ProtocolGateway::getDefaultTimeout() const {
    return m_defaultTimeout;
}

int ProtocolGateway::getDefaultMaxRetries() const {
    return m_defaultMaxRetries;
}

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------
ProtocolStats ProtocolGateway::getStats(ProtocolType protocol) const {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    auto it = m_protocolStats.find(protocol);
    if (it != m_protocolStats.end()) {
        return it->second;
    }
    return ProtocolStats{};
}

ProtocolStats ProtocolGateway::getStats(const std::string& clientId) const {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    auto it = m_clientStats.find(clientId);
    if (it != m_clientStats.end()) {
        return it->second;
    }
    return ProtocolStats{};
}

void ProtocolGateway::resetStats(ProtocolType protocol) {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    auto it = m_protocolStats.find(protocol);
    if (it != m_protocolStats.end()) {
        it->second = ProtocolStats{};
        it->second.lastReset = std::chrono::system_clock::now();
    }
}

void ProtocolGateway::resetStats(const std::string& clientId) {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    auto it = m_clientStats.find(clientId);
    if (it != m_clientStats.end()) {
        it->second = ProtocolStats{};
        it->second.lastReset = std::chrono::system_clock::now();
    }
}

// ---------------------------------------------------------------------------
// Health check
// ---------------------------------------------------------------------------
bool ProtocolGateway::checkClientHealth(const std::string& clientId) const {
    auto client = getClient(clientId);
    if (!client) return false;

    auto now = std::chrono::system_clock::now();
    auto inactive = std::chrono::duration_cast<std::chrono::seconds>(
        now - client->lastActivity).count();
    return client->isActive && (inactive < client->responseTimeout.count() / 1000 * 3);
}

std::vector<std::string> ProtocolGateway::getUnhealthyClients() const {
    std::vector<std::string> result;
    auto clients = getAllClients();
    for (const auto& client : clients) {
        if (!checkClientHealth(client->clientId)) {
            result.push_back(client->clientId);
        }
    }
    return result;
}

void ProtocolGateway::setOnMessageReceived(std::function<void(const ProtocolMessage&)> callback) {
    m_onMessageReceived = callback;
}

void ProtocolGateway::setOnClientConnected(std::function<void(const std::string&)> callback) {
    m_onClientConnected = callback;
}

void ProtocolGateway::setOnClientDisconnected(std::function<void(const std::string&)> callback) {
    m_onClientDisconnected = callback;
}

void ProtocolGateway::setOnError(std::function<void(const std::string&, const std::string&)> callback) {
    m_onError = callback;
}

void ProtocolGateway::setHealthCheckInterval(std::chrono::milliseconds interval) {
    m_healthCheckInterval = interval;
}

// ---------------------------------------------------------------------------
// Internal loops
// ---------------------------------------------------------------------------
void ProtocolGateway::messageDispatcherLoop() {
    while (m_running.load()) {
        std::unique_lock<std::mutex> lock(m_incomingMutex);
        m_incomingCv.wait_for(lock, std::chrono::milliseconds(100), [this] {
            return !m_incomingQueue.empty() || !m_running.load();
        });

        if (!m_running.load()) break;

        if (!m_incomingQueue.empty()) {
            ProtocolMessage msg = m_incomingQueue.front();
            m_incomingQueue.pop();
            lock.unlock();

            if (m_onMessageReceived) {
                m_onMessageReceived(msg);
            }

            routeMessage(msg);
        }

        processRetryQueue();
    }
}

void ProtocolGateway::healthMonitorLoop() {
    while (m_running.load()) {
        auto unhealthy = getUnhealthyClients();
        for (const auto& clientId : unhealthy) {
            if (m_onError) {
                m_onError(clientId, "Client health check failed - no recent activity");
            }
        }

        std::this_thread::sleep_for(m_healthCheckInterval);
    }
}

void ProtocolGateway::processRetryQueue() {
    std::lock_guard<std::mutex> lock(m_retryMutex);
    auto now = std::chrono::system_clock::now();

    for (auto it = m_retryQueue.begin(); it != m_retryQueue.end(); ) {
        if (it->retryCount >= it->timeout.count()) {
            // Max retries exceeded
            if (m_onError) {
                m_onError(it->sourceId, "Message delivery failed after " +
                          std::to_string(it->retryCount) + " retries");
            }
            it = m_retryQueue.erase(it);
            continue;
        }

        it->retryCount++;
        // Retry delivery
        routeMessage(*it);
        ++it;
    }
}

bool ProtocolGateway::routeMessage(const ProtocolMessage& message) {
    std::lock_guard<std::mutex> lock(m_routesMutex);

    bool routed = false;

    // Check explicit routes
    for (const auto& route : m_routes) {
        if (!route.enabled) continue;
        if (route.protocol == message.protocol || route.protocol == ProtocolType::UNKNOWN) {
            sendMessageToBus(route.destinationBus, message);
            routed = true;
        }
    }

    // If no explicit route, broadcast to all clients of same protocol
    if (!routed) {
        routed = sendMessageToProtocol(message.protocol, message);
    }

    return routed;
}

} // namespace powsys365
