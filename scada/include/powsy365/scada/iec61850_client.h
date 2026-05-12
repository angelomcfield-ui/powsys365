#pragma once

#include "protocol_gateway.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <map>
#include <mutex>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <queue>
#include <chrono>

namespace powsys365 {

// ---------------------------------------------------------------------------
// IEC 61850 data structures
// ---------------------------------------------------------------------------

struct IedServerEndpoint {
    std::string iedName;
    std::string ipAddress;
    int port = 102;
    std::string apTitle = "1,3,9999,23";
    int aeQualifier = 12;
};

struct DatasetEntry {
    std::string reference;   // e.g., "MMXU1$MX$PhV$phsA$cVal$magnitude"
    std::string description;
    std::string functionalConstraint; // MX, ST, CF, etc.
    double value = 0.0;
    std::string unit;
    std::chrono::system_clock::time_point timestamp;
    uint32_t quality = 0;
    bool valid = false;
};

struct Dataset {
    std::string name;        // e.g., " Measurement"
    std::string reference;   // LD/LN$Datasetname
    std::vector<DatasetEntry> entries;
    std::chrono::system_clock::time_point lastRead;
};

struct RcbConfig {
    std::string rcbReference;     // e.g., "LLN0$RP$URCB01"
    std::string datasetReference;
    int triggerOptions = 0;       // dchg, qchg, dupd, integrity, gi
    int integrityPeriod = 0;      // seconds
    bool buffered = false;
    bool enabled = false;
};

struct GooseSubscription {
    std::string appId;            // GOOSE ID
    std::string goCbRef;          // GOOSE control block reference
    std::string datSet;           // Dataset reference
    std::string macAddress;       // Multicast MAC
    int vlanId = 0;
    int vlanPriority = 4;
    int appIdNum = 0;
    std::vector<uint8_t> lastPdu;
    std::chrono::system_clock::time_point lastReceived;
    int stNum = 0;
    int sqNum = 0;
    bool connected = false;
};

struct GooseDataEntry {
    std::string name;
    int type; // BOOLEAN, INT8, INT32, FLOAT32, etc.
    union {
        bool boolVal;
        int8_t i8;
        int16_t i16;
        int32_t i32;
        int64_t i64;
        float f32;
        double f64;
    } value;
    uint32_t quality = 0;
};

struct SampledValuesSubscription {
    std::string svId;             // Sampled Values ID
    std::string srcMac;
    std::string dstMac;
    int noAsdu = 1;
    int smpRate = 4800;
    int smpMod = 0;
    int vlanId = 0;
    std::vector<std::string> dataRefs;
    bool active = false;
};

struct SmpSynchStatus {
    int smpSynch = 0; // 0=local, 1=remote, 2=off
    int smpCnt = 0;
    std::vector<float> samples; // IA, IB, IC, IN, VA, VB, VC, VN
    std::chrono::system_clock::time_point timestamp;
};

// ---------------------------------------------------------------------------
// IEC 61850 Client
// ---------------------------------------------------------------------------
class Iec61850Client {
public:
    Iec61850Client();
    ~Iec61850Client();

    // Connection
    bool connect(const IedServerEndpoint& endpoint);
    void disconnect();
    bool isConnected() const;
    std::string getConnectionState() const;

    // Server info
    std::vector<std::string> listLogicalDevices();
    std::vector<std::string> listLogicalNodes(const std::string& ldName);
    std::vector<Dataset> listDatasets();

    // MMS data access
    bool readDataset(const std::string& datasetRef, Dataset& outDataset);
    bool readObject(const std::string& objectRef, DatasetEntry& outEntry);
    bool writeObject(const std::string& objectRef, const DatasetEntry& value);
    bool readDirectory(const std::string& ldName, std::vector<std::string>& outEntries);

    // Report Control Blocks (RCB)
    bool configureRCB(const RcbConfig& config);
    bool enableReporting(const std::string& rcbRef, bool enable);
    bool triggerGeneralInterrogation(const std::string& rcbRef);
    void setOnReportReceived(std::function<void(const Dataset&)> callback);

    // GOOSE subscriber
    bool subscribeGoose(const GooseSubscription& subscription);
    void unsubscribeGoose(const std::string& appId);
    std::vector<GooseSubscription> getActiveGooseSubscriptions() const;
    void setOnGooseReceived(std::function<void(const GooseSubscription&, const std::vector<GooseDataEntry>&)> callback);

