#include "powsy365/scada/dnp3_client.h"
#include <iostream>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <netdb.h>
#include <cmath>
#include <chrono>
#include <sstream>

namespace powsys365 {

// ============================================================================
// Dnp3Client
// ============================================================================
Dnp3Client::Dnp3Client() = default;

Dnp3Client::~Dnp3Client() {
    disconnect();
}

// ---------------------------------------------------------------------------
// Connection lifecycle
// ---------------------------------------------------------------------------
bool Dnp3Client::connect(const Dnp3Endpoint& endpoint) {
    if (m_connected.load()) return true;

    m_endpoint = endpoint;

    if (endpoint.isSerial) {
        // Serial connection (RTU)
        m_fd = ::open(endpoint.address.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
        if (m_fd < 0) return false;

        if (!configureSerialPort()) {
            ::close(m_fd);
            m_fd = -1;
            return false;
        }
    } else {
        // TCP connection
        m_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (m_fd < 0) return false;

        struct hostent* server = gethostbyname(endpoint.address.c_str());
        if (!server) {
            close(m_fd);
            m_fd = -1;
            return false;
        }

        struct sockaddr_in servAddr;
        std::memset(&servAddr, 0, sizeof(servAddr));
        servAddr.sin_family = AF_INET;
        servAddr.sin_port = htons(endpoint.port);
        std::memcpy(&servAddr.sin_addr.s_addr, server->h_addr, server->h_length);

        // Set connect timeout
        int flags = fcntl(m_fd, F_GETFL, 0);
        fcntl(m_fd, F_SETFL, flags | O_NONBLOCK);

        int result = ::connect(m_fd, (struct sockaddr*)&servAddr, sizeof(servAddr));
        if (result < 0 && errno != EINPROGRESS) {
            close(m_fd);
            m_fd = -1;
            return false;
        }

        fd_set fdset;
        FD_ZERO(&fdset);
        FD_SET(m_fd, &fdset);
        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;

        result = select(m_fd + 1, nullptr, &fdset, nullptr, &tv);
        if (result <= 0) {
            close(m_fd);
            m_fd = -1;
            return false;
        }

        int soError;
        socklen_t len = sizeof(soError);
        getsockopt(m_fd, SOL_SOCKET, SO_ERROR, &soError, &len);
        if (soError != 0) {
            close(m_fd);
            m_fd = -1;
            return false;
        }

        fcntl(m_fd, F_SETFL, flags);
    }

    m_connected.store(true);
    m_running.store(true);
    m_transportSeq = 0;
    m_appSeq = 0;

    // Start unsolicited listener thread
    m_unsolicitedThread = std::thread(&Dnp3Client::unsolicitedListenerLoop, this);

    // Perform link reset
    if (!performLinkReset()) {
        disconnect();
        return false;
    }

    return true;
}

void Dnp3Client::disconnect() {
    m_running.store(false);
    m_connected.store(false);

    if (m_unsolicitedThread.joinable()) {
        m_unsolicitedThread.join();
    }

    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

bool Dnp3Client::isConnected() const {
    return m_connected.load();
}

// ---------------------------------------------------------------------------
// Serial configuration
// ---------------------------------------------------------------------------
bool Dnp3Client::configureSerialPort() {
    struct termios tty;
    if (tcgetattr(m_fd, &tty) != 0) return false;

    // Set baud rate
    speed_t baud;
    switch (m_endpoint.baudRate) {
        case 1200: baud = B1200; break;
        case 2400: baud = B2400; break;
        case 4800: baud = B4800; break;
        case 9600: baud = B9600; break;
        case 19200: baud = B19200; break;
        case 38400: baud = B38400; break;
        case 57600: baud = B57600; break;
        case 115200: baud = B115200; break;
        default: baud = B9600; break;
    }
    cfsetospeed(&tty, baud);
    cfsetispeed(&tty, baud);

    // 8 data bits
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;

    // Stop bits
    if (m_endpoint.stopBits == 2) {
        tty.c_cflag |= CSTOPB;
    } else {
        tty.c_cflag &= ~CSTOPB;
    }

    // Parity
    if (m_endpoint.parity == "EVEN") {
        tty.c_cflag |= PARENB;
        tty.c_cflag &= ~PARODD;
    } else if (m_endpoint.parity == "ODD") {
        tty.c_cflag |= PARENB;
        tty.c_cflag |= PARODD;
    } else {
        tty.c_cflag &= ~PARENB;
    }

    tty.c_cflag |= CREAD | CLOCAL; // Enable receiver, ignore modem control
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG); // Raw mode
    tty.c_iflag &= ~(IXON | IXOFF | IXANY); // No software flow control
    tty.c_oflag &= ~OPOST; // Raw output

    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 5; // 0.5 second timeout

    return tcsetattr(m_fd, TCSANOW, &tty) == 0;
}

// ---------------------------------------------------------------------------
// CRC calculation using DNP3 lookup table
// ---------------------------------------------------------------------------
uint16_t Dnp3Client::calculateCrc(const std::vector<uint8_t>& data, size_t offset, size_t len) {
    uint16_t crc = 0;
    for (size_t i = 0; i < len; ++i) {
        uint8_t pos = (crc >> 8) ^ data[offset + i];
        crc = (crc << 8) ^ CRC_TABLE[pos];
    }
    // Final XOR with 0x0000 (DNP3 uses CRC-16-ANSI)
    return ~crc;
}

uint16_t Dnp3Client::calculateCrc(const uint8_t* data, size_t len) {
    uint16_t crc = 0;
    for (size_t i = 0; i < len; ++i) {
        uint8_t pos = (crc >> 8) ^ data[i];
        crc = (crc << 8) ^ CRC_TABLE[pos];
    }
    return ~crc;
}

bool Dnp3Client::validateFrameCrc(const std::vector<uint8_t>& frame) {
    if (frame.size() < 8) return false;
    // Header CRC: bytes 0-7 (first 8 bytes), CRC at bytes 8-9
    uint16_t headerCrc = (static_cast<uint16_t>(frame[8]) << 8) | frame[9];
    uint16_t calcHeaderCrc = calculateCrc(frame.data(), 0, 8);
    if (headerCrc != calcHeaderCrc) return false;

    // Validate block CRCs for payload blocks
    for (size_t blockStart = 10; blockStart + 18 <= frame.size(); blockStart += 18) {
        uint16_t blockCrc = (static_cast<uint16_t>(frame[blockStart + 16]) << 8) | frame[blockStart + 17];
        uint16_t calcBlockCrc = calculateCrc(frame.data(), blockStart, 16);
        if (blockCrc != calcBlockCrc) return false;
    }
    return true;
}

std::vector<uint8_t> Dnp3Client::appendCrcChunks(const std::vector<uint8_t>& userData) {
    std::vector<uint8_t> result;
    size_t offset = 0;

    while (offset < userData.size()) {
        size_t chunkLen = std::min(size_t(16), userData.size() - offset);
        result.insert(result.end(), userData.begin() + offset, userData.begin() + offset + chunkLen);

        // Pad chunk to 16 bytes with 0x00
        if (chunkLen < 16) {
            result.insert(result.end(), 16 - chunkLen, 0x00);
        }

        uint16_t crc = calculateCrc(userData.data(), offset, chunkLen);
        result.push_back(static_cast<uint8_t>(crc & 0xFF));
        result.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));

        offset += chunkLen;
    }

    return result;
}

