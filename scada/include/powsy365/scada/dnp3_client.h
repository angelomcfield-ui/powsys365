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

namespace powsys365 {

// ---------------------------------------------------------------------------
// DNP3 data structures
// ---------------------------------------------------------------------------

struct Dnp3Endpoint {
    std::string name;
    std::string address;     // IP:port or serial device path
    bool isSerial = false;
    int baudRate = 9600;
    int dataBits = 8;
    int stopBits = 1;
    std::string parity = "NONE";
    // Link layer
    int sourceAddress = 1;
    int destinationAddress = 10;
};

// DNP3 Group/Variation definitions
enum class Dnp3GroupVariation : uint16_t {
    // Binary Input
    BI_GRP = 0x0101,  // Group 1 Var 1 - Binary Input (packed)
    BI_EVT_GRP = 0x0201, // Group 2 Var 1 - Binary Input Event
    // Binary Output
    BO_GRP = 0x0A01,  // Group 10 Var 1 - Binary Output
    BO_STS_GRP = 0x0B01, // Group 11 Var 1 - Binary Output Status
    // Counter
    CTR_GRP = 0x0501, // Group 5 Var 1 - 32-bit counter
    CTR_EVT_GRP = 0x0601, // Group 6 Var 1 - Counter Event
    // Analog Input
    AI_32_GRP = 0x1E01, // Group 30 Var 1 - 32-bit analog input
    AI_16_GRP = 0x1E02, // Group 30 Var 2 - 16-bit analog input
    AI_FLT_GRP = 0x1E05, // Group 30 Var 5 - floating point analog
    AI_EVT_GRP = 0x2001, // Group 32 Var 1 - Analog Input Event
    // Analog Output
    AO_32_GRP = 0x2901, // Group 41 Var 1 - 32-bit analog output
    AO_FLT_GRP = 0x2903, // Group 41 Var 3 - floating point analog output
    // Time
    TIME_GRP = 0x3201, // Group 50 Var 1 - Time and Date
    // File
    FILE_GRP = 0x3401, // Group 52 Var 1 - File Control
    // Class data
    CLASS0_GRP = 0x3C01, // Group 60 Var 1 - Class 0 data
    CLASS1_GRP = 0x3C02, // Group 60 Var 2 - Class 1 event data
    CLASS2_GRP = 0x3C03, // Group 60 Var 3 - Class 2 event data
    CLASS3_GRP = 0x3C04, // Group 60 Var 4 - Class 3 event data
    // Internal Indications
    II_GRP = 0x5001, // Group 80 Var 1 - Internal Indications
};

struct Dnp3AnalogPoint {
    uint16_t index = 0;
    double value = 0.0;
    uint32_t quality = 0;
    std::chrono::system_clock::time_point timestamp;
    bool online = true;
    bool overRange = false;
    bool commLost = false;
};

struct Dnp3BinaryPoint {
    uint16_t index = 0;
    bool value = false;
    uint8_t quality = 0;
    std::chrono::system_clock::time_point timestamp;
    bool online = true;
};

struct Dnp3CounterPoint {
    uint16_t index = 0;
    uint32_t value = 0;
    uint32_t quality = 0;
    std::chrono::system_clock::time_point timestamp;
};

struct Dnp3ControlResult {
    bool success = false;
    uint16_t index = 0;
    uint8_t status = 0;
    std::string statusText;
    std::chrono::system_clock::time_point timestamp;
};

// Internal Indications
struct InternalIndications {
    bool allStations = false;
    bool class1Events = false;
    bool class2Events = false;
    bool class3Events = false;
    bool needTime = false;
    bool localControl = false;
    bool deviceTrouble = false;
    bool deviceRestart = false;
    bool funcNotSupported = false;
};

// ---------------------------------------------------------------------------
// DNP3 Client (Master)
// ---------------------------------------------------------------------------
class Dnp3Client {
public:
    Dnp3Client();
    ~Dnp3Client();

    // Connection lifecycle
    bool connect(const Dnp3Endpoint& endpoint);
    void disconnect();
    bool isConnected() const;

    // Link layer
    bool performLinkReset();
    bool testLinkStatus();
    bool confirmLinkFrame(uint16_t destination);

