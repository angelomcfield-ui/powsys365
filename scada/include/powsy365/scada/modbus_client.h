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
#include <condition_variable>
#include <optional>

namespace powsys365 {

// ---------------------------------------------------------------------------
// Modbus exception codes
// ---------------------------------------------------------------------------
enum class ModbusException : uint8_t {
    NONE = 0x00,
    ILLEGAL_FUNCTION = 0x01,
    ILLEGAL_DATA_ADDRESS = 0x02,
    ILLEGAL_DATA_VALUE = 0x03,
    SLAVE_DEVICE_FAILURE = 0x04,
    ACKNOWLEDGE = 0x05,
    SLAVE_DEVICE_BUSY = 0x06,
    NEGATIVE_ACKNOWLEDGE = 0x07,
    MEMORY_PARITY_ERROR = 0x08,
    GATEWAY_PATH_UNAVAILABLE = 0x0A,
    GATEWAY_TARGET_DEVICE_FAILED = 0x0B
};

std::string modbusExceptionToString(ModbusException ex);

// ---------------------------------------------------------------------------
// Modbus endpoint configuration
// ---------------------------------------------------------------------------
struct ModbusEndpoint {
    std::string name;
    std::string address;     // IP address for TCP, serial device for RTU
    int port = 502;          // TCP port (default Modbus TCP)
    bool isRtu = false;      // true = RTU over serial
    int baudRate = 9600;
    int dataBits = 8;
    int stopBits = 1;
    char parity = 'N';       // N=None, E=Even, O=Odd
    int slaveId = 1;         // Unit identifier
    std::chrono::milliseconds responseTimeout{2000};
    int connectRetries = 3;
};

// ---------------------------------------------------------------------------
// Register mapping for electrical variables
// ---------------------------------------------------------------------------
enum class RegisterType : uint16_t {
    HOLDING_REGISTER = 0x03,
    INPUT_REGISTER = 0x04,
    COIL = 0x01,
    DISCRETE_INPUT = 0x02
};

struct RegisterMapping {
    std::string variableName;
    std::string description;
    RegisterType regType;
    uint16_t address;
    int registerCount = 1;   // 1 for 16-bit, 2 for 32-bit/float
    float scaleFactor = 1.0f;
    float offset = 0.0f;
    std::string unit;
    std::string dataType;    // "UINT16", "INT16", "UINT32", "INT32", "FLOAT32"
};

// ---------------------------------------------------------------------------
// Electrical variable mapping
// ---------------------------------------------------------------------------
struct ElectricalVariable {
    std::string name;
    double value = 0.0;
    std::string unit;
    std::chrono::system_clock::time_point timestamp;
    bool valid = false;
    uint16_t rawRegister[2] = {0, 0};
};

// ---------------------------------------------------------------------------
// Modbus response structures
// ---------------------------------------------------------------------------
struct ReadRegistersResponse {
    bool success = false;
    std::vector<uint16_t> registers;
    ModbusException exception = ModbusException::NONE;
    std::chrono::system_clock::time_point timestamp;
};

struct WriteRegisterResponse {
    bool success = false;
    uint16_t writtenAddress = 0;
    uint16_t writtenValue = 0;
    ModbusException exception = ModbusException::NONE;
};

// ---------------------------------------------------------------------------
// Poll configuration
// ---------------------------------------------------------------------------
struct PollGroup {
    std::string groupId;
    std::string name;
    std::vector<RegisterMapping> registers;
    std::chrono::milliseconds interval{1000};
    bool enabled = true;
    std::function<void(const std::map<std::string, ElectricalVariable>&)> onDataReceived;
};

// ---------------------------------------------------------------------------
// Modbus Client (TCP and RTU)
// ---------------------------------------------------------------------------
class ModbusClient {
public:
    ModbusClient();
    ~ModbusClient();

    // Connection lifecycle
    bool connect(const ModbusEndpoint& endpoint);
    void disconnect();
    bool isConnected() const;
    bool reconnect();

    // Core Modbus functions
    ReadRegistersResponse readHoldingRegisters(uint16_t startAddress, uint16_t count);
    ReadRegistersResponse readInputRegisters(uint16_t startAddress, uint16_t count);
    std::vector<bool> readCoils(uint16_t startAddress, uint16_t count);
    std::vector<bool> readDiscreteInputs(uint16_t startAddress, uint16_t count);
    WriteRegisterResponse writeSingleRegister(uint16_t address, uint16_t value);
    WriteRegisterResponse writeMultipleRegisters(uint16_t startAddress,
                                                   const std::vector<uint16_t>& values);
    WriteRegisterResponse writeSingleCoil(uint16_t address, bool value);
    WriteRegisterResponse writeMultipleCoils(uint16_t startAddress,
                                               const std::vector<bool>& values);