// ---------------------------------------------------------------------------
// Link layer
// ---------------------------------------------------------------------------
std::vector<uint8_t> Dnp3Client::buildLinkFrame(uint8_t ctrl, uint16_t dest,
                                                  uint16_t src, const std::vector<uint8_t>& userData) {
    std::vector<uint8_t> frame;
    frame.push_back(0x05); // Start bytes
    frame.push_back(0x64);

    uint8_t lenByte = static_cast<uint8_t>(5 + (userData.empty() ? 0 : (userData.size() + 1) / 16 * 18 + 10));
    frame.push_back(lenByte); // Length
    frame.push_back(ctrl);
    frame.push_back(static_cast<uint8_t>(dest & 0xFF));
    frame.push_back(static_cast<uint8_t>((dest >> 8) & 0xFF));
    frame.push_back(static_cast<uint8_t>(src & 0xFF));
    frame.push_back(static_cast<uint8_t>((src >> 8) & 0xFF));

    // Header CRC
    uint16_t headerCrc = calculateCrc(frame.data(), 0, 8);
    frame.push_back(static_cast<uint8_t>(headerCrc & 0xFF));
    frame.push_back(static_cast<uint8_t>((headerCrc >> 8) & 0xFF));

    // User data with CRC blocks
    if (!userData.empty()) {
        auto crcData = appendCrcChunks(userData);
        frame.insert(frame.end(), crcData.begin(), crcData.end());
    }

    return frame;
}

bool Dnp3Client::performLinkReset() {
    auto frame = buildLinkFrame(LINK_PRI_RESET_USER,
                                 static_cast<uint16_t>(m_endpoint.destinationAddress),
                                 static_cast<uint16_t>(m_endpoint.sourceAddress),
                                 {});

    for (int retry = 0; retry < m_linkRetries; ++retry) {
        if (!sendFrame(frame)) return false;

        std::vector<uint8_t> response;
        if (receiveFrame(response, m_appTimeout)) {
            if (response.size() >= 5 && response[3] == LINK_SEC_ACK) {
                return true;
            }
        }
    }
    return false;
}

bool Dnp3Client::testLinkStatus() {
    auto frame = buildLinkFrame(LINK_PRI_TEST_LINK,
                                 static_cast<uint16_t>(m_endpoint.destinationAddress),
                                 static_cast<uint16_t>(m_endpoint.sourceAddress),
                                 {});
    if (!sendFrame(frame)) return false;

    std::vector<uint8_t> response;
    return receiveFrame(response, m_appTimeout);
}

bool Dnp3Client::confirmLinkFrame(uint16_t destination) {
    uint8_t ctrl = 0x00; // Secondary ACK
    auto frame = buildLinkFrame(ctrl, destination,
                                 static_cast<uint16_t>(m_endpoint.sourceAddress), {});
    return sendFrame(frame);
}

// ---------------------------------------------------------------------------
// Transport layer
// ---------------------------------------------------------------------------
std::vector<uint8_t> Dnp3Client::buildTransportSegment(const std::vector<uint8_t>& appData,
                                                         bool first, bool final, int sequence) {
    std::vector<uint8_t> segments;
    size_t offset = 0;
    int seq = sequence & 0x3F;

    while (offset < appData.size()) {
        size_t chunkSize = std::min(size_t(249), appData.size() - offset);

        uint8_t th = static_cast<uint8_t>(seq & 0x3F);
        if (offset == 0 && first) th |= 0x40; // FIR
        if (offset + chunkSize >= appData.size() && final) th |= 0x80; // FIN
        if (offset > 0) th &= ~0x40; // Not FIR for subsequent

        std::vector<uint8_t> segment;
        segment.push_back(th);
        segment.insert(segment.end(), appData.begin() + offset, appData.begin() + offset + chunkSize);
        segments.insert(segments.end(), segment.begin(), segment.end());

        offset += chunkSize;
        seq = (seq + 1) & 0x3F;
    }

    return segments;
}

// ---------------------------------------------------------------------------
// Application layer
// ---------------------------------------------------------------------------
std::vector<uint8_t> Dnp3Client::buildApplicationRequest(uint8_t funcCode,
                                                           const std::vector<uint8_t>& objects) {
    std::vector<uint8_t> appPdu;

    // Application control
    uint8_t appCtrl = static_cast<uint8_t>((m_appSeq & 0x0F) << 4);
    m_appSeq = (m_appSeq + 1) & 0x0F;
    appPdu.push_back(appCtrl);
    appPdu.push_back(funcCode);
    appPdu.insert(appPdu.end(), objects.begin(), objects.end());

    return appPdu;
}

std::vector<uint8_t> Dnp3Client::buildObjectHeader(uint8_t group, uint8_t variation,
                                                     uint8_t qualifier, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> header;
    header.push_back(group);
    header.push_back(variation);
    header.push_back(qualifier);
    header.insert(header.end(), data.begin(), data.end());
    return header;
}

std::vector<uint8_t> Dnp3Client::buildReadRequest(
    const std::vector<std::tuple<uint8_t, uint8_t, uint8_t>>& groupVarQual) {
    std::vector<uint8_t> objects;
    for (const auto& [group, variation, qualifier] : groupVarQual) {
        auto header = buildObjectHeader(group, variation, qualifier, {});
        objects.insert(objects.end(), header.begin(), header.end());
    }
    return buildApplicationRequest(APP_FC_READ, objects);
}

std::vector<uint8_t> Dnp3Client::buildCROB(uint16_t index, uint8_t code, uint8_t count,
                                             uint32_t onTime, uint32_t offTime) {
    std::vector<uint8_t> data;
    data.push_back(static_cast<uint8_t>(index & 0xFF));
    data.push_back(static_cast<uint8_t>((index >> 8) & 0xFF));
    data.push_back(code);
    data.push_back(count);
    data.push_back(static_cast<uint8_t>(onTime & 0xFF));
    data.push_back(static_cast<uint8_t>((onTime >> 8) & 0xFF));
    data.push_back(static_cast<uint8_t>(offTime & 0xFF));
    data.push_back(static_cast<uint8_t>((offTime >> 8) & 0xFF));
    data.push_back(0x00); // status (reserved)
    return data;
}

std::vector<uint8_t> Dnp3Client::buildAnalogControlBlock(uint16_t index, double value) {
    std::vector<uint8_t> data;
    data.push_back(static_cast<uint8_t>(index & 0xFF));
    data.push_back(static_cast<uint8_t>((index >> 8) & 0xFF));
    // Encode as 32-bit float (Group 41 Var 3)
    float fval = static_cast<float>(value);
    uint8_t* fp = reinterpret_cast<uint8_t*>(&fval);
    data.push_back(fp[0]);
    data.push_back(fp[1]);
    data.push_back(fp[2]);
    data.push_back(fp[3]);
    // Control status byte
    data.push_back(0x00);
    return data;
}

// ---------------------------------------------------------------------------
// Parse functions
// ---------------------------------------------------------------------------
bool Dnp3Client::parseLinkFrame(const std::vector<uint8_t>& frame, uint8_t& ctrl,
                                  uint16_t& dest, uint16_t& src, std::vector<uint8_t>& userData) {
    if (frame.size() < 10) return false;
    if (frame[0] != 0x05 || frame[1] != 0x64) return false;

    ctrl = frame[3];
    dest = static_cast<uint16_t>(frame[4]) | (static_cast<uint16_t>(frame[5]) << 8);
    src = static_cast<uint16_t>(frame[6]) | (static_cast<uint16_t>(frame[7]) << 8);

    // Validate header CRC
    uint16_t headerCrc = (static_cast<uint16_t>(frame[9]) << 8) | frame[8];
    uint16_t calcCrc = calculateCrc(frame.data(), 0, 8);
    if (headerCrc != calcCrc) return false;

    // Extract user data from blocks
    userData.clear();
    for (size_t blockStart = 10; blockStart + 18 <= frame.size(); blockStart += 18) {
        // Validate block CRC
        uint16_t blockCrc = (static_cast<uint16_t>(frame[blockStart + 17]) << 8) | frame[blockStart + 16];
        uint16_t calcBlockCrc = calculateCrc(frame.data(), blockStart, 16);
        if (blockCrc != calcBlockCrc) return false;

        // Determine actual data length in block (last block may be shorter)
        size_t remaining = frame.size() - blockStart - 2;
        size_t dataInBlock = std::min(size_t(16), remaining);

        // Check if this is the last block
        if (blockStart + 18 >= frame.size()) {
            // Last block - find actual data length (non-zero or padded)
            dataInBlock = 16; // Use all 16 bytes; upper layer handles padding
        }

        userData.insert(userData.end(), frame.begin() + blockStart,
                        frame.begin() + blockStart + dataInBlock);
    }

    // Remove padding zeros at the end
    while (!userData.empty() && userData.back() == 0x00) {
        userData.pop_back();
    }

    return true;
}

