#include "mqtt_client.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <sstream>

namespace powsys365::xtalk {

// ============================================================================
// Constructor / Destructor
// ============================================================================

MqttClient::MqttClient() = default;

MqttClient::~MqttClient() {
    disconnect();
}

// ============================================================================
// Utilidades
// ============================================================================

void MqttClient::log(const std::string& message) {
    if (!logEnabled_) return;
    std::lock_guard<std::mutex> lock(callbacksMutex_);
    if (logCallback_) {
        try { logCallback_(message); } catch (...) {}
    }
}

void MqttClient::notifyConnectionState(MqttConnectionState state) {
    std::lock_guard<std::mutex> lock(callbacksMutex_);
    if (connectionCallback_) {
        try { connectionCallback_(state); } catch (...) {}
    }
}

void MqttClient::notifyMessage(const MqttMessage& msg) {
    std::lock_guard<std::mutex> lock(callbacksMutex_);
    if (messageCallback_) {
        try { messageCallback_(msg); } catch (...) {}
    }
}

uint16_t MqttClient::getNextPacketId() {
    uint16_t id = nextPacketId_.fetch_add(1);
    if (id == 0) id = nextPacketId_.fetch_add(1);
    return id;
}

std::vector<uint8_t> MqttClient::encodeRemainingLength(size_t length) {
    std::vector<uint8_t> result;
    do {
        uint8_t byte = length % 128;
        length /= 128;
        if (length > 0) byte |= 0x80;
        result.push_back(byte);
    } while (length > 0);
    return result;
}

size_t MqttClient::decodeRemainingLength(uint8_t* data, size_t maxLen, size_t& bytesRead) {
    size_t multiplier = 1;
    size_t value = 0;
    bytesRead = 0;
    do {
        if (bytesRead >= maxLen) return 0;
        uint8_t byte = data[bytesRead++];
        value += (byte & 0x7F) * multiplier;
        if (multiplier > 128 * 128 * 128) return 0;
        multiplier *= 128;
        if ((byte & 0x80) == 0) break;
    } while (true);
    return value;
}

// ============================================================================
// Codificacion de paquetes MQTT
// ============================================================================

std::vector<uint8_t> MqttClient::encodeConnect(const MqttConnectionConfig& cfg) {
    std::vector<uint8_t> packet;
    std::vector<uint8_t> payload;

    // Protocol Name
    payload.push_back(0);
    payload.push_back(4);
    payload.push_back('M');
    payload.push_back('Q');
    payload.push_back('T');
    payload.push_back('T');

    // Protocol Level (MQTT 3.1.1)
    payload.push_back(4);

    // Connect Flags
    uint8_t connectFlags = 0x00;
    connectFlags |= 0x02; // Clean Session siempre activo
    if (!cfg.username.empty()) connectFlags |= 0x80;
    if (!cfg.password.empty()) connectFlags |= 0x40;
    if (!cfg.willTopic.empty()) {
        connectFlags |= 0x04;
        connectFlags |= (static_cast<uint8_t>(cfg.willQoS) & 0x03) << 3;
        if (cfg.willRetain) connectFlags |= 0x20;
    }
    payload.push_back(connectFlags);

    // Keep Alive
    payload.push_back(static_cast<uint8_t>(cfg.keepAliveSeconds >> 8));
    payload.push_back(static_cast<uint8_t>(cfg.keepAliveSeconds & 0xFF));

    // Client ID
    std::string clientId = cfg.clientId.empty() ? "powsys365_mqtt_" + std::to_string(getpid()) : cfg.clientId;
    payload.push_back(static_cast<uint8_t>(clientId.length() >> 8));
    payload.push_back(static_cast<uint8_t>(clientId.length() & 0xFF));
    payload.insert(payload.end(), clientId.begin(), clientId.end());

    // Will Topic & Message
    if (!cfg.willTopic.empty()) {
        payload.push_back(static_cast<uint8_t>(cfg.willTopic.length() >> 8));
        payload.push_back(static_cast<uint8_t>(cfg.willTopic.length() & 0xFF));
        payload.insert(payload.end(), cfg.willTopic.begin(), cfg.willTopic.end());
        payload.push_back(static_cast<uint8_t>(cfg.willPayload.size() >> 8));
        payload.push_back(static_cast<uint8_t>(cfg.willPayload.size() & 0xFF));
        payload.insert(payload.end(), cfg.willPayload.begin(), cfg.willPayload.end());
    }

    // Username
    if (!cfg.username.empty()) {
        payload.push_back(static_cast<uint8_t>(cfg.username.length() >> 8));
        payload.push_back(static_cast<uint8_t>(cfg.username.length() & 0xFF));
        payload.insert(payload.end(), cfg.username.begin(), cfg.username.end());
    }

    // Password
    if (!cfg.password.empty()) {
        payload.push_back(static_cast<uint8_t>(cfg.password.length() >> 8));
        payload.push_back(static_cast<uint8_t>(cfg.password.length() & 0xFF));
        payload.insert(payload.end(), cfg.password.begin(), cfg.password.end());
    }

    // Fixed Header
    packet.push_back(0x10); // CONNECT
    auto remainingLen = encodeRemainingLength(payload.size());
    packet.insert(packet.end(), remainingLen.begin(), remainingLen.end());
    packet.insert(packet.end(), payload.begin(), payload.end());

    return packet;
}

std::vector<uint8_t> MqttClient::encodePublish(const MqttMessage& msg) {
    std::vector<uint8_t> packet;
    std::vector<uint8_t> payload;

    // Topic
    payload.push_back(static_cast<uint8_t>(msg.topic.length() >> 8));
    payload.push_back(static_cast<uint8_t>(msg.topic.length() & 0xFF));
    payload.insert(payload.end(), msg.topic.begin(), msg.topic.end());

    // Packet ID para QoS > 0
    if (msg.qos != MqttQoS::AT_MOST_ONCE) {
        payload.push_back(static_cast<uint8_t>(msg.packetId >> 8));
        payload.push_back(static_cast<uint8_t>(msg.packetId & 0xFF));
    }

    // Payload
    payload.insert(payload.end(), msg.payload.begin(), msg.payload.end());

    // Fixed Header
    uint8_t fixedHeader = 0x30;
    uint8_t qos = static_cast<uint8_t>(msg.qos) & 0x03;
    fixedHeader |= (qos << 1);
    if (msg.duplicate && qos > 0) fixedHeader |= 0x08;
    if (msg.retain) fixedHeader |= 0x01;

    packet.push_back(fixedHeader);
    auto remainingLen = encodeRemainingLength(payload.size());
    packet.insert(packet.end(), remainingLen.begin(), remainingLen.end());
    packet.insert(packet.end(), payload.begin(), payload.end());

    return packet;
}

std::vector<uint8_t> MqttClient::encodePubAck(uint16_t packetId) {
    std::vector<uint8_t> packet;
    packet.push_back(0x40); // PUBACK
    packet.push_back(0x02); // Remaining length = 2
    packet.push_back(static_cast<uint8_t>(packetId >> 8));
    packet.push_back(static_cast<uint8_t>(packetId & 0xFF));
    return packet;
}

std::vector<uint8_t> MqttClient::encodePubRec(uint16_t packetId) {
    std::vector<uint8_t> packet;
    packet.push_back(0x50); // PUBREC
    packet.push_back(0x02);
    packet.push_back(static_cast<uint8_t>(packetId >> 8));
    packet.push_back(static_cast<uint8_t>(packetId & 0xFF));
    return packet;
}

std::vector<uint8_t> MqttClient::encodePubRel(uint16_t packetId) {
    std::vector<uint8_t> packet;
    packet.push_back(0x62); // PUBREL (QoS 1)
    packet.push_back(0x02);
    packet.push_back(static_cast<uint8_t>(packetId >> 8));
    packet.push_back(static_cast<uint8_t>(packetId & 0xFF));
    return packet;
}

std::vector<uint8_t> MqttClient::encodePubComp(uint16_t packetId) {
    std::vector<uint8_t> packet;
    packet.push_back(0x70); // PUBCOMP
    packet.push_back(0x02);
    packet.push_back(static_cast<uint8_t>(packetId >> 8));
    packet.push_back(static_cast<uint8_t>(packetId & 0xFF));
    return packet;
}

std::vector<uint8_t> MqttClient::encodeSubscribe(uint16_t packetId,
                                                  const std::vector<MqttTopicFilter>& filters) {
    std::vector<uint8_t> packet;
    std::vector<uint8_t> payload;

    // Packet ID
    payload.push_back(static_cast<uint8_t>(packetId >> 8));
    payload.push_back(static_cast<uint8_t>(packetId & 0xFF));

    // Topic Filters
    for (const auto& tf : filters) {
        payload.push_back(static_cast<uint8_t>(tf.filter.length() >> 8));
        payload.push_back(static_cast<uint8_t>(tf.filter.length() & 0xFF));
        payload.insert(payload.end(), tf.filter.begin(), tf.filter.end());
        payload.push_back(static_cast<uint8_t>(tf.qos) & 0x03);
    }

    // Fixed Header
    packet.push_back(0x82); // SUBSCRIBE
    auto remainingLen = encodeRemainingLength(payload.size());
    packet.insert(packet.end(), remainingLen.begin(), remainingLen.end());
    packet.insert(packet.end(), payload.begin(), payload.end());

    return packet;
}

std::vector<uint8_t> MqttClient::encodeUnsubscribe(uint16_t packetId,
                                                    const std::vector<std::string>& topics) {
    std::vector<uint8_t> packet;
    std::vector<uint8_t> payload;

    payload.push_back(static_cast<uint8_t>(packetId >> 8));
    payload.push_back(static_cast<uint8_t>(packetId & 0xFF));

    for (const auto& topic : topics) {
        payload.push_back(static_cast<uint8_t>(topic.length() >> 8));
        payload.push_back(static_cast<uint8_t>(topic.length() & 0xFF));
        payload.insert(payload.end(), topic.begin(), topic.end());
    }

    packet.push_back(0xA2); // UNSUBSCRIBE
    auto remainingLen = encodeRemainingLength(payload.size());
    packet.insert(packet.end(), remainingLen.begin(), remainingLen.end());
    packet.insert(packet.end(), payload.begin(), payload.end());

    return packet;
}

std::vector<uint8_t> MqttClient::encodePingReq() {
    return {0xC0, 0x00};
}

std::vector<uint8_t> MqttClient::encodeDisconnect() {
    return {0xE0, 0x00};
}

// ============================================================================
// Decodificacion
// ============================================================================

MqttMessage MqttClient::decodePublish(uint8_t fixedHeader, const std::vector<uint8_t>& payload) {
    MqttMessage msg;
    msg.qos = static_cast<MqttQoS>((fixedHeader >> 1) & 0x03);
    msg.retain = (fixedHeader & 0x01) != 0;
    msg.duplicate = (fixedHeader & 0x08) != 0;
    msg.timestamp = std::chrono::system_clock::now();

    size_t idx = 0;
    // Topic
    uint16_t topicLen = (static_cast<uint16_t>(payload[idx]) << 8) | payload[idx + 1];
    idx += 2;
    msg.topic = std::string(payload.begin() + idx, payload.begin() + idx + topicLen);
    idx += topicLen;

    // Packet ID para QoS > 0
    if (msg.qos != MqttQoS::AT_MOST_ONCE) {
        msg.packetId = (static_cast<uint16_t>(payload[idx]) << 8) | payload[idx + 1];
        idx += 2;
    }

    // Payload
    msg.payload.assign(payload.begin() + idx, payload.end());

    return msg;
}

uint16_t MqttClient::decodePubAck(const std::vector<uint8_t>& payload) {
    if (payload.size() < 2) return 0;
    return (static_cast<uint16_t>(payload[0]) << 8) | payload[1];
}

bool MqttClient::decodeConnAck(const std::vector<uint8_t>& payload, uint8_t& returnCode) {
    if (payload.size() < 2) return false;
    returnCode = payload[1];
    return true;
}

// ============================================================================
// TCP y Conexion
// ============================================================================

bool MqttClient::doTcpConnect() {
    std::lock_guard<std::mutex> lock(socketMutex_);

    if (socketFd_ >= 0) {
        close(socketFd_);
        socketFd_ = -1;
    }

    struct hostent* host = gethostbyname(config_.host.c_str());
    if (!host) {
        log("Error: No se pudo resolver host: " + config_.host);
        return false;
    }

    socketFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socketFd_ < 0) {
        log("Error: No se pudo crear socket");
        return false;
    }

