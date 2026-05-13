#pragma once

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <memory>
#include <atomic>
#include <chrono>
#include <functional>
#include <optional>
#include <cstdint>

namespace powsys365::xtalk {

// ============================================================================
// Estructuras WebRTC
// ============================================================================

enum class RoomState {
    CREATED,
    WAITING,
    CONNECTED,
    CLOSED,
    ERROR_STATE
};

enum class PeerConnectionState {
    NEW_PEER,
    CONNECTING,
    CONNECTED,
    DISCONNECTED,
    FAILED,
    CLOSED
};

enum class MediaType {
    AUDIO,
    VIDEO,
    SCREEN_SHARE,
    DATA_CHANNEL
};

struct MediaTrack {
    int    trackId;
    int    ownerPeerId;
    MediaType mediaType;
    std::string codec;
    int    bitrateKbps;
    int    resolutionWidth;
    int    resolutionHeight;
    int    fps;
    bool   isActive;
};

struct PeerConnection {
    int    peerId;
    int    roomId;
    std::string displayName;
    PeerConnectionState state;
    std::vector<MediaTrack> tracks;
    std::chrono::system_clock::time_point joinedAt;
    std::chrono::system_clock::time_point lastPing;
};

struct Room {
    int    roomId;
    std::string name;
    RoomState state;
    int    createdBy;
    std::chrono::system_clock::time_point createdAt;
    std::map<int, PeerConnection> peers;
    bool   isRecording;
    std::string recordingPath;
    std::string password;  // hash si esta protegida
};

// ICE Candidate
struct IceCandidate {
    std::string sdpMid;
    int         sdpMLineIndex;
    std::string candidate;
};

// Session Description Protocol
struct SdpDescription {
    std::string type;    // "offer" | "answer"
    std::string sdp;
};

// ============================================================================
// Criptografia X25519 (Signal Protocol)
// ============================================================================

class X25519KeyPair {
public:
    X25519KeyPair();

    // Generar par de claves
    void generate();

    // Getters
    const std::vector<uint8_t>& getPublicKey() const { return publicKey_; }
    const std::vector<uint8_t>& getPrivateKey() const { return privateKey_; }

    // Derivar clave compartida
    std::vector<uint8_t> deriveSharedSecret(const std::vector<uint8_t>& otherPublicKey) const;

    // Serializacion
    std::string serializePublic() const;
    static std::vector<uint8_t> deserializePublic(const std::string& hex);

private:
    std::vector<uint8_t> publicKey_;   // 32 bytes
    std::vector<uint8_t> privateKey_;  // 32 bytes

    static constexpr size_t X25519_KEY_SIZE = 32;

    // Curva X25519: clamping y operaciones
    static void clampPrivateKey(std::vector<uint8_t>& key);
    static void x25519ScalarMult(std::vector<uint8_t>& out,
                                 const std::vector<uint8_t>& scalar,
                                 const std::vector<uint8_t>& point);

    // Montgomery ladder
    static void ladderStep(uint32_t& x2, uint32_t& z2,
                           uint32_t& x3, uint32_t& z3,
                           uint32_t& swap, uint32_t& bit,
                           const uint32_t* const a24);
};

// ============================================================================
// WebRTCManager
// ============================================================================

class WebRTCManager {
public:
    WebRTCManager();
    ~WebRTCManager();

    // --- Gestion de salas ---
    int  createRoom(const std::string& name, int createdBy,
                   const std::string& password = "");
    bool joinRoom(int roomId, int peerId, const std::string& displayName);
    bool leaveRoom(int roomId, int peerId);
    bool closeRoom(int roomId);
    std::optional<Room> getRoom(int roomId) const;
    std::vector<Room> listActiveRooms() const;
    bool verifyRoomPassword(int roomId, const std::string& password) const;

    // --- Senalizacion SDP ---
    bool setLocalDescription(int roomId, int peerId, const SdpDescription& sdp);
    bool setRemoteDescription(int roomId, int peerId, int targetPeerId,
                              const SdpDescription& sdp);
    std::optional<SdpDescription> getRemoteDescription(int roomId, int peerId,
                                                         int targetPeerId) const;

    // --- ICE Candidates ---
    bool addIceCandidate(int roomId, int peerId, const IceCandidate& candidate);
    std::vector<IceCandidate> getPendingCandidates(int roomId, int peerId) const;
    void clearCandidates(int roomId, int peerId);

    // --- Screen Sharing ---
    bool startScreenShare(int roomId, int peerId);
    bool stopScreenShare(int roomId, int peerId);
    bool isScreenSharing(int roomId, int peerId) const;
    int  getScreenSharer(int roomId) const; // -1 si nadie

    // --- Recording ---
    bool startRecording(int roomId, const std::string& outputPath);
    bool stopRecording(int roomId);
    bool isRecording(int roomId) const;
    std::string getRecordingPath(int roomId) const;

    // --- Cifrado E2E ---
    X25519KeyPair generatePeerKeys(int peerId);
    bool registerPublicKey(int peerId, const std::vector<uint8_t>& publicKey);
    std::vector<uint8_t> getPeerPublicKey(int peerId) const;
    std::vector<uint8_t> establishE2ESession(int peerId1, int peerId2);

    // --- Estado ---
    PeerConnectionState getPeerState(int roomId, int peerId) const;
    bool sendPing(int roomId, int peerId);
    std::vector<int> getStalePeerIds(int roomId,
                                     std::chrono::seconds threshold = std::chrono::seconds(30)) const;

    // --- Callbacks ---
    using RoomCallback = std::function<void(int roomId, RoomState)>;
    using PeerCallback = std::function<void(int roomId, int peerId, PeerConnectionState)>;
    void onRoomStateChange(RoomCallback callback);
    void onPeerStateChange(PeerCallback callback);

private:
    mutable std::mutex roomsMutex_;
    std::map<int, Room> rooms_;

    mutable std::mutex keysMutex_;
    std::map<int, X25519KeyPair> peerKeys_;
    std::map<int, std::vector<uint8_t>> peerPublicKeys_;

    // ICE candidates pendientes por (roomId, peerId, fromPeerId)
    mutable std::mutex candidatesMutex_;
    std::map<std::tuple<int, int, int>, std::vector<IceCandidate>> pendingCandidates_;

    // SDP cache: (roomId, fromPeerId, toPeerId) -> SDP
    mutable std::mutex sdpMutex_;
    std::map<std::tuple<int, int, int>, SdpDescription> sdpCache_;

    std::atomic<int> nextRoomId_{1};

    std::mutex callbacksMutex_;
    std::vector<RoomCallback> roomCallbacks_;
    std::vector<PeerCallback> peerCallbacks_;

    void notifyRoomChange(int roomId, RoomState state);
    void notifyPeerChange(int roomId, int peerId, PeerConnectionState state);
};

} // namespace powsys365::xtalk