bool Dnp3Client::parseApplicationResponse(const std::vector<uint8_t>& data, uint8_t& funcCode,
                                            uint8_t& ctrl, std::vector<std::vector<uint8_t>>& objects,
                                            InternalIndications& ii) {
    if (data.size() < 2) return false;

    ctrl = data[0];
    funcCode = data[1];

    // Parse internal indications
    if (data.size() >= 4) {
        uint16_t iiBytes = static_cast<uint16_t>(data[2]) | (static_cast<uint16_t>(data[3]) << 8);
        ii.allStations = (iiBytes & 0x0001) != 0;
        ii.class1Events = (iiBytes & 0x0002) != 0;
        ii.class2Events = (iiBytes & 0x0004) != 0;
        ii.class3Events = (iiBytes & 0x0008) != 0;
        ii.needTime = (iiBytes & 0x0010) != 0;
        ii.localControl = (iiBytes & 0x0020) != 0;
        ii.deviceTrouble = (iiBytes & 0x0040) != 0;
        ii.deviceRestart = (iiBytes & 0x0080) != 0;
        ii.funcNotSupported = (iiBytes & 0x0200) != 0;
    }

    // Parse objects from byte 4 onwards
    size_t offset = 4;
    while (offset + 3 <= data.size()) {
        std::vector<uint8_t> objData;
        uint8_t group, variation, qualifier;
        if (!parseObjectHeader(data, offset, group, variation, qualifier, objData)) {
            break;
        }
        // Reconstruct full object with header
        std::vector<uint8_t> fullObj;
        fullObj.push_back(group);
        fullObj.push_back(variation);
        fullObj.push_back(qualifier);
        fullObj.insert(fullObj.end(), objData.begin(), objData.end());
        objects.push_back(fullObj);
    }

    return true;
}

bool Dnp3Client::parseObjectHeader(const std::vector<uint8_t>& data, size_t& offset,
                                     uint8_t& group, uint8_t& variation, uint8_t& qualifier,
                                     std::vector<uint8_t>& objectData) {
    if (offset + 3 > data.size()) return false;

    group = data[offset++];
    variation = data[offset++];
    qualifier = data[offset++];

    uint8_t qc = (qualifier >> 5) & 0x07; // qualifier code

    switch (qc) {
        case 0x00: { // 8-bit start-stop
            if (offset + 2 > data.size()) return false;
            uint8_t start = data[offset++];
            uint8_t stop = data[offset++];
            size_t count = (stop >= start) ? (stop - start + 1) : 0;
            size_t objSize = getObjectSize(group, variation);
            if (objSize == 0) return false;
            size_t totalLen = count * objSize;
            if (offset + totalLen > data.size()) return false;
            objectData.assign(data.begin() + offset, data.begin() + offset + totalLen);
            offset += totalLen;
            break;
        }
        case 0x01: { // 16-bit start-stop
            if (offset + 4 > data.size()) return false;
            uint16_t start = static_cast<uint16_t>(data[offset]) | (static_cast<uint16_t>(data[offset+1]) << 8);
            offset += 2;
            uint16_t stop = static_cast<uint16_t>(data[offset]) | (static_cast<uint16_t>(data[offset+1]) << 8);
            offset += 2;
            size_t count = (stop >= start) ? (stop - start + 1) : 0;
            size_t objSize = getObjectSize(group, variation);
            if (objSize == 0) return false;
            size_t totalLen = count * objSize;
            if (offset + totalLen > data.size()) return false;
            objectData.assign(data.begin() + offset, data.begin() + offset + totalLen);
            offset += totalLen;
            break;
        }
        case 0x03: { // All
            objectData.clear();
            break;
        }
        case 0x04: { // 8-bit count
            if (offset + 1 > data.size()) return false;
            uint8_t count = data[offset++];
            size_t objSize = getObjectSize(group, variation);
            if (objSize == 0) return false;
            size_t totalLen = count * objSize;
            if (offset + totalLen > data.size()) return false;
            objectData.assign(data.begin() + offset, data.begin() + offset + totalLen);
            offset += totalLen;
            break;
        }
        case 0x06: { // 8-bit count and prefix
            if (offset + 1 > data.size()) return false;
            uint8_t count = data[offset++];
            // Prefix (index) + data
            size_t objSize = getObjectSize(group, variation) + 1; // +1 for prefix
            size_t totalLen = count * objSize;
            if (offset + totalLen > data.size()) return false;
            objectData.assign(data.begin() + offset, data.begin() + offset + totalLen);
            offset += totalLen;
            break;
        }
        case 0x07: { // 16-bit count
            if (offset + 2 > data.size()) return false;
            uint16_t count = static_cast<uint16_t>(data[offset]) | (static_cast<uint16_t>(data[offset+1]) << 8);
            offset += 2;
            size_t objSize = getObjectSize(group, variation);
            if (objSize == 0) return false;
            size_t totalLen = count * objSize;
            if (offset + totalLen > data.size()) return false;
            objectData.assign(data.begin() + offset, data.begin() + offset + totalLen);
            offset += totalLen;
            break;
        }
        default:
            return false;
    }

    return true;
}

size_t Dnp3Client::getObjectSize(uint8_t group, uint8_t variation) {
    // Binary Input (Group 1)
    if (group == 1 && variation == 1) return 1; // packed (1 bit)
    if (group == 1 && variation == 2) return 1; // with flags
    // Binary Input Event (Group 2)
    if (group == 2 && variation == 1) return 1; // without time
    if (group == 2 && variation == 2) return 7; // with absolute time
    if (group == 2 && variation == 3) return 1; // with relative time
    // Binary Output (Group 10)
    if (group == 10 && variation == 1) return 1; // packed
    if (group == 10 && variation == 2) return 1; // output status
    // Binary Output Event (Group 11)
    if (group == 11 && variation == 1) return 1; // without time
    if (group == 11 && variation == 2) return 7; // with time
    // Counter (Group 20)
    if (group == 20 && variation == 1) return 5; // 32-bit with flag
    if (group == 20 && variation == 2) return 3; // 16-bit with flag
    if (group == 20 && variation == 5) return 4; // 32-bit without flag
    if (group == 20 && variation == 6) return 2; // 16-bit without flag
    // Counter Event (Group 22)
    if (group == 22 && variation == 1) return 5; // 32-bit without time
    if (group == 22 && variation == 2) return 3; // 16-bit without time
    if (group == 22 && variation == 5) return 11; // 32-bit with time
    if (group == 22 && variation == 6) return 9; // 16-bit with time
    // Analog Input (Group 30)
    if (group == 30 && variation == 1) return 5; // 32-bit
    if (group == 30 && variation == 2) return 3; // 16-bit
    if (group == 30 && variation == 3) return 5; // 32-bit without flag
    if (group == 30 && variation == 4) return 3; // 16-bit without flag
    if (group == 30 && variation == 5) return 5; // float
    if (group == 30 && variation == 6) return 5; // double
    // Analog Input Event (Group 32)
    if (group == 32 && variation == 1) return 5; // 32-bit without time
    if (group == 32 && variation == 2) return 3; // 16-bit without time
    if (group == 32 && variation == 3) return 5; // 32-bit with time
    if (group == 32 && variation == 4) return 3; // 16-bit with time
    if (group == 32 && variation == 5) return 5; // float without time
    if (group == 32 && variation == 6) return 11; // float with time
    if (group == 32 && variation == 7) return 5; // double without time
    if (group == 32 && variation == 8) return 11; // double with time
    // Analog Output (Group 40)
    if (group == 40 && variation == 1) return 5; // 32-bit
    if (group == 40 && variation == 2) return 3; // 16-bit
    if (group == 40 && variation == 3) return 5; // float
    if (group == 40 && variation == 4) return 9; // double
    // Analog Output Status (Group 41)
    if (group == 41 && variation == 1) return 5; // 32-bit
    if (group == 41 && variation == 2) return 3; // 16-bit
    if (group == 41 && variation == 3) return 5; // float
    if (group == 41 && variation == 4) return 9; // double
    // Time (Group 50)
    if (group == 50 && variation == 1) return 6; // absolute time
    if (group == 50 && variation == 3) return 6; // absolute time at last recorded
    // Class (Group 60)
    if (group == 60) return 0; // no data
    // Internal Indications (Group 80)
    if (group == 80 && variation == 1) return 2;
    // Control Relay Output Block (Group 12)
    if (group == 12 && variation == 1) return 11;
    // Binary Command Event (Group 13)
    if (group == 13 && variation == 1) return 1;
    if (group == 13 && variation == 2) return 7;
    return 0;
}

