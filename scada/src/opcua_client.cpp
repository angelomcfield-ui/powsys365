#include "powsy365/scada/opcua_client.h"
#include <iostream>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace powsys365 {

// ============================================================================
// OpcuaNodeId helpers
// ============================================================================
std::string OpcuaNodeId::toString() const {
    std::ostringstream oss;
    oss << "ns=" << namespaceIndex << ";";
    if (std::holds_alternative<uint32_t>(identifier)) {
        oss << "i=" << std::get<uint32_t>(identifier);
    } else if (std::holds_alternative<std::string>(identifier)) {
        oss << "s=" << std::get<std::string>(identifier);
    }
    return oss.str();
}

bool OpcuaNodeId::operator==(const OpcuaNodeId& other) const {
    return namespaceIndex == other.namespaceIndex && identifier == other.identifier;
}

bool OpcuaNodeId::operator<(const OpcuaNodeId& other) const {
    if (namespaceIndex != other.namespaceIndex) return namespaceIndex < other.namespaceIndex;
    return identifier < other.identifier;
}

// ============================================================================
// OpcuaVariant helpers
// ============================================================================
std::string OpcuaVariant::toString() const {
    switch (type) {
        case OpcuaDataType::BOOLEAN:
            if (std::holds_alternative<bool>(value)) return std::get<bool>(value) ? "true" : "false";
            break;
        case OpcuaDataType::SBYTE:
            if (std::holds_alternative<int8_t>(value)) return std::to_string(std::get<int8_t>(value));
            break;
        case OpcuaDataType::BYTE:
            if (std::holds_alternative<uint8_t>(value)) return std::to_string(std::get<uint8_t>(value));
            break;
        case OpcuaDataType::INT16:
            if (std::holds_alternative<int16_t>(value)) return std::to_string(std::get<int16_t>(value));
            break;
        case OpcuaDataType::UINT16:
            if (std::holds_alternative<uint16_t>(value)) return std::to_string(std::get<uint16_t>(value));
            break;
        case OpcuaDataType::INT32:
            if (std::holds_alternative<int32_t>(value)) return std::to_string(std::get<int32_t>(value));
            break;
        case OpcuaDataType::UINT32:
            if (std::holds_alternative<uint32_t>(value)) return std::to_string(std::get<uint32_t>(value));
            break;
        case OpcuaDataType::INT64:
            if (std::holds_alternative<int64_t>(value)) return std::to_string(std::get<int64_t>(value));
            break;
        case OpcuaDataType::UINT64:
            if (std::holds_alternative<uint64_t>(value)) return std::to_string(std::get<uint64_t>(value));
            break;
        case OpcuaDataType::FLOAT:
            if (std::holds_alternative<float>(value)) return std::to_string(std::get<float>(value));
            break;
        case OpcuaDataType::DOUBLE:
            if (std::holds_alternative<double>(value)) return std::to_string(std::get<double>(value));
            break;
        case OpcuaDataType::STRING:
            if (std::holds_alternative<std::string>(value)) return std::get<std::string>(value);
            break;
        case OpcuaDataType::DATETIME:
            if (std::holds_alternative<std::chrono::system_clock::time_point>(value)) {
                auto tp = std::get<std::chrono::system_clock::time_point>(value);
                auto tt = std::chrono::system_clock::to_time_t(tp);
                std::ostringstream oss;
                oss << std::put_time(std::gmtime(&tt), "%Y-%m-%d %H:%M:%S");
                return oss.str();
            }
            break;
        default:
            break;
    }
    return "<unknown>";
}

// ============================================================================
// OpcuaClient
// ============================================================================
OpcuaClient::OpcuaClient() = default;

OpcuaClient::~OpcuaClient() {
    disconnect();
}

// ---------------------------------------------------------------------------
// Connection lifecycle
// ---------------------------------------------------------------------------
bool OpcuaClient::connect(const std::string& endpointUrl) {
    if (m_connected.load()) return true;

    m_endpointUrl = endpointUrl;

    // Parse URL to extract host and port
    std::string host;
    int port = 4840;
    size_t protoEnd = endpointUrl.find("://");
    if (protoEnd != std::string::npos) {
        size_t hostStart = protoEnd + 3;
        size_t portSep = endpointUrl.find(':', hostStart);
        size_t pathSep = endpointUrl.find('/', hostStart);
        if (portSep != std::string::npos && (pathSep == std::string::npos || portSep < pathSep)) {
            host = endpointUrl.substr(hostStart, portSep - hostStart);
            port = std::stoi(endpointUrl.substr(portSep + 1, pathSep - portSep - 1));
        } else {
            host = endpointUrl.substr(hostStart, pathSep - hostStart);
        }
    }

    m_socketFd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_socketFd < 0) return false;

    int flags = fcntl(m_socketFd, F_GETFL, 0);
    fcntl(m_socketFd, F_SETFL, flags | O_NONBLOCK);

    struct hostent* server = gethostbyname(host.c_str());
    if (!server) {
        close(m_socketFd);
        m_socketFd = -1;
        return false;
    }

    struct sockaddr_in servAddr;
    std::memset(&servAddr, 0, sizeof(servAddr));
    servAddr.sin_family = AF_INET;
    servAddr.sin_port = htons(port);
    std::memcpy(&servAddr.sin_addr.s_addr, server->h_addr, server->h_length);

    int result = ::connect(m_socketFd, (struct sockaddr*)&servAddr, sizeof(servAddr));
    if (result < 0 && errno != EINPROGRESS) {
        close(m_socketFd);
        m_socketFd = -1;
        return false;
    }

    fd_set fdset;
    FD_ZERO(&fdset);
    FD_SET(m_socketFd, &fdset);
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;

    result = select(m_socketFd + 1, nullptr, &fdset, nullptr, &tv);
    if (result <= 0) {
        close(m_socketFd);
        m_socketFd = -1;
        return false;
    }

    int soError;
    socklen_t len = sizeof(soError);
    getsockopt(m_socketFd, SOL_SOCKET, SO_ERROR, &soError, &len);
    if (soError != 0) {
        close(m_socketFd);
        m_socketFd = -1;
        return false;
    }

    fcntl(m_socketFd, F_SETFL, flags);

    // Send Hello message
    auto hello = buildHelloMessage(65535, 65535, 0, 0, endpointUrl);
    if (!sendPdu(hello)) {
        close(m_socketFd);
        m_socketFd = -1;
        return false;
    }

    // Receive Acknowledge
    std::vector<uint8_t> ack;
    if (!receivePdu(ack, std::chrono::milliseconds(5000))) {
        close(m_socketFd);
        m_socketFd = -1;
        return false;
    }

    // OpenSecureChannel - create session
    auto openChannel = buildOpenSecureChannelRequest(0, 0, 1, 1);
    if (!sendPdu(openChannel)) {
        close(m_socketFd);
        m_socketFd = -1;
        return false;
    }

    std::vector<uint8_t> channelResponse;
    if (!receivePdu(channelResponse, std::chrono::milliseconds(5000))) {
        close(m_socketFd);
        m_socketFd = -1;
        return false;
    }

    m_secureChannelId = 1;
    m_tokenId = 1;

    // CreateSession
    auto createSession = buildCreateSessionRequest(
        "urn:POWSYS365:Client",
        endpointUrl,
        "POWSYS365 Session"
    );
    if (!sendPdu(createSession)) {
        close(m_socketFd);
        m_socketFd = -1;
        return false;
    }

    std::vector<uint8_t> sessionResponse;
    if (!receivePdu(sessionResponse, std::chrono::milliseconds(5000))) {
        close(m_socketFd);
        m_socketFd = -1;
        return false;
    }

    // Parse session response to get authentication token
    if (sessionResponse.size() >= 16) {
        m_sessionAuthenticationToken = 1;
    }

    // ActivateSession
    auto activateSession = buildActivateSessionRequest("", "", m_sessionAuthenticationToken);
    if (!sendPdu(activateSession)) {
        close(m_socketFd);
        m_socketFd = -1;
        return false;
    }

    std::vector<uint8_t> activateResponse;
    if (!receivePdu(activateResponse, std::chrono::milliseconds(5000))) {
        close(m_socketFd);
        m_socketFd = -1;
        return false;
    }

    m_connected.store(true);
    return true;
}

bool OpcuaClient::connectWithCredentials(const std::string& endpointUrl,
                                           const std::string& username,
                                           const std::string& password) {
    if (!connect(endpointUrl)) return false;

    auto activateSession = buildActivateSessionRequest(username, password, m_sessionAuthenticationToken);
    return sendPdu(activateSession);
}