    // Timeout
    struct timeval tv;
    tv.tv_sec = config_.connectTimeoutMs / 1000;
    tv.tv_usec = (config_.connectTimeoutMs % 1000) * 1000;
    setsockopt(socketFd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(socketFd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    // TCP_NODELAY
    int flag = 1;
    setsockopt(socketFd_, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(config_.port);
    memcpy(&serverAddr.sin_addr, host->h_addr_list[0], host->h_length);

    if (::connect(socketFd_, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        log("Error: No se pudo conectar a " + config_.host + ":" + std::to_string(config_.port));
        close(socketFd_);
        socketFd_ = -1;
        return false;
    }

    log("Conectado TCP a " + config_.host + ":" + std::to_string(config_.port));
    return true;
}

bool MqttClient::doMqttConnect() {
    auto packet = encodeConnect(config_);
    if (!sendPacket(packet)) {
        log("Error: Fallo al enviar CONNECT");
        return false;
    }

    // Esperar CONNACK
    std::vector<uint8_t> response;
    if (!readPacket(response)) {
        log("Error: No se recibio CONNACK");
        return false;
    }

    if (response.empty() || (response[0] & 0xF0) != 0x20) {
        log("Error: Paquete no es CONNACK");
        return false;
    }

    uint8_t returnCode;
    std::vector<uint8_t> connackPayload(response.begin() + 2, response.end());
    if (!decodeConnAck(connackPayload, returnCode)) {
        log("Error: CONNACK invalido");
        return false;
    }

    if (returnCode != 0) {
        log("Error: CONNACK return code = " + std::to_string(static_cast<int>(returnCode)));
        return false;
    }

    log("Conectado a broker MQTT exitosamente");
    return true;
}

bool MqttClient::sendPacket(const std::vector<uint8_t>& packet) {
    std::lock_guard<std::mutex> lock(socketMutex_);
    if (socketFd_ < 0) return false;

    size_t totalSent = 0;
    while (totalSent < packet.size()) {
        ssize_t sent = ::send(socketFd_, packet.data() + totalSent,
                             packet.size() - totalSent, 0);
        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            return false;
        }
        totalSent += sent;
    }
    return true;
}

bool MqttClient::readPacket(std::vector<uint8_t>& packet) {
    std::lock_guard<std::mutex> lock(socketMutex_);
    packet.clear();
    if (socketFd_ < 0) return false;

    // Leer fixed header (primer byte)
    uint8_t header;
    ssize_t n = recv(socketFd_, &header, 1, 0);
    if (n <= 0) return false;
    packet.push_back(header);

    // Leer remaining length
    uint8_t rlByte;
    size_t multiplier = 1;
    size_t remainingLength = 0;
    do {
        n = recv(socketFd_, &rlByte, 1, 0);
        if (n <= 0) return false;
        packet.push_back(rlByte);
        remainingLength += (rlByte & 0x7F) * multiplier;
        multiplier *= 128;
    } while ((rlByte & 0x80) != 0);

    // Leer payload
    size_t totalRead = 0;
    while (totalRead < remainingLength) {
        uint8_t buffer[1024];
        size_t toRead = std::min(remainingLength - totalRead, sizeof(buffer));
        n = recv(socketFd_, buffer, toRead, 0);
        if (n <= 0) return false;
        packet.insert(packet.end(), buffer, buffer + n);
        totalRead += n;
    }

    return true;
}

// ============================================================================
// API Publica
// ============================================================================

bool MqttClient::connect(const MqttConnectionConfig& config) {
    config_ = config;
    state_ = MqttConnectionState::CONNECTING;
    notifyConnectionState(MqttConnectionState::CONNECTING);
    log("Conectando a MQTT broker...");

    if (!doTcpConnect()) {
        state_ = MqttConnectionState::ERROR_STATE;
        notifyConnectionState(MqttConnectionState::ERROR_STATE);
        return false;
    }

    if (!doMqttConnect()) {
        state_ = MqttConnectionState::ERROR_STATE;
        notifyConnectionState(MqttConnectionState::ERROR_STATE);
        return false;
    }

    state_ = MqttConnectionState::CONNECTED;
    shouldRun_ = true;
    notifyConnectionState(MqttConnectionState::CONNECTED);

    // Iniciar threads
    receiveThread_ = std::thread(&MqttClient::receiveLoop, this);
    keepAliveThread_ = std::thread(&MqttClient::keepAliveLoop, this);

    // Re-suscribirse a topics previos si no es clean session
    if (!config_.cleanSession) {
        std::lock_guard<std::mutex> subLock(subscriptionsMutex_);
        if (!subscriptions_.empty()) {
            std::vector<MqttTopicFilter> filters;
            for (const auto& [topic, qos] : subscriptions_) {
                MqttTopicFilter tf{topic, qos};
                filters.push_back(tf);
            }
            uint16_t pktId = getNextPacketId();
            auto subPacket = encodeSubscribe(pktId, filters);
            sendPacket(subPacket);
        }
    }

    return true;
}

bool MqttClient::disconnect() {
    shouldRun_ = false;

    auto discPacket = encodeDisconnect();
    sendPacket(discPacket);

    if (receiveThread_.joinable()) receiveThread_.join();
    if (keepAliveThread_.joinable()) keepAliveThread_.join();

    std::lock_guard<std::mutex> lock(socketMutex_);
    if (socketFd_ >= 0) {
        close(socketFd_);
        socketFd_ = -1;
    }

    state_ = MqttConnectionState::DISCONNECTED;
    notifyConnectionState(MqttConnectionState::DISCONNECTED);
    log("Desconectado del broker MQTT");
    return true;
}

bool MqttClient::isConnected() const {
    return state_ == MqttConnectionState::CONNECTED;
}

MqttConnectionState MqttClient::getConnectionState() const {
    return state_;
}

bool MqttClient::publish(const std::string& topic,
                         const std::vector<uint8_t>& payload,
                         MqttQoS qos, bool retain) {
    if (state_ != MqttConnectionState::CONNECTED) return false;

    MqttMessage msg;
    msg.topic = topic;
    msg.payload = payload;
    msg.qos = qos;
    msg.retain = retain;
    msg.duplicate = false;
    msg.packetId = (qos != MqttQoS::AT_MOST_ONCE) ? getNextPacketId() : 0;

    auto packet = encodePublish(msg);
    if (!sendPacket(packet)) return false;

    messagesSent_++;

    // QoS 1: esperar PUBACK
    if (qos == MqttQoS::AT_LEAST_ONCE) {
        std::lock_guard<std::mutex> lock(outboxMutex_);
        outbox_[msg.packetId] = msg;
    }
    // QoS 2: esperar PUBREC
    if (qos == MqttQoS::EXACTLY_ONCE) {
        std::lock_guard<std::mutex> lock(outboxMutex_);
        outbox_[msg.packetId] = msg;
    }

    return true;
}

bool MqttClient::publishString(const std::string& topic,
                               const std::string& message,
                               MqttQoS qos, bool retain) {
    std::vector<uint8_t> payload(message.begin(), message.end());
    return publish(topic, payload, qos, retain);
}

bool MqttClient::publishString(const std::string& topic,
                               const std::string& message,
                               uint8_t qos, bool retain) {
    return publishString(topic, message, static_cast<MqttQoS>(qos), retain);
}

bool MqttClient::subscribe(const std::string& topicFilter, MqttQoS qos) {
    if (state_ != MqttConnectionState::CONNECTED) return false;

    MqttTopicFilter tf{topicFilter, qos};
    uint16_t pktId = getNextPacketId();
    auto packet = encodeSubscribe(pktId, {tf});

    if (!sendPacket(packet)) return false;

    std::lock_guard<std::mutex> lock(subscriptionsMutex_);
    subscriptions_[topicFilter] = qos;
    log("Suscrito a: " + topicFilter);
    return true;
}

bool MqttClient::subscribeMultiple(const std::vector<MqttTopicFilter>& filters) {
    if (state_ != MqttConnectionState::CONNECTED) return false;
    if (filters.empty()) return false;

    uint16_t pktId = getNextPacketId();
    auto packet = encodeSubscribe(pktId, filters);
    if (!sendPacket(packet)) return false;

    std::lock_guard<std::mutex> lock(subscriptionsMutex_);
    for (const auto& tf : filters) {
        subscriptions_[tf.filter] = tf.qos;
    }
    return true;
}

bool MqttClient::unsubscribe(const std::string& topicFilter) {
    if (state_ != MqttConnectionState::CONNECTED) return false;

    uint16_t pktId = getNextPacketId();
    auto packet = encodeUnsubscribe(pktId, {topicFilter});
    if (!sendPacket(packet)) return false;

    std::lock_guard<std::mutex> lock(subscriptionsMutex_);
    subscriptions_.erase(topicFilter);
    return true;
}

std::vector<std::string> MqttClient::getSubscribedTopics() const {
    std::lock_guard<std::mutex> lock(subscriptionsMutex_);
    std::vector<std::string> result;
    for (const auto& [topic, qos] : subscriptions_) {
        result.push_back(topic);
    }
    return result;
}

// ============================================================================
// Recepcion y procesamiento
// ============================================================================

void MqttClient::receiveLoop() {
    while (shouldRun_) {
        std::vector<uint8_t> packet;
        if (!readPacket(packet)) {
            if (shouldRun_) {
                state_ = MqttConnectionState::ERROR_STATE;
                notifyConnectionState(MqttConnectionState::ERROR_STATE);
                log("Error: Conexion perdida con el broker");

                // Reconexion automatica
                if (autoReconnect_) {
                    state_ = MqttConnectionState::RECONNECTING;
                    notifyConnectionState(MqttConnectionState::RECONNECTING);
                    log("Reconectando...");
                    while (shouldRun_ && (maxReconnectRetries_ < 0 || currentRetry_ < maxReconnectRetries_)) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(reconnectIntervalMs_));
                        if (doTcpConnect() && doMqttConnect()) {
                            state_ = MqttConnectionState::CONNECTED;
                            notifyConnectionState(MqttConnectionState::CONNECTED);
                            currentRetry_ = 0;
                            log("Reconexion exitosa");
                            break;
                        }
                        currentRetry_++;
                    }
                    if (state_ != MqttConnectionState::CONNECTED) {
                        shouldRun_ = false;
                    }
                } else {
                    shouldRun_ = false;
                }
            }
            break;
        }

        if (packet.empty()) continue;

        uint8_t packetType = packet[0] & 0xF0;
        size_t headerLen = 1;
        size_t rlBytes = 0;
        // Skip remaining length bytes
        while (headerLen < packet.size() && (packet[headerLen] & 0x80)) headerLen++;
        headerLen++;

        std::vector<uint8_t> payload(packet.begin() + headerLen, packet.end());

        switch (packetType) {
            case 0x30: { // PUBLISH
                auto msg = decodePublish(packet[0], payload);
                messagesReceived_++;

                // Responder segun QoS
                if (msg.qos == MqttQoS::AT_LEAST_ONCE) {
                    sendPacket(encodePubAck(msg.packetId));
                } else if (msg.qos == MqttQoS::EXACTLY_ONCE) {
                    {
                        std::lock_guard<std::mutex> lock(qos2Mutex_);
                        qos2Received_.insert(msg.packetId);
                    }
                    sendPacket(encodePubRec(msg.packetId));
                }

                notifyMessage(msg);
                break;
            }
            case 0x40: { // PUBACK
                uint16_t pktId = decodePubAck(payload);
                std::lock_guard<std::mutex> lock(outboxMutex_);
                outbox_.erase(pktId);
                break;
            }
            case 0x50: { // PUBREC
                uint16_t pktId = decodePubAck(payload);
                handlePubRec(pktId);
                break;
            }
            case 0x60: { // PUBREL
                uint16_t pktId = decodePubAck(payload);
                handlePubRel(pktId);
                break;
            }
            case 0x70: { // PUBCOMP
                uint16_t pktId = decodePubAck(payload);
                handlePubComp(pktId);
                break;
            }
            case 0xD0: { // PINGRESP
                // Keep-alive response, no action needed
                break;
            }
            default:
                break;
        }
    }
}

void MqttClient::keepAliveLoop() {
    while (shouldRun_) {
        for (int i = 0; i < config_.keepAliveSeconds * 10 && shouldRun_; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (!shouldRun_) break;

        if (state_ == MqttConnectionState::CONNECTED) {
            auto pingPacket = encodePingReq();
            if (!sendPacket(pingPacket)) {
                log("Error: Fallo PINGREQ");
            }
        }
    }
}

// ============================================================================
// QoS 2 Flow Control
// ============================================================================

void MqttClient::handlePubRec(uint16_t packetId) {
    // Recibimos PUBREC, enviar PUBREL y mover a released
    {
        std::lock_guard<std::mutex> lock(qos2Mutex_);
        qos2Released_.insert(packetId);
    }
    sendPacket(encodePubRel(packetId));
}

void MqttClient::handlePubRel(uint16_t packetId) {
    // Recibimos PUBREL, enviar PUBCOMP
    {
        std::lock_guard<std::mutex> lock(qos2Mutex_);
        qos2Received_.erase(packetId);
    }
    sendPacket(encodePubComp(packetId));
}

void MqttClient::handlePubComp(uint16_t packetId) {
    // Flujo QoS 2 completo, eliminar del outbox
    {
        std::lock_guard<std::mutex> lock(qos2Mutex_);
        qos2Released_.erase(packetId);
    }
    std::lock_guard<std::mutex> lock(outboxMutex_);
    outbox_.erase(packetId);
}

// ============================================================================
// Callbacks y Configuracion
// ============================================================================

void MqttClient::onMessage(MqttMessageCallback callback) {
    std::lock_guard<std::mutex> lock(callbacksMutex_);
    messageCallback_ = callback;
}

void MqttClient::onConnectionChange(MqttConnectionCallback callback) {
    std::lock_guard<std::mutex> lock(callbacksMutex_);
    connectionCallback_ = callback;
}

void MqttClient::onLog(MqttLogCallback callback) {
    std::lock_guard<std::mutex> lock(callbacksMutex_);
    logCallback_ = callback;
}

void MqttClient::setAutoReconnect(bool enabled, int retryIntervalMs, int maxRetries) {
    autoReconnect_ = enabled;
    reconnectIntervalMs_ = retryIntervalMs;
    maxReconnectRetries_ = maxRetries;
}

void MqttClient::setLogEnabled(bool enabled) {
    logEnabled_ = enabled;
}

// ============================================================================
// Estadisticas
// ============================================================================

size_t MqttClient::getMessagesSent() const {
    return messagesSent_;
}

size_t MqttClient::getMessagesReceived() const {
    return messagesReceived_;
}

size_t MqttClient::getPendingOutboxSize() const {
    std::lock_guard<std::mutex> lock(outboxMutex_);
    return outbox_.size();
}

} // namespace powsys365::xtalk