void Dnp3Client::parseAnalogObjects(const std::vector<uint8_t>& data, uint8_t group,
                                      uint8_t variation, uint8_t qualifier,
                                      std::vector<Dnp3AnalogPoint>& out) {
    uint8_t prefixCode = qualifier & 0x07; // Extract prefix code (index size)
    bool hasPrefix = (prefixCode == 6 || prefixCode == 5 || prefixCode == 4 || prefixCode == 3 || prefixCode == 2 || prefixCode == 1);

    // Use start/stop or count from qualifier
    uint8_t rangeCode = (qualifier >> 5) & 0x07;
    uint16_t startIndex = 0;
    uint16_t count = 0;
    size_t offset = 0;

    switch (rangeCode) {
        case 0: // 8-bit start-stop
            if (data.size() < 2) return;
            startIndex = data[0];
            count = (data[1] >= data[0]) ? (data[1] - data[0] + 1) : 0;
            offset = 2;
            break;
        case 1: // 16-bit start-stop
            if (data.size() < 4) return;
            startIndex = static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
            {
                uint16_t stop = static_cast<uint16_t>(data[2]) | (static_cast<uint16_t>(data[3]) << 8);
                count = (stop >= startIndex) ? (stop - startIndex + 1) : 0;
            }
            offset = 4;
            break;
        case 4: // 8-bit count (no index)
            if (data.size() < 1) return;
            count = data[0];
            offset = 1;
            break;
        case 7: // 16-bit count (no index)
            if (data.size() < 2) return;
            count = static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
            offset = 2;
            break;
        default:
            return;
    }

    for (uint16_t i = 0; i < count && offset < data.size(); ++i) {
        Dnp3AnalogPoint pt;

        // Handle prefix/index
        if (hasPrefix) {
            if (prefixCode == 6) { // 8-bit index prefix
                if (offset >= data.size()) break;
                pt.index = data[offset++];
            } else if (prefixCode == 4) { // 16-bit index prefix
                if (offset + 1 >= data.size()) break;
                pt.index = static_cast<uint16_t>(data[offset]) | (static_cast<uint16_t>(data[offset+1]) << 8);
                offset += 2;
            } else {
                pt.index = startIndex + i;
            }
        } else {
            pt.index = startIndex + i;
        }

        // Parse value based on variation
        if ((group == 30 || group == 32 || group == 40 || group == 41) && variation == 1) {
            // 32-bit with flag
            if (offset + 5 > data.size()) break;
            pt.quality = data[offset++];
            uint32_t raw = static_cast<uint32_t>(data[offset]) |
                          (static_cast<uint32_t>(data[offset+1]) << 8) |
                          (static_cast<uint32_t>(data[offset+2]) << 16) |
                          (static_cast<uint32_t>(data[offset+3]) << 24);
            pt.value = static_cast<double>(static_cast<int32_t>(raw));
            offset += 4;
        } else if ((group == 30 || group == 32 || group == 40 || group == 41) && variation == 2) {
            // 16-bit with flag
            if (offset + 3 > data.size()) break;
            pt.quality = data[offset++];
            int16_t raw = static_cast<int16_t>(data[offset]) | (static_cast<int16_t>(data[offset+1]) << 8);
            pt.value = static_cast<double>(raw);
            offset += 2;
        } else if ((group == 30 || group == 32) && (variation == 3 || variation == 4)) {
            // Without flag (Class 0 scan)
            if (group == 30 && variation == 3) {
                if (offset + 4 > data.size()) break;
                uint32_t raw = static_cast<uint32_t>(data[offset]) |
                              (static_cast<uint32_t>(data[offset+1]) << 8) |
                              (static_cast<uint32_t>(data[offset+2]) << 16) |
                              (static_cast<uint32_t>(data[offset+3]) << 24);
                pt.value = static_cast<double>(static_cast<int32_t>(raw));
                pt.quality = 0x01; // online
                offset += 4;
            } else if (group == 30 && variation == 4) {
                if (offset + 2 > data.size()) break;
                int16_t raw = static_cast<int16_t>(data[offset]) | (static_cast<int16_t>(data[offset+1]) << 8);
                pt.value = static_cast<double>(raw);
                pt.quality = 0x01; // online
                offset += 2;
            }
        } else if ((group == 30 || group == 32 || group == 40 || group == 41) && variation == 5) {
            // Float
            if (offset + 5 > data.size()) break;
            pt.quality = data[offset++];
            float fval;
            std::memcpy(&fval, &data[offset], sizeof(fval));
            pt.value = static_cast<double>(fval);
            offset += 4;
        } else if ((group == 30 || group == 32 || group == 40 || group == 41) && variation == 6) {
            // Double
            if (offset + 9 > data.size()) break;
            pt.quality = data[offset++];
            std::memcpy(&pt.value, &data[offset], sizeof(double));
            offset += 8;
        }

        pt.online = (pt.quality & 0x01) != 0;
        pt.overRange = (pt.quality & 0x02) != 0;
        pt.commLost = (pt.quality & 0x04) != 0;
        pt.timestamp = std::chrono::system_clock::now();
        out.push_back(pt);
    }
}

void Dnp3Client::parseBinaryObjects(const std::vector<uint8_t>& data, uint8_t group,
                                      uint8_t variation, uint8_t qualifier,
                                      std::vector<Dnp3BinaryPoint>& out) {
    uint8_t rangeCode = (qualifier >> 5) & 0x07;
    uint16_t startIndex = 0;
    uint16_t count = 0;
    size_t offset = 0;

    switch (rangeCode) {
        case 0:
            if (data.size() < 2) return;
            startIndex = data[0];
            count = (data[1] >= data[0]) ? (data[1] - data[0] + 1) : 0;
            offset = 2;
            break;
        case 1:
            if (data.size() < 4) return;
            startIndex = static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
            count = static_cast<uint16_t>(data[2]) | (static_cast<uint16_t>(data[3]) << 8);
            count = (count >= startIndex) ? (count - startIndex + 1) : 0;
            offset = 4;
            break;
        case 4:
            if (data.size() < 1) return;
            count = data[0];
            offset = 1;
            break;
        case 7:
            if (data.size() < 2) return;
            count = static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
            offset = 2;
            break;
        default:
            return;
    }

    if (group == 1 && variation == 1) {
        // Packed binary input (1 bit each)
        for (uint16_t i = 0; i < count; ++i) {
            size_t byteIdx = offset + (i / 8);
            if (byteIdx >= data.size()) break;
            size_t bitIdx = i % 8;
            Dnp3BinaryPoint pt;
            pt.index = startIndex + i;
            pt.value = (data[byteIdx] >> bitIdx) & 0x01;
            pt.quality = 0x01; // online
            pt.timestamp = std::chrono::system_clock::now();
            out.push_back(pt);
        }
    } else if ((group == 1 && variation == 2) || (group == 10 && variation == 2) ||
               (group == 2 && variation == 1) || (group == 11 && variation == 1)) {
        // With flags (1 byte each)
        for (uint16_t i = 0; i < count; ++i) {
            if (offset >= data.size()) break;
            Dnp3BinaryPoint pt;
            pt.index = startIndex + i;
            pt.quality = data[offset++];
            pt.value = (pt.quality & 0x80) != 0;
            pt.online = (pt.quality & 0x01) != 0;
            pt.timestamp = std::chrono::system_clock::now();
            out.push_back(pt);
        }
    }
}