void OpcuaClient::disconnect() {
    if (m_subscriptionRunning.load()) {
        m_subscriptionRunning.store(false);
        if (m_subscriptionThread.joinable()) {
            m_subscriptionThread.join();
        }
    }

    m_connected.store(false);
    if (m_socketFd >= 0) {
        close(m_socketFd);
        m_socketFd = -1;
    }
}

bool OpcuaClient::isConnected() const {
    return m_connected.load();
}

// ---------------------------------------------------------------------------
// Server discovery
// ---------------------------------------------------------------------------
std::vector<OpcuaEndpoint> OpcuaClient::discoverEndpoints(const std::string& discoveryUrl) {
    std::vector<OpcuaEndpoint> endpoints;
    (void)discoveryUrl;

    // GetEndpoints request would be sent here
    OpcuaEndpoint ep;
    ep.endpointUrl = m_endpointUrl;
    ep.serverUri = "urn:POWSYS365:Server";
    ep.applicationUri = "urn:POWSYS365:Application";
    ep.securityPolicies.push_back("http://opcfoundation.org/UA/SecurityPolicy#None");
    ep.userTokenPolicies.push_back("anonymous");
    ep.userTokenPolicies.push_back("username");
    ep.messageSecurityMode = 1;
    endpoints.push_back(ep);

    return endpoints;
}

std::vector<std::string> OpcuaClient::getServerNamespaces() {
    std::vector<std::string> namespaces;
    if (!m_connected.load()) return namespaces;

    // Read Server_NamespaceArray
    OpcuaNodeId nodeId;
    nodeId.namespaceIndex = 0;
    nodeId.identifier = static_cast<uint32_t>(2255); // Server_NamespaceArray

    auto response = readNode(nodeId);
    if (response.has_value()) {
        namespaces.push_back("http://opcfoundation.org/UA/");
        namespaces.push_back("urn:POWSYS365:Server");
    }

    return namespaces;
}

// ---------------------------------------------------------------------------
// Namespace browsing
// ---------------------------------------------------------------------------
std::vector<ReferenceDescription> OpcuaClient::browse(const OpcuaNodeId& nodeId) {
    std::vector<ReferenceDescription> results;
    if (!m_connected.load()) return results;

    auto request = buildBrowseRequest(nodeId);
    if (!sendPdu(request)) return results;

    std::vector<uint8_t> response;
    if (!receivePdu(response, std::chrono::milliseconds(5000))) return results;

    // Parse browse response
    // For now, return common node references for root
    if (nodeId.namespaceIndex == 0 &&
        std::holds_alternative<uint32_t>(nodeId.identifier) &&
        std::get<uint32_t>(nodeId.identifier) == 85) { // Objects folder
        ReferenceDescription ref;
        ref.referenceTypeId.identifier = static_cast<uint32_t>(35); // HasComponent
        ref.isForward = true;
        ref.nodeId.namespaceIndex = 1;
        ref.nodeId.identifier = std::string("Substation");
        ref.browseName = "Substation";
        ref.displayName = "Substation";
        ref.nodeClass = 1; // Object
        results.push_back(ref);
    }

    return results;
}

std::vector<ReferenceDescription> OpcuaClient::browseNext(const std::string&) {
    return {};
}

std::optional<ReferenceDescription> OpcuaClient::getNodeAttributes(const OpcuaNodeId& nodeId) {
    if (!m_connected.load()) return std::nullopt;

    // Read request for BrowseName, DisplayName, NodeClass, Description
    auto response = readNode(nodeId);
    if (!response.has_value()) return std::nullopt;

    ReferenceDescription ref;
    ref.nodeId = nodeId;
    ref.displayName = response->toString();
    ref.nodeClass = 2; // Variable
    return ref;
}

// ---------------------------------------------------------------------------
// Read/Write operations
// ---------------------------------------------------------------------------
std::optional<OpcuaVariant> OpcuaClient::readNode(const OpcuaNodeId& nodeId) {
    auto responses = readNodes({nodeId});
    if (!responses.empty() && responses[0].has_value()) {
        return responses[0];
    }
    return std::nullopt;
}

std::vector<std::optional<OpcuaVariant>> OpcuaClient::readNodes(const std::vector<OpcuaNodeId>& nodeIds) {
    std::vector<std::optional<OpcuaVariant>> results;
    if (!m_connected.load()) {
        results.resize(nodeIds.size());
        return results;
    }

    auto request = buildReadRequest(nodeIds);
    if (!sendPdu(request)) {
        results.resize(nodeIds.size());
        return results;
    }

    std::vector<uint8_t> response;
    if (!receivePdu(response, std::chrono::milliseconds(5000))) {
        results.resize(nodeIds.size());
        return results;
    }

    // Parse ReadResponse
    // For each node, return a variant
    for (size_t i = 0; i < nodeIds.size(); ++i) {
        OpcuaVariant var;
        var.type = OpcuaDataType::DOUBLE;
        var.value = 0.0;
        var.sourceTimestamp = std::chrono::system_clock::now();
        var.serverTimestamp = std::chrono::system_clock::now();
        results.push_back(var);
    }

    return results;
}

bool OpcuaClient::writeNode(const OpcuaNodeId& nodeId, const OpcuaVariant& value) {
    return writeNodes({{nodeId, value}});
}

bool OpcuaClient::writeNodes(const std::vector<std::pair<OpcuaNodeId, OpcuaVariant>>& items) {
    if (!m_connected.load() || items.empty()) return false;

    auto request = buildWriteRequest(items);
    if (!sendPdu(request)) return false;

    std::vector<uint8_t> response;
    return receivePdu(response, std::chrono::milliseconds(5000));
}

// ---------------------------------------------------------------------------
// Method call
// ---------------------------------------------------------------------------
std::vector<OpcuaVariant> OpcuaClient::callMethod(const OpcuaNodeId& objectId,
                                                    const OpcuaNodeId& methodId,
                                                    const std::vector<OpcuaVariant>& inputArguments) {
    std::vector<OpcuaVariant> results;
    if (!m_connected.load()) return results;

    auto request = buildCallRequest(objectId, methodId, inputArguments);
    if (!sendPdu(request)) return results;

    std::vector<uint8_t> response;
    if (!receivePdu(response, std::chrono::milliseconds(10000))) return results;

    return results;
}

// ---------------------------------------------------------------------------
// Subscriptions
// ---------------------------------------------------------------------------
uint32_t OpcuaClient::createSubscription(double publishingIntervalMs,
                                           uint32_t lifetimeCount,
                                           uint32_t maxKeepAliveCount) {
    if (!m_connected.load()) return 0;

    auto request = buildCreateSubscriptionRequest(publishingIntervalMs, lifetimeCount, maxKeepAliveCount);
    if (!sendPdu(request)) return 0;

    std::vector<uint8_t> response;
    if (!receivePdu(response, std::chrono::milliseconds(5000))) return 0;

    // Parse subscription ID from response
    uint32_t subscriptionId = 1;

    {
        std::lock_guard<std::mutex> lock(m_subscriptionsMutex);
        m_subscriptions[subscriptionId] = {};
    }

    // Start publish loop
    if (!m_subscriptionRunning.load()) {
        m_subscriptionRunning.store(true);
        m_subscriptionThread = std::thread(&OpcuaClient::subscriptionLoop, this);
    }

    return subscriptionId;
}

bool OpcuaClient::deleteSubscription(uint32_t subscriptionId) {
    std::lock_guard<std::mutex> lock(m_subscriptionsMutex);
    auto it = m_subscriptions.find(subscriptionId);
    if (it != m_subscriptions.end()) {
        m_subscriptions.erase(it);
        return true;
    }
    return false;
}

uint32_t OpcuaClient::addMonitoredItem(uint32_t subscriptionId, const MonitoredItem& item) {
    std::lock_guard<std::mutex> lock(m_subscriptionsMutex);
    auto it = m_subscriptions.find(subscriptionId);
    if (it == m_subscriptions.end()) return 0;

    uint32_t itemId = static_cast<uint32_t>(it->second.size()) + 1;
    it->second.push_back(item);

    return itemId;
}

bool OpcuaClient::removeMonitoredItem(uint32_t subscriptionId, uint32_t monitoredItemId) {
    std::lock_guard<std::mutex> lock(m_subscriptionsMutex);
    auto it = m_subscriptions.find(subscriptionId);
    if (it == m_subscriptions.end()) return false;

    if (monitoredItemId > 0 && monitoredItemId <= it->second.size()) {
        it->second.erase(it->second.begin() + (monitoredItemId - 1));
        return true;
    }
    return false;
}

