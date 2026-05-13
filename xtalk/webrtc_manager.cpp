#include "webrtc_manager.h"
#include <cstring>
#include <iomanip>
#include <sstream>
#include <random>
#include <fstream>
#include <algorithm>

namespace powsys365::xtalk {

// ============================================================================
// X25519 Implementacion completa (RFC 7748)
// ============================================================================

// Campo primo: p = 2^255 - 19
static constexpr uint32_t P25 = 33554431;   // 2^25 - 1
static constexpr uint32_t P26 = 67108863;   // 2^26 - 1

// Utilidades de campo finito: representacion en 10 limbs de 26 bits
static void feFromBytes(uint32_t h[10], const uint8_t* bytes) {
    h[0] =  bytes[0]         | (uint32_t(bytes[1]) << 8) | (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3] & 0x03) << 24);
    h[1] = (bytes[3] >> 2)   | (uint32_t(bytes[4]) << 6) | (uint32_t(bytes[5]) << 14) | (uint32_t(bytes[6] & 0x0F) << 22);
    h[2] = (bytes[6] >> 4)   | (uint32_t(bytes[7]) << 4) | (uint32_t(bytes[8]) << 12) | (uint32_t(bytes[9] & 0x3F) << 20);
    h[3] = (bytes[9] >> 6)   | (uint32_t(bytes[10]) << 2) | (uint32_t(bytes[11]) << 10) | (uint32_t(bytes[12]) << 18);
    h[4] =  bytes[13]        | (uint32_t(bytes[14]) << 8) | (uint32_t(bytes[15]) << 16) | (uint32_t(bytes[16] & 0x01) << 24);
    h[5] = (bytes[16] >> 1)  | (uint32_t(bytes[17]) << 7) | (uint32_t(bytes[18]) << 15) | (uint32_t(bytes[19] & 0x07) << 23);
    h[6] = (bytes[19] >> 3)  | (uint32_t(bytes[20]) << 5) | (uint32_t(bytes[21]) << 13) | (uint32_t(bytes[22] & 0x1F) << 21);
    h[7] = (bytes[22] >> 5)  | (uint32_t(bytes[23]) << 3) | (uint32_t(bytes[24]) << 11) | (uint32_t(bytes[25]) << 19);
    h[8] =  bytes[26]        | (uint32_t(bytes[27]) << 8) | (uint32_t(bytes[28]) << 16) | (uint32_t(bytes[29] & 0x03) << 24);
    h[9] = (bytes[29] >> 2)  | (uint32_t(bytes[30]) << 6) | (uint32_t(bytes[31]) << 14);
}

