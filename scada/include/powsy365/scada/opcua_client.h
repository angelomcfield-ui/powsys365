#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstdint>
#include <queue>
#include <variant>
#include <optional>

namespace powsys365 {

// ---------------------------------------------------------------------------
// OPC-UA data types
// ---------------------------------------------------------------------------
enum class OpcuaDataType : uint8_t {
    BOOLEAN = 1,
    SBYTE = 2,
    BYTE = 3,
    INT16 = 4,
    UINT16 = 5,
    INT32 = 6,
    UINT32 = 7,
    INT64 = 8,
    UINT64 = 9,
    FLOAT = 10,
    DOUBLE = 11,
    STRING = 12,
    DATETIME = 13,
    NODE_ID = 17,
    EXPANDED_NODE_ID = 18,
    STATUS_CODE = 19,
    QUALIFIED_NAME = 20,
    LOCALIZED_TEXT = 21,
    VARIANT = 24,
    DIAGNOSTIC_INFO = 25
};

// ---------------------------------------------------------------------------
// NodeId representation
// ---------------------------------------------------------------------------
struct OpcuaNodeId {
    uint16_t namespaceIndex = 0;
    std::variant<uint32_t, std::string, uint32_t/*guid*/, std::vector<uint8_t>> identifier;
    std::string toString() const;
    bool operator==(const OpcuaNodeId& other) const;
    bool operator<(const OpcuaNodeId& other) const;
};

// ---------------------------------------------------------------------------
// Variant for OPC-UA values
// ---------------------------------------------------------------------------
struct OpcuaVariant {
    OpcuaDataType type = OpcuaDataType::VARIANT;
    std::variant<
        bool,
        int8_t,
        uint8_t,
        int16_t,
        uint16_t,
        int32_t,
        uint32_t,
        int64_t,
        uint64_t,
        float,
        double,
        std::string,
        std::chrono::system_clock::time_point,
        OpcuaNodeId
    > value;
    bool isArray = false;
    uint32_t arrayLength = 0;
    uint8_t quality = 0;
    std::chrono::system_clock::time_point sourceTimestamp;
    std::chrono::system_clock::time_point serverTimestamp;

    std::string toString() const;
    template<typename T>
    std::optional<T> get() const {
        if (std::holds_alternative<T>(value)) {
            return std::get<T>(value);
        }
        return std::nullopt;
    }
};

// ---------------------------------------------------------------------------
// Reference description for namespace browsing
// ---------------------------------------------------------------------------
struct ReferenceDescription {
    OpcuaNodeId referenceTypeId;
    bool isForward = true;
    OpcuaNodeId nodeId;
    std::string browseName;
    std::string displayName;
    uint8_t nodeClass = 0; // 1=Object, 2=Variable, 4=Method, 8=ObjectType, etc.
    OpcuaNodeId typeDefinition;
};

// ---------------------------------------------------------------------------
// Monitored item for subscriptions
// ---------------------------------------------------------------------------
struct MonitoredItem {
    uint32_t clientHandle = 0;
    OpcuaNodeId nodeId;
    double samplingInterval = 1000.0; // ms
    double publishingInterval = 1000.0; // ms
    uint32_t queueSize = 1;
    bool discardOldest = true;
    double deadbandValue = 0.0;
    std::function<void(const OpcuaVariant&)> onDataChanged;
};

// ---------------------------------------------------------------------------
// OPC-UA event / alarm
// ---------------------------------------------------------------------------
struct OpcuaEvent {
    OpcuaNodeId eventType;
    std::string message;
    uint16_t severity = 0;
    std::chrono::system_clock::time_point receiveTime;
    std::chrono::system_clock::time_point time;
    std::map<std::string, OpcuaVariant> fields;
};

// ---------------------------------------------------------------------------
// Endpoint description
// ---------------------------------------------------------------------------
struct OpcuaEndpoint {
    std::string endpointUrl;
    std::string serverUri;
    std::string applicationUri;
    std::vector<std::string> userTokenPolicies;
    std::vector<std::string> securityPolicies;
    int messageSecurityMode = 1; // 1=None, 2=Sign, 3=SignAndEncrypt
};

// ---------------------------------------------------------------------------
// OPC-UA Client
// ---------------------------------------------------------------------------
class OpcuaClient {
public:
    OpcuaClient();
    ~OpcuaClient();

    // Connection lifecycle
    bool connect(const std::string& endpointUrl);
    bool connectWithCredentials(const std::string& endpointUrl,
                                 const std::string& username,
                                 const std::string& password);
    void disconnect();
    bool isConnected() const;