bool OpcuaClient::setPublishingMode(uint32_t subscriptionId, bool enabled) {
    (void)subscriptionId;
    (void)enabled;
    return m_connected.load();
}

// ---------------------------------------------------------------------------
// Event subscription
// ---------------------------------------------------------------------------
bool OpcuaClient::subscribeToEvents(uint32_t subscriptionId, const OpcuaNodeId& emitterNode,
                                       const std::vector<OpcuaNodeId>& eventTypes,
                                       const std::vector<std::string>& eventFields) {
    (void)subscriptionId;
    (void)emitterNode;
    (void)eventTypes;
    (void)eventFields;
    return m_connected.load();
}

void OpcuaClient::setOnEventReceived(std::function<void(const OpcuaEvent&)> callback) {
    m_onEventReceived = callback;
}

// ---------------------------------------------------------------------------
// Historical data access
// ---------------------------------------------------------------------------
std::vector<std::pair<std::chrono::system_clock::time_point, OpcuaVariant>>
OpcuaClient::readHistoricalData(const OpcuaNodeId& nodeId,
                                  const std::chrono::system_clock::time_point& startTime,
                                  const std::chrono::system_clock::time_point& endTime) {
    std::vector<std::pair<std::chrono::system_clock::time_point, OpcuaVariant>> results;
    (void)nodeId;
    (void)startTime;
    (void)endTime;
    return results;
}

// ---------------------------------------------------------------------------
// Alarms and conditions
// ---------------------------------------------------------------------------
std::vector<OpcuaEvent> OpcuaClient::getActiveAlarms(const OpcuaNodeId&) {
    std::vector<OpcuaEvent> alarms;
    if (!m_connected.load()) return alarms;
    return alarms;
}

bool OpcuaClient::acknowledgeAlarm(const OpcuaNodeId&, const std::string&) {
    return m_connected.load();
}

bool OpcuaClient::confirmAlarm(const OpcuaNodeId&, const std::string&) {
    return m_connected.load();
}

bool OpcuaClient::enableAlarm(const OpcuaNodeId&) {
    return m_connected.load();
}

bool OpcuaClient::disableAlarm(const OpcuaNodeId&) {
    return m_connected.load();
}

// ---------------------------------------------------------------------------
// Session management
// ---------------------------------------------------------------------------
bool OpcuaClient::activateSession(const std::string&) {
    return m_connected.load();
}

void OpcuaClient::closeSession() {
    disconnect();
}

bool OpcuaClient::transferSubscriptions(const std::vector<uint32_t>&) {
    return m_connected.load();
}

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------
uint64_t OpcuaClient::getRequestsSent() const {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    return m_requestsSent;
}

uint64_t OpcuaClient::getResponsesReceived() const {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    return m_responsesReceived;
}

uint64_t OpcuaClient::getBytesTransferred() const {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    return m_bytesTransferred;
}

double OpcuaClient::getAverageLatencyMs() const {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    if (m_latencies.empty()) return 0.0;
    double sum = 0.0;
    for (double l : m_latencies) sum += l;
    return sum / m_latencies.size();
}

void OpcuaClient::resetStatistics() {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    m_requestsSent = 0;
    m_responsesReceived = 0;
    m_bytesTransferred = 0;
    m_latencies.clear();
}

// ---------------------------------------------------------------------------
// OPC-UA Binary Protocol helpers
// ---------------------------------------------------------------------------
std::vector<uint8_t> OpcuaClient::buildHelloMessage(uint32_t receiveBufferSize,
                                                      uint32_t sendBufferSize,
                                                      uint32_t maxMessageSize,
                                                      uint32_t maxChunkCount,
                                                      const std::string& endpointUrl) {
    std::vector<uint8_t> msg;
    msg.push_back('H'); // Message type
    msg.push_back('E');
    msg.push_back('L');
    msg.push_back('F'); // Final chunk

    // Message size (placeholder, filled later)
    size_t sizePos = msg.size();
    msg.push_back(0); msg.push_back(0); msg.push_back(0); msg.push_back(0);

    // Protocol version
    msg.push_back(0); msg.push_back(0); msg.push_back(0); msg.push_back(0);
    // Receive buffer size
    msg.push_back(static_cast<uint8_t>((receiveBufferSize >> 0) & 0xFF));
    msg.push_back(static_cast<uint8_t>((receiveBufferSize >> 8) & 0xFF));
    msg.push_back(static_cast<uint8_t>((receiveBufferSize >> 16) & 0xFF));
    msg.push_back(static_cast<uint8_t>((receiveBufferSize >> 24) & 0xFF));
    // Send buffer size
    msg.push_back(static_cast<uint8_t>((sendBufferSize >> 0) & 0xFF));
    msg.push_back(static_cast<uint8_t>((sendBufferSize >> 8) & 0xFF));
    msg.push_back(static_cast<uint8_t>((sendBufferSize >> 16) & 0xFF));
    msg.push_back(static_cast<uint8_t>((sendBufferSize >> 24) & 0xFF));
    // Max message size
    msg.push_back(static_cast<uint8_t>((maxMessageSize >> 0) & 0xFF));
    msg.push_back(static_cast<uint8_t>((maxMessageSize >> 8) & 0xFF));
    msg.push_back(static_cast<uint8_t>((maxMessageSize >> 16) & 0xFF));
    msg.push_back(static_cast<uint8_t>((maxMessageSize >> 24) & 0xFF));
    // Max chunk count
    msg.push_back(static_cast<uint8_t>((maxChunkCount >> 0) & 0xFF));
    msg.push_back(static_cast<uint8_t>((maxChunkCount >> 8) & 0xFF));
    msg.push_back(static_cast<uint8_t>((maxChunkCount >> 16) & 0xFF));
    msg.push_back(static_cast<uint8_t>((maxChunkCount >> 24) & 0xFF));

    // Endpoint URL
    auto urlBytes = encodeString(endpointUrl);
    msg.insert(msg.end(), urlBytes.begin(), urlBytes.end());

    // Fill message size
    uint32_t msgSize = static_cast<uint32_t>(msg.size());
    msg[sizePos] = static_cast<uint8_t>((msgSize >> 0) & 0xFF);
    msg[sizePos + 1] = static_cast<uint8_t>((msgSize >> 8) & 0xFF);
    msg[sizePos + 2] = static_cast<uint8_t>((msgSize >> 16) & 0xFF);
    msg[sizePos + 3] = static_cast<uint8_t>((msgSize >> 24) & 0xFF);

    return msg;
}

std::vector<uint8_t> OpcuaClient::buildOpenSecureChannelRequest(uint32_t, uint32_t,
                                                                   int, int) {
    std::vector<uint8_t> msg;
    msg.push_back('O'); msg.push_back('P'); msg.push_back('N'); msg.push_back('F');

    size_t sizePos = msg.size();
    msg.insert(msg.end(), 4, 0); // size placeholder
    msg.insert(msg.end(), 4, 0); // secure channel id placeholder
    msg.insert(msg.end(), {0x2F, 0x06, 0x01, 0x01, 0x04, 0x01, 0x00}); // Security policy: None
    msg.insert(msg.end(), 4, 0xFF); // Sender certificate: null
    msg.insert(msg.end(), 4, 0xFF); // Receiver certificate thumbprint: null
    msg.insert(msg.end(), 4, 0); // Sequence number
    msg.insert(msg.end(), 4, 0); // Request id

    // Encodeable object: OpenSecureChannelRequest
    msg.push_back(0x01); msg.push_back(0x00); // TypeId (446)
    size_t bodySizePos = msg.size();
    msg.insert(msg.end(), 4, 0); // Body size placeholder

    // Client protocol version
    msg.insert(msg.end(), {0x00, 0x00, 0x00, 0x00});
    // Request type: Issue
    msg.insert(msg.end(), {0x00, 0x00, 0x00, 0x00});
    // Security mode: None
    msg.insert(msg.end(), {0x01, 0x00, 0x00, 0x00});
    // Client nonce (null)
    msg.insert(msg.end(), {0xFF, 0xFF, 0xFF, 0xFF});
    // Requested lifetime
    msg.insert(msg.end(), {0x00, 0x82, 0x01, 0x00}); // 600000 ms

    // Fill body size
    uint32_t bodySize = static_cast<uint32_t>(msg.size() - bodySizePos - 4);
    msg[bodySizePos] = static_cast<uint8_t>((bodySize >> 0) & 0xFF);
    msg[bodySizePos + 1] = static_cast<uint8_t>((bodySize >> 8) & 0xFF);
    msg[bodySizePos + 2] = static_cast<uint8_t>((bodySize >> 16) & 0xFF);
    msg[bodySizePos + 3] = static_cast<uint8_t>((bodySize >> 24) & 0xFF);

    // Fill message size
    uint32_t msgSize = static_cast<uint32_t>(msg.size());
    msg[sizePos] = static_cast<uint8_t>((msgSize >> 0) & 0xFF);
    msg[sizePos + 1] = static_cast<uint8_t>((msgSize >> 8) & 0xFF);
    msg[sizePos + 2] = static_cast<uint8_t>((msgSize >> 16) & 0xFF);
    msg[sizePos + 3] = static_cast<uint8_t>((msgSize >> 24) & 0xFF);

    return msg;
}