void Dnp3Client::parseCounterObjects(const std::vector<uint8_t>& data, uint8_t group,
                                       uint8_t variation, uint8_t qualifier,
                                       std::vector<Dnp3CounterPoint>& out) {
    uint8_t rangeCode = (qualifier >> 5) & 0x07;
    uint16_t startIndex = 0;
    uint16_t count = 0;
    size_t offset = 0;

    switch (rangeCode) {
        case 0:
            if (data.size() < 2) return;
            startIndex = data[0];
            count = (data[1] >= data[0]) ? (data[1] - data[0] + 1) : 0;
            offset = 2;
            break;
        case 1:
            if (data.size() < 4) return;
            startIndex = static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
            count = static_cast<uint16_t>(data[2]) | (static_cast<uint16_t>(data[3]) << 8);
            count = (count >= startIndex) ? (count - startIndex + 1) : 0;
            offset = 4;
            break;
        case 4:
            if (data.size() < 1) return;
            count = data[0];
            offset = 1;
            break;
        default:
            return;
    }

    for (uint16_t i = 0; i < count; ++i) {
        Dnp3CounterPoint pt;
        pt.index = startIndex + i;

        if (group == 20 && (variation == 1 || variation == 5)) {
            if (offset + (variation == 1 ? 5 : 4) > data.size()) break;
            if (variation == 1) {
                pt.quality = data[offset++];
            } else {
                pt.quality = 0x01;
            }
            pt.value = static_cast<uint32_t>(data[offset]) |
                      (static_cast<uint32_t>(data[offset+1]) << 8) |
                      (static_cast<uint32_t>(data[offset+2]) << 16) |
                      (static_cast<uint32_t>(data[offset+3]) << 24);
            offset += 4;
        } else if (group == 20 && (variation == 2 || variation == 6)) {
            if (offset + (variation == 2 ? 3 : 2) > data.size()) break;
            if (variation == 2) {
                pt.quality = data[offset++];
            } else {
                pt.quality = 0x01;
            }
            pt.value = static_cast<uint16_t>(data[offset]) | (static_cast<uint16_t>(data[offset+1]) << 8);
            offset += 2;
        }

        pt.timestamp = std::chrono::system_clock::now();
        out.push_back(pt);
    }
}

// ---------------------------------------------------------------------------
// Read functions
// ---------------------------------------------------------------------------
bool Dnp3Client::readClass0(std::vector<Dnp3AnalogPoint>& analogs,
                              std::vector<Dnp3BinaryPoint>& binaries,
                              std::vector<Dnp3CounterPoint>& counters) {
    analogs.clear();
    binaries.clear();
    counters.clear();

    auto appRequest = buildReadRequest({
        {60, 1, 0x06}, // Class 0 all objects (8-bit count qualifier)
    });

    auto transportSegments = buildTransportSegment(appRequest, true, true, m_transportSeq++);
    auto frame = buildLinkFrame(LINK_PRI_CONFIRMED_USER,
                                 static_cast<uint16_t>(m_endpoint.destinationAddress),
                                 static_cast<uint16_t>(m_endpoint.sourceAddress),
                                 transportSegments);

    std::vector<uint8_t> response;
    if (!sendFrame(frame)) return false;
    if (!receiveFrame(response, m_appTimeout)) return false;

    uint8_t ctrl, funcCode;
    uint16_t dest, src;
    std::vector<uint8_t> userData;
    InternalIndications ii;

    if (!parseLinkFrame(response, ctrl, dest, src, userData)) {
        std::lock_guard<std::mutex> lock(m_statsMutex);
        m_framesRejected++;
        return false;
    }

    // Reassemble transport segments
    std::vector<uint8_t> appData;
    size_t tOffset = 0;
    while (tOffset < userData.size()) {
        uint8_t th = userData[tOffset++];
        size_t chunkSize = std::min(size_t(249), userData.size() - tOffset);
        appData.insert(appData.end(), userData.begin() + tOffset, userData.begin() + tOffset + chunkSize);
        tOffset += chunkSize;
    }

    std::vector<std::vector<uint8_t>> objects;
    if (!parseApplicationResponse(appData, funcCode, ctrl, objects, ii)) return false;

    for (const auto& obj : objects) {
        if (obj.size() < 3) continue;
        uint8_t group = obj[0];
        uint8_t variation = obj[1];
        uint8_t qualifier = obj[2];
        std::vector<uint8_t> objData(obj.begin() + 3, obj.end());

        if (group == 1) {
            parseBinaryObjects(objData, group, variation, qualifier, binaries);
        } else if (group == 30) {
            parseAnalogObjects(objData, group, variation, qualifier, analogs);
        } else if (group == 20) {
            parseCounterObjects(objData, group, variation, qualifier, counters);
        }
    }

    // Update internal indications
    m_lastII = ii;

    return true;
}

bool Dnp3Client::readClass1(std::vector<Dnp3AnalogPoint>& analogs,
                              std::vector<Dnp3BinaryPoint>& binaries) {
    analogs.clear();
    binaries.clear();

    auto appRequest = buildReadRequest({
        {60, 2, 0x06}, // Class 1 event data
        {60, 3, 0x06}, // Class 2 event data
        {60, 4, 0x06}, // Class 3 event data
    });

    auto transportSegments = buildTransportSegment(appRequest, true, true, m_transportSeq++);
    auto frame = buildLinkFrame(LINK_PRI_CONFIRMED_USER,
                                 static_cast<uint16_t>(m_endpoint.destinationAddress),
                                 static_cast<uint16_t>(m_endpoint.sourceAddress),
                                 transportSegments);

    std::vector<uint8_t> response;
    if (!sendFrame(frame)) return false;
    if (!receiveFrame(response, m_appTimeout)) return false;

    uint8_t ctrl;
    uint16_t dest, src;
    std::vector<uint8_t> userData;
    if (!parseLinkFrame(response, ctrl, dest, src, userData)) return false;

    // Parse and extract event objects
    std::vector<uint8_t> appData;
    size_t tOffset = 0;
    while (tOffset < userData.size()) {
        uint8_t th = userData[tOffset++];
        bool fir = (th & 0x40) != 0;
        bool fin = (th & 0x80) != 0;
        (void)fir; (void)fin;
        size_t chunkSize = std::min(size_t(249), userData.size() - tOffset);
        appData.insert(appData.end(), userData.begin() + tOffset, userData.begin() + tOffset + chunkSize);
        tOffset += chunkSize;
    }

    uint8_t funcCode;
    std::vector<std::vector<uint8_t>> objects;
    InternalIndications ii;
    parseApplicationResponse(appData, funcCode, ctrl, objects, ii);

    for (const auto& obj : objects) {
        if (obj.size() < 3) continue;
        uint8_t group = obj[0];
        uint8_t variation = obj[1];
        uint8_t qualifier = obj[2];
        std::vector<uint8_t> objData(obj.begin() + 3, obj.end());

        if (group == 2) {
            parseBinaryObjects(objData, group, variation, qualifier, binaries);
        } else if (group == 32) {
            parseAnalogObjects(objData, group, variation, qualifier, analogs);
        }
    }

    m_lastII = ii;
    return true;
}

bool Dnp3Client::readClass2(std::vector<Dnp3AnalogPoint>& analogs,
                              std::vector<Dnp3BinaryPoint>& binaries) {
    // Similar to readClass1 with Class 2 and 3
    return readClass1(analogs, binaries);
}

bool Dnp3Client::readClass3(std::vector<Dnp3AnalogPoint>& analogs,
                              std::vector<Dnp3BinaryPoint>& binaries) {
    // Similar to readClass1 with Class 3 only
    return readClass1(analogs, binaries);
}