    // Sampled Values subscriber
    bool subscribeSampledValues(const SampledValuesSubscription& subscription);
    void unsubscribeSampledValues(const std::string& svId);
    std::vector<SampledValuesSubscription> getActiveSvSubscriptions() const;
    void setOnSvReceived(std::function<void(const SampledValuesSubscription&, const SmpSynchStatus&)> callback);

    // File services
    bool getFile(const std::string& remoteFileName, const std::string& localFileName);
    std::vector<std::string> listFiles(const std::string& directory);

    // Settings (SGCB)
    bool selectActiveSettingGroup(int groupNum);
    bool readSettingGroupValues(int groupNum, std::vector<DatasetEntry>& outValues);
    bool writeSettingGroupValues(int groupNum, const std::vector<DatasetEntry>& values);

    // Control (SBO / Direct)
    bool operateControl(const std::string& controlObjectRef, bool value);
    bool operateControlSBO(const std::string& controlObjectRef, bool value);

    // Statistics
    uint64_t getMessagesSent() const;
    uint64_t getMessagesReceived() const;
    uint64_t getBytesTransferred() const;
    double getAverageLatencyMs() const;
    void resetStatistics();

private:
    void receiveLoop();
    void gooseReceiveLoop();
    void svReceiveLoop();
    void reportHandlerThread();
    bool sendMmsPdu(const std::vector<uint8_t>& pdu);
    bool receiveMmsPdu(std::vector<uint8_t>& pdu, std::chrono::milliseconds timeout);
    void processMmsPdu(const std::vector<uint8_t>& pdu);
    void processGoosePdu(const std::vector<uint8_t>& pdu, const std::string& sourceMac);
    void processSvPdu(const std::vector<uint8_t>& pdu);
    std::vector<uint8_t> buildMmsInitiateRequest();
    std::vector<uint8_t> buildMmsGetNameListRequest(const std::string& domain);
    std::vector<uint8_t> buildMmsReadRequest(const std::string& objectRef);
    std::vector<uint8_t> buildMmsWriteRequest(const std::string& objectRef, const DatasetEntry& value);

    // MMS BER encoding helpers
    std::vector<uint8_t> encodeBerLength(size_t length);
    std::vector<uint8_t> encodeBerInteger(int64_t value);
    std::vector<uint8_t> encodeBerReal(double value);
    std::vector<uint8_t> encodeBerVisibleString(const std::string& value);
    std::vector<uint8_t> encodeBerBoolean(bool value);
    std::vector<uint8_t> encodeBerOctetString(const std::vector<uint8_t>& data);
    std::vector<uint8_t> wrapMmsPdu(int tag, const std::vector<uint8_t>& content);
    std::vector<uint8_t> buildFullyEncodedPdu(const std::vector<std::vector<uint8_t>>& components);
    size_t decodeBerLength(const std::vector<uint8_t>& data, size_t offset, size_t& outBytesRead);
    int64_t decodeBerInteger(const std::vector<uint8_t>& data, size_t offset, size_t length);
    double decodeBerReal(const std::vector<uint8_t>& data, size_t offset, size_t length);
    std::string decodeBerVisibleString(const std::vector<uint8_t>& data, size_t offset, size_t length);

    // Connection
    IedServerEndpoint m_endpoint;
    std::atomic<bool> m_connected{false};
    int m_socketFd = -1;

    // Threads
    std::atomic<bool> m_running{false};
    std::thread m_receiveThread;
    std::thread m_gooseThread;
    std::thread m_svThread;
    std::thread m_reportThread;

    // GOOSE
    mutable std::mutex m_gooseMutex;
    std::map<std::string, GooseSubscription> m_gooseSubscriptions;
    int m_gooseSocketFd = -1;

    // SV
    mutable std::mutex m_svMutex;
    std::map<std::string, SampledValuesSubscription> m_svSubscriptions;
    int m_svSocketFd = -1;

    // Reports
    std::mutex m_reportMutex;
    std::condition_variable m_reportCv;
    std::queue<Dataset> m_reportQueue;
    std::function<void(const Dataset&)> m_onReportReceived;
    std::function<void(const GooseSubscription&, const std::vector<GooseDataEntry>&)> m_onGooseReceived;
    std::function<void(const SampledValuesSubscription&, const SmpSynchStatus&)> m_onSvReceived;

    // Statistics
    mutable std::mutex m_statsMutex;
    uint64_t m_messagesSent = 0;
    uint64_t m_messagesReceived = 0;
    uint64_t m_bytesTransferred = 0;
    std::vector<double> m_latencies;

    // Sequence counter for MMS
    std::atomic<uint32_t> m_invokeId{1};
};

} // namespace powsys365