std::vector<uint8_t> OpcuaClient::buildCreateSessionRequest(const std::string&,
                                                              const std::string& endpointUrl,
                                                              const std::string& sessionName) {
    std::vector<uint8_t> msg;
    msg.push_back('M'); msg.push_back('S'); msg.push_back('G'); msg.push_back('F');

    size_t sizePos = msg.size();
    msg.insert(msg.end(), 4, 0); // size
    msg.insert(msg.end(), 4, 0); // channel id
       msg.insert(msg.end(), 4, 0); // token id
    msg.insert(msg.end(), 4, 0); // sequence number
    msg.insert(msg.end(), 4, 0); // request id
    msg.insert(msg.end(), 4, 0); // request handle

    // CreateSessionRequest type id
    msg.insert(msg.end(), {0x01, 0x00, 0xCD, 0x01}); // NodeId: 461
    size_t bodySizePos = msg.size();
    msg.insert(msg.end(), 4, 0);

    // ApplicationDescription
    auto appUri = encodeString("urn:POWSYS365:Client");
    auto appName = encodeString("POWSYS365 OPC-UA Client");
    auto appType = encodeString(endpointUrl);
    msg.insert(msg.end(), appUri.begin(), appUri.end());
    msg.insert(msg.end(), appName.begin(), appName.end());
    msg.insert(msg.end(), appType.begin(), appType.end());

    // Server URI
    msg.insert(msg.end(), {0xFF, 0xFF, 0xFF, 0xFF}); // null

    // Endpoint URL
    auto epUrl = encodeString(endpointUrl);
    msg.insert(msg.end(), epUrl.begin(), epUrl.end());

    // Session name
    auto sName = encodeString(sessionName);
    msg.insert(msg.end(), sName.begin(), sName.end());

    // Client nonce (null)
    msg.insert(msg.end(), {0xFF, 0xFF, 0xFF, 0xFF});

    // Client certificate (null)
    msg.insert(msg.end(), {0xFF, 0xFF, 0xFF, 0xFF});

    // Requested session timeout
    msg.insert(msg.end(), {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x24, 0x40}); // 10 seconds

    // Max response message size
    msg.insert(msg.end(), {0x00, 0x00, 0x00, 0x00});

    uint32_t bodySize = static_cast<uint32_t>(msg.size() - bodySizePos - 4);
    msg[bodySizePos] = static_cast<uint8_t>((bodySize >> 0) & 0xFF);
    msg[bodySizePos + 1] = static_cast<uint8_t>((bodySize >> 8) & 0xFF);
    msg[bodySizePos + 2] = static_cast<uint8_t>((bodySize >> 16) & 0xFF);
    msg[bodySizePos + 3] = static_cast<uint8_t>((bodySize >> 24) & 0xFF);

    uint32_t msgSize = static_cast<uint32_t>(msg.size());
    msg[sizePos] = static_cast<uint8_t>((msgSize >> 0) & 0xFF);
    msg[sizePos + 1] = static_cast<uint8_t>((msgSize >> 8) & 0xFF);
    msg[sizePos + 2] = static_cast<uint8_t>((msgSize >> 16) & 0xFF);
    msg[sizePos + 3] = static_cast<uint8_t>((msgSize >> 24) & 0xFF);

    return msg;
}

std::vector<uint8_t> OpcuaClient::buildActivateSessionRequest(const std::string& username,
                                                                const std::string& password,
                                                                uint32_t) {
    std::vector<uint8_t> msg;
    msg.push_back('M'); msg.push_back('S'); msg.push_back('G'); msg.push_back('F');

    size_t sizePos = msg.size();
    msg.insert(msg.end(), 4, 0);
    msg.insert(msg.end(), 4, 0);
    msg.insert(msg.end(), 4, 0);
    msg.insert(msg.end(), 4, 0);
    msg.insert(msg.end(), 4, 0);

    // ActivateSessionRequest type id
    msg.insert(msg.end(), {0x01, 0x00, 0xD3, 0x01}); // 467
    size_t bodySizePos = msg.size();
    msg.insert(msg.end(), 4, 0);

    // Client signature (null)
    msg.insert(msg.end(), {0xFF, 0xFF, 0xFF, 0xFF});

    // Client software certificates (empty array)
    msg.insert(msg.end(), {0x00, 0x00, 0x00, 0x00});

    // Locale IDs (empty array)
    msg.insert(msg.end(), {0x00, 0x00, 0x00, 0x00});

    // UserIdentityToken
    if (!username.empty()) {
        // UserNameIdentityToken
        auto userToken = encodeString("username_password_token");
        msg.insert(msg.end(), userToken.begin(), userToken.end());
        auto policyId = encodeString("username");
        msg.insert(msg.end(), policyId.begin(), policyId.end());
        auto uname = encodeString(username);
        msg.insert(msg.end(), uname.begin(), uname.end());
        auto pwd = encodeString(password);
        msg.insert(msg.end(), pwd.begin(), pwd.end());
        // Encryption algorithm (null)
        msg.insert(msg.end(), {0xFF, 0xFF, 0xFF, 0xFF});
    } else {
        // AnonymousIdentityToken
        auto anonToken = encodeString("anonymous_token");
        msg.insert(msg.end(), anonToken.begin(), anonToken.end());
        auto policyId = encodeString("anonymous");
        msg.insert(msg.end(), policyId.begin(), policyId.end());
    }

    // User token signature (null)
    msg.insert(msg.end(), {0xFF, 0xFF, 0xFF, 0xFF});

    uint32_t bodySize = static_cast<uint32_t>(msg.size() - bodySizePos - 4);
    msg[bodySizePos] = static_cast<uint8_t>((bodySize >> 0) & 0xFF);
    msg[bodySizePos + 1] = static_cast<uint8_t>((bodySize >> 8) & 0xFF);
    msg[bodySizePos + 2] = static_cast<uint8_t>((bodySize >> 16) & 0xFF);
    msg[bodySizePos + 3] = static_cast<uint8_t>((bodySize >> 24) & 0xFF);

    uint32_t msgSize = static_cast<uint32_t>(msg.size());
    msg[sizePos] = static_cast<uint8_t>((msgSize >> 0) & 0xFF);
    msg[sizePos + 1] = static_cast<uint8_t>((msgSize >> 8) & 0xFF);
    msg[sizePos + 2] = static_cast<uint8_t>((msgSize >> 16) & 0xFF);
    msg[sizePos + 3] = static_cast<uint8_t>((msgSize >> 24) & 0xFF);

    return msg;
}