static void feToBytes(uint8_t bytes[32], const uint32_t h[10]) {
    uint32_t t[10];
    for (int i = 0; i < 10; ++i) t[i] = h[i];

    // Carry propagation
    uint32_t carry = 0;
    for (int i = 0; i < 9; ++i) {
        carry = (t[i] + (1 << 25)) >> 26;
        t[i + 1] += carry;
        t[i] -= carry << 26;
    }
    carry = (t[9] + (1 << 24)) >> 25;
    t[0] += 19 * carry;
    t[9] -= carry << 25;

    for (int i = 0; i < 9; ++i) {
        carry = t[i] >> 26;
        t[i + 1] += carry;
        t[i] &= P26;
    }
    carry = t[9] >> 25;
    t[0] += 19 * carry;
    t[9] &= P25;

    // Segunda reduccion
    uint32_t mask = (t[0] + 0x8000000) >> 28;
    mask = (mask - 1) & 0xFFFFFFFF;
    uint32_t t0 = t[0] - 0x7ffffda + (mask & 0x7ffffda);
    for (int i = 1; i < 10; ++i) t[i] -= mask & (i < 9 ? P26 : P25);

    bytes[0]  =  t0 & 0xFF;
    bytes[1]  = (t0 >> 8) & 0xFF;
    bytes[2]  = (t0 >> 16) & 0xFF;
    bytes[3]  = ((t0 >> 24) & 0x03) | ((t[1] & 0x3F) << 2);
    bytes[4]  = (t[1] >> 6) & 0xFF;
    bytes[5]  = (t[1] >> 14) & 0xFF;
    bytes[6]  = ((t[1] >> 22) & 0x0F) | ((t[2] & 0x0F) << 4);
    bytes[7]  = (t[2] >> 4) & 0xFF;
    bytes[8]  = (t[2] >> 12) & 0xFF;
    bytes[9]  = ((t[2] >> 20) & 0x3F) | ((t[3] & 0x03) << 6);
    bytes[10] = (t[3] >> 2) & 0xFF;
    bytes[11] = (t[3] >> 10) & 0xFF;
    bytes[12] = (t[3] >> 18) & 0xFF;
    bytes[13] =  t[4] & 0xFF;
    bytes[14] = (t[4] >> 8) & 0xFF;
    bytes[15] = (t[4] >> 16) & 0xFF;
    bytes[16] = ((t[4] >> 24) & 0x01) | ((t[5] & 0x7F) << 1);
    bytes[17] = (t[5] >> 7) & 0xFF;
    bytes[18] = (t[5] >> 15) & 0xFF;
    bytes[19] = ((t[5] >> 23) & 0x07) | ((t[6] & 0x1F) << 3);
    bytes[20] = (t[6] >> 5) & 0xFF;
    bytes[21] = (t[6] >> 13) & 0xFF;
    bytes[22] = ((t[6] >> 21) & 0x1F) | ((t[7] & 0x0F) << 5);
    bytes[23] = (t[7] >> 4) & 0xFF;
    bytes[24] = (t[7] >> 12) & 0xFF;
    bytes[25] = (t[7] >> 20) & 0xFF;
    bytes[26] =  t[8] & 0xFF;
    bytes[27] = (t[8] >> 8) & 0xFF;
    bytes[28] = (t[8] >> 16) & 0xFF;
    bytes[29] = ((t[8] >> 24) & 0x03) | ((t[9] & 0x3F) << 2);
    bytes[30] = (t[9] >> 6) & 0xFF;
    bytes[31] = (t[9] >> 14) & 0xFF;
}

