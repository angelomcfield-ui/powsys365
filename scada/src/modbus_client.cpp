#include "powsy365/scada/modbus_client.h"
#include <iostream>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <termios.h>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace powsys365 {

std::string modbusExceptionToString(ModbusException ex) {
    switch (ex) {
        case ModbusException::NONE: return "None";
        case ModbusException::ILLEGAL_FUNCTION: return "Illegal Function";
        case ModbusException::ILLEGAL_DATA_ADDRESS: return "Illegal Data Address";
        case ModbusException::ILLEGAL_DATA_VALUE: return "Illegal Data Value";
        case ModbusException::SLAVE_DEVICE_FAILURE: return "Slave Device Failure";
        case ModbusException::ACKNOWLEDGE: return "Acknowledge";
        case ModbusException::SLAVE_DEVICE_BUSY: return "Slave Device Busy";
        case ModbusException::NEGATIVE_ACKNOWLEDGE: return "Negative Acknowledge";
        case ModbusException::MEMORY_PARITY_ERROR: return "Memory Parity Error";
        case ModbusException::GATEWAY_PATH_UNAVAILABLE: return "Gateway Path Unavailable";
        case ModbusException::GATEWAY_TARGET_DEVICE_FAILED: return "Gateway Target Device Failed";
        default: return "Unknown";
    }
}

// ============================================================================
// ModbusClient
// ============================================================================
ModbusClient::ModbusClient() = default;

ModbusClient::~ModbusClient() {
    stopPolling();
    disconnect();
}