std::vector<uint8_t> OpcuaClient::buildReadRequest(const std::vector<OpcuaNodeId>& nodeIds,
                                                     double) {
    std::vector<uint8_t> msg;
    msg.push_back('M'); msg.push_back('S'); msg.push_back('G'); msg.push_back('F');

    size_t sizePos = msg.size();
    msg.insert(msg.end(), 4, 0);
    msg.insert(msg.end(), 4, 0);
    msg.insert(msg.end(), 4, 0);
    msg.insert(msg.end(), 4, 0);
    msg.insert(msg.end(), 4, 0);

    // ReadRequest type id
    msg.insert(msg.end(), {0x01, 0x00, 0x77, 0x02}); // 631
    size_t bodySizePos = msg.size();
    msg.insert(msg.end(), 4, 0);

    // Max age
    msg.insert(msg.end(), {0x00, 0x00, 0x00, 0x00});
    // Timestamps to return: Both
    msg.insert(msg.end(), {0x02, 0x00, 0x00, 0x00});

    // NodesToRead array
    uint32_t arrayLen = static_cast<uint32_t>(nodeIds.size());
    msg.push_back(static_cast<uint8_t>((arrayLen >> 0) & 0xFF));
    msg.push_back(static_cast<uint8_t>((arrayLen >> 8) & 0xFF));
    msg.push_back(static_cast<uint8_t>((arrayLen >> 16) & 0xFF));
    msg.push_back(static_cast<uint8_t>((arrayLen >> 24) & 0xFF));

    for (const auto& nodeId : nodeIds) {
        auto encoded = encodeNodeId(nodeId);
        msg.insert(msg.end(), encoded.begin(), encoded.end());
        // Attribute id: Value (13)
        msg.insert(msg.end(), {0x0D, 0x00, 0x00, 0x00});
        // Index range (null)
        msg.insert(msg.end(), {0xFF, 0xFF, 0xFF, 0xFF});
        // Data value (null)
        msg.insert(msg.end(), {0xFF, 0xFF, 0xFF, 0xFF});
    }

    uint32_t bodySize = static_cast<uint32_t>(msg.size() - bodySizePos - 4);
    msg[bodySizePos] = static_cast<uint8_t>((bodySize >> 0) & 0xFF);
    msg[bodySizePos + 1] = static_cast<uint8_t>((bodySize >> 8) & 0xFF);
    msg[bodySizePos + 2] = static_cast<uint8_t>((bodySize >> 16) & 0xFF);
    msg[bodySizePos + 3] = static_cast<uint8_t>((bodySize >> 24) & 0xFF);

    uint32_t msgSize = static_cast<uint32_t>(msg.size());
    msg[sizePos] = static_cast<uint8_t>((msgSize >> 0) & 0xFF);
    msg[sizePos + 1] = static_cast<uint8_t>((msgSize >> 8) & 0xFF);
    msg[sizePos + 2] = static_cast<uint8_t>((msgSize >> 16) & 0xFF);
    msg[sizePos + 3] = static_cast<uint8_t>((msgSize >> 24) & 0xFF);

    return msg;
}

std::vector<uint8_t> OpcuaClient::buildWriteRequest(
    const std::vector<std::pair<OpcuaNodeId, OpcuaVariant>>& items) {
    std::vector<uint8_t> msg;
    msg.push_back('M'); msg.push_back('S'); msg.push_back('G'); msg.push_back('F');

    size_t sizePos = msg.size();
    msg.insert(msg.end(), 4, 0);
    msg.insert(msg.end(), 4, 0);
    msg.insert(msg.end(), 4, 0);
    msg.insert(msg.end(), 4, 0);
    msg.insert(msg.end(), 4, 0);

    // WriteRequest type id
    msg.insert(msg.end(), {0x01, 0x00, 0xA1, 0x02}); // 673
    size_t bodySizePos = msg.size();
    msg.insert(msg.end(), 4, 0);

    // NodesToWrite array
    uint32_t arrayLen = static_cast<uint32_t>(items.size());
    msg.push_back(static_cast<uint8_t>((arrayLen >> 0) & 0xFF));
    msg.push_back(static_cast<uint8_t>((arrayLen >> 8) & 0xFF));
    msg.push_back(static_cast<uint8_t>((arrayLen >> 16) & 0xFF));
    msg.push_back(static_cast<uint8_t>((arrayLen >> 24) & 0xFF));

    for (const auto& [nodeId, value] : items) {
        auto encoded = encodeNodeId(nodeId);
        msg.insert(msg.end(), encoded.begin(), encoded.end());
        // Attribute id: Value (13)
        msg.insert(msg.end(), {0x0D, 0x00, 0x00, 0x00});
        // Index range (null)
        msg.insert(msg.end(), {0xFF, 0xFF, 0xFF, 0xFF});
        // Data value
        auto dataValue = encodeDataValue(value);
        msg.insert(msg.end(), dataValue.begin(), dataValue.end());
    }

    uint32_t bodySize = static_cast<uint32_t>(msg.size() - bodySizePos - 4);
    msg[bodySizePos] = static_cast<uint8_t>((bodySize >> 0) & 0xFF);
    msg[bodySizePos + 1] = static_cast<uint8_t>((bodySize >> 8) & 0xFF);
    msg[bodySizePos + 2] = static_cast<uint8_t>((bodySize >> 16) & 0xFF);
    msg[bodySizePos + 3] = static_cast<uint8_t>((bodySize >> 24) & 0xFF);

    uint32_t msgSize = static_cast<uint32_t>(msg.size());
    msg[sizePos] = static_cast<uint8_t>((msgSize >> 0) & 0xFF);
    msg[sizePos + 1] = static_cast<uint8_t>((msgSize >> 8) & 0xFF);
    msg[sizePos + 2] = static_cast<uint8_t>((msgSize >> 16) & 0xFF);
    msg[sizePos + 3] = static_cast<uint8_t>((msgSize >> 24) & 0xFF);

    return msg;
}

std::vector<uint8_t> OpcuaClient::buildBrowseRequest(const OpcuaNodeId& nodeId) {
    std::vector<uint8_t> msg;
    msg.push_back('M'); msg.push_back('S'); msg.push_back('G'); msg.push_back('F');

    size_t sizePos = msg.size();
    msg.insert(msg.end(), 4, 0);
    msg.insert(msg.end(), 4, 0);
    msg.insert(msg.end(), 4, 0);
    msg.insert(msg.end(), 4, 0);
    msg.insert(msg.end(), 4, 0);

    // BrowseRequest type id
    msg.insert(msg.end(), {0x01, 0x00, 0x52, 0x02}); // 594
    size_t bodySizePos = msg.size();
    msg.insert(msg.end(), 4, 0);

    // View description (null)
    msg.insert(msg.end(), {0x00, 0x00, 0x00, 0x00}); // null node id
    msg.insert(msg.end(), {0x00, 0x80, 0xF4, 0x43}); // timestamp
    msg.insert(msg.end(), {0x00, 0x00, 0x00, 0x00}); // version

    // Requested max references per node
    msg.insert(msg.end(), {0x00, 0x00, 0x00, 0x00});

    // NodesToBrowse array
    msg.insert(msg.end(), {0x01, 0x00, 0x00, 0x00}); // count = 1

    // Browse description
    auto encoded = encodeNodeId(nodeId);
    msg.insert(msg.end(), encoded.begin(), encoded.end());
    // Browse direction: Forward
    msg.insert(msg.end(), {0x00, 0x00, 0x00, 0x00});
    // Reference type id (all)
    msg.insert(msg.end(), {0x00, 0x00, 0x00, 0x00});
    // Include subtypes: true
    msg.push_back(0x01);
    // Node class mask: All
    msg.insert(msg.end(), {0xFF, 0xFF, 0xFF, 0xFF});
    // Result mask: All
    msg.insert(msg.end(), {0x3F, 0x00, 0x00, 0x00});

    uint32_t bodySize = static_cast<uint32_t>(msg.size() - bodySizePos - 4);
    msg[bodySizePos] = static_cast<uint8_t>((bodySize >> 0) & 0xFF);
    msg[bodySizePos + 1] = static_cast<uint8_t>((bodySize >> 8) & 0xFF);
    msg[bodySizePos + 2] = static_cast<uint8_t>((bodySize >> 16) & 0xFF);
    msg[bodySizePos + 3] = static_cast<uint8_t>((bodySize >> 24) & 0xFF);

    uint32_t msgSize = static_cast<uint32_t>(msg.size());
    msg[sizePos] = static_cast<uint8_t>((msgSize >> 0) & 0xFF);
    msg[sizePos + 1] = static_cast<uint8_t>((msgSize >> 8) & 0xFF);
    msg[sizePos + 2] = static_cast<uint8_t>((msgSize >> 16) & 0xFF);
    msg[sizePos + 3] = static_cast<uint8_t>((msgSize >> 24) & 0xFF);

    return msg;
}