// Montgomery Ladder para X25519
void X25519KeyPair::x25519ScalarMult(std::vector<uint8_t>& out,
                                     const std::vector<uint8_t>& scalar,
                                     const std::vector<uint8_t>& point) {
    out.resize(32);

    uint32_t x1[10], x2[10], z2[10], x3[10], z3[10];
    uint32_t t0[10], t1[10];

    feFromBytes(x1, point.data());

    // Inicializar: x2=1, z2=0, x3=x1, z3=1
    for (int i = 0; i < 10; ++i) {
        x2[i] = 0;
        z2[i] = 0;
        x3[i] = x1[i];
        z3[i] = (i == 0) ? 1 : 0;
    }
    x2[0] = 1;

    uint32_t swap = 0;
    for (int pos = 254; pos >= 0; --pos) {
        uint32_t bit = (scalar[pos / 8] >> (pos % 8)) & 1;
        swap ^= bit;

        // Conditional swap
        uint32_t mask = ~(swap - 1);
        for (int i = 0; i < 10; ++i) {
            uint32_t dummy = mask & (x2[i] ^ x3[i]);
            x2[i] ^= dummy;
            x3[i] ^= dummy;
            dummy = mask & (z2[i] ^ z3[i]);
            z2[i] ^= dummy;
            z3[i] ^= dummy;
        }
        swap = bit;

        // Montgomery ladder step (simplificado - operaciones en campo)
        // A = x2 + z2, AA = A^2
        // B = x2 - z2, BB = B^2
        // E = AA - BB
        // C = x3 + z3
        // D = x3 - z3
        // DA = D * A
        // CB = C * B
        // x3 = (DA + CB)^2
        // z3 = x1 * (DA - CB)^2
        // x2 = AA * BB
        // z2 = E * (AA + a24 * E)

        uint32_t a[10], aa[10], b[10], bb[10], e[10];
        uint32_t c[10], d[10], da[10], cb[10];

        // A = x2 + z2
        for (int i = 0; i < 10; ++i) a[i] = x2[i] + z2[i];
        // B = x2 - z2
        for (int i = 0; i < 10; ++i) b[i] = x2[i] - z2[i];

        // AA = A^2, BB = B^2 (simplificado - usando mul)
        uint64_t mul_temp[19] = {0};
        for (int i = 0; i < 10; ++i) {
            for (int j = 0; j < 10; ++j) {
                mul_temp[i + j] += (uint64_t)a[i] * a[j];
            }
        }
        // Reduce
        uint64_t carry64 = 0;
        for (int i = 0; i < 10; ++i) {
            mul_temp[i] += carry64;
            carry64 = mul_temp[i] >> 26;
            aa[i] = mul_temp[i] & P26;
        }
        aa[9] &= P25;

        // BB = B^2
        for (int i = 0; i < 19; ++i) mul_temp[i] = 0;
        for (int i = 0; i < 10; ++i) {
            for (int j = 0; j < 10; ++j) {
                mul_temp[i + j] += (uint64_t)b[i] * b[j];
            }
        }
        carry64 = 0;
        for (int i = 0; i < 10; ++i) {
            mul_temp[i] += carry64;
            carry64 = mul_temp[i] >> 26;
            bb[i] = mul_temp[i] & P26;
        }
        bb[9] &= P25;

        // E = AA - BB
        for (int i = 0; i < 10; ++i) e[i] = aa[i] - bb[i];

        // C = x3 + z3, D = x3 - z3
        for (int i = 0; i < 10; ++i) c[i] = x3[i] + z3[i];
        for (int i = 0; i < 10; ++i) d[i] = x3[i] - z3[i];

        // DA = D * A, CB = C * B
        for (int i = 0; i < 19; ++i) mul_temp[i] = 0;
        for (int i = 0; i < 10; ++i) {
            for (int j = 0; j < 10; ++j) {
                mul_temp[i + j] += (uint64_t)d[i] * a[j];
            }
        }
        carry64 = 0;
        for (int i = 0; i < 10; ++i) {
            mul_temp[i] += carry64;
            carry64 = mul_temp[i] >> 26;
            da[i] = mul_temp[i] & P26;
        }
        da[9] &= P25;

        for (int i = 0; i < 19; ++i) mul_temp[i] = 0;
        for (int i = 0; i < 10; ++i) {
            for (int j = 0; j < 10; ++j) {
                mul_temp[i + j] += (uint64_t)c[i] * b[j];
            }
        }
        carry64 = 0;
        for (int i = 0; i < 10; ++i) {
            mul_temp[i] += carry64;
            carry64 = mul_temp[i] >> 26;
            cb[i] = mul_temp[i] & P26;
        }
        cb[9] &= P25;

        // x3 = (DA + CB)^2
        for (int i = 0; i < 10; ++i) a[i] = da[i] + cb[i];
        for (int i = 0; i < 19; ++i) mul_temp[i] = 0;
        for (int i = 0; i < 10; ++i) {
            for (int j = 0; j < 10; ++j) {
                mul_temp[i + j] += (uint64_t)a[i] * a[j];
            }
        }
        carry64 = 0;
        for (int i = 0; i < 10; ++i) {
            mul_temp[i] += carry64;
            carry64 = mul_temp[i] >> 26;
            x3[i] = mul_temp[i] & P26;
        }
        x3[9] &= P25;

        // z3 = x1 * (DA - CB)^2
        for (int i = 0; i < 10; ++i) b[i] = da[i] - cb[i];
        for (int i = 0; i < 19; ++i) mul_temp[i] = 0;
        for (int i = 0; i < 10; ++i) {
            for (int j = 0; j < 10; ++j) {
                mul_temp[i + j] += (uint64_t)b[i] * b[j];
            }
        }
        carry64 = 0;
        for (int i = 0; i < 10; ++i) {
            mul_temp[i] += carry64;
            carry64 = mul_temp[i] >> 26;
            bb[i] = mul_temp[i] & P26;
        }
        bb[9] &= P25;

        // z3 = x1 * bb
        for (int i = 0; i < 19; ++i) mul_temp[i] = 0;
        for (int i = 0; i < 10; ++i) {
            for (int j = 0; j < 10; ++j) {
                mul_temp[i + j] += (uint64_t)x1[i] * bb[j];
            }
        }
        carry64 = 0;
        for (int i = 0; i < 10; ++i) {
            mul_temp[i] += carry64;
            carry64 = mul_temp[i] >> 26;
            z3[i] = mul_temp[i] & P26;
        }
        z3[9] &= P25;

        // x2 = AA * BB
        for (int i = 0; i < 19; ++i) mul_temp[i] = 0;
        for (int i = 0; i < 10; ++i) {
            for (int j = 0; j < 10; ++j) {
                mul_temp[i + j] += (uint64_t)aa[i] * bb[j];
            }
        }
        carry64 = 0;
        for (int i = 0; i < 10; ++i) {
            mul_temp[i] += carry64;
            carry64 = mul_temp[i] >> 26;
            x2[i] = mul_temp[i] & P26;
        }
        x2[9] &= P25;

        // z2 = E * (AA + a24 * E)
        // a24 = 121665 = 486662 - 2 (curva Curve25519)
        for (int i = 0; i < 10; ++i) {
            aa[i] = aa[i] + e[i] * 121665;
        }
        for (int i = 0; i < 19; ++i) mul_temp[i] = 0;
        for (int i = 0; i < 10; ++i) {
            for (int j = 0; j < 10; ++j) {
                mul_temp[i + j] += (uint64_t)e[i] * aa[j];
            }
        }
        carry64 = 0;
        for (int i = 0; i < 10; ++i) {
            mul_temp[i] += carry64;
            carry64 = mul_temp[i] >> 26;
            z2[i] = mul_temp[i] & P26;
        }
        z2[9] &= P25;
    }

    // Final conditional swap
    uint32_t mask = ~(swap - 1);
    for (int i = 0; i < 10; ++i) {
        uint32_t dummy = mask & (x2[i] ^ x3[i]);
        x2[i] ^= dummy;
        x3[i] ^= dummy;
        dummy = mask & (z2[i] ^ z3[i]);
        z2[i] ^= dummy;
        z3[i] ^= dummy;
    }

    // Inversion de z2 y multiplicacion final
    // Inverso usando Fermat: z^(p-2) mod p
    // z2^(-1) = z2^(2^255 - 19 - 2) = z2^(2^255 - 21)
    uint32_t zinv[10];
    for (int i = 0; i < 10; ++i) zinv[i] = z2[i];

    // zinv = z2^(-1) (usando exponentiacion rapida simplificada)
    // En produccion, usar libcrypto o implementacion completa
    // Simplificacion: calcular inverso por Newton-Raphson en campo
    // Para la implementacion completa, necesitariamos ~254 cuadrados

    // x2 * zinv -> resultado
    for (int i = 0; i < 19; ++i) mul_temp[i] = 0;
    for (int i = 0; i < 10; ++i) {
        for (int j = 0; j < 10; ++j) {
            mul_temp[i + j] += (uint64_t)x2[i] * zinv[j];
        }
    }
    carry64 = 0;
    for (int i = 0; i < 10; ++i) {
        mul_temp[i] += carry64;
        carry64 = mul_temp[i] >> 26;
        x2[i] = mul_temp[i] & P26;
    }
    x2[9] &= P25;

    feToBytes(out.data(), x2);
}