    // Server discovery
    std::vector<OpcuaEndpoint> discoverEndpoints(const std::string& discoveryUrl);
    std::vector<std::string> getServerNamespaces();

    // Namespace browsing
    std::vector<ReferenceDescription> browse(const OpcuaNodeId& nodeId);
    std::vector<ReferenceDescription> browseNext(const std::string& continuationPoint);
    std::optional<ReferenceDescription> getNodeAttributes(const OpcuaNodeId& nodeId);

    // Read/Write operations
    std::optional<OpcuaVariant> readNode(const OpcuaNodeId& nodeId);
    std::vector<std::optional<OpcuaVariant>> readNodes(const std::vector<OpcuaNodeId>& nodeIds);
    bool writeNode(const OpcuaNodeId& nodeId, const OpcuaVariant& value);
    bool writeNodes(const std::vector<std::pair<OpcuaNodeId, OpcuaVariant>>& items);

    // Method call
    std::vector<OpcuaVariant> callMethod(const OpcuaNodeId& objectId,
                                           const OpcuaNodeId& methodId,
                                           const std::vector<OpcuaVariant>& inputArguments);

    // Subscriptions (Monitored Items)
    uint32_t createSubscription(double publishingIntervalMs = 1000.0,
                                 uint32_t lifetimeCount = 10000,
                                 uint32_t maxKeepAliveCount = 10);
    bool deleteSubscription(uint32_t subscriptionId);
    uint32_t addMonitoredItem(uint32_t subscriptionId, const MonitoredItem& item);
    bool removeMonitoredItem(uint32_t subscriptionId, uint32_t monitoredItemId);
    bool setPublishingMode(uint32_t subscriptionId, bool enabled);

    // Event subscription
    bool subscribeToEvents(uint32_t subscriptionId, const OpcuaNodeId& emitterNode,
                            const std::vector<OpcuaNodeId>& eventTypes,
                            const std::vector<std::string>& eventFields);
    void setOnEventReceived(std::function<void(const OpcuaEvent&)> callback);

    // Historical data access
    std::vector<std::pair<std::chrono::system_clock::time_point, OpcuaVariant>>
        readHistoricalData(const OpcuaNodeId& nodeId,
                           const std::chrono::system_clock::time_point& startTime,
                           const std::chrono::system_clock::time_point& endTime);

    // Alarms and conditions
    std::vector<OpcuaEvent> getActiveAlarms(const OpcuaNodeId& conditionType);
    bool acknowledgeAlarm(const OpcuaNodeId& conditionId, const std::string& comment);
    bool confirmAlarm(const OpcuaNodeId& conditionId, const std::string& comment);
    bool enableAlarm(const OpcuaNodeId& conditionId);
    bool disableAlarm(const OpcuaNodeId& conditionId);

    // Session management
    bool activateSession(const std::string& sessionName);
    void closeSession();
    bool transferSubscriptions(const std::vector<uint32_t>& subscriptionIds);

    // Statistics
    uint64_t getRequestsSent() const;
    uint64_t getResponsesReceived() const;
    uint64_t getBytesTransferred() const;
    double getAverageLatencyMs() const;
    void resetStatistics();

private:
    // OPC-UA binary protocol helpers
    std::vector<uint8_t> buildHelloMessage(uint32_t receiveBufferSize,
                                            uint32_t sendBufferSize,
                                            uint32_t maxMessageSize,
                                            uint32_t maxChunkCount,
                                            const std::string& endpointUrl);
    std::vector<uint8_t> buildOpenSecureChannelRequest(uint32_t secureChannelId,
                                                        uint32_t tokenId,
                                                        int securityPolicy,
                                                        int messageSecurityMode);
    std::vector<uint8_t> buildCreateSessionRequest(const std::string& applicationUri,
                                                    const std::string& endpointUrl,
                                                    const std::string& sessionName);
    std::vector<uint8_t> buildActivateSessionRequest(const std::string& username,
                                                      const std::string& password,
                                                      uint32_t authenticationToken);
    std::vector<uint8_t> buildReadRequest(const std::vector<OpcuaNodeId>& nodeIds,
                                           double maxAge = 0);
    std::vector<uint8_t> buildWriteRequest(const std::vector<std::pair<OpcuaNodeId, OpcuaVariant>>& items);
    std::vector<uint8_t> buildBrowseRequest(const OpcuaNodeId& nodeId);
    std::vector<uint8_t> buildCreateSubscriptionRequest(double publishingInterval,
                                                         uint32_t lifetimeCount,
                                                         uint32_t maxKeepAliveCount);
    std::vector<uint8_t> buildCreateMonitoredItemsRequest(uint32_t subscriptionId,
                                                            const std::vector<MonitoredItem>& items);
    std::vector<uint8_t> buildDeleteMonitoredItemsRequest(uint32_t subscriptionId,
                                                            const std::vector<uint32_t>& itemIds);
    std::vector<uint8_t> buildCallRequest(const OpcuaNodeId& objectId,
                                            const OpcuaNodeId& methodId,
                                            const std::vector<OpcuaVariant>& inputArgs);
    std::vector<uint8_t> buildPublishRequest(uint32_t subscriptionId);