    // Application layer - Read functions
    bool readClass0(std::vector<Dnp3AnalogPoint>& analogs,
                    std::vector<Dnp3BinaryPoint>& binaries,
                    std::vector<Dnp3CounterPoint>& counters);
    bool readClass1(std::vector<Dnp3AnalogPoint>& analogs,
                    std::vector<Dnp3BinaryPoint>& binaries);
    bool readClass2(std::vector<Dnp3AnalogPoint>& analogs,
                    std::vector<Dnp3BinaryPoint>& binaries);
    bool readClass3(std::vector<Dnp3AnalogPoint>& analogs,
                    std::vector<Dnp3BinaryPoint>& binaries);
    bool readAnalogInputs(const std::vector<uint16_t>& indices,
                          std::vector<Dnp3AnalogPoint>& outPoints);
    bool readBinaryInputs(const std::vector<uint16_t>& indices,
                          std::vector<Dnp3BinaryPoint>& outPoints);
    bool readCounters(const std::vector<uint16_t>& indices,
                      std::vector<Dnp3CounterPoint>& outPoints);

    // Application layer - Control functions
    Dnp3ControlResult operateCROB(uint16_t index, uint8_t code, uint8_t count = 1,
                                   uint32_t onTimeMs = 100, uint32_t offTimeMs = 0);
    Dnp3ControlResult operateAnalogOutput(uint16_t index, double value);
    Dnp3ControlResult selectBeforeOperateCROB(uint16_t index, uint8_t code);
    Dnp3ControlResult selectBeforeOperateAnalog(uint16_t index, double value);

    // Time synchronization
    bool synchronizeTime();
    bool readTime(std::chrono::system_clock::time_point& outTime);
    bool enableUnsolicitedMessages(bool class1, bool class2, bool class3);
    bool disableUnsolicitedMessages();

    // Cold/Warm restart
    bool coldRestart(uint16_t& outDelaySeconds);
    bool warmRestart(uint16_t& outDelaySeconds);

    // Configuration
    void setApplicationTimeout(std::chrono::milliseconds timeout);
    void setLinkRetries(int retries);
    void setUnsolicitedCallback(std::function<void(const std::vector<Dnp3AnalogPoint>&,
                                                     const std::vector<Dnp3BinaryPoint>&)> cb);

    // Statistics
    uint64_t getFramesSent() const;
    uint64_t getFramesReceived() const;
    uint64_t getFramesRejected() const;
    InternalIndications getInternalIndications() const;
    void resetStatistics();

private:
    // Frame construction
    std::vector<uint8_t> buildLinkFrame(uint8_t ctrl, uint16_t dest, uint16_t src,
                                         const std::vector<uint8_t>& userData);
    std::vector<uint8_t> buildTransportSegment(const std::vector<uint8_t>& appData, bool first, bool final, int sequence);
    std::vector<uint8_t> buildApplicationRequest(uint8_t funcCode, const std::vector<uint8_t>& objects = {});
    std::vector<uint8_t> buildObjectHeader(uint8_t group, uint8_t variation,
                                            uint8_t qualifier, const std::vector<uint8_t>& data = {});
    std::vector<uint8_t> buildReadRequest(const std::vector<std::tuple<uint8_t,uint8_t,uint8_t>>& groupVarQual);
    std::vector<uint8_t> buildCROB(uint16_t index, uint8_t code, uint8_t count,
                                    uint32_t onTime, uint32_t offTime);
    std::vector<uint8_t> buildAnalogControlBlock(uint16_t index, double value);

    // Frame parsing
    bool parseLinkFrame(const std::vector<uint8_t>& frame, uint8_t& ctrl,
                        uint16_t& dest, uint16_t& src, std::vector<uint8_t>& userData);
    bool parseApplicationResponse(const std::vector<uint8_t>& data,
                                   uint8_t& funcCode, uint8_t& ctrl,
                                   std::vector<std::vector<uint8_t>>& objects,
                                   InternalIndications& ii);
    bool parseObjectHeader(const std::vector<uint8_t>& data, size_t& offset,
                           uint8_t& group, uint8_t& variation, uint8_t& qualifier,
                           std::vector<uint8_t>& objectData);
    void parseAnalogObjects(const std::vector<uint8_t>& data, uint8_t group, uint8_t variation,
                            uint8_t qualifier, std::vector<Dnp3AnalogPoint>& out);
    void parseBinaryObjects(const std::vector<uint8_t>& data, uint8_t group, uint8_t variation,
                            uint8_t qualifier, std::vector<Dnp3BinaryPoint>& out);
    void parseCounterObjects(const std::vector<uint8_t>& data, uint8_t group, uint8_t variation,
                              uint8_t qualifier, std::vector<Dnp3CounterPoint>& out);

    // CRC
    uint16_t calculateCrc(const std::vector<uint8_t>& data, size_t offset, size_t len);
    uint16_t calculateCrc(const uint8_t* data, size_t len);
    bool validateFrameCrc(const std::vector<uint8_t>& frame);
    std::vector<uint8_t> appendCrcChunks(const std::vector<uint8_t>& userData);