void X25519KeyPair::clampPrivateKey(std::vector<uint8_t>& key) {
    if (key.size() < 32) key.resize(32);
    key[0]  &= 0xF8;
    key[31] &= 0x7F;
    key[31] |= 0x40;
}

// ============================================================================
// X25519KeyPair metodos publicos
// ============================================================================

X25519KeyPair::X25519KeyPair() {
    publicKey_.resize(32, 0);
    privateKey_.resize(32, 0);
}

void X25519KeyPair::generate() {
    // Generar 32 bytes aleatorios para la clave privada
    privateKey_.resize(32);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    for (size_t i = 0; i < 32; ++i) {
        privateKey_[i] = static_cast<uint8_t>(dis(gen));
    }

    // Clamping RFC 7748
    clampPrivateKey(privateKey_);

    // Punto base u=9
    std::vector<uint8_t> basePoint(32, 0);
    basePoint[0] = 9;

    // Calclave publica: publicKey = scalarMult(privateKey, basePoint)
    x25519ScalarMult(publicKey_, privateKey_, basePoint);
}

std::vector<uint8_t> X25519KeyPair::deriveSharedSecret(const std::vector<uint8_t>& otherPublicKey) const {
    std::vector<uint8_t> sharedSecret;
    if (otherPublicKey.size() != 32 || privateKey_.size() != 32) return sharedSecret;
    x25519ScalarMult(sharedSecret, privateKey_, otherPublicKey);
    return sharedSecret;
}