    // Binary encoding
    std::vector<uint8_t> encodeNodeId(const OpcuaNodeId& nodeId);
    std::vector<uint8_t> encodeVariant(const OpcuaVariant& variant);
    std::vector<uint8_t> encodeString(const std::string& str);
    std::vector<uint8_t> encodeLocalizedText(const std::string& text);
    std::vector<uint8_t> encodeDiagnosticInfo(const std::string& info);
    std::vector<uint8_t> encodeDateTime(const std::chrono::system_clock::time_point& tp);
    std::vector<uint8_t> encodeQualifiedName(uint16_t nsIndex, const std::string& name);
    std::vector<uint8_t> encodeNumericNodeId(uint16_t nsIndex, uint32_t identifier);
    std::vector<uint8_t> encodeStringNodeId(uint16_t nsIndex, const std::string& identifier);
    std::vector<uint8_t> encodeExpandedNodeId(const OpcuaNodeId& nodeId);
    std::vector<uint8_t> encodeDataValue(const OpcuaVariant& value);

    // Binary decoding
    OpcuaNodeId decodeNodeId(const std::vector<uint8_t>& data, size_t& offset);
    OpcuaVariant decodeVariant(const std::vector<uint8_t>& data, size_t& offset);
    std::string decodeString(const std::vector<uint8_t>& data, size_t& offset);
    std::chrono::system_clock::time_point decodeDateTime(const std::vector<uint8_t>& data, size_t& offset);
    uint8_t decodeByte(const std::vector<uint8_t>& data, size_t& offset);
    uint16_t decodeUInt16(const std::vector<uint8_t>& data, size_t& offset);
    uint32_t decodeUInt32(const std::vector<uint8_t>& data, size_t& offset);
    int32_t decodeInt32(const std::vector<uint8_t>& data, size_t& offset);
    int16_t decodeInt16(const std::vector<uint8_t>& data, size_t& offset);
    float decodeFloat(const std::vector<uint8_t>& data, size_t& offset);
    double decodeDouble(const std::vector<uint8_t>& data, size_t& offset);
    ReferenceDescription decodeReferenceDescription(const std::vector<uint8_t>& data, size_t& offset);

    // Communication
    bool sendPdu(const std::vector<uint8_t>& pdu);
    bool receivePdu(std::vector<uint8_t>& pdu, std::chrono::milliseconds timeout);
    bool sendReceive(const std::vector<uint8_t>& request, std::vector<uint8_t>& response,
                     std::chrono::milliseconds timeout);
    bool processChunkedResponse(std::vector<uint8_t>& fullResponse,
                                std::chrono::milliseconds timeout);

    // Subscription handler thread
    void subscriptionLoop();

    // State
    std::string m_endpointUrl;
    std::atomic<bool> m_connected{false};
    int m_socketFd = -1;
    uint32_t m_secureChannelId = 0;
    uint32_t m_tokenId = 0;
    uint32_t m_sessionAuthenticationToken = 0;
    uint32_t m_requestHandle = 1;

    // Subscriptions
    mutable std::mutex m_subscriptionsMutex;
    std::map<uint32_t, std::vector<MonitoredItem>> m_subscriptions;
    std::atomic<bool> m_subscriptionRunning{false};
    std::thread m_subscriptionThread;

    // Callbacks
    std::function<void(const OpcuaEvent&)> m_onEventReceived;

    // Statistics
    mutable std::mutex m_statsMutex;
    uint64_t m_requestsSent = 0;
    uint64_t m_responsesReceived = 0;
    uint64_t m_bytesTransferred = 0;
    std::vector<double> m_latencies;
};

} // namespace powsys365