    // Transport
    bool sendFrame(const std::vector<uint8_t>& frame);
    bool receiveFrame(std::vector<uint8_t>& frame, std::chrono::milliseconds timeout);
    bool sendAppRequest(const std::vector<uint8_t>& appData);
    bool receiveAppResponse(std::vector<uint8_t>& response, std::chrono::milliseconds timeout);

    // Serial helpers
    bool configureSerialPort();
    bool writeSerial(const std::vector<uint8_t>& data);
    bool readSerial(std::vector<uint8_t>& data, size_t expectedLen, std::chrono::milliseconds timeout);

    // Unsolicited handler
    void unsolicitedListenerLoop();

    // Constants
    static constexpr uint8_t LINK_PRI_RESET_USER = 0xC0;
    static constexpr uint8_t LINK_PRI_TEST_LINK = 0xC2;
    static constexpr uint8_t LINK_PRI_CONFIRMED_USER = 0xC3;
    static constexpr uint8_t LINK_PRI_UNCONFIRMED_USER = 0xC4;
    static constexpr uint8_t LINK_PRI_REQUEST_LINK = 0xC5;
    static constexpr uint8_t LINK_SEC_ACK = 0x00;
    static constexpr uint8_t LINK_SEC_NACK = 0x01;
    static constexpr uint8_t LINK_SEC_LINK_STATUS = 0x0B;
    static constexpr uint8_t LINK_SEC_NOT_SUPPORTED = 0x0F;

    static constexpr uint8_t APP_FC_CONFIRM = 0x00;
    static constexpr uint8_t APP_FC_READ = 0x01;
    static constexpr uint8_t APP_FC_WRITE = 0x02;
    static constexpr uint8_t APP_FC_SELECT = 0x03;
    static constexpr uint8_t APP_FC_OPERATE = 0x04;
    static constexpr uint8_t APP_FC_DIRECT_OPERATE = 0x05;
    static constexpr uint8_t APP_FC_DIRECT_OPERATE_NR = 0x06;
    static constexpr uint8_t APP_FC_FREEZE = 0x07;
    static constexpr uint8_t APP_FC_FREEZE_NR = 0x08;
    static constexpr uint8_t APP_FC_FREEZE_CLEAR = 0x09;
    static constexpr uint8_t APP_FC_FREEZE_CLEAR_NR = 0x0A;
    static constexpr uint8_t APP_FC_COLD_RESTART = 0x0D;
    static constexpr uint8_t APP_FC_WARM_RESTART = 0x0E;
    static constexpr uint8_t APP_FC_INITIALIZE_DATA = 0x0F;
    static constexpr uint8_t APP_FC_APPLICATION_CONFIRM = 0x11;
    static constexpr uint8_t APP_FC_ENABLE_UNSOLICITED = 0x14;
    static constexpr uint8_t APP_FC_DISABLE_UNSOLICITED = 0x15;
    static constexpr uint8_t APP_FC_ASSIGN_CLASS = 0x16;
    static constexpr uint8_t APP_FC_DELAY_MEASURE = 0x17;
    static constexpr uint8_t APP_FC_RECORD_CURRENT_TIME = 0x18;
    static constexpr uint8_t APP_FC_OPEN_FILE = 0x19;
    static constexpr uint8_t APP_FC_CLOSE_FILE = 0x1A;
    static constexpr uint8_t APP_FC_DELETE_FILE = 0x1B;
    static constexpr uint8_t APP_FC_AUTHENTICATE_FILE = 0x1C;
    static constexpr uint8_t APP_FC_ABORT_FILE = 0x1D;
    static constexpr uint8_t APP_FC_ACTIVATE_CONFIG = 0x1E;
    static constexpr uint8_t APP_FC_AUTHENTICATE = 0x20;
    static constexpr uint8_t APP_FC_ENABLE_AUTO_EVENTS = 0x1B;
    static constexpr uint8_t APP_FC_DISABLE_AUTO_EVENTS = 0x1C;
    static constexpr uint8_t APP_FC_ASSIGN_CLASS_2 = 0x16;