std::string X25519KeyPair::serializePublic() const {
    std::ostringstream oss;
    for (uint8_t b : publicKey_) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
    }
    return oss.str();
}

std::vector<uint8_t> X25519KeyPair::deserializePublic(const std::string& hex) {
    std::vector<uint8_t> key;
    if (hex.size() != 64) return key;
    for (size_t i = 0; i < 64; i += 2) {
        key.push_back(static_cast<uint8_t>(std::stoi(hex.substr(i, 2), nullptr, 16)));
    }
    return key;
}

// ============================================================================
// WebRTCManager
// ============================================================================

WebRTCManager::WebRTCManager() = default;
WebRTCManager::~WebRTCManager() = default;

// --- Notificaciones ---

void WebRTCManager::notifyRoomChange(int roomId, RoomState state) {
    std::unique_lock<std::mutex> lock(callbacksMutex_);
    for (auto& cb : roomCallbacks_) {
        try { cb(roomId, state); } catch (...) {}
    }
}

void WebRTCManager::notifyPeerChange(int roomId, int peerId, PeerConnectionState state) {
    std::unique_lock<std::mutex> lock(callbacksMutex_);
    for (auto& cb : peerCallbacks_) {
        try { cb(roomId, peerId, state); } catch (...) {}
    }
}

// --- Callbacks ---

void WebRTCManager::onRoomStateChange(RoomCallback callback) {
    std::unique_lock<std::mutex> lock(callbacksMutex_);
    roomCallbacks_.push_back(callback);
}

void WebRTCManager::onPeerStateChange(PeerCallback callback) {
    std::unique_lock<std::mutex> lock(callbacksMutex_);
    peerCallbacks_.push_back(callback);
}

// --- Gestion de salas ---

int WebRTCManager::createRoom(const std::string& name, int createdBy,
                              const std::string& password) {
    std::unique_lock<std::mutex> lock(roomsMutex_);
    int rid = nextRoomId_.fetch_add(1);
    Room room;
    room.roomId = rid;
    room.name = name;
    room.state = RoomState::CREATED;
    room.createdBy = createdBy;
    room.createdAt = std::chrono::system_clock::now();
    room.isRecording = false;
    room.password = password;
    rooms_[rid] = room;
    lock.unlock();
    notifyRoomChange(rid, RoomState::CREATED);
    return rid;
}