bool Dnp3Client::readAnalogInputs(const std::vector<uint16_t>& indices,
                                    std::vector<Dnp3AnalogPoint>& outPoints) {
    if (indices.empty()) return false;

    // Build object with 16-bit index prefix and 32-bit analog values
    std::vector<uint8_t> objects;
    objects.push_back(30); // Group 30 (Analog Input)
    objects.push_back(1);  // Var 1 (32-bit with flag)
    objects.push_back(0x5B); // Qualifier: 16-bit index prefix, 16-bit count

    uint16_t count = static_cast<uint16_t>(indices.size());
    objects.push_back(static_cast<uint8_t>(count & 0xFF));
    objects.push_back(static_cast<uint8_t>((count >> 8) & 0xFF));

    for (uint16_t idx : indices) {
        objects.push_back(static_cast<uint8_t>(idx & 0xFF));
        objects.push_back(static_cast<uint8_t>((idx >> 8) & 0xFF));
    }

    auto appRequest = buildApplicationRequest(APP_FC_READ, objects);
    auto transportSegments = buildTransportSegment(appRequest, true, true, m_transportSeq++);
    auto frame = buildLinkFrame(LINK_PRI_CONFIRMED_USER,
                                 static_cast<uint16_t>(m_endpoint.destinationAddress),
                                 static_cast<uint16_t>(m_endpoint.sourceAddress),
                                 transportSegments);

    std::vector<uint8_t> response;
    if (!sendFrame(frame)) return false;
    if (!receiveFrame(response, m_appTimeout)) return false;

    uint8_t ctrl;
    uint16_t dest, src;
    std::vector<uint8_t> userData;
    InternalIndications ii;
    if (!parseLinkFrame(response, ctrl, dest, src, userData)) return false;

    std::vector<uint8_t> appData;
    size_t tOffset = 0;
    while (tOffset < userData.size()) {
        if (tOffset >= userData.size()) break;
        uint8_t th = userData[tOffset++];
        (void)th;
        size_t chunkSize = std::min(size_t(249), userData.size() - tOffset);
        appData.insert(appData.end(), userData.begin() + tOffset, userData.begin() + tOffset + chunkSize);
        tOffset += chunkSize;
    }

    uint8_t funcCode;
    std::vector<std::vector<uint8_t>> parsedObjects;
    parseApplicationResponse(appData, funcCode, ctrl, parsedObjects, ii);

    for (const auto& obj : parsedObjects) {
        if (obj.size() < 3) continue;
        uint8_t group = obj[0];
        uint8_t variation = obj[1];
        uint8_t qualifier = obj[2];
        std::vector<uint8_t> objData(obj.begin() + 3, obj.end());

        if (group == 30) {
            parseAnalogObjects(objData, group, variation, qualifier, outPoints);
        }
    }

    m_lastII = ii;
    return !outPoints.empty();
}

bool Dnp3Client::readBinaryInputs(const std::vector<uint16_t>& indices,
                                    std::vector<Dnp3BinaryPoint>& outPoints) {
    if (indices.empty()) return false;
    (void)indices;

    auto appRequest = buildReadRequest({
        {1, 2, 0x06}, // Binary Input with flags
    });

    auto transportSegments = buildTransportSegment(appRequest, true, true, m_transportSeq++);
    auto frame = buildLinkFrame(LINK_PRI_CONFIRMED_USER,
                                 static_cast<uint16_t>(m_endpoint.destinationAddress),
                                 static_cast<uint16_t>(m_endpoint.sourceAddress),
                                 transportSegments);

    std::vector<uint8_t> response;
    if (!sendFrame(frame)) return false;
    if (!receiveFrame(response, m_appTimeout)) return false;

    uint8_t ctrl;
    uint16_t dest, src;
    std::vector<uint8_t> userData;
    InternalIndications ii;
    if (!parseLinkFrame(response, ctrl, dest, src, userData)) return false;

    std::vector<uint8_t> appData;
    size_t tOffset = 0;
    while (tOffset < userData.size()) {
        uint8_t th = userData[tOffset++];
        (void)th;
        size_t chunkSize = std::min(size_t(249), userData.size() - tOffset);
        appData.insert(appData.end(), userData.begin() + tOffset, userData.begin() + tOffset + chunkSize);
        tOffset += chunkSize;
    }

    uint8_t funcCode;
    std::vector<std::vector<uint8_t>> objects;
    parseApplicationResponse(appData, funcCode, ctrl, objects, ii);

    for (const auto& obj : objects) {
        if (obj.size() < 3) continue;
        uint8_t group = obj[0];
        uint8_t variation = obj[1];
        uint8_t qualifier = obj[2];
        std::vector<uint8_t> objData(obj.begin() + 3, obj.end());

        if (group == 1) {
            parseBinaryObjects(objData, group, variation, qualifier, outPoints);
        }
    }

    m_lastII = ii;
    return !outPoints.empty();
}

bool Dnp3Client::readCounters(const std::vector<uint16_t>& indices,
                                std::vector<Dnp3CounterPoint>& outPoints) {
    if (indices.empty()) return false;
    (void)indices;

    auto appRequest = buildReadRequest({
        {20, 5, 0x06}, // 32-bit counter without flag
    });

    auto transportSegments = buildTransportSegment(appRequest, true, true, m_transportSeq++);
    auto frame = buildLinkFrame(LINK_PRI_CONFIRMED_USER,
                                 static_cast<uint16_t>(m_endpoint.destinationAddress),
                                 static_cast<uint16_t>(m_endpoint.sourceAddress),
                                 transportSegments);

    std::vector<uint8_t> response;
    if (!sendFrame(frame)) return false;
    if (!receiveFrame(response, m_appTimeout)) return false;

    uint8_t ctrl;
    uint16_t dest, src;
    std::vector<uint8_t> userData;
    InternalIndications ii;
    if (!parseLinkFrame(response, ctrl, dest, src, userData)) return false;

    std::vector<uint8_t> appData;
    size_t tOffset = 0;
    while (tOffset < userData.size()) {
        uint8_t th = userData[tOffset++];
        (void)th;
        size_t chunkSize = std::min(size_t(249), userData.size() - tOffset);
        appData.insert(appData.end(), userData.begin() + tOffset, userData.begin() + tOffset + chunkSize);
        tOffset += chunkSize;
    }

    uint8_t funcCode;
    std::vector<std::vector<uint8_t>> objects;
    parseApplicationResponse(appData, funcCode, ctrl, objects, ii);

    for (const auto& obj : objects) {
        if (obj.size() < 3) continue;
        uint8_t group = obj[0];
        uint8_t variation = obj[1];
        uint8_t qualifier = obj[2];
        std::vector<uint8_t> objData(obj.begin() + 3, obj.end());

        if (group == 20) {
            parseCounterObjects(objData, group, variation, qualifier, outPoints);
        }
    }

    m_lastII = ii;
    return !outPoints.empty();
}

// ---------------------------------------------------------------------------
// Control functions
// ---------------------------------------------------------------------------
Dnp3ControlResult Dnp3Client::operateCROB(uint16_t index, uint8_t code, uint8_t count,
                                             uint32_t onTimeMs, uint32_t offTimeMs) {
    Dnp3ControlResult result;
    result.index = index;
    result.timestamp = std::chrono::system_clock::now();

    if (!m_connected.load()) {
        result.statusText = "Not connected";
        return result;
    }

    std::vector<uint8_t> objects;
    objects.push_back(12); // Group 12 (Control Relay Output Block)
    objects.push_back(1);  // Var 1 (CROB)
    objects.push_back(0x17); // Qualifier: 8-bit count, 16-bit index prefix

    objects.push_back(0x01); // Count = 1
    objects.push_back(static_cast<uint8_t>(index & 0xFF));
    objects.push_back(static_cast<uint8_t>((index >> 8) & 0xFF));

    // CROB fields
    objects.push_back(code); // Control code
    objects.push_back(count); // Count
    objects.push_back(static_cast<uint8_t>(onTimeMs & 0xFF));
    objects.push_back(static_cast<uint8_t>((onTimeMs >> 8) & 0xFF));
    objects.push_back(static_cast<uint8_t>(offTimeMs & 0xFF));
    objects.push_back(static_cast<uint8_t>((offTimeMs >> 8) & 0xFF));
    objects.push_back(0x00); // Status

    auto appRequest = buildApplicationRequest(APP_FC_DIRECT_OPERATE, objects);
    auto transportSegments = buildTransportSegment(appRequest, true, true, m_transportSeq++);
    auto frame = buildLinkFrame(LINK_PRI_CONFIRMED_USER,
                                 static_cast<uint16_t>(m_endpoint.destinationAddress),
                                 static_cast<uint16_t>(m_endpoint.sourceAddress),
                                 transportSegments);

    std::vector<uint8_t> response;
    if (!sendFrame(frame)) {
        result.statusText = "Send failed";
        return result;
    }
    if (!receiveFrame(response, m_appTimeout)) {
        result.statusText = "No response";
        return result;
    }

    // Parse response
    uint8_t ctrl;
    uint16_t dest, src;
    std::vector<uint8_t> userData;
    InternalIndications ii;
    if (!parseLinkFrame(response, ctrl, dest, src, userData)) {
        result.statusText = "Invalid frame";
        return result;
    }

    result.success = true;
    m_lastII = ii;
    return result;
}