    static constexpr uint16_t CRC_TABLE[256] = {
        0x0000,0x365E,0x6CBC,0x5AE2,0xD978,0xEF26,0xB5C4,0x839A,
        0xFF89,0xC9D7,0x9335,0xA56B,0x26F1,0x10AF,0x4A4D,0x7C13,
        0xB26B,0x8435,0xDED7,0xE889,0x6B13,0x5D4D,0x07AF,0x31F1,
        0x4DE2,0x7BBC,0x215E,0x1700,0x949A,0xA2C4,0xF826,0xCE78,
        0x29AF,0x1FF1,0x4513,0x734D,0xF0D7,0xC689,0x9C6B,0xAA35,
        0xD626,0xE078,0xBA9A,0x8CC4,0x0F5E,0x3900,0x63E2,0x55BC,
        0x9BC4,0xAD9A,0xF778,0xC126,0x42BC,0x74E2,0x2E00,0x185E,
        0x644D,0x5213,0x08F1,0x3EAF,0xBD35,0x8B6B,0xD189,0xE7D7,
        0x535E,0x6500,0x3FE2,0x09BC,0x8A26,0xBC78,0xE69A,0xD0C4,
        0xACD7,0x9A89,0xC06B,0xF635,0x75AF,0x43F1,0x1913,0x2F4D,
        0xE135,0xD76B,0x8D89,0xBBD7,0x384D,0x0E13,0x54F1,0x62AF,
        0x1EBC,0x28E2,0x7200,0x445E,0xC7C4,0xF19A,0xAB78,0x9D26,
        0x7AF1,0x4CAF,0x164D,0x2013,0xA389,0x95D7,0xCF35,0xF96B,
        0x8578,0xB326,0xE9C4,0xDF9A,0x5C00,0x6A5E,0x30BC,0x06E2,
        0xC89A,0xFEC4,0xA426,0x9278,0x11E2,0x27BC,0x7D5E,0x4B00,
        0x3713,0x014D,0x5BAF,0x6DF1,0xEE6B,0xD835,0x82D7,0xB489,
        0xA6BC,0x90E2,0xCA00,0xFC5E,0x7FC4,0x499A,0x1378,0x2526,
        0x5935,0x6F6B,0x3589,0x03D7,0x804D,0xB613,0xECF1,0xDAAF,
        0x14D7,0x2289,0x786B,0x4E35,0xCDAF,0xFBF1,0xA113,0x974D,
        0xEB5E,0xDD00,0x87E2,0xB1BC,0x3226,0x0478,0x5E9A,0x68C4,
        0x8F13,0xB94D,0xE3AF,0xD5F1,0x566B,0x6035,0x3AD7,0x0C89,
        0x709A,0x46C4,0x1C26,0x2A78,0xA9E2,0x9FBC,0xC55E,0xF300,
        0x3D78,0x0B26,0x51C4,0x679A,0xE400,0xD25E,0x88BC,0xBEE2,
        0xC2F1,0xF4AF,0xAE4D,0x9813,0x1B89,0x2DD7,0x7735,0x416B,
        0xF5E2,0xC3BC,0x995E,0xAF00,0x2C9A,0x1AC4,0x4026,0x7678,
        0x0A6B,0x3C35,0x66D7,0x5089,0xD313,0xE54D,0xBFAF,0x89F1,
        0x4789,0x71D7,0x2B35,0x1D6B,0x9EF1,0xA8AF,0xF24D,0xC413,
        0xB800,0x8E5E,0xD4BC,0xE2E2,0x6178,0x5726,0x0DC4,0x3B9A,
        0xDC4D,0xEA13,0xB0F1,0x86AF,0x0535,0x336B,0x6989,0x5FD7,
        0x23C4,0x159A,0x4F78,0x7926,0xFABC,0xCCE2,0x9600,0xA05E,
        0x6E26,0x5878,0x029A,0x34C4,0xB75E,0x8100,0xDBE2,0xEDBC,
        0x91AF,0xA7F1,0xFD13,0xCB4D,0x48D7,0x7E89,0x246B,0x1235
    };

    // State
    Dnp3Endpoint m_endpoint;
    std::atomic<bool> m_connected{false};
    int m_fd = -1;
    int m_transportSeq = 0;
    int m_appSeq = 0;

    // Threading
    std::atomic<bool> m_running{false};
    std::thread m_unsolicitedThread;
    std::mutex m_sendMutex;

    // Configuration
    std::chrono::milliseconds m_appTimeout{5000};
    int m_linkRetries = 3;

    // Callbacks
    std::function<void(const std::vector<Dnp3AnalogPoint>&,
                       const std::vector<Dnp3BinaryPoint>&)> m_unsolicitedCallback;

    // Statistics
    mutable std::mutex m_statsMutex;
    uint64_t m_framesSent = 0;
    uint64_t m_framesReceived = 0;
    uint64_t m_framesRejected = 0;
    InternalIndications m_lastII;
};

} // namespace powsys365
