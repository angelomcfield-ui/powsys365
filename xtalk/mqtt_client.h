#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <mutex>
#include <atomic>
#include <thread>
#include <queue>
#include <condition_variable>
#include <chrono>
#include <optional>
#include <cstdint>

namespace powsys365::xtalk {

// ============================================================================
// Tipos MQTT
// ============================================================================

enum class MqttQoS : uint8_t {
    AT_MOST_ONCE  = 0,  // QoS 0: Fire and forget
    AT_LEAST_ONCE = 1,  // QoS 1: Entrega confirmada
    EXACTLY_ONCE  = 2   // QoS 2: Entrega exactamente una vez
};

enum class MqttConnectionState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    RECONNECTING,
    ERROR_STATE
};

// Paquetes MQTT
enum class MqttPacketType : uint8_t {
    CONNECT     = 1,
    CONNACK     = 2,
    PUBLISH     = 3,
    PUBACK      = 4,
    PUBREC      = 5,
    PUBREL      = 6,
    PUBCOMP     = 7,
    SUBSCRIBE   = 8,
    SUBACK      = 9,
    UNSUBSCRIBE = 10,
    UNSUBACK    = 11,
    PINGREQ     = 12,
    PINGRESP    = 13,
    DISCONNECT  = 14
};

struct MqttMessage {
    std::string topic;
    std::vector<uint8_t> payload;
    MqttQoS qos;
    bool retain;
    bool duplicate;
    uint16_t packetId;
    std::chrono::system_clock::time_point timestamp;
};

struct MqttTopicFilter {
    std::string filter;
    MqttQoS qos;
};

struct MqttConnectionConfig {
    std::string host;
    uint16_t port = 1883;
    std::string clientId;
    std::string username;
    std::string password;
    int keepAliveSeconds = 60;
    bool cleanSession = true;
    int connectTimeoutMs = 10000;
    std::string willTopic;
    std::vector<uint8_t> willPayload;
    MqttQoS willQoS = MqttQoS::AT_MOST_ONCE;
    bool willRetain = false;
};

// ============================================================================
// Callbacks
// ============================================================================

using MqttMessageCallback = std::function<void(const MqttMessage&)>;
using MqttConnectionCallback = std::function<void(MqttConnectionState)>;
using MqttLogCallback = std::function<void(const std::string&)>;

// ============================================================================
// MqttClient
// ============================================================================

class MqttClient {
public:
    MqttClient();
    ~MqttClient();

    // --- Conexion ---
    bool connect(const MqttConnectionConfig& config);
    bool disconnect();
    bool isConnected() const;
    MqttConnectionState getConnectionState() const;

    // --- Publicacion ---
    bool publish(const std::string& topic,
                 const std::vector<uint8_t>& payload,
                 MqttQoS qos = MqttQoS::AT_MOST_ONCE,
                 bool retain = false);
    bool publishString(const std::string& topic,
                       const std::string& message,
                       MqttQoS qos = MqttQoS::AT_MOST_ONCE,
                       bool retain = false);
    bool publishString(const std::string& topic,
                       const std::string& message,
                       uint8_t qos,
                       bool retain = false);

    // --- Subscripcion ---
    bool subscribe(const std::string& topicFilter, MqttQoS qos = MqttQoS::AT_LEAST_ONCE);
    bool subscribeMultiple(const std::vector<MqttTopicFilter>& filters);
    bool unsubscribe(const std::string& topicFilter);
    std::vector<std::string> getSubscribedTopics() const;

    // --- Callbacks ---
    void onMessage(MqttMessageCallback callback);
    void onConnectionChange(MqttConnectionCallback callback);
    void onLog(MqttLogCallback callback);

    // --- Reconexion automatica ---
    void setAutoReconnect(bool enabled, int retryIntervalMs = 5000, int maxRetries = -1);

    // --- Configuracion ---
    void setLogEnabled(bool enabled);