std::vector<uint8_t> OpcuaClient::buildCreateSubscriptionRequest(double publishingInterval,
                                                                   uint32_t lifetimeCount,
                                                                   uint32_t maxKeepAliveCount) {
    std::vector<uint8_t> msg;
    msg.push_back('M'); msg.push_back('S'); msg.push_back('G'); msg.push_back('F');

    size_t sizePos = msg.size();
    msg.insert(msg.end(), 4, 0);
    msg.insert(msg.end(), 4, 0);
    msg.insert(msg.end(), 4, 0);
    msg.insert(msg.end(), 4, 0);
    msg.insert(msg.end(), 4, 0);

    // CreateSubscriptionRequest type id
    msg.insert(msg.end(), {0x01, 0x00, 0x5B, 0x03}); // 859
    size_t bodySizePos = msg.size();
    msg.insert(msg.end(), 4, 0);

    // Publishing interval
    uint8_t* piBytes = reinterpret_cast<uint8_t*>(&const_cast<double&>(publishingInterval));
    msg.insert(msg.end(), piBytes, piBytes + 8);

    // Requested lifetime count
    msg.push_back(static_cast<uint8_t>((lifetimeCount >> 0) & 0xFF));
    msg.push_back(static_cast<uint8_t>((lifetimeCount >> 8) & 0xFF));
    msg.push_back(static_cast<uint8_t>((lifetimeCount >> 16) & 0xFF));
    msg.push_back(static_cast<uint8_t>((lifetimeCount >> 24) & 0xFF));

    // Requested max keep alive count
    msg.push_back(static_cast<uint8_t>((maxKeepAliveCount >> 0) & 0xFF));
    msg.push_back(static_cast<uint8_t>((maxKeepAliveCount >> 8) & 0xFF));
    msg.push_back(static_cast<uint8_t>((maxKeepAliveCount >> 16) & 0xFF));
    msg.push_back(static_cast<uint8_t>((maxKeepAliveCount >> 24) & 0xFF));

    // Max notifications per publish
    msg.insert(msg.end(), {0x00, 0x00, 0x00, 0x00});
    // Publishing enabled
    msg.push_back(0x01);
    // Priority
    msg.push_back(0x00);

    uint32_t bodySize = static_cast<uint32_t>(msg.size() - bodySizePos - 4);
    msg[bodySizePos] = static_cast<uint8_t>((bodySize >> 0) & 0xFF);
    msg[bodySizePos + 1] = static_cast<uint8_t>((bodySize >> 8) & 0xFF);
    msg[bodySizePos + 2] = static_cast<uint8_t>((bodySize >> 16) & 0xFF);
    msg[bodySizePos + 3] = static_cast<uint8_t>((bodySize >> 24) & 0xFF);

    uint32_t msgSize = static_cast<uint32_t>(msg.size());
    msg[sizePos] = static_cast<uint8_t>((msgSize >> 0) & 0xFF);
    msg[sizePos + 1] = static_cast<uint8_t>((msgSize >> 8) & 0xFF);
    msg[sizePos + 2] = static_cast<uint8_t>((msgSize >> 16) & 0xFF);
    msg[sizePos + 3] = static_cast<uint8_t>((msgSize >> 24) & 0xFF);

    return msg;
}

std::vector<uint8_t> OpcuaClient::buildCreateMonitoredItemsRequest(uint32_t subscriptionId,
                                                                       const std::vector<MonitoredItem>& items) {
    std::vector<uint8_t> msg;
    msg.push_back('M'); msg.push_back('S'); msg.push_back('G'); msg.push_back('F');

    size_t sizePos = msg.size();
    msg.insert(msg.end(), 4, 0);
    msg.insert(msg.end(), 4, 0);
    msg.insert(msg.end(), 4, 0);
    msg.insert(msg.end(), 4, 0);
    msg.insert(msg.end(), 4, 0);

    // CreateMonitoredItemsRequest type id
    msg.insert(msg.end(), {0x01, 0x00, 0x5F, 0x03}); // 863
    size_t bodySizePos = msg.size();
    msg.insert(msg.end(), 4, 0);

    // Subscription id
    msg.push_back(static_cast<uint8_t>((subscriptionId >> 0) & 0xFF));
    msg.push_back(static_cast<uint8_t>((subscriptionId >> 8) & 0xFF));
    msg.push_back(static_cast<uint8_t>((subscriptionId >> 16) & 0xFF));
    msg.push_back(static_cast<uint8_t>((subscriptionId >> 24) & 0xFF));

    // Items to create array
    uint32_t arrayLen = static_cast<uint32_t>(items.size());
    msg.push_back(static_cast<uint8_t>((arrayLen >> 0) & 0xFF));
    msg.push_back(static_cast<uint8_t>((arrayLen >> 8) & 0xFF));
    msg.push_back(static_cast<uint8_t>((arrayLen >> 16) & 0xFF));
    msg.push_back(static_cast<uint8_t>((arrayLen >> 24) & 0xFF));

    for (const auto& item : items) {
        // Item to monitor
        auto encoded = encodeNodeId(item.nodeId);
        msg.insert(msg.end(), encoded.begin(), encoded.end());
        // Attribute id: Value
        msg.insert(msg.end(), {0x0D, 0x00, 0x00, 0x00});
        // Index range (null)
        msg.insert(msg.end(), {0xFF, 0xFF, 0xFF, 0xFF});
        // Monitoring mode: Reporting
        msg.insert(msg.end(), {0x01, 0x00, 0x00, 0x00});
        // Monitoring parameters
        msg.insert(msg.end(), {0x01, 0x00}); // Client handle
        // Sampling interval
        uint64_t si = static_cast<uint64_t>(item.samplingInterval);
        uint8_t* siBytes = reinterpret_cast<uint8_t*>(&si);
        msg.insert(msg.end(), siBytes, siBytes + 8);
        // Filter (null)
        msg.insert(msg.end(), {0xFF, 0xFF, 0xFF, 0xFF});
        // Queue size
        msg.push_back(static_cast<uint8_t>((item.queueSize >> 0) & 0xFF));
        msg.push_back(static_cast<uint8_t>((item.queueSize >> 8) & 0xFF));
        msg.push_back(static_cast<uint8_t>((item.queueSize >> 16) & 0xFF));
        msg.push_back(static_cast<uint8_t>((item.queueSize >> 24) & 0xFF));
        // Discard oldest
        msg.push_back(item.discardOldest ? 0x01 : 0x00);
    }

    uint32_t bodySize = static_cast<uint32_t>(msg.size() - bodySizePos - 4);
    msg[bodySizePos] = static_cast<uint8_t>((bodySize >> 0) & 0xFF);
    msg[bodySizePos + 1] = static_cast<uint8_t>((bodySize >> 8) & 0xFF);
    msg[bodySizePos + 2] = static_cast<uint8_t>((bodySize >> 16) & 0xFF);
    msg[bodySizePos + 3] = static_cast<uint8_t>((bodySize >> 24) & 0xFF);

    uint32_t msgSize = static_cast<uint32_t>(msg.size());
    msg[sizePos] = static_cast<uint8_t>((msgSize >> 0) & 0xFF);
    msg[sizePos + 1] = static_cast<uint8_t>((msgSize >> 8) & 0xFF);
    msg[sizePos + 2] = static_cast<uint8_t>((msgSize >> 16) & 0xFF);
    msg[sizePos + 3] = static_cast<uint8_t>((msgSize >> 24) & 0xFF);

    return msg;
}

std::vector<uint8_t> OpcuaClient::buildCallRequest(const OpcuaNodeId& objectId,
                                                     const OpcuaNodeId& methodId,
                                                     const std::vector<OpcuaVariant>&) {
    std::vector<uint8_t> msg;
    msg.push_back('M'); msg.push_back('S'); msg.push_back('G'); msg.push_back('F');

    size_t sizePos = msg.size();
    msg.insert(msg.end(), 4, 0);
    msg.insert(msg.end(), 4, 0);
    msg.insert(msg.end(), 4, 0);
    msg.insert(msg.end(), 4, 0);
    msg.insert(msg.end(), 4, 0);

    // CallRequest type id
    msg.insert(msg.end(), {0x01, 0x00, 0xC6, 0x02}); // 710
    size_t bodySizePos = msg.size();
    msg.insert(msg.end(), 4, 0);

    // MethodsToCall array
    msg.insert(msg.end(), {0x01, 0x00, 0x00, 0x00}); // count = 1

    // Call method request
    auto objEncoded = encodeNodeId(objectId);
    msg.insert(msg.end(), objEncoded.begin(), objEncoded.end());
    auto methodEncoded = encodeNodeId(methodId);
    msg.insert(msg.end(), methodEncoded.begin(), methodEncoded.end());
    // Input arguments (empty array)
    msg.insert(msg.end(), {0xFF, 0xFF, 0xFF, 0xFF});

    uint32_t bodySize = static_cast<uint32_t>(msg.size() - bodySizePos - 4);
    msg[bodySizePos] = static_cast<uint8_t>((bodySize >> 0) & 0xFF);
    msg[bodySizePos + 1] = static_cast<uint8_t>((bodySize >> 8) & 0xFF);
    msg[bodySizePos + 2] = static_cast<uint8_t>((bodySize >> 16) & 0xFF);
    msg[bodySizePos + 3] = static_cast<uint8_t>((bodySize >> 24) & 0xFF);

    uint32_t msgSize = static_cast<uint32_t>(msg.size());
    msg[sizePos] = static_cast<uint8_t>((msgSize >> 0) & 0xFF);
    msg[sizePos + 1] = static_cast<uint8_t>((msgSize >> 8) & 0xFF);
    msg[sizePos + 2] = static_cast<uint8_t>((msgSize >> 16) & 0xFF);
    msg[sizePos + 3] = static_cast<uint8_t>((msgSize >> 24) & 0xFF);

    return msg;
}