    // 32-bit operations (combines 2 registers)
    std::optional<float> readFloat32(uint16_t address, RegisterType type);
    std::optional<uint32_t> readUInt32(uint16_t address, RegisterType type);
    std::optional<int32_t> readInt32(uint16_t address, RegisterType type);
    bool writeFloat32(uint16_t address, float value);
    bool writeUInt32(uint16_t address, uint32_t value);
    bool writeInt32(uint16_t address, int32_t value);

    // Electrical variable mapping
    void addRegisterMapping(const RegisterMapping& mapping);
    void removeRegisterMapping(const std::string& variableName);
    std::vector<RegisterMapping> getRegisterMappings() const;
    std::map<std::string, ElectricalVariable> readAllMappedVariables();
    std::optional<ElectricalVariable> readVariable(const std::string& variableName);

    // Polling system
    void addPollGroup(const PollGroup& group);
    void removePollGroup(const std::string& groupId);
    void startPolling();
    void stopPolling();
    bool isPolling() const;

    // Statistics
    uint64_t getRequestsSent() const;
    uint64_t getResponsesReceived() const;
    uint64_t getTimeouts() const;
    uint64_t getErrors() const;
    double getAverageResponseTimeMs() const;
    void resetStatistics();

private:
    // Frame building
    std::vector<uint8_t> buildTcpFrame(uint16_t transactionId, uint8_t unitId,
                                        uint8_t functionCode,
                                        const std::vector<uint8_t>& data);
    std::vector<uint8_t> buildRtuFrame(uint8_t slaveId, uint8_t functionCode,
                                        const std::vector<uint8_t>& data);
    uint16_t calculateCrc16(const std::vector<uint8_t>& data);

    // Frame parsing
    bool parseTcpResponse(const std::vector<uint8_t>& response,
                          uint16_t expectedTransactionId,
                          uint8_t& unitId, uint8_t& functionCode,
                          std::vector<uint8_t>& data,
                          ModbusException& exception);
    bool parseRtuResponse(const std::vector<uint8_t>& response,
                          uint8_t expectedSlaveId,
                          uint8_t& functionCode,
                          std::vector<uint8_t>& data,
                          ModbusException& exception);

    // Data conversion
    float registersToFloat(uint16_t high, uint16_t low);
    uint32_t registersToUInt32(uint16_t high, uint16_t low);
    int32_t registersToInt32(uint16_t high, uint16_t low);
    std::pair<uint16_t, uint16_t> floatToRegisters(float value);
    std::pair<uint16_t, uint16_t> uint32ToRegisters(uint32_t value);
    std::pair<uint16_t, uint16_t> int32ToRegisters(int32_t value);
    ElectricalVariable decodeRegisters(const RegisterMapping& mapping,
                                       const std::vector<uint16_t>& registers);

    // Communication
    bool sendRaw(const std::vector<uint8_t>& frame);
    bool receiveRaw(std::vector<uint8_t>& response, size_t expectedLen,
                    std::chrono::milliseconds timeout);
    bool tcpTransaction(const std::vector<uint8_t>& request,
                        std::vector<uint8_t>& response,
                        size_t expectedResponseLen);
    bool rtuTransaction(const std::vector<uint8_t>& request,
                        std::vector<uint8_t>& response,
                        size_t expectedResponseLen);

    // Polling thread
    void pollingLoop();

    // State
    ModbusEndpoint m_endpoint;
    std::atomic<bool> m_connected{false};
    int m_fd = -1;
    uint16_t m_transactionId = 1;

    // Mappings
    mutable std::mutex m_mappingsMutex;
    std::map<std::string, RegisterMapping> m_registerMappings;

    // Polling
    std::atomic<bool> m_polling{false};
    std::thread m_pollThread;
    mutable std::mutex m_pollGroupsMutex;
    std::map<std::string, PollGroup> m_pollGroups;

    // Statistics
    mutable std::mutex m_statsMutex;
    uint64_t m_requestsSent = 0;
    uint64_t m_responsesReceived = 0;
    uint64_t m_timeouts = 0;
    uint64_t m_errors = 0;
    std::vector<double> m_responseTimes;
};

} // namespace powsys365