    // --- Estadisticas ---
    size_t getMessagesSent() const;
    size_t getMessagesReceived() const;
    size_t getPendingOutboxSize() const;

private:
    MqttConnectionConfig config_;
    std::atomic<MqttConnectionState> state_{MqttConnectionState::DISCONNECTED};
    std::atomic<bool> shouldRun_{false};
    std::atomic<bool> autoReconnect_{false};
    int reconnectIntervalMs_ = 5000;
    int maxReconnectRetries_ = -1;
    int currentRetry_ = 0;

    // Socket
    int socketFd_ = -1;
    std::mutex socketMutex_;

    // Packet IDs
    std::atomic<uint16_t> nextPacketId_{1};

    // Subscripciones
    mutable std::mutex subscriptionsMutex_;
    std::map<std::string, MqttQoS> subscriptions_;

    // Outbox (mensajes pendientes de confirmacion QoS 1 y 2)
    mutable std::mutex outboxMutex_;
    std::map<uint16_t, MqttMessage> outbox_;

    // Callbacks
    mutable std::mutex callbacksMutex_;
    MqttMessageCallback messageCallback_;
    MqttConnectionCallback connectionCallback_;
    MqttLogCallback logCallback_;

    // Threading
    std::thread receiveThread_;
    std::thread keepAliveThread_;

    // Estadisticas
    std::atomic<size_t> messagesSent_{0};
    std::atomic<size_t> messagesReceived_{0};
    std::atomic<bool> logEnabled_{true};

    // --- Metodos internos ---
    bool doTcpConnect();
    bool doMqttConnect();
    void doDisconnect();
    void receiveLoop();
    void keepAliveLoop();
    void processIncomingPacket(uint8_t* data, size_t len);

    // --- Codificacion/Decodificacion de paquetes ---
    std::vector<uint8_t> encodeConnect(const MqttConnectionConfig& cfg);
    std::vector<uint8_t> encodePublish(const MqttMessage& msg);
    std::vector<uint8_t> encodePubAck(uint16_t packetId);
    std::vector<uint8_t> encodePubRec(uint16_t packetId);
    std::vector<uint8_t> encodePubRel(uint16_t packetId);
    std::vector<uint8_t> encodePubComp(uint16_t packetId);
    std::vector<uint8_t> encodeSubscribe(uint16_t packetId,
                                          const std::vector<MqttTopicFilter>& filters);
    std::vector<uint8_t> encodeUnsubscribe(uint16_t packetId,
                                            const std::vector<std::string>& topics);
    std::vector<uint8_t> encodePingReq();
    std::vector<uint8_t> encodeDisconnect();

    // Decodificacion
    bool decodeConnAck(const std::vector<uint8_t>& payload, uint8_t& returnCode);
    MqttMessage decodePublish(uint8_t fixedHeader, const std::vector<uint8_t>& payload);
    uint16_t decodePubAck(const std::vector<uint8_t>& payload);

    // --- Utilidades ---
    uint16_t getNextPacketId();
    bool sendPacket(const std::vector<uint8_t>& packet);
    bool readPacket(std::vector<uint8_t>& packet);
    std::vector<uint8_t> encodeRemainingLength(size_t length);
    size_t decodeRemainingLength(uint8_t* data, size_t maxLen, size_t& bytesRead);
    void log(const std::string& message);
    void notifyConnectionState(MqttConnectionState state);
    void notifyMessage(const MqttMessage& msg);

    // QoS 2 flow control
    void handlePubRec(uint16_t packetId);
    void handlePubRel(uint16_t packetId);
    void handlePubComp(uint16_t packetId);
    std::mutex qos2Mutex_;
    std::set<uint16_t> qos2Received_;  // PacketIds recibidos en PUBREC
    std::set<uint16_t> qos2Released_;  // PacketIds en PUBREL
};

} // namespace powsys365::xtalk