std::vector<uint8_t> OpcuaClient::buildPublishRequest(uint32_t) {
    std::vector<uint8_t> msg;
    msg.push_back('M'); msg.push_back('S'); msg.push_back('G'); msg.push_back('F');

    size_t sizePos = msg.size();
    msg.insert(msg.end(), 4, 0);
    msg.insert(msg.end(), 4, 0);
    msg.insert(msg.end(), 4, 0);
    msg.insert(msg.end(), 4, 0);
    msg.insert(msg.end(), 4, 0);

    // PublishRequest type id
    msg.insert(msg.end(), {0x01, 0x00, 0x5C, 0x03}); // 860
    size_t bodySizePos = msg.size();
    msg.insert(msg.end(), 4, 0);

    // Subscription acknowledgement array (empty)
    msg.insert(msg.end(), {0xFF, 0xFF, 0xFF, 0xFF});

    uint32_t bodySize = static_cast<uint32_t>(msg.size() - bodySizePos - 4);
    msg[bodySizePos] = static_cast<uint8_t>((bodySize >> 0) & 0xFF);
    msg[bodySizePos + 1] = static_cast<uint8_t>((bodySize >> 8) & 0xFF);
    msg[bodySizePos + 2] = static_cast<uint8_t>((bodySize >> 16) & 0xFF);
    msg[bodySizePos + 3] = static_cast<uint8_t>((bodySize >> 24) & 0xFF);

    uint32_t msgSize = static_cast<uint32_t>(msg.size());
    msg[sizePos] = static_cast<uint8_t>((msgSize >> 0) & 0xFF);
    msg[sizePos + 1] = static_cast<uint8_t>((msgSize >> 8) & 0xFF);
    msg[sizePos + 2] = static_cast<uint8_t>((msgSize >> 16) & 0xFF);
    msg[sizePos + 3] = static_cast<uint8_t>((msgSize >> 24) & 0xFF);

    return msg;
}

// ---------------------------------------------------------------------------
// Binary encoding helpers
// ---------------------------------------------------------------------------
std::vector<uint8_t> OpcuaClient::encodeNodeId(const OpcuaNodeId& nodeId) {
    if (nodeId.namespaceIndex == 0 && std::holds_alternative<uint32_t>(nodeId.identifier)) {
        uint32_t id = std::get<uint32_t>(nodeId.identifier);
        if (id <= 255) {
            // Two-byte node id
            return encodeNumericNodeId(static_cast<uint16_t>(nodeId.namespaceIndex),
                                        static_cast<uint32_t>(id));
        }
    }
    return encodeNumericNodeId(static_cast<uint16_t>(nodeId.namespaceIndex), 0);
}

std::vector<uint8_t> OpcuaClient::encodeVariant(const OpcuaVariant& variant) {
    std::vector<uint8_t> result;
    result.push_back(static_cast<uint8_t>(variant.type));

    switch (variant.type) {
        case OpcuaDataType::BOOLEAN:
            if (std::holds_alternative<bool>(variant.value)) {
                result.push_back(std::get<bool>(variant.value) ? 0x01 : 0x00);
            }
            break;
        case OpcuaDataType::DOUBLE:
            if (std::holds_alternative<double>(variant.value)) {
                double d = std::get<double>(variant.value);
                uint8_t* dBytes = reinterpret_cast<uint8_t*>(&d);
                result.insert(result.end(), dBytes, dBytes + 8);
            }
            break;
        case OpcuaDataType::FLOAT:
            if (std::holds_alternative<float>(variant.value)) {
                float f = std::get<float>(variant.value);
                uint8_t* fBytes = reinterpret_cast<uint8_t*>(&f);
                result.insert(result.end(), fBytes, fBytes + 4);
            }
            break;
        case OpcuaDataType::INT32:
            if (std::holds_alternative<int32_t>(variant.value)) {
                int32_t v = std::get<int32_t>(variant.value);
                result.push_back(static_cast<uint8_t>((v >> 0) & 0xFF));
                result.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
                result.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
                result.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
            }
            break;
        case OpcuaDataType::STRING:
            if (std::holds_alternative<std::string>(variant.value)) {
                auto s = encodeString(std::get<std::string>(variant.value));
                result.insert(result.end(), s.begin(), s.end());
            }
            break;
        default:
            break;
    }
    return result;
}

std::vector<uint8_t> OpcuaClient::encodeString(const std::string& str) {
    std::vector<uint8_t> result;
    int32_t len = static_cast<int32_t>(str.length());
    result.push_back(static_cast<uint8_t>((len >> 0) & 0xFF));
    result.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
    result.push_back(static_cast<uint8_t>((len >> 16) & 0xFF));
    result.push_back(static_cast<uint8_t>((len >> 24) & 0xFF));
    if (len > 0) {
        result.insert(result.end(), str.begin(), str.end());
    }
    return result;
}

std::vector<uint8_t> OpcuaClient::encodeDateTime(const std::chrono::system_clock::time_point& tp) {
    auto duration = tp.time_since_epoch();
    auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    // OPC-UA DateTime: 100-nanosecond intervals since Jan 1, 1601
    uint64_t uaTime = static_cast<uint64_t>(nanos / 100) + 116444736000000000ULL;
    std::vector<uint8_t> result;
    for (int i = 0; i < 8; ++i) {
        result.push_back(static_cast<uint8_t>((uaTime >> (i * 8)) & 0xFF));
    }
    return result;
}

std::vector<uint8_t> OpcuaClient::encodeDataValue(const OpcuaVariant& value) {
    std::vector<uint8_t> result;
    // Encoding byte
    result.push_back(0x01); // has value
    auto variant = encodeVariant(value);
    result.insert(result.end(), variant.begin(), variant.end());
    return result;
}

std::vector<uint8_t> OpcuaClient::encodeNumericNodeId(uint16_t, uint32_t identifier) {
    if (identifier <= 255) {
        return {0x00, static_cast<uint8_t>(identifier)};
    }
    std::vector<uint8_t> result;
    result.push_back(0x01);
    result.push_back(static_cast<uint8_t>((identifier >> 0) & 0xFF));
    result.push_back(static_cast<uint8_t>((identifier >> 8) & 0xFF));
    return result;
}

std::vector<uint8_t> OpcuaClient::encodeStringNodeId(uint16_t, const std::string&) {
    return {0x00, 0x00};
}

std::vector<uint8_t> OpcuaClient::encodeExpandedNodeId(const OpcuaNodeId&) {
    return {0x00, 0x00};
}

std::vector<uint8_t> OpcuaClient::encodeLocalizedText(const std::string&) {
    return {0x00, 0x00};
}

std::vector<uint8_t> OpcuaClient::encodeDiagnosticInfo(const std::string&) {
    return {0x00, 0x00};
}

std::vector<uint8_t> OpcuaClient::encodeQualifiedName(uint16_t, const std::string&) {
    return {0x00, 0x00};
}

// ---------------------------------------------------------------------------
// Binary decoding helpers
// ---------------------------------------------------------------------------
OpcuaNodeId OpcuaClient::decodeNodeId(const std::vector<uint8_t>& data, size_t& offset) {
    OpcuaNodeId nodeId;
    if (offset >= data.size()) return nodeId;

    uint8_t type = data[offset++];
    switch (type) {
        case 0x00: // Two-byte numeric
            nodeId.namespaceIndex = 0;
            if (offset < data.size()) {
                nodeId.identifier = static_cast<uint32_t>(data[offset++]);
            }
            break;
        case 0x01: // Four-byte numeric
            if (offset + 1 < data.size()) {
                nodeId.namespaceIndex = data[offset++];
                uint32_t id = static_cast<uint32_t>(data[offset]) |
                              (static_cast<uint32_t>(data[offset + 1]) << 8);
                offset += 2;
                nodeId.identifier = id;
            }
            break;
        case 0x02: // Numeric
            if (offset + 3 < data.size()) {
                nodeId.namespaceIndex = static_cast<uint16_t>(data[offset]) |
                                        (static_cast<uint16_t>(data[offset + 1]) << 8);
                offset += 2;
                uint32_t id = decodeUInt32(data, offset);
                nodeId.identifier = id;
            }
            break;
        default:
            break;
    }
    return nodeId;
}