// ---------------------------------------------------------------------------
// Connection lifecycle
// ---------------------------------------------------------------------------
bool ModbusClient::connect(const ModbusEndpoint& endpoint) {
    if (m_connected.load()) return true;

    m_endpoint = endpoint;

    if (endpoint.isRtu) {
        // Serial RTU connection
        m_fd = ::open(endpoint.address.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
        if (m_fd < 0) return false;

        struct termios tty;
        if (tcgetattr(m_fd, &tty) != 0) {
            ::close(m_fd);
            m_fd = -1;
            return false;
        }

        speed_t baud;
        switch (endpoint.baudRate) {
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

        tty.c_cflag &= ~CSIZE;
        tty.c_cflag |= CS8;

        if (endpoint.stopBits == 2) tty.c_cflag |= CSTOPB;
        else tty.c_cflag &= ~CSTOPB;

        if (endpoint.parity == 'E') {
            tty.c_cflag |= PARENB;
            tty.c_cflag &= ~PARODD;
        } else if (endpoint.parity == 'O') {
            tty.c_cflag |= PARENB;
            tty.c_cflag |= PARODD;
        } else {
            tty.c_cflag &= ~PARENB;
        }

        tty.c_cflag |= CREAD | CLOCAL;
        tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
        tty.c_iflag &= ~(IXON | IXOFF | IXANY);
        tty.c_oflag &= ~OPOST;

        tty.c_cc[VMIN] = 0;
        tty.c_cc[VTIME] = 1;

        if (tcsetattr(m_fd, TCSANOW, &tty) != 0) {
            ::close(m_fd);
            m_fd = -1;
            return false;
        }
    } else {
        // TCP connection
        m_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (m_fd < 0) return false;

        int flags = fcntl(m_fd, F_GETFL, 0);
        fcntl(m_fd, F_SETFL, flags | O_NONBLOCK);

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
    return true;
}

void ModbusClient::disconnect() {
    stopPolling();

    m_connected.store(false);
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

bool ModbusClient::isConnected() const {
    return m_connected.load();
}

bool ModbusClient::reconnect() {
    disconnect();
    return connect(m_endpoint);
}

// ---------------------------------------------------------------------------
// CRC-16 for Modbus RTU
// ---------------------------------------------------------------------------
uint16_t ModbusClient::calculateCrc16(const std::vector<uint8_t>& data) {
    uint16_t crc = 0xFFFF;
    for (uint8_t byte : data) {
        crc ^= byte;
        for (int i = 0; i < 8; ++i) {
            if (crc & 0x0001) {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

// ---------------------------------------------------------------------------
// Frame building
// ---------------------------------------------------------------------------
std::vector<uint8_t> ModbusClient::buildTcpFrame(uint16_t transactionId, uint8_t unitId,
                                                   uint8_t functionCode,
                                                   const std::vector<uint8_t>& data) {
    std::vector<uint8_t> frame;
    // MBAP header
    frame.push_back(static_cast<uint8_t>((transactionId >> 8) & 0xFF));
    frame.push_back(static_cast<uint8_t>(transactionId & 0xFF));
    frame.push_back(0x00); // Protocol ID (Modbus)
    frame.push_back(0x00);
    uint16_t length = static_cast<uint16_t>(1 + 1 + data.size()); // unit + FC + data
    frame.push_back(static_cast<uint8_t>((length >> 8) & 0xFF));
    frame.push_back(static_cast<uint8_t>(length & 0xFF));
    frame.push_back(unitId);
    frame.push_back(functionCode);
    frame.insert(frame.end(), data.begin(), data.end());
    return frame;
}

std::vector<uint8_t> ModbusClient::buildRtuFrame(uint8_t slaveId, uint8_t functionCode,
                                                   const std::vector<uint8_t>& data) {
    std::vector<uint8_t> frame;
    frame.push_back(slaveId);
    frame.push_back(functionCode);
    frame.insert(frame.end(), data.begin(), data.end());
    uint16_t crc = calculateCrc16(frame);
    frame.push_back(static_cast<uint8_t>(crc & 0xFF));
    frame.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
    return frame;
}

// ---------------------------------------------------------------------------
// Frame parsing
// ---------------------------------------------------------------------------
bool ModbusClient::parseTcpResponse(const std::vector<uint8_t>& response,
                                      uint16_t expectedTransactionId,
                                      uint8_t& unitId, uint8_t& functionCode,
                                      std::vector<uint8_t>& data,
                                      ModbusException& exception) {
    if (response.size() < 9) return false;

    uint16_t tid = (static_cast<uint16_t>(response[0]) << 8) | response[1];
    if (tid != expectedTransactionId) return false;

    // Protocol ID check
    if (response[2] != 0x00 || response[3] != 0x00) return false;

    uint16_t length = (static_cast<uint16_t>(response[4]) << 8) | response[5];
    if (length + 6 > response.size()) return false;

    unitId = response[6];
    functionCode = response[7];

    // Check for exception
    if (functionCode & 0x80) {
        exception = static_cast<ModbusException>(response[8]);
        functionCode &= 0x7F;
        return true; // Response was received but it's an exception
    }

    exception = ModbusException::NONE;
    data.assign(response.begin() + 9, response.begin() + 6 + length);
    return true;
}

bool ModbusClient::parseRtuResponse(const std::vector<uint8_t>& response,
                                      uint8_t expectedSlaveId,
                                      uint8_t& functionCode,
                                      std::vector<uint8_t>& data,
                                      ModbusException& exception) {
    if (response.size() < 5) return false;

    uint8_t slaveId = response[0];
    if (slaveId != expectedSlaveId) return false;

    // Validate CRC
    if (response.size() >= 2) {
        std::vector<uint8_t> crcCheck(response.begin(), response.end() - 2);
        uint16_t receivedCrc = (static_cast<uint16_t>(response[response.size()-1]) << 8) |
                                response[response.size()-2];
        uint16_t calcCrc = calculateCrc16(crcCheck);
        if (calcCrc != receivedCrc) return false;
    }

    functionCode = response[1];

    if (functionCode & 0x80) {
        exception = static_cast<ModbusException>(response[2]);
        functionCode &= 0x7F;
        return true;
    }

    exception = ModbusException::NONE;
    data.assign(response.begin() + 2, response.end() - 2);
    return true;
}

// ---------------------------------------------------------------------------
// Communication
// ---------------------------------------------------------------------------
bool ModbusClient::sendRaw(const std::vector<uint8_t>& frame) {
    if (m_fd < 0) return false;

    ssize_t sent;
    if (m_endpoint.isRtu) {
        sent = ::write(m_fd, frame.data(), frame.size());
        tcdrain(m_fd);
    } else {
        sent = send(m_fd, frame.data(), frame.size(), MSG_NOSIGNAL);
    }

    if (sent < 0) return false;

    std::lock_guard<std::mutex> lock(m_statsMutex);
    m_requestsSent++;
    return true;
}

bool ModbusClient::receiveRaw(std::vector<uint8_t>& response, size_t expectedLen,
                                std::chrono::milliseconds timeout) {
    if (m_fd < 0) return false;

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(m_fd, &readfds);
    struct timeval tv;
    tv.tv_sec = static_cast<long>(timeout.count() / 1000);
    tv.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);

    int ready = select(m_fd + 1, &readfds, nullptr, nullptr, &tv);
    if (ready <= 0) return false;

    if (m_endpoint.isRtu) {
        // RTU: read until inter-character timeout
        uint8_t buffer[512];
        ssize_t totalRead = 0;
        auto lastRead = std::chrono::steady_clock::now();
        auto charTimeout = std::chrono::milliseconds(
            (1000 * 11) / m_endpoint.baudRate + 1);

        while (totalRead < static_cast<ssize_t>(sizeof(buffer))) {
            ssize_t bytesRead = ::read(m_fd, buffer + totalRead, sizeof(buffer) - totalRead);
            if (bytesRead > 0) {
                totalRead += bytesRead;
                lastRead = std::chrono::steady_clock::now();
            } else {
                auto elapsed = std::chrono::steady_clock::now() - lastRead;
                if (elapsed > charTimeout) break;
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
        if (totalRead < 5) return false;
        response.assign(buffer, buffer + totalRead);
    } else {
        // TCP
        response.resize(expectedLen);
        ssize_t received = recv(m_fd, response.data(), expectedLen, MSG_WAITALL);
        if (received <= 0) return false;
        response.resize(received);
    }

    std::lock_guard<std::mutex> lock(m_statsMutex);
    m_responsesReceived++;
    return true;
}

bool ModbusClient::tcpTransaction(const std::vector<uint8_t>& request,
                                    std::vector<uint8_t>& response, size_t expectedResponseLen) {
    if (!sendRaw(request)) return false;

    auto start = std::chrono::steady_clock::now();
    if (!receiveRaw(response, expectedResponseLen, m_endpoint.responseTimeout)) {
        std::lock_guard<std::mutex> lock(m_statsMutex);
        m_timeouts++;
        return false;
    }
    auto end = std::chrono::steady_clock::now();

    double elapsed = std::chrono::duration<double, std::milli>(end - start).count();
    std::lock_guard<std::mutex> lock(m_statsMutex);
    m_responseTimes.push_back(elapsed);
    if (m_responseTimes.size() > 1000) m_responseTimes.erase(m_responseTimes.begin());

    return true;
}

bool ModbusClient::rtuTransaction(const std::vector<uint8_t>& request,
                                    std::vector<uint8_t>& response, size_t) {
    if (!sendRaw(request)) return false;

    // Wait for turnaround delay (3.5 char times)
    std::this_thread::sleep_for(std::chrono::microseconds(
        (35000000) / m_endpoint.baudRate));

    auto start = std::chrono::steady_clock::now();
    if (!receiveRaw(response, 256, m_endpoint.responseTimeout)) {
        std::lock_guard<std::mutex> lock(m_statsMutex);
        m_timeouts++;
        return false;
    }
    auto end = std::chrono::steady_clock::now();

    double elapsed = std::chrono::duration<double, std::milli>(end - start).count();
    std::lock_guard<std::mutex> lock(m_statsMutex);
    m_responseTimes.push_back(elapsed);
    if (m_responseTimes.size() > 1000) m_responseTimes.erase(m_responseTimes.begin());

    return true;
}

// ---------------------------------------------------------------------------
// Core Modbus functions
// ---------------------------------------------------------------------------
ReadRegistersResponse ModbusClient::readHoldingRegisters(uint16_t startAddress, uint16_t count) {
    ReadRegistersResponse result;
    if (!m_connected.load()) {
        result.exception = ModbusException::SLAVE_DEVICE_FAILURE;
        return result;
    }

    std::vector<uint8_t> requestData;
    requestData.push_back(static_cast<uint8_t>((startAddress >> 8) & 0xFF));
    requestData.push_back(static_cast<uint8_t>(startAddress & 0xFF));
    requestData.push_back(static_cast<uint8_t>((count >> 8) & 0xFF));
    requestData.push_back(static_cast<uint8_t>(count & 0xFF));

    std::vector<uint8_t> frame;
    if (m_endpoint.isRtu) {
        frame = buildRtuFrame(static_cast<uint8_t>(m_endpoint.slaveId), 0x03, requestData);
    } else {
        uint16_t tid = m_transactionId++;
        frame = buildTcpFrame(tid, static_cast<uint8_t>(m_endpoint.slaveId), 0x03, requestData);
    }

    std::vector<uint8_t> response;
    size_t expectedLen = m_endpoint.isRtu ? (5 + count * 2 + 2) : (9 + count * 2);
    bool success = m_endpoint.isRtu ?
        rtuTransaction(frame, response, expectedLen) :
        tcpTransaction(frame, response, expectedLen);

    if (!success) {
        result.exception = ModbusException::SLAVE_DEVICE_FAILURE;
        return result;
    }

    uint8_t unitId = 0, functionCode = 0;
    std::vector<uint8_t> data;
    ModbusException exception;

    bool parsed;
    if (m_endpoint.isRtu) {
        parsed = parseRtuResponse(response, static_cast<uint8_t>(m_endpoint.slaveId),
                                    functionCode, data, exception);
    } else {
        parsed = parseTcpResponse(response, m_transactionId - 1, unitId, functionCode,
                                    data, exception);
    }

    if (!parsed || exception != ModbusException::NONE) {
        result.exception = exception;
        if (exception != ModbusException::NONE) {
            std::lock_guard<std::mutex> lock(m_statsMutex);
            m_errors++;
        }
        return result;
    }

    if (data.size() < 1 || data[0] != count * 2) {
        result.exception = ModbusException::SLAVE_DEVICE_FAILURE;
        return result;
    }

    for (size_t i = 1; i + 1 < data.size(); i += 2) {
        uint16_t reg = (static_cast<uint16_t>(data[i]) << 8) | data[i + 1];
        result.registers.push_back(reg);
    }

    result.success = true;
    result.timestamp = std::chrono::system_clock::now();
    return result;
}

ReadRegistersResponse ModbusClient::readInputRegisters(uint16_t startAddress, uint16_t count) {
    ReadRegistersResponse result;
    if (!m_connected.load()) {
        result.exception = ModbusException::SLAVE_DEVICE_FAILURE;
        return result;
    }

    std::vector<uint8_t> requestData;
    requestData.push_back(static_cast<uint8_t>((startAddress >> 8) & 0xFF));
    requestData.push_back(static_cast<uint8_t>(startAddress & 0xFF));
    requestData.push_back(static_cast<uint8_t>((count >> 8) & 0xFF));
    requestData.push_back(static_cast<uint8_t>(count & 0xFF));

    std::vector<uint8_t> frame;
    if (m_endpoint.isRtu) {
        frame = buildRtuFrame(static_cast<uint8_t>(m_endpoint.slaveId), 0x04, requestData);
    } else {
        uint16_t tid = m_transactionId++;
        frame = buildTcpFrame(tid, static_cast<uint8_t>(m_endpoint.slaveId), 0x04, requestData);
    }

    std::vector<uint8_t> response;
    size_t expectedLen = m_endpoint.isRtu ? (5 + count * 2 + 2) : (9 + count * 2);
    bool success = m_endpoint.isRtu ?
        rtuTransaction(frame, response, expectedLen) :
        tcpTransaction(frame, response, expectedLen);

    if (!success) {
        result.exception = ModbusException::SLAVE_DEVICE_FAILURE;
        return result;
    }

    uint8_t unitId = 0, functionCode = 0;
    std::vector<uint8_t> data;
    ModbusException exception;

    bool parsed;
    if (m_endpoint.isRtu) {
        parsed = parseRtuResponse(response, static_cast<uint8_t>(m_endpoint.slaveId),
                                    functionCode, data, exception);
    } else {
        parsed = parseTcpResponse(response, m_transactionId - 1, unitId, functionCode,
                                    data, exception);
    }

    if (!parsed || exception != ModbusException::NONE) {
        result.exception = exception;
        return result;
    }

    if (data.size() < 1 || data[0] != count * 2) {
        result.exception = ModbusException::SLAVE_DEVICE_FAILURE;
        return result;
    }

    for (size_t i = 1; i + 1 < data.size(); i += 2) {
        uint16_t reg = (static_cast<uint16_t>(data[i]) << 8) | data[i + 1];
        result.registers.push_back(reg);
    }

    result.success = true;
    result.timestamp = std::chrono::system_clock::now();
    return result;
}

std::vector<bool> ModbusClient::readCoils(uint16_t startAddress, uint16_t count) {
    std::vector<bool> result;
    if (!m_connected.load()) return result;

    std::vector<uint8_t> requestData;
    requestData.push_back(static_cast<uint8_t>((startAddress >> 8) & 0xFF));
    requestData.push_back(static_cast<uint8_t>(startAddress & 0xFF));
    requestData.push_back(static_cast<uint8_t>((count >> 8) & 0xFF));
    requestData.push_back(static_cast<uint8_t>(count & 0xFF));

    std::vector<uint8_t> frame;
    if (m_endpoint.isRtu) {
        frame = buildRtuFrame(static_cast<uint8_t>(m_endpoint.slaveId), 0x01, requestData);
    } else {
        uint16_t tid = m_transactionId++;
        frame = buildTcpFrame(tid, static_cast<uint8_t>(m_endpoint.slaveId), 0x01, requestData);
    }

    std::vector<uint8_t> response;
    uint8_t byteCount = (count + 7) / 8;
    size_t expectedLen = m_endpoint.isRtu ? (5 + byteCount + 2) : (9 + byteCount);
    bool success = m_endpoint.isRtu ?
        rtuTransaction(frame, response, expectedLen) :
        tcpTransaction(frame, response, expectedLen);

    if (!success) return result;

    uint8_t unitId = 0, functionCode = 0;
    std::vector<uint8_t> data;
    ModbusException exception;

    bool parsed;
    if (m_endpoint.isRtu) {
        parsed = parseRtuResponse(response, static_cast<uint8_t>(m_endpoint.slaveId),
                                    functionCode, data, exception);
    } else {
        parsed = parseTcpResponse(response, m_transactionId - 1, unitId, functionCode,
                                    data, exception);
    }

    if (!parsed || exception != ModbusException::NONE || data.size() < 2) return result;

    uint8_t bc = data[0];
    for (uint16_t i = 0; i < count && i < bc * 8; ++i) {
        uint8_t byte = data[1 + i / 8];
        result.push_back((byte >> (i % 8)) & 0x01);
    }

    return result;
}

std::vector<bool> ModbusClient::readDiscreteInputs(uint16_t startAddress, uint16_t count) {
    std::vector<bool> result;
    if (!m_connected.load()) return result;

    std::vector<uint8_t> requestData;
    requestData.push_back(static_cast<uint8_t>((startAddress >> 8) & 0xFF));
    requestData.push_back(static_cast<uint8_t>(startAddress & 0xFF));
    requestData.push_back(static_cast<uint8_t>((count >> 8) & 0xFF));
    requestData.push_back(static_cast<uint8_t>(count & 0xFF));

    std::vector<uint8_t> frame;
    if (m_endpoint.isRtu) {
        frame = buildRtuFrame(static_cast<uint8_t>(m_endpoint.slaveId), 0x02, requestData);
    } else {
        uint16_t tid = m_transactionId++;
        frame = buildTcpFrame(tid, static_cast<uint8_t>(m_endpoint.slaveId), 0x02, requestData);
    }

    std::vector<uint8_t> response;
    uint8_t byteCount = (count + 7) / 8;
    size_t expectedLen = m_endpoint.isRtu ? (5 + byteCount + 2) : (9 + byteCount);
    bool success = m_endpoint.isRtu ?
        rtuTransaction(frame, response, expectedLen) :
        tcpTransaction(frame, response, expectedLen);

    if (!success) return result;

    uint8_t unitId = 0, functionCode = 0;
    std::vector<uint8_t> data;
    ModbusException exception;

    bool parsed;
    if (m_endpoint.isRtu) {
        parsed = parseRtuResponse(response, static_cast<uint8_t>(m_endpoint.slaveId),
                                    functionCode, data, exception);
    } else {
        parsed = parseTcpResponse(response, m_transactionId - 1, unitId, functionCode,
                                    data, exception);
    }

    if (!parsed || exception != ModbusException::NONE || data.size() < 2) return result;

    uint8_t bc = data[0];
    for (uint16_t i = 0; i < count && i < bc * 8; ++i) {
        uint8_t byte = data[1 + i / 8];
        result.push_back((byte >> (i % 8)) & 0x01);
    }

    return result;
}

WriteRegisterResponse ModbusClient::writeSingleRegister(uint16_t address, uint16_t value) {
    WriteRegisterResponse result;
    if (!m_connected.load()) return result;

    std::vector<uint8_t> requestData;
    requestData.push_back(static_cast<uint8_t>((address >> 8) & 0xFF));
    requestData.push_back(static_cast<uint8_t>(address & 0xFF));
    requestData.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    requestData.push_back(static_cast<uint8_t>(value & 0xFF));

    std::vector<uint8_t> frame;
    if (m_endpoint.isRtu) {
        frame = buildRtuFrame(static_cast<uint8_t>(m_endpoint.slaveId), 0x06, requestData);
    } else {
        uint16_t tid = m_transactionId++;
        frame = buildTcpFrame(tid, static_cast<uint8_t>(m_endpoint.slaveId), 0x06, requestData);
    }

    std::vector<uint8_t> response;
    size_t expectedLen = m_endpoint.isRtu ? 8 : 12;
    bool success = m_endpoint.isRtu ?
        rtuTransaction(frame, response, expectedLen) :
        tcpTransaction(frame, response, expectedLen);

    if (!success) return result;

    uint8_t unitId = 0, functionCode = 0;
    std::vector<uint8_t> data;
    ModbusException exception;

    bool parsed;
    if (m_endpoint.isRtu) {
        parsed = parseRtuResponse(response, static_cast<uint8_t>(m_endpoint.slaveId),
                                    functionCode, data, exception);
    } else {
        parsed = parseTcpResponse(response, m_transactionId - 1, unitId, functionCode,
                                    data, exception);
    }

    if (!parsed || exception != ModbusException::NONE || data.size() < 4) {
        result.exception = exception;
        return result;
    }

    result.writtenAddress = (static_cast<uint16_t>(data[0]) << 8) | data[1];
    result.writtenValue = (static_cast<uint16_t>(data[2]) << 8) | data[3];
    result.success = true;
    result.timestamp = std::chrono::system_clock::now();
    return result;
}

WriteRegisterResponse ModbusClient::writeMultipleRegisters(uint16_t startAddress,
                                                               const std::vector<uint16_t>& values) {
    WriteRegisterResponse result;
    if (!m_connected.load() || values.empty()) return result;

    std::vector<uint8_t> requestData;
    requestData.push_back(static_cast<uint8_t>((startAddress >> 8) & 0xFF));
    requestData.push_back(static_cast<uint8_t>(startAddress & 0xFF));
    requestData.push_back(static_cast<uint8_t>((values.size() >> 8) & 0xFF));
    requestData.push_back(static_cast<uint8_t>(values.size() & 0xFF));
    requestData.push_back(static_cast<uint8_t>(values.size() * 2));

    for (uint16_t val : values) {
        requestData.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
        requestData.push_back(static_cast<uint8_t>(val & 0xFF));
    }

    std::vector<uint8_t> frame;
    if (m_endpoint.isRtu) {
        frame = buildRtuFrame(static_cast<uint8_t>(m_endpoint.slaveId), 0x10, requestData);
    } else {
        uint16_t tid = m_transactionId++;
        frame = buildTcpFrame(tid, static_cast<uint8_t>(m_endpoint.slaveId), 0x10, requestData);
    }

    std::vector<uint8_t> response;
    size_t expectedLen = m_endpoint.isRtu ? 8 : 12;
    bool success = m_endpoint.isRtu ?
        rtuTransaction(frame, response, expectedLen) :
        tcpTransaction(frame, response, expectedLen);

    if (!success) return result;

    uint8_t unitId = 0, functionCode = 0;
    std::vector<uint8_t> data;
    ModbusException exception;

    bool parsed;
    if (m_endpoint.isRtu) {
        parsed = parseRtuResponse(response, static_cast<uint8_t>(m_endpoint.slaveId),
                                    functionCode, data, exception);
    } else {
        parsed = parseTcpResponse(response, m_transactionId - 1, unitId, functionCode,
                                    data, exception);
    }

    if (!parsed || exception != ModbusException::NONE || data.size() < 4) {
        result.exception = exception;
        return result;
    }

    result.writtenAddress = (static_cast<uint16_t>(data[0]) << 8) | data[1];
    result.writtenValue = static_cast<uint16_t>((data[2] << 8) | data[3]);
    result.success = true;
    result.timestamp = std::chrono::system_clock::now();
    return result;
}

WriteRegisterResponse ModbusClient::writeSingleCoil(uint16_t address, bool value) {
    WriteRegisterResponse result;
    if (!m_connected.load()) return result;

    std::vector<uint8_t> requestData;
    requestData.push_back(static_cast<uint8_t>((address >> 8) & 0xFF));
    requestData.push_back(static_cast<uint8_t>(address & 0xFF));
    requestData.push_back(value ? 0xFF : 0x00);
    requestData.push_back(0x00);

    std::vector<uint8_t> frame;
    if (m_endpoint.isRtu) {
        frame = buildRtuFrame(static_cast<uint8_t>(m_endpoint.slaveId), 0x05, requestData);
    } else {
        uint16_t tid = m_transactionId++;
        frame = buildTcpFrame(tid, static_cast<uint8_t>(m_endpoint.slaveId), 0x05, requestData);
    }

    std::vector<uint8_t> response;
    size_t expectedLen = m_endpoint.isRtu ? 8 : 12;
    bool success = m_endpoint.isRtu ?
        rtuTransaction(frame, response, expectedLen) :
        tcpTransaction(frame, response, expectedLen);

    if (!success) return result;

    uint8_t unitId = 0, functionCode = 0;
    std::vector<uint8_t> data;
    ModbusException exception;

    bool parsed;
    if (m_endpoint.isRtu) {
        parsed = parseRtuResponse(response, static_cast<uint8_t>(m_endpoint.slaveId),
                                    functionCode, data, exception);
    } else {
        parsed = parseTcpResponse(response, m_transactionId - 1, unitId, functionCode,
                                    data, exception);
    }

    if (!parsed || exception != ModbusException::NONE || data.size() < 4) {
        result.exception = exception;
        return result;
    }

    result.writtenAddress = (static_cast<uint16_t>(data[0]) << 8) | data[1];
    result.writtenValue = (static_cast<uint16_t>(data[2]) << 8) | data[3];
    result.success = true;
    result.timestamp = std::chrono::system_clock::now();
    return result;
}

WriteRegisterResponse ModbusClient::writeMultipleCoils(uint16_t startAddress,
                                                           const std::vector<bool>& values) {
    WriteRegisterResponse result;
    if (!m_connected.load() || values.empty()) return result;

    uint16_t count = static_cast<uint16_t>(values.size());
    uint8_t byteCount = (count + 7) / 8;
    std::vector<uint8_t> coilBytes(byteCount, 0);
    for (size_t i = 0; i < values.size(); ++i) {
        if (values[i]) coilBytes[i / 8] |= (1 << (i % 8));
    }

    std::vector<uint8_t> requestData;
    requestData.push_back(static_cast<uint8_t>((startAddress >> 8) & 0xFF));
    requestData.push_back(static_cast<uint8_t>(startAddress & 0xFF));
    requestData.push_back(static_cast<uint8_t>((count >> 8) & 0xFF));
    requestData.push_back(static_cast<uint8_t>(count & 0xFF));
    requestData.push_back(byteCount);
    requestData.insert(requestData.end(), coilBytes.begin(), coilBytes.end());

    std::vector<uint8_t> frame;
    if (m_endpoint.isRtu) {
        frame = buildRtuFrame(static_cast<uint8_t>(m_endpoint.slaveId), 0x0F, requestData);
    } else {
        uint16_t tid = m_transactionId++;
        frame = buildTcpFrame(tid, static_cast<uint8_t>(m_endpoint.slaveId), 0x0F, requestData);
    }

    std::vector<uint8_t> response;
    size_t expectedLen = m_endpoint.isRtu ? 8 : 12;
    bool success = m_endpoint.isRtu ?
        rtuTransaction(frame, response, expectedLen) :
        tcpTransaction(frame, response, expectedLen);

    if (!success) return result;

    uint8_t unitId = 0, functionCode = 0;
    std::vector<uint8_t> data;
    ModbusException exception;

    bool parsed;
    if (m_endpoint.isRtu) {
        parsed = parseRtuResponse(response, static_cast<uint8_t>(m_endpoint.slaveId),
                                    functionCode, data, exception);
    } else {
        parsed = parseTcpResponse(response, m_transactionId - 1, unitId, functionCode,
                                    data, exception);
    }

    if (!parsed || exception != ModbusException::NONE || data.size() < 4) {
        result.exception = exception;
        return result;
    }

    result.writtenAddress = (static_cast<uint16_t>(data[0]) << 8) | data[1];
    result.writtenValue = static_cast<uint16_t>((data[2] << 8) | data[3]);
    result.success = true;
    result.timestamp = std::chrono::system_clock::now();
    return result;
}

// ---------------------------------------------------------------------------
// 32-bit operations
// ---------------------------------------------------------------------------
std::optional<float> ModbusClient::readFloat32(uint16_t address, RegisterType type) {
    uint8_t funcCode = (type == RegisterType::HOLDING_REGISTER) ? 0x03 : 0x04;
    (void)funcCode;

    auto response = readHoldingRegisters(address, 2);
    if (!response.success || response.registers.size() < 2) return std::nullopt;

    return registersToFloat(response.registers[0], response.registers[1]);
}

std::optional<uint32_t> ModbusClient::readUInt32(uint16_t address, RegisterType type) {
    (void)type;
    auto response = readHoldingRegisters(address, 2);
    if (!response.success || response.registers.size() < 2) return std::nullopt;

    return registersToUInt32(response.registers[0], response.registers[1]);
}

std::optional<int32_t> ModbusClient::readInt32(uint16_t address, RegisterType type) {
    (void)type;
    auto response = readHoldingRegisters(address, 2);
    if (!response.success || response.registers.size() < 2) return std::nullopt;

    return registersToInt32(response.registers[0], response.registers[1]);
}

bool ModbusClient::writeFloat32(uint16_t address, float value) {
    auto [high, low] = floatToRegisters(value);
    auto result = writeMultipleRegisters(address, {high, low});
    return result.success;
}

bool ModbusClient::writeUInt32(uint16_t address, uint32_t value) {
    auto [high, low] = uint32ToRegisters(value);
    auto result = writeMultipleRegisters(address, {high, low});
    return result.success;
}

bool ModbusClient::writeInt32(uint16_t address, int32_t value) {
    auto [high, low] = int32ToRegisters(value);
    auto result = writeMultipleRegisters(address, {high, low});
    return result.success;
}

// ---------------------------------------------------------------------------
// Data conversion
// ---------------------------------------------------------------------------
float ModbusClient::registersToFloat(uint16_t high, uint16_t low) {
    uint32_t combined = (static_cast<uint32_t>(high) << 16) | low;
    float result;
    std::memcpy(&result, &combined, sizeof(result));
    return result;
}

uint32_t ModbusClient::registersToUInt32(uint16_t high, uint16_t low) {
    return (static_cast<uint32_t>(high) << 16) | low;
}

int32_t ModbusClient::registersToInt32(uint16_t high, uint16_t low) {
    return static_cast<int32_t>((static_cast<uint32_t>(high) << 16) | low);
}

std::pair<uint16_t, uint16_t> ModbusClient::floatToRegisters(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return {static_cast<uint16_t>((bits >> 16) & 0xFFFF),
            static_cast<uint16_t>(bits & 0xFFFF)};
}

std::pair<uint16_t, uint16_t> ModbusClient::uint32ToRegisters(uint32_t value) {
    return {static_cast<uint16_t>((value >> 16) & 0xFFFF),
            static_cast<uint16_t>(value & 0xFFFF)};
}

std::pair<uint16_t, uint16_t> ModbusClient::int32ToRegisters(int32_t value) {
    uint32_t uval = static_cast<uint32_t>(value);
    return {static_cast<uint16_t>((uval >> 16) & 0xFFFF),
            static_cast<uint16_t>(uval & 0xFFFF)};
}

ElectricalVariable ModbusClient::decodeRegisters(const RegisterMapping& mapping,
                                                     const std::vector<uint16_t>& registers) {
    ElectricalVariable var;
    var.name = mapping.variableName;
    var.unit = mapping.unit;
    var.timestamp = std::chrono::system_clock::now();
    var.valid = false;

    if (registers.size() < static_cast<size_t>(mapping.registerCount)) return var;

    if (mapping.registerCount == 1) {
        if (mapping.dataType == "UINT16") {
            var.value = static_cast<double>(registers[0]) * mapping.scaleFactor + mapping.offset;
            var.rawRegister[0] = registers[0];
            var.valid = true;
        } else if (mapping.dataType == "INT16") {
            var.value = static_cast<double>(static_cast<int16_t>(registers[0])) *
                         mapping.scaleFactor + mapping.offset;
            var.rawRegister[0] = registers[0];
            var.valid = true;
        }
    } else if (mapping.registerCount == 2 && registers.size() >= 2) {
        if (mapping.dataType == "FLOAT32") {
            var.value = static_cast<double>(registersToFloat(registers[0], registers[1])) *
                         mapping.scaleFactor + mapping.offset;
            var.rawRegister[0] = registers[0];
            var.rawRegister[1] = registers[1];
            var.valid = true;
        } else if (mapping.dataType == "UINT32") {
            var.value = static_cast<double>(registersToUInt32(registers[0], registers[1])) *
                         mapping.scaleFactor + mapping.offset;
            var.rawRegister[0] = registers[0];
            var.rawRegister[1] = registers[1];
            var.valid = true;
        } else if (mapping.dataType == "INT32") {
            var.value = static_cast<double>(registersToInt32(registers[0], registers[1])) *
                         mapping.scaleFactor + mapping.offset;
            var.rawRegister[0] = registers[0];
            var.rawRegister[1] = registers[1];
            var.valid = true;
        }
    }

    return var;
}

// ---------------------------------------------------------------------------
// Register mapping management
// ---------------------------------------------------------------------------
void ModbusClient::addRegisterMapping(const RegisterMapping& mapping) {
    std::lock_guard<std::mutex> lock(m_mappingsMutex);
    m_registerMappings[mapping.variableName] = mapping;
}

void ModbusClient::removeRegisterMapping(const std::string& variableName) {
    std::lock_guard<std::mutex> lock(m_mappingsMutex);
    m_registerMappings.erase(variableName);
}

std::vector<RegisterMapping> ModbusClient::getRegisterMappings() const {
    std::lock_guard<std::mutex> lock(m_mappingsMutex);
    std::vector<RegisterMapping> result;
    for (const auto& [name, mapping] : m_registerMappings) {
        result.push_back(mapping);
    }
    return result;
}

std::map<std::string, ElectricalVariable> ModbusClient::readAllMappedVariables() {
    std::map<std::string, ElectricalVariable> result;
    std::vector<RegisterMapping> mappings;
    {
        std::lock_guard<std::mutex> lock(m_mappingsMutex);
        for (const auto& [name, mapping] : m_registerMappings) {
            mappings.push_back(mapping);
        }
    }

    for (const auto& mapping : mappings) {
        auto var = readVariable(mapping.variableName);
        if (var.has_value()) {
            result[mapping.variableName] = var.value();
        }
    }

    return result;
}

std::optional<ElectricalVariable> ModbusClient::readVariable(const std::string& variableName) {
    std::lock_guard<std::mutex> lock(m_mappingsMutex);
    auto it = m_registerMappings.find(variableName);
    if (it == m_registerMappings.end()) return std::nullopt;

    const auto& mapping = it->second;
    lock.unlock();

    ReadRegistersResponse response;
    if (mapping.regType == RegisterType::HOLDING_REGISTER) {
        response = readHoldingRegisters(mapping.address, static_cast<uint16_t>(mapping.registerCount));
    } else {
        response = readInputRegisters(mapping.address, static_cast<uint16_t>(mapping.registerCount));
    }

    if (!response.success) return std::nullopt;

    return decodeRegisters(mapping, response.registers);
}

// ---------------------------------------------------------------------------
// Polling system
// ---------------------------------------------------------------------------
void ModbusClient::addPollGroup(const PollGroup& group) {
    std::lock_guard<std::mutex> lock(m_pollGroupsMutex);
    m_pollGroups[group.groupId] = group;
}

void ModbusClient::removePollGroup(const std::string& groupId) {
    std::lock_guard<std::mutex> lock(m_pollGroupsMutex);
    m_pollGroups.erase(groupId);
}

void ModbusClient::startPolling() {
    if (m_polling.load()) return;
    m_polling.store(true);
    m_pollThread = std::thread(&ModbusClient::pollingLoop, this);
}

void ModbusClient::stopPolling() {
    m_polling.store(false);
    if (m_pollThread.joinable()) {
        m_pollThread.join();
    }
}

bool ModbusClient::isPolling() const {
    return m_polling.load();
}

void ModbusClient::pollingLoop() {
    while (m_polling.load()) {
        std::map<std::string, PollGroup> groups;
        {
            std::lock_guard<std::mutex> lock(m_pollGroupsMutex);
            groups = m_pollGroups;
        }

        for (const auto& [id, group] : groups) {
            if (!group.enabled) continue;

            std::map<std::string, ElectricalVariable> variables;
            for (const auto& reg : group.registers) {
                auto varOpt = readVariable(reg.variableName);
                if (varOpt.has_value()) {
                    variables[reg.variableName] = varOpt.value();
                }
            }

            if (group.onDataReceived) {
                group.onDataReceived(variables);
            }

            // Sleep for poll interval
            std::this_thread::sleep_for(group.interval);
        }
    }
}

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------
uint64_t ModbusClient::getRequestsSent() const {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    return m_requestsSent;
}

uint64_t ModbusClient::getResponsesReceived() const {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    return m_responsesReceived;
}

uint64_t ModbusClient::getTimeouts() const {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    return m_timeouts;
}

uint64_t ModbusClient::getErrors() const {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    return m_errors;
}

double ModbusClient::getAverageResponseTimeMs() const {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    if (m_responseTimes.empty()) return 0.0;
    double sum = 0.0;
    for (double t : m_responseTimes) sum += t;
    return sum / m_responseTimes.size();
}

void ModbusClient::resetStatistics() {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    m_requestsSent = 0;
    m_responsesReceived = 0;
    m_timeouts = 0;
    m_errors = 0;
    m_responseTimes.clear();
}

} // namespace powsys365