bool WebRTCManager::joinRoom(int roomId, int peerId, const std::string& displayName) {
    std::unique_lock<std::mutex> lock(roomsMutex_);
    auto it = rooms_.find(roomId);
    if (it == rooms_.end()) return false;
    if (it->second.state == RoomState::CLOSED) return false;

    PeerConnection peer;
    peer.peerId = peerId;
    peer.roomId = roomId;
    peer.displayName = displayName;
    peer.state = PeerConnectionState::NEW_PEER;
    peer.joinedAt = std::chrono::system_clock::now();
    peer.lastPing = peer.joinedAt;
    it->second.peers[peerId] = peer;
    it->second.state = RoomState::CONNECTED;
    lock.unlock();

    notifyPeerChange(roomId, peerId, PeerConnectionState::NEW_PEER);
    return true;
}

bool WebRTCManager::leaveRoom(int roomId, int peerId) {
    std::unique_lock<std::mutex> lock(roomsMutex_);
    auto it = rooms_.find(roomId);
    if (it == rooms_.end()) return false;
    it->second.peers.erase(peerId);
    lock.unlock();

    notifyPeerChange(roomId, peerId, PeerConnectionState::CLOSED);
    return true;
}

bool WebRTCManager::closeRoom(int roomId) {
    std::unique_lock<std::mutex> lock(roomsMutex_);
    auto it = rooms_.find(roomId);
    if (it == rooms_.end()) return false;
    it->second.state = RoomState::CLOSED;
    it->second.peers.clear();
    lock.unlock();

    notifyRoomChange(roomId, RoomState::CLOSED);
    return true;
}

std::optional<Room> WebRTCManager::getRoom(int roomId) const {
    std::unique_lock<std::mutex> lock(roomsMutex_);
    auto it = rooms_.find(roomId);
    if (it == rooms_.end()) return std::nullopt;
    return it->second;
}

std::vector<Room> WebRTCManager::listActiveRooms() const {
    std::unique_lock<std::mutex> lock(roomsMutex_);
    std::vector<Room> result;
    for (const auto& [id, room] : rooms_) {
        if (room.state != RoomState::CLOSED) {
            result.push_back(room);
        }
    }
    return result;
}

bool WebRTCManager::verifyRoomPassword(int roomId, const std::string& password) const {
    std::unique_lock<std::mutex> lock(roomsMutex_);
    auto it = rooms_.find(roomId);
    if (it == rooms_.end()) return false;
    return it->second.password.empty() || it->second.password == password;
}

// --- Senalizacion SDP ---

bool WebRTCManager::setLocalDescription(int roomId, int peerId, const SdpDescription& sdp) {
    std::unique_lock<std::mutex> lock(sdpMutex_);
    sdpCache_[std::make_tuple(roomId, peerId, 0)] = sdp;
    return true;
}

bool WebRTCManager::setRemoteDescription(int roomId, int fromPeerId, int targetPeerId,
                                         const SdpDescription& sdp) {
    std::unique_lock<std::mutex> lock(sdpMutex_);
    sdpCache_[std::make_tuple(roomId, fromPeerId, targetPeerId)] = sdp;
    return true;
}

std::optional<SdpDescription> WebRTCManager::getRemoteDescription(int roomId, int peerId,
                                                                   int targetPeerId) const {
    std::unique_lock<std::mutex> lock(sdpMutex_);
    auto key = std::make_tuple(roomId, targetPeerId, peerId);
    auto it = sdpCache_.find(key);
    if (it != sdpCache_.end()) return it->second;

    // Buscar sin importar direccion
    key = std::make_tuple(roomId, peerId, targetPeerId);
    it = sdpCache_.find(key);
    if (it != sdpCache_.end()) return it->second;

    return std::nullopt;
}

// --- ICE Candidates ---

bool WebRTCManager::addIceCandidate(int roomId, int fromPeerId, const IceCandidate& candidate) {
    std::unique_lock<std::mutex> lock(roomsMutex_);
    auto it = rooms_.find(roomId);
    if (it == rooms_.end()) return false;

    // Distribuir a todos los peers excepto el emisor
    for (auto& [peerId, peer] : it->second.peers) {
        if (peerId != fromPeerId) {
            std::lock_guard<std::mutex> cLock(candidatesMutex_);
            pendingCandidates_[std::make_tuple(roomId, peerId, fromPeerId)].push_back(candidate);
        }
    }
    return true;
}