Dnp3ControlResult Dnp3Client::operateAnalogOutput(uint16_t index, double value) {
    Dnp3ControlResult result;
    result.index = index;
    result.timestamp = std::chrono::system_clock::now();

    if (!m_connected.load()) {
        result.statusText = "Not connected";
        return result;
    }

    std::vector<uint8_t> objects;
    objects.push_back(41); // Group 41 (Analog Output)
    objects.push_back(3);  // Var 3 (Float)
    objects.push_back(0x17); // Qualifier: 8-bit count, 16-bit index prefix

    objects.push_back(0x01); // Count = 1
    objects.push_back(static_cast<uint8_t>(index & 0xFF));
    objects.push_back(static_cast<uint8_t>((index >> 8) & 0xFF));

    float fval = static_cast<float>(value);
    uint8_t* fp = reinterpret_cast<uint8_t*>(&fval);
    objects.push_back(fp[0]);
    objects.push_back(fp[1]);
    objects.push_back(fp[2]);
    objects.push_back(fp[3]);
    objects.push_back(0x00); // Status

    auto appRequest = buildApplicationRequest(APP_FC_DIRECT_OPERATE, objects);
    auto transportSegments = buildTransportSegment(appRequest, true, true, m_transportSeq++);
    auto frame = buildLinkFrame(LINK_PRI_CONFIRMED_USER,
                                 static_cast<uint16_t>(m_endpoint.destinationAddress),
                                 static_cast<uint16_t>(m_endpoint.sourceAddress),
                                 transportSegments);

    std::vector<uint8_t> response;
    if (!sendFrame(frame) || !receiveFrame(response, m_appTimeout)) {
        result.statusText = "Communication failed";
        return result;
    }

    result.success = true;
    return result;
}

Dnp3ControlResult Dnp3Client::selectBeforeOperateCROB(uint16_t index, uint8_t code) {
    // Select phase
    Dnp3ControlResult result;
    result.index = index;
    result.timestamp = std::chrono::system_clock::now();

    if (!m_connected.load()) {
        result.statusText = "Not connected";
        return result;
    }

    std::vector<uint8_t> objects;
    objects.push_back(12); // Group 12 (CROB)
    objects.push_back(1);  // Var 1
    objects.push_back(0x17); // Qualifier

    objects.push_back(0x01); // Count = 1
    objects.push_back(static_cast<uint8_t>(index & 0xFF));
    objects.push_back(static_cast<uint8_t>((index >> 8) & 0xFF));

    objects.push_back(code); // Control code
    objects.push_back(0x01); // Count
    objects.push_back(0x00); objects.push_back(0x00); // On time
    objects.push_back(0x00); objects.push_back(0x00); // Off time
    objects.push_back(0x00); // Status

    auto appRequest = buildApplicationRequest(APP_FC_SELECT, objects);
    auto transportSegments = buildTransportSegment(appRequest, true, true, m_transportSeq++);
    auto frame = buildLinkFrame(LINK_PRI_CONFIRMED_USER,
                                 static_cast<uint16_t>(m_endpoint.destinationAddress),
                                 static_cast<uint16_t>(m_endpoint.sourceAddress),
                                 transportSegments);

    std::vector<uint8_t> response;
    if (!sendFrame(frame) || !receiveFrame(response, m_appTimeout)) {
        result.statusText = "Select failed";
        return result;
    }

    // Operate phase with same sequence
    return operateCROB(index, code, 1, 100, 0);
}

Dnp3ControlResult Dnp3Client::selectBeforeOperateAnalog(uint16_t index, double value) {
    (void)index;
    (void)value;
    Dnp3ControlResult result;
    result.statusText = "Not implemented";
    return result;
}

// ---------------------------------------------------------------------------
// Time synchronization
// ---------------------------------------------------------------------------
bool Dnp3Client::synchronizeTime() {
    if (!m_connected.load()) return false;

    auto now = std::chrono::system_clock::now();
    auto millisSinceEpoch = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    uint64_t dnpTime = static_cast<uint64_t>(millisSinceEpoch) * 10000ULL + 116444736000000000ULL;

    std::vector<uint8_t> timeData;
    timeData.push_back(50); // Group 50 (Time and Date)
    timeData.push_back(1);  // Var 1 (absolute time)
    timeData.push_back(0x07); // Qualifier: 8-bit count, no index
    timeData.push_back(0x01); // Count = 1

    // DNP3 time format: 48-bit integer (100 microseconds since Jan 1, 1970 UTC)
    for (int i = 0; i < 6; ++i) {
        timeData.push_back(static_cast<uint8_t>((dnpTime >> (i * 8)) & 0xFF));
    }

    auto appRequest = buildApplicationRequest(APP_FC_WRITE, timeData);
    auto transportSegments = buildTransportSegment(appRequest, true, true, m_transportSeq++);
    auto frame = buildLinkFrame(LINK_PRI_CONFIRMED_USER,
                                 static_cast<uint16_t>(m_endpoint.destinationAddress),
                                 static_cast<uint16_t>(m_endpoint.sourceAddress),
                                 transportSegments);

    std::vector<uint8_t> response;
    if (!sendFrame(frame)) return false;
    if (!receiveFrame(response, m_appTimeout)) return false;

    return true;
}

bool Dnp3Client::readTime(std::chrono::system_clock::time_point& outTime) {
    if (!m_connected.load()) return false;

    auto appRequest = buildApplicationRequest(APP_FC_DELAY_MEASURE, {});
    auto transportSegments = buildTransportSegment(appRequest, true, true, m_transportSeq++);
    auto frame = buildLinkFrame(LINK_PRI_CONFIRMED_USER,
                                 static_cast<uint16_t>(m_endpoint.destinationAddress),
                                 static_cast<uint16_t>(m_endpoint.sourceAddress),
                                 transportSegments);

    std::vector<uint8_t> response;
    if (!sendFrame(frame)) return false;
    if (!receiveFrame(response, m_appTimeout)) return false;

    outTime = std::chrono::system_clock::now();
    return true;
}

bool Dnp3Client::enableUnsolicitedMessages(bool class1, bool class2, bool class3) {
    if (!m_connected.load()) return false;

    std::vector<uint8_t> objects;
    if (class1) {
        objects.push_back(60); objects.push_back(2); objects.push_back(0x06);
        objects.push_back(0x00); // Null qualifier (no data)
    }
    if (class2) {
        objects.push_back(60); objects.push_back(3); objects.push_back(0x06);
        objects.push_back(0x00);
    }
    if (class3) {
        objects.push_back(60); objects.push_back(4); objects.push_back(0x06);
        objects.push_back(0x00);
    }

    auto appRequest = buildApplicationRequest(APP_FC_ENABLE_UNSOLICITED, objects);
    auto transportSegments = buildTransportSegment(appRequest, true, true, m_transportSeq++);
    auto frame = buildLinkFrame(LINK_PRI_CONFIRMED_USER,
                                 static_cast<uint16_t>(m_endpoint.destinationAddress),
                                 static_cast<uint16_t>(m_endpoint.sourceAddress),
                                 transportSegments);

    std::vector<uint8_t> response;
    return sendFrame(frame) && receiveFrame(response, m_appTimeout);
}