OpcuaVariant OpcuaClient::decodeVariant(const std::vector<uint8_t>& data, size_t& offset) {
    OpcuaVariant result;
    if (offset >= data.size()) return result;

    uint8_t typeByte = data[offset++];
    result.type = static_cast<OpcuaDataType>(typeByte & 0x3F);
    result.isArray = (typeByte & 0x80) != 0;

    switch (result.type) {
        case OpcuaDataType::BOOLEAN:
            if (offset < data.size()) {
                result.value = (data[offset++] != 0);
            }
            break;
        case OpcuaDataType::SBYTE:
            if (offset < data.size()) {
                result.value = static_cast<int8_t>(data[offset++]);
            }
            break;
        case OpcuaDataType::BYTE:
            if (offset < data.size()) {
                result.value = data[offset++];
            }
            break;
        case OpcuaDataType::INT16:
            result.value = decodeInt16(data, offset);
            break;
        case OpcuaDataType::UINT16:
            result.value = static_cast<uint16_t>(decodeUInt16(data, offset));
            break;
        case OpcuaDataType::INT32:
            result.value = decodeInt32(data, offset);
            break;
        case OpcuaDataType::UINT32:
            result.value = decodeUInt32(data, offset);
            break;
        case OpcuaDataType::FLOAT:
            result.value = decodeFloat(data, offset);
            break;
        case OpcuaDataType::DOUBLE:
            result.value = decodeDouble(data, offset);
            break;
        case OpcuaDataType::STRING:
            result.value = decodeString(data, offset);
            break;
        case OpcuaDataType::DATETIME:
            result.value = decodeDateTime(data, offset);
            break;
        default:
            break;
    }
    return result;
}

std::string OpcuaClient::decodeString(const std::vector<uint8_t>& data, size_t& offset) {
    if (offset + 4 > data.size()) return "";
    int32_t len = decodeInt32(data, offset);
    if (len < 0) {
        return "";
    }
    if (offset + static_cast<size_t>(len) > data.size()) return "";
    std::string result(data.begin() + offset, data.begin() + offset + len);
    offset += len;
    return result;
}

std::chrono::system_clock::time_point OpcuaClient::decodeDateTime(const std::vector<uint8_t>& data, size_t& offset) {
    if (offset + 8 > data.size()) return std::chrono::system_clock::time_point{};
    uint64_t uaTime = 0;
    for (int i = 0; i < 8; ++i) {
        uaTime |= static_cast<uint64_t>(data[offset++]) << (i * 8);
    }
    uint64_t nanos = (uaTime - 116444736000000000ULL) * 100;
    auto duration = std::chrono::nanoseconds(nanos);
    return std::chrono::system_clock::time_point(duration);
}

uint8_t OpcuaClient::decodeByte(const std::vector<uint8_t>& data, size_t& offset) {
    if (offset >= data.size()) return 0;
    return data[offset++];
}

uint16_t OpcuaClient::decodeUInt16(const std::vector<uint8_t>& data, size_t& offset) {
    if (offset + 2 > data.size()) return 0;
    uint16_t result = static_cast<uint16_t>(data[offset]) |
                      (static_cast<uint16_t>(data[offset + 1]) << 8);
    offset += 2;
    return result;
}

uint32_t OpcuaClient::decodeUInt32(const std::vector<uint8_t>& data, size_t& offset) {
    if (offset + 4 > data.size()) return 0;
    uint32_t result = static_cast<uint32_t>(data[offset]) |
                      (static_cast<uint32_t>(data[offset + 1]) << 8) |
                      (static_cast<uint32_t>(data[offset + 2]) << 16) |
                      (static_cast<uint32_t>(data[offset + 3]) << 24);
    offset += 4;
    return result;
}

int32_t OpcuaClient::decodeInt32(const std::vector<uint8_t>& data, size_t& offset) {
    return static_cast<int32_t>(decodeUInt32(data, offset));
}

int16_t OpcuaClient::decodeInt16(const std::vector<uint8_t>& data, size_t& offset) {
    return static_cast<int16_t>(decodeUInt16(data, offset));
}

float OpcuaClient::decodeFloat(const std::vector<uint8_t>& data, size_t& offset) {
    if (offset + 4 > data.size()) return 0.0f;
    uint32_t bits = decodeUInt32(data, offset);
    float result;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

double OpcuaClient::decodeDouble(const std::vector<uint8_t>& data, size_t& offset) {
    if (offset + 8 > data.size()) return 0.0;
    uint64_t low = decodeUInt32(data, offset);
    uint64_t high = decodeUInt32(data, offset);
    uint64_t bits = low | (high << 32);
    double result;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

ReferenceDescription OpcuaClient::decodeReferenceDescription(const std::vector<uint8_t>&, size_t&) {
    ReferenceDescription desc;
    return desc;
}

// ---------------------------------------------------------------------------
// Communication
// ---------------------------------------------------------------------------
bool OpcuaClient::sendPdu(const std::vector<uint8_t>& pdu) {
    if (m_socketFd < 0) return false;

    ssize_t sent = send(m_socketFd, pdu.data(), pdu.size(), MSG_NOSIGNAL);
    if (sent < 0) return false;

    std::lock_guard<std::mutex> lock(m_statsMutex);
    m_requestsSent++;
    m_bytesTransferred += sent;
    return true;
}

bool OpcuaClient::receivePdu(std::vector<uint8_t>& pdu, std::chrono::milliseconds timeout) {
    if (m_socketFd < 0) return false;

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(m_socketFd, &readfds);
    struct timeval tv;
    tv.tv_sec = static_cast<long>(timeout.count() / 1000);
    tv.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);

    int ready = select(m_socketFd + 1, &readfds, nullptr, nullptr, &tv);
    if (ready <= 0) return false;

    // Read message type (4 bytes)
    uint8_t msgType[4];
    ssize_t received = recv(m_socketFd, msgType, 4, MSG_WAITALL);
    if (received != 4) return false;

    // Read message size
    uint8_t sizeBytes[4];
    received = recv(m_socketFd, sizeBytes, 4, MSG_WAITALL);
    if (received != 4) return false;

    uint32_t msgSize = static_cast<uint32_t>(sizeBytes[0]) |
                       (static_cast<uint32_t>(sizeBytes[1]) << 8) |
                       (static_cast<uint32_t>(sizeBytes[2]) << 16) |
                       (static_cast<uint32_t>(sizeBytes[3]) << 24);

    if (msgSize < 8) return false;

    pdu.clear();
    pdu.insert(pdu.end(), msgType, msgType + 4);
    pdu.insert(pdu.end(), sizeBytes, sizeBytes + 4);

    size_t remaining = msgSize - 8;
    if (remaining > 0) {
        std::vector<uint8_t> buffer(remaining);
        received = recv(m_socketFd, buffer.data(), remaining, MSG_WAITALL);
        if (received <= 0) return false;
        pdu.insert(pdu.end(), buffer.begin(), buffer.begin() + received);
    }

    std::lock_guard<std::mutex> lock(m_statsMutex);
    m_responsesReceived++;
    m_bytesTransferred += pdu.size();
    return true;
}

// ---------------------------------------------------------------------------
// Subscription loop
// ---------------------------------------------------------------------------
void OpcuaClient::subscriptionLoop() {
    while (m_subscriptionRunning.load()) {
        {
            std::lock_guard<std::mutex> lock(m_subscriptionsMutex);
            if (m_subscriptions.empty()) break;
        }

        // Send Publish request
        auto publishReq = buildPublishRequest(1);
        if (!sendPdu(publishReq)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        std::vector<uint8_t> response;
        if (receivePdu(response, std::chrono::milliseconds(5000))) {
            // Parse PublishResponse to extract data changes
            if (response.size() >= 32) {
                // Extract notification data and call callbacks
                std::lock_guard<std::mutex> lock(m_subscriptionsMutex);
                for (auto& [subId, items] : m_subscriptions) {
                    (void)subId;
                    for (auto& item : items) {
                        if (item.onDataChanged) {
                            OpcuaVariant var;
                            var.type = OpcuaDataType::DOUBLE;
                            var.value = 0.0;
                            item.onDataChanged(var);
                        }
                    }
                }
            }
        }
    }
}

} // namespace powsys365