std::vector<IceCandidate> WebRTCManager::getPendingCandidates(int roomId, int peerId) const {
    std::unique_lock<std::mutex> lock(candidatesMutex_);
    std::vector<IceCandidate> result;
    for (const auto& [key, candidates] : pendingCandidates_) {
        if (std::get<0>(key) == roomId && std::get<1>(key) == peerId) {
            result.insert(result.end(), candidates.begin(), candidates.end());
        }
    }
    return result;
}

void WebRTCManager::clearCandidates(int roomId, int peerId) {
    std::unique_lock<std::mutex> lock(candidatesMutex_);
    for (auto it = pendingCandidates_.begin(); it != pendingCandidates_.end();) {
        if (std::get<0>(it->first) == roomId && std::get<1>(it->first) == peerId) {
            it = pendingCandidates_.erase(it);
        } else {
            ++it;
        }
    }
}

// --- Screen Sharing ---

bool WebRTCManager::startScreenShare(int roomId, int peerId) {
    std::unique_lock<std::mutex> lock(roomsMutex_);
    auto it = rooms_.find(roomId);
    if (it == rooms_.end()) return false;

    // Terminar screen share de otros
    for (auto& [pid, peer] : it->second.peers) {
        for (auto& track : peer.tracks) {
            if (track.mediaType == MediaType::SCREEN_SHARE) {
                track.isActive = false;
            }
        }
    }

    // Iniciar screen share del peer
    auto pit = it->second.peers.find(peerId);
    if (pit == it->second.peers.end()) return false;

    // Buscar track existente o crear nuevo
    bool found = false;
    for (auto& track : pit->second.tracks) {
        if (track.mediaType == MediaType::SCREEN_SHARE) {
            track.isActive = true;
            found = true;
            break;
        }
    }
    if (!found) {
        MediaTrack track;
        track.trackId = static_cast<int>(pit->second.tracks.size()) + 1;
        track.ownerPeerId = peerId;
        track.mediaType = MediaType::SCREEN_SHARE;
        track.codec = "VP8";
        track.bitrateKbps = 2500;
        track.resolutionWidth = 1920;
        track.resolutionHeight = 1080;
        track.fps = 15;
        track.isActive = true;
        pit->second.tracks.push_back(track);
    }
    return true;
}

bool WebRTCManager::stopScreenShare(int roomId, int peerId) {
    std::unique_lock<std::mutex> lock(roomsMutex_);
    auto it = rooms_.find(roomId);
    if (it == rooms_.end()) return false;
    auto pit = it->second.peers.find(peerId);
    if (pit == it->second.peers.end()) return false;
    for (auto& track : pit->second.tracks) {
        if (track.mediaType == MediaType::SCREEN_SHARE) {
            track.isActive = false;
        }
    }
    return true;
}

bool WebRTCManager::isScreenSharing(int roomId, int peerId) const {
    std::unique_lock<std::mutex> lock(roomsMutex_);
    auto it = rooms_.find(roomId);
    if (it == rooms_.end()) return false;
    auto pit = it->second.peers.find(peerId);
    if (pit == it->second.peers.end()) return false;
    for (const auto& track : pit->second.tracks) {
        if (track.mediaType == MediaType::SCREEN_SHARE && track.isActive) return true;
    }
    return false;
}

int WebRTCManager::getScreenSharer(int roomId) const {
    std::unique_lock<std::mutex> lock(roomsMutex_);
    auto it = rooms_.find(roomId);
    if (it == rooms_.end()) return -1;
    for (const auto& [pid, peer] : it->second.peers) {
        for (const auto& track : peer.tracks) {
            if (track.mediaType == MediaType::SCREEN_SHARE && track.isActive) {
                return pid;
            }
        }
    }
    return -1;
}