bool Dnp3Client::disableUnsolicitedMessages() {
    auto appRequest = buildApplicationRequest(APP_FC_DISABLE_UNSOLICITED, {});
    auto transportSegments = buildTransportSegment(appRequest, true, true, m_transportSeq++);
    auto frame = buildLinkFrame(LINK_PRI_CONFIRMED_USER,
                                 static_cast<uint16_t>(m_endpoint.destinationAddress),
                                 static_cast<uint16_t>(m_endpoint.sourceAddress),
                                 transportSegments);

    std::vector<uint8_t> response;
    return sendFrame(frame) && receiveFrame(response, m_appTimeout);
}

// ---------------------------------------------------------------------------
// Restart
// ---------------------------------------------------------------------------
bool Dnp3Client::coldRestart(uint16_t& outDelaySeconds) {
    if (!m_connected.load()) return false;

    auto appRequest = buildApplicationRequest(APP_FC_COLD_RESTART, {});
    auto transportSegments = buildTransportSegment(appRequest, true, true, m_transportSeq++);
    auto frame = buildLinkFrame(LINK_PRI_CONFIRMED_USER,
                                 static_cast<uint16_t>(m_endpoint.destinationAddress),
                                 static_cast<uint16_t>(m_endpoint.sourceAddress),
                                 transportSegments);

    std::vector<uint8_t> response;
    if (!sendFrame(frame) || !receiveFrame(response, m_appTimeout)) {
        outDelaySeconds = 0;
        return false;
    }

    outDelaySeconds = 10; // Default delay
    return true;
}

bool Dnp3Client::warmRestart(uint16_t& outDelaySeconds) {
    if (!m_connected.load()) return false;

    auto appRequest = buildApplicationRequest(APP_FC_WARM_RESTART, {});
    auto transportSegments = buildTransportSegment(appRequest, true, true, m_transportSeq++);
    auto frame = buildLinkFrame(LINK_PRI_CONFIRMED_USER,
                                 static_cast<uint16_t>(m_endpoint.destinationAddress),
                                 static_cast<uint16_t>(m_endpoint.sourceAddress),
                                 transportSegments);

    std::vector<uint8_t> response;
    if (!sendFrame(frame) || !receiveFrame(response, m_appTimeout)) {
        outDelaySeconds = 0;
        return false;
    }

    outDelaySeconds = 5;
    return true;
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
void Dnp3Client::setApplicationTimeout(std::chrono::milliseconds timeout) {
    m_appTimeout = timeout;
}

void Dnp3Client::setLinkRetries(int retries) {
    m_linkRetries = retries;
}

void Dnp3Client::setUnsolicitedCallback(
    std::function<void(const std::vector<Dnp3AnalogPoint>&,
                       const std::vector<Dnp3BinaryPoint>&)> cb) {
    m_unsolicitedCallback = cb;
}

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------
uint64_t Dnp3Client::getFramesSent() const {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    return m_framesSent;
}

uint64_t Dnp3Client::getFramesReceived() const {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    return m_framesReceived;
}

uint64_t Dnp3Client::getFramesRejected() const {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    return m_framesRejected;
}

InternalIndications Dnp3Client::getInternalIndications() const {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    return m_lastII;
}

void Dnp3Client::resetStatistics() {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    m_framesSent = 0;
    m_framesReceived = 0;
    m_framesRejected = 0;
}

// ---------------------------------------------------------------------------
// Frame transport
// ---------------------------------------------------------------------------
bool Dnp3Client::sendFrame(const std::vector<uint8_t>& frame) {
    if (m_fd < 0) return false;

    std::lock_guard<std::mutex> lock(m_sendMutex);

    ssize_t sent;
    if (m_endpoint.isSerial) {
        // For serial, add inter-character delay for RTU timing
        sent = ::write(m_fd, frame.data(), frame.size());
        tcdrain(m_fd);
    } else {
        sent = send(m_fd, frame.data(), frame.size(), MSG_NOSIGNAL);
    }

    if (sent < 0) return false;

    std::lock_guard<std::mutex> statsLock(m_statsMutex);
    m_framesSent++;
    return true;
}

bool Dnp3Client::receiveFrame(std::vector<uint8_t>& frame, std::chrono::milliseconds timeout) {
    if (m_fd < 0) return false;

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(m_fd, &readfds);
    struct timeval tv;
    tv.tv_sec = static_cast<long>(timeout.count() / 1000);
    tv.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);

    int ready = select(m_fd + 1, &readfds, nullptr, nullptr, &tv);
    if (ready <= 0) return false;

    if (m_endpoint.isSerial) {
        // RTU frame: read until timeout (3.5 char times)
        uint8_t buffer[292]; // Max DNP3 frame size
        ssize_t totalRead = 0;
        auto startTime = std::chrono::steady_clock::now();
        auto charTimeout = std::chrono::milliseconds(
            (1000 * 11) / m_endpoint.baudRate + 1); // ~3.5 char times

        while (totalRead < static_cast<ssize_t>(sizeof(buffer))) {
            ssize_t bytesRead = ::read(m_fd, buffer + totalRead, sizeof(buffer) - totalRead);
            if (bytesRead > 0) {
                totalRead += bytesRead;
                startTime = std::chrono::steady_clock::now();
            } else {
                auto elapsed = std::chrono::steady_clock::now() - startTime;
                if (elapsed > charTimeout) break;
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }

        if (totalRead < 10) return false;
        frame.assign(buffer, buffer + totalRead);
    } else {
        // TCP: read full frame
        uint8_t header[10];
        ssize_t received = recv(m_fd, header, 10, MSG_WAITALL);
        if (received != 10) return false;

        if (header[0] != 0x05 || header[1] != 0x64) return false;

        uint8_t len = header[2];
        size_t payloadLen = 0;
        if (len > 5) {
            payloadLen = (len - 5) * 18;
            if (payloadLen > 250) payloadLen = 250;
        }

        frame.assign(header, header + 10);

        if (payloadLen > 0) {
            std::vector<uint8_t> payload(payloadLen);
            received = recv(m_fd, payload.data(), payloadLen, MSG_DONTWAIT);
            if (received > 0) {
                frame.insert(frame.end(), payload.begin(), payload.begin() + received);
            }
        }
    }

    std::lock_guard<std::mutex> statsLock(m_statsMutex);
    m_framesReceived++;
    return true;
}

// ---------------------------------------------------------------------------
// Unsolicited listener
// ---------------------------------------------------------------------------
void Dnp3Client::unsolicitedListenerLoop() {
    while (m_running.load()) {
        std::vector<uint8_t> response;
        if (receiveFrame(response, std::chrono::milliseconds(500))) {
            // Check if this is an unsolicited response (func code 0x81 or 0x82)
            uint8_t ctrl;
            uint16_t dest, src;
            std::vector<uint8_t> userData;

            if (parseLinkFrame(response, ctrl, dest, src, userData)) {
                // Parse transport and application layers
                if (userData.size() > 1) {
                    uint8_t appCtrl = userData[1]; // Skip transport header
                    uint8_t appFunc = userData.size() > 2 ? userData[2] : 0;
                    (void)appCtrl;

                    if (appFunc == 0x81 || appFunc == 0x82) {
                        // Unsolicited response
                        std::vector<Dnp3AnalogPoint> analogs;
                        std::vector<Dnp3BinaryPoint> binaries;

                        if (m_unsolicitedCallback) {
                            m_unsolicitedCallback(analogs, binaries);
                        }
                    }
                }
            }
        }
    }
}

// CROB control codes
static constexpr uint8_t CROB_NUL = 0x00;
static constexpr uint8_t CROB_PULSE_ON = 0x01;
static constexpr uint8_t CROB_PULSE_OFF = 0x02;
static constexpr uint8_t CROB_LATCH_ON = 0x03;
static constexpr uint8_t CROB_LATCH_OFF = 0x04;
static constexpr uint8_t CROB_CLOSE = 0x41; // Close + queue
static constexpr uint8_t CROB_TRIP = 0x81;  // Trip + queue

} // namespace powsys365