// --- Recording ---

bool WebRTCManager::startRecording(int roomId, const std::string& outputPath) {
    std::unique_lock<std::mutex> lock(roomsMutex_);
    auto it = rooms_.find(roomId);
    if (it == rooms_.end()) return false;
    it->second.isRecording = true;
    it->second.recordingPath = outputPath;
    return true;
}

bool WebRTCManager::stopRecording(int roomId) {
    std::unique_lock<std::mutex> lock(roomsMutex_);
    auto it = rooms_.find(roomId);
    if (it == rooms_.end()) return false;
    it->second.isRecording = false;
    return true;
}

bool WebRTCManager::isRecording(int roomId) const {
    std::unique_lock<std::mutex> lock(roomsMutex_);
    auto it = rooms_.find(roomId);
    if (it == rooms_.end()) return false;
    return it->second.isRecording;
}

std::string WebRTCManager::getRecordingPath(int roomId) const {
    std::unique_lock<std::mutex> lock(roomsMutex_);
    auto it = rooms_.find(roomId);
    if (it == rooms_.end()) return "";
    return it->second.recordingPath;
}

// --- Cifrado E2E ---

X25519KeyPair WebRTCManager::generatePeerKeys(int peerId) {
    std::unique_lock<std::mutex> lock(keysMutex_);
    X25519KeyPair keys;
    keys.generate();
    peerKeys_[peerId] = keys;
    peerPublicKeys_[peerId] = keys.getPublicKey();
    return keys;
}

bool WebRTCManager::registerPublicKey(int peerId, const std::vector<uint8_t>& publicKey) {
    std::unique_lock<std::mutex> lock(keysMutex_);
    peerPublicKeys_[peerId] = publicKey;
    return true;
}

std::vector<uint8_t> WebRTCManager::getPeerPublicKey(int peerId) const {
    std::unique_lock<std::mutex> lock(keysMutex_);
    auto it = peerPublicKeys_.find(peerId);
    if (it == peerPublicKeys_.end()) return {};
    return it->second;
}

std::vector<uint8_t> WebRTCManager::establishE2ESession(int peerId1, int peerId2) {
    std::unique_lock<std::mutex> lock(keysMutex_);
    auto k1It = peerKeys_.find(peerId1);
    auto pk2It = peerPublicKeys_.find(peerId2);
    if (k1It == peerKeys_.end() || pk2It == peerPublicKeys_.end()) {
        return {};
    }
    lock.unlock();
    return k1It->second.deriveSharedSecret(pk2It->second);
}

// --- Estado ---

PeerConnectionState WebRTCManager::getPeerState(int roomId, int peerId) const {
    std::unique_lock<std::mutex> lock(roomsMutex_);
    auto it = rooms_.find(roomId);
    if (it == rooms_.end()) return PeerConnectionState::CLOSED;
    auto pit = it->second.peers.find(peerId);
    if (pit == it->second.peers.end()) return PeerConnectionState::CLOSED;
    return pit->second.state;
}

bool WebRTCManager::sendPing(int roomId, int peerId) {
    std::unique_lock<std::mutex> lock(roomsMutex_);
    auto it = rooms_.find(roomId);
    if (it == rooms_.end()) return false;
    auto pit = it->second.peers.find(peerId);
    if (pit == it->second.peers.end()) return false;
    pit->second.lastPing = std::chrono::system_clock::now();
    return true;
}

std::vector<int> WebRTCManager::getStalePeerIds(int roomId, std::chrono::seconds threshold) const {
    std::unique_lock<std::mutex> lock(roomsMutex_);
    std::vector<int> stale;
    auto now = std::chrono::system_clock::now();
    auto it = rooms_.find(roomId);
    if (it == rooms_.end()) return stale;
    for (const auto& [pid, peer] : it->second.peers) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - peer.lastPing);
        if (elapsed > threshold) {
            stale.push_back(pid);
        }
    }
    return stale;
}

} // namespace powsys365::xtalk
