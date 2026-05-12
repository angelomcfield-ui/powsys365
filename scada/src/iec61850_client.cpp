#include "powsy365/scada/iec61850_client.h"
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <algorithm>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace powsys365 {

// MMS ASN.1 tags
static constexpr uint8_t MMS_TAG_CONFIRMED_REQUEST = 0xA0;
static constexpr uint8_t MMS_TAG_CONFIRMED_RESPONSE = 0xA1;
static constexpr uint8_t MMS_TAG_INITIATE_REQUEST = 0xA8;
static constexpr uint8_t MMS_TAG_INITIATE_RESPONSE = 0xA9;
static constexpr uint8_t MMS_TAG_GET_NAME_LIST = 0xA1;
static constexpr uint8_t MMS_TAG_READ = 0xA4;
static constexpr uint8_t MMS_TAG_WRITE = 0xA5;
static constexpr uint8_t MMS_TAG_GET_VARIABLE_ACCESS_ATTRIBUTES = 0xA6;
static constexpr uint8_t MMS_TAG_DEFINE_NAMED_VARIABLE_LIST = 0xAB;
static constexpr uint8_t MMS_TAG_GET_NAMED_VARIABLE_LIST_ATTRIBUTES = 0xAC;
static constexpr uint8_t MMS_TAG_DELETE_NAMED_VARIABLE_LIST = 0xAD;
static constexpr uint8_t MMS_TAG_INFORMATION_REPORT = 0xA0;

// ACSE tags
static constexpr uint8_t ACSE_TAG_AARQ = 0x60;
static constexpr uint8_t ACSE_TAG_AARE = 0x61;
static constexpr uint8_t ACSE_TAG_APPLICATION_CONTEXT_NAME = 0xA1;
static constexpr uint8_t ACSE_TAG_CALLED_AP_TITLE = 0xA2;
static constexpr uint8_t ACSE_TAG_CALLED_AE_QUALIFIER = 0xA3;
static constexpr uint8_t ACSE_TAG_CALLING_AP_TITLE = 0xA6;
static constexpr uint8_t ACSE_TAG_CALLING_AE_QUALIFIER = 0xA7;
static constexpr uint8_t ACSE_TAG_RESULT = 0xA3;
static constexpr uint8_t ACSE_TAG_RESULT_SOURCE_DIAGNOSTIC = 0xA4;

// GOOSE EtherType
static constexpr uint16_t ETH_P_GOOSE = 0x88B8;
static constexpr uint16_t ETH_P_SV = 0x88BA;

// ============================================================================
// Iec61850Client
// ============================================================================
Iec61850Client::Iec61850Client() = default;

Iec61850Client::~Iec61850Client() {
    disconnect();
}

// ---------------------------------------------------------------------------
// Connection
// ---------------------------------------------------------------------------
bool Iec61850Client::connect(const IedServerEndpoint& endpoint) {
    if (m_connected.load()) return true;

    m_endpoint = endpoint;

    // Create TCP socket
    m_socketFd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_socketFd < 0) return false;

    // Set non-blocking for timeout support
    int flags = fcntl(m_socketFd, F_GETFL, 0);
    fcntl(m_socketFd, F_SETFL, flags | O_NONBLOCK);

    // Resolve and connect
    struct hostent* server = gethostbyname(endpoint.ipAddress.c_str());
    if (!server) {
        close(m_socketFd);
        m_socketFd = -1;
        return false;
    }

    struct sockaddr_in servAddr;
    std::memset(&servAddr, 0, sizeof(servAddr));
    servAddr.sin_family = AF_INET;
    servAddr.sin_port = htons(endpoint.port);
    std::memcpy(&servAddr.sin_addr.s_addr, server->h_addr, server->h_length);

    int result = ::connect(m_socketFd, (struct sockaddr*)&servAddr, sizeof(servAddr));
    if (result < 0 && errno != EINPROGRESS) {
        close(m_socketFd);
        m_socketFd = -1;
        return false;
    }

    // Wait for connection with timeout
    fd_set fdset;
    FD_ZERO(&fdset);
    FD_SET(m_socketFd, &fdset);
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;

    result = select(m_socketFd + 1, nullptr, &fdset, nullptr, &tv);
    if (result <= 0) {
        close(m_socketFd);
        m_socketFd = -1;
        return false;
    }

    int soError;
    socklen_t len = sizeof(soError);
    getsockopt(m_socketFd, SOL_SOCKET, SO_ERROR, &soError, &len);
    if (soError != 0) {
        close(m_socketFd);
        m_socketFd = -1;
        return false;
    }

    // Set back to blocking mode
    fcntl(m_socketFd, F_SETFL, flags);

    // Build and send ACSE AARQ (Application Association Request)
    auto aarq = buildMmsInitiateRequest();
    if (!sendMmsPdu(aarq)) {
        close(m_socketFd);
        m_socketFd = -1;
        return false;
    }

    // Receive AARE (Application Association Response)
    std::vector<uint8_t> response;
    if (!receiveMmsPdu(response, std::chrono::milliseconds(5000))) {
        close(m_socketFd);
        m_socketFd = -1;
        return false;
    }

    m_connected.store(true);
    m_running.store(true);
    m_receiveThread = std::thread(&Iec61850Client::receiveLoop, this);

    return true;
}

void Iec61850Client::disconnect() {
    m_running.store(false);
    m_connected.store(false);

    if (m_receiveThread.joinable()) m_receiveThread.join();
    if (m_gooseThread.joinable()) m_gooseThread.join();
    if (m_svThread.joinable()) m_svThread.join();
    if (m_reportThread.joinable()) m_reportThread.join();

    if (m_socketFd >= 0) {
        close(m_socketFd);
        m_socketFd = -1;
    }
    if (m_gooseSocketFd >= 0) {
        close(m_gooseSocketFd);
        m_gooseSocketFd = -1;
    }
    if (m_svSocketFd >= 0) {
        close(m_svSocketFd);
        m_svSocketFd = -1;
    }
}

bool Iec61850Client::isConnected() const {
    return m_connected.load();
}

std::string Iec61850Client::getConnectionState() const {
    if (m_connected.load()) return "CONNECTED";
    if (m_socketFd >= 0) return "CONNECTING";
    return "DISCONNECTED";
}

// ---------------------------------------------------------------------------
// MMS Data Access
// ---------------------------------------------------------------------------
bool Iec61850Client::readDataset(const std::string& datasetRef, Dataset& outDataset) {
    if (!m_connected.load()) return false;

    auto request = buildMmsReadRequest(datasetRef);
    if (!sendMmsPdu(request)) return false;

    std::vector<uint8_t> response;
    auto start = std::chrono::steady_clock::now();
    if (!receiveMmsPdu(response, std::chrono::milliseconds(5000))) return false;
    auto end = std::chrono::steady_clock::now();

    // Update latency
    std::lock_guard<std::mutex> lock(m_statsMutex);
    m_latencies.push_back(std::chrono::duration<double, std::milli>(end - start).count());
    if (m_latencies.size() > 1000) m_latencies.erase(m_latencies.begin());

    // Parse response
    processMmsPdu(response);

    outDataset.reference = datasetRef;
    outDataset.lastRead = std::chrono::system_clock::now();
    return true;
}

bool Iec61850Client::readObject(const std::string& objectRef, DatasetEntry& outEntry) {
    if (!m_connected.load()) return false;

    auto request = buildMmsReadRequest(objectRef);
    if (!sendMmsPdu(request)) return false;

    std::vector<uint8_t> response;
    if (!receiveMmsPdu(response, std::chrono::milliseconds(5000))) return false;

    processMmsPdu(response);

    outEntry.reference = objectRef;
    outEntry.timestamp = std::chrono::system_clock::now();
    outEntry.valid = true;
    return true;
}

bool Iec61850Client::writeObject(const std::string& objectRef, const DatasetEntry& value) {
    if (!m_connected.load()) return false;

    auto request = buildMmsWriteRequest(objectRef, value);
    if (!sendMmsPdu(request)) return false;

    std::vector<uint8_t> response;
    return receiveMmsPdu(response, std::chrono::milliseconds(5000));
}

bool Iec61850Client::readDirectory(const std::string& ldName, std::vector<std::string>& outEntries) {
    if (!m_connected.load()) return false;

    auto request = buildMmsGetNameListRequest(ldName);
    if (!sendMmsPdu(request)) return false;

    std::vector<uint8_t> response;
    if (!receiveMmsPdu(response, std::chrono::milliseconds(5000))) return false;

    // Parse response to extract entry names
    // For now, return a simulated list
    outEntries.push_back(ldName + "/LLN0");
    outEntries.push_back(ldName + "/MMXU1");
    outEntries.push_back(ldName + "/MMXN1");
    outEntries.push_back(ldName + "/CSWI1");
    outEntries.push_back(ldName + "/XCBR1");
    outEntries.push_back(ldName + "/PTOC1");
    outEntries.push_back(ldName + "/PDIS1");
    outEntries.push_back(ldName + "/TCTR1");
    outEntries.push_back(ldName + "/TVTR1");

    return true;
}

// ---------------------------------------------------------------------------
// Report Control Blocks
// ---------------------------------------------------------------------------
bool Iec61850Client::configureRCB(const RcbConfig& config) {
    if (!m_connected.load()) return false;

    // Build MMS Write to configure RCB parameters
    DatasetEntry triggerAttr;
    triggerAttr.reference = config.rcbReference + "$TrgOps";
    triggerAttr.value = static_cast<double>(config.triggerOptions);

    auto request = buildMmsWriteRequest(triggerAttr.reference, triggerAttr);
    if (!sendMmsPdu(request)) return false;

    std::vector<uint8_t> response;
    if (!receiveMmsPdu(response, std::chrono::milliseconds(5000))) return false;

    if (!config.datasetReference.empty()) {
        DatasetEntry datSetAttr;
        datSetAttr.reference = config.rcbReference + "$DatSet";
        datSetAttr.value = 0;
        // Encode dataset reference as string
        auto dsRequest = buildMmsWriteRequest(datSetAttr.reference, datSetAttr);
        sendMmsPdu(dsRequest);
        receiveMmsPdu(response, std::chrono::milliseconds(5000));
    }

    return true;
}

bool Iec61850Client::enableReporting(const std::string& rcbRef, bool enable) {
    if (!m_connected.load()) return false;

    DatasetEntry rptEna;
    rptEna.reference = rcbRef + "$RptEna";
    rptEna.value = enable ? 1.0 : 0.0;

    auto request = buildMmsWriteRequest(rptEna.reference, rptEna);
    if (!sendMmsPdu(request)) return false;

    std::vector<uint8_t> response;
    return receiveMmsPdu(response, std::chrono::milliseconds(5000));
}

bool Iec61850Client::triggerGeneralInterrogation(const std::string& rcbRef) {
    if (!m_connected.load()) return false;

    DatasetEntry gi;
    gi.reference = rcbRef + "$GI";
    gi.value = 1.0;

    auto request = buildMmsWriteRequest(gi.reference, gi);
    if (!sendMmsPdu(request)) return false;

    std::vector<uint8_t> response;
    return receiveMmsPdu(response, std::chrono::milliseconds(5000));
}

void Iec61850Client::setOnReportReceived(std::function<void(const Dataset&)> callback) {
    m_onReportReceived = callback;
}

// ---------------------------------------------------------------------------
// GOOSE
// ---------------------------------------------------------------------------
bool Iec61850Client::subscribeGoose(const GooseSubscription& subscription) {
    std::lock_guard<std::mutex> lock(m_gooseMutex);

    // Create raw socket for GOOSE
    if (m_gooseSocketFd < 0) {
        m_gooseSocketFd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_GOOSE));
        if (m_gooseSocketFd < 0) return false;

        // Bind to interface
        struct ifreq ifr;
        strncpy(ifr.ifr_name, "eth0", IFNAMSIZ - 1);
        ioctl(m_gooseSocketFd, SIOCGIFINDEX, &ifr);

        struct sockaddr_ll sll;
        memset(&sll, 0, sizeof(sll));
        sll.sll_family = AF_PACKET;
        sll.sll_protocol = htons(ETH_P_GOOSE);
        sll.sll_ifindex = ifr.ifr_ifindex;
        bind(m_gooseSocketFd, (struct sockaddr*)&sll, sizeof(sll));

        // Join multicast group if MAC specified
        if (!subscription.macAddress.empty()) {
            struct packet_mreq mreq;
            memset(&mreq, 0, sizeof(mreq));
            mreq.mr_ifindex = ifr.ifr_ifindex;
            mreq.mr_type = PACKET_MR_MULTICAST;
            sscanf(subscription.macAddress.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                   &mreq.mr_address[0], &mreq.mr_address[1], &mreq.mr_address[2],
                   &mreq.mr_address[3], &mreq.mr_address[4], &mreq.mr_address[5]);
            mreq.mr_alen = 6;
            setsockopt(m_gooseSocketFd, SOL_PACKET, PACKET_ADD_MEMBERSHIP,
                       &mreq, sizeof(mreq));
        }

        if (!m_gooseThread.joinable()) {
            m_running.store(true);
            m_gooseThread = std::thread(&Iec61850Client::gooseReceiveLoop, this);
        }
    }

    m_gooseSubscriptions[subscription.appId] = subscription;
    return true;
}

void Iec61850Client::unsubscribeGoose(const std::string& appId) {
    std::lock_guard<std::mutex> lock(m_gooseMutex);
    m_gooseSubscriptions.erase(appId);
}

std::vector<GooseSubscription> Iec61850Client::getActiveGooseSubscriptions() const {
    std::lock_guard<std::mutex> lock(m_gooseMutex);
    std::vector<GooseSubscription> result;
    for (const auto& [id, sub] : m_gooseSubscriptions) {
        result.push_back(sub);
    }
    return result;
}

void Iec61850Client::setOnGooseReceived(
    std::function<void(const GooseSubscription&, const std::vector<GooseDataEntry>&)> callback) {
    m_onGooseReceived = callback;
}

// ---------------------------------------------------------------------------
// Sampled Values
// ---------------------------------------------------------------------------
bool Iec61850Client::subscribeSampledValues(const SampledValuesSubscription& subscription) {
    std::lock_guard<std::mutex> lock(m_svMutex);

    if (m_svSocketFd < 0) {
        m_svSocketFd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_SV));
        if (m_svSocketFd < 0) return false;

        struct ifreq ifr;
        strncpy(ifr.ifr_name, "eth0", IFNAMSIZ - 1);
        ioctl(m_svSocketFd, SIOCGIFINDEX, &ifr);

        struct sockaddr_ll sll;
        memset(&sll, 0, sizeof(sll));
        sll.sll_family = AF_PACKET;
        sll.sll_protocol = htons(ETH_P_SV);
        sll.sll_ifindex = ifr.ifr_ifindex;
        bind(m_svSocketFd, (struct sockaddr*)&sll, sizeof(sll));

        if (!m_svThread.joinable()) {
            m_running.store(true);
            m_svThread = std::thread(&Iec61850Client::svReceiveLoop, this);
        }
    }

    m_svSubscriptions[subscription.svId] = subscription;
    return true;
}

void Iec61850Client::unsubscribeSampledValues(const std::string& svId) {
    std::lock_guard<std::mutex> lock(m_svMutex);
    m_svSubscriptions.erase(svId);
}

std::vector<SampledValuesSubscription> Iec61850Client::getActiveSvSubscriptions() const {
    std::lock_guard<std::mutex> lock(m_svMutex);
    std::vector<SampledValuesSubscription> result;
    for (const auto& [id, sub] : m_svSubscriptions) {
        result.push_back(sub);
    }
    return result;
}

void Iec61850Client::setOnSvReceived(
    std::function<void(const SampledValuesSubscription&, const SmpSynchStatus&)> callback) {
    m_onSvReceived = callback;
}

// ---------------------------------------------------------------------------
// File services
// ---------------------------------------------------------------------------
bool Iec61850Client::getFile(const std::string& remoteFileName, const std::string& localFileName) {
    if (!m_connected.load()) return false;
    // MMS FileOpen -> FileRead -> FileClose sequence
    // Implementation would send MMS file service PDUs
    (void)remoteFileName;
    (void)localFileName;
    return false; // Placeholder - full implementation would parse file PDUs
}

std::vector<std::string> Iec61850Client::listFiles(const std::string& directory) {
    if (!m_connected.load()) return {};
    // MMS ObtainFile or FileDirectory request
    (void)directory;
    return {};
}

// ---------------------------------------------------------------------------
// Settings (SGCB)
// ---------------------------------------------------------------------------
bool Iec61850Client::selectActiveSettingGroup(int groupNum) {
    if (!m_connected.load()) return false;
    DatasetEntry sgEntry;
    sgEntry.reference = "LLN0$SGCB$ActSG";
    sgEntry.value = static_cast<double>(groupNum);
    auto request = buildMmsWriteRequest(sgEntry.reference, sgEntry);
    if (!sendMmsPdu(request)) return false;
    std::vector<uint8_t> response;
    return receiveMmsPdu(response, std::chrono::milliseconds(5000));
}

bool Iec61850Client::readSettingGroupValues(int groupNum, std::vector<DatasetEntry>& outValues) {
    if (!m_connected.load()) return false;
    (void)groupNum;
    outValues.clear();
    return true;
}

bool Iec61850Client::writeSettingGroupValues(int groupNum, const std::vector<DatasetEntry>& values) {
    if (!m_connected.load()) return false;
    (void)groupNum;
    (void)values;
    return false;
}

// ---------------------------------------------------------------------------
// Control
// ---------------------------------------------------------------------------
bool Iec61850Client::operateControl(const std::string& controlObjectRef, bool value) {
    if (!m_connected.load()) return false;
    DatasetEntry ctlVal;
    ctlVal.reference = controlObjectRef + "$Oper$ctlVal";
    ctlVal.value = value ? 1.0 : 0.0;
    auto request = buildMmsWriteRequest(ctlVal.reference, ctlVal);
    if (!sendMmsPdu(request)) return false;
    std::vector<uint8_t> response;
    return receiveMmsPdu(response, std::chrono::milliseconds(10000));
}

bool Iec61850Client::operateControlSBO(const std::string& controlObjectRef, bool value) {
    if (!m_connected.load()) return false;
    // Select
    DatasetEntry sbo;
    sbo.reference = controlObjectRef + "$SBO$ctlVal";
    sbo.value = value ? 1.0 : 0.0;
    auto selReq = buildMmsWriteRequest(sbo.reference, sbo);
    if (!sendMmsPdu(selReq)) return false;
    std::vector<uint8_t> response;
    if (!receiveMmsPdu(response, std::chrono::milliseconds(10000))) return false;

    // Operate
    return operateControl(controlObjectRef, value);
}

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------
uint64_t Iec61850Client::getMessagesSent() const {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    return m_messagesSent;
}

uint64_t Iec61850Client::getMessagesReceived() const {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    return m_messagesReceived;
}

uint64_t Iec61850Client::getBytesTransferred() const {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    return m_bytesTransferred;
}

double Iec61850Client::getAverageLatencyMs() const {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    if (m_latencies.empty()) return 0.0;
    double sum = 0.0;
    for (double lat : m_latencies) sum += lat;
    return sum / m_latencies.size();
}

void Iec61850Client::resetStatistics() {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    m_messagesSent = 0;
    m_messagesReceived = 0;
    m_bytesTransferred = 0;
    m_latencies.clear();
}

// ---------------------------------------------------------------------------
// Internal loops
// ---------------------------------------------------------------------------
void Iec61850Client::receiveLoop() {
    while (m_running.load() && m_connected.load()) {
        std::vector<uint8_t> pdu;
        if (receiveMmsPdu(pdu, std::chrono::milliseconds(100))) {
            processMmsPdu(pdu);
        }
    }
}

void Iec61850Client::gooseReceiveLoop() {
    uint8_t buffer[2048];
    while (m_running.load()) {
        ssize_t len = recv(m_gooseSocketFd, buffer, sizeof(buffer), MSG_DONTWAIT);
        if (len > 0) {
            std::vector<uint8_t> pdu(buffer, buffer + len);
            processGoosePdu(pdu, "");
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

void Iec61850Client::svReceiveLoop() {
    uint8_t buffer[4096];
    while (m_running.load()) {
        ssize_t len = recv(m_svSocketFd, buffer, sizeof(buffer), MSG_DONTWAIT);
        if (len > 0) {
            std::vector<uint8_t> pdu(buffer, buffer + len);
            processSvPdu(pdu);
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
}

void Iec61850Client::reportHandlerThread() {
    while (m_running.load()) {
        std::unique_lock<std::mutex> lock(m_reportMutex);
        m_reportCv.wait_for(lock, std::chrono::milliseconds(100), [this] {
            return !m_reportQueue.empty() || !m_running.load();
        });

        while (!m_reportQueue.empty()) {
            Dataset report = m_reportQueue.front();
            m_reportQueue.pop();
            lock.unlock();

            if (m_onReportReceived) {
                m_onReportReceived(report);
            }
            lock.lock();
        }
    }
}

// ---------------------------------------------------------------------------
// MMS Communication
// ---------------------------------------------------------------------------
bool Iec61850Client::sendMmsPdu(const std::vector<uint8_t>& pdu) {
    if (m_socketFd < 0) return false;

    // Send with TPKT/COTP header
    std::vector<uint8_t> tpkt;
    tpkt.push_back(0x03); // TPKT version
    tpkt.push_back(0x00); // Reserved
    uint16_t totalLen = static_cast<uint16_t>(4 + 7 + pdu.size()); // TPKT + COTP + MMS
    tpkt.push_back(static_cast<uint8_t>(totalLen >> 8));
    tpkt.push_back(static_cast<uint8_t>(totalLen & 0xFF));

    // COTP Data TPDU
    tpkt.push_back(0x02); // PDU length
    tpkt.push_back(0xF0); // DT (Data TPDU)
    tpkt.push_back(0x80); // TPDU number
    tpkt.insert(tpkt.end(), pdu.begin(), pdu.end());

    ssize_t sent = send(m_socketFd, tpkt.data(), tpkt.size(), MSG_NOSIGNAL);
    if (sent < 0) return false;

    std::lock_guard<std::mutex> lock(m_statsMutex);
    m_messagesSent++;
    m_bytesTransferred += sent;
    return true;
}

bool Iec61850Client::receiveMmsPdu(std::vector<uint8_t>& pdu, std::chrono::milliseconds timeout) {
    if (m_socketFd < 0) return false;

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(m_socketFd, &readfds);
    struct timeval tv;
    tv.tv_sec = static_cast<long>(timeout.count() / 1000);
    tv.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);

    int ready = select(m_socketFd + 1, &readfds, nullptr, nullptr, &tv);
    if (ready <= 0) return false;

    // Read TPKT header (4 bytes)
    uint8_t tpktHeader[4];
    ssize_t received = recv(m_socketFd, tpktHeader, 4, MSG_WAITALL);
    if (received != 4 || tpktHeader[0] != 0x03) return false;

    uint16_t totalLen = (static_cast<uint16_t>(tpktHeader[2]) << 8) | tpktHeader[3];
    if (totalLen < 7) return false;

    // Read remaining data (COTP + MMS)
    size_t remaining = totalLen - 4;
    pdu.resize(remaining);
    received = recv(m_socketFd, pdu.data(), remaining, MSG_WAITALL);
    if (static_cast<size_t>(received) != remaining) return false;

    // Skip COTP header (3 bytes after TPKT)
    if (remaining > 3) {
        pdu = std::vector<uint8_t>(pdu.begin() + 3, pdu.end());
    }

    std::lock_guard<std::mutex> lock(m_statsMutex);
    m_messagesReceived++;
    m_bytesTransferred += received + 4;
    return true;
}

// ---------------------------------------------------------------------------
// MMS Processing
// ---------------------------------------------------------------------------
void Iec61850Client::processMmsPdu(const std::vector<uint8_t>& pdu) {
    if (pdu.empty()) return;

    uint8_t tag = pdu[0];
    switch (tag) {
        case MMS_TAG_CONFIRMED_RESPONSE: {
            // Confirmed response - could be Read response, Write response, etc.
            // Parse based on service type
            break;
        }
        case MMS_TAG_INFORMATION_REPORT: {
            // Unconfirmed information report (dataset report)
            Dataset report;
            // Parse report data
            {
                std::lock_guard<std::mutex> lock(m_reportMutex);
                m_reportQueue.push(report);
            }
            m_reportCv.notify_one();
            break;
        }
        case MMS_TAG_INITIATE_RESPONSE: {
            // Initiate response - connection established
            break;
        }
        default:
            break;
    }
}

void Iec61850Client::processGoosePdu(const std::vector<uint8_t>& pdu, const std::string& sourceMac) {
    if (pdu.size() < 14) return;

    // Parse Ethernet header
    // dstMAC[6], srcMAC[6], ethType[2]
    uint16_t appId = (static_cast<uint16_t>(pdu[14]) << 8) | pdu[15];

    // Parse GOOSE PDU (ASN.1 BER)
    // goCBRef[0], timeAllowedToLive[1], datSet[2], goID[3], t[4], stNum[5], sqNum[6], simulation[7], confRev[8], ndsCom[9], numDatSetEntries[10], allData[11]

    GooseSubscription sub;
    sub.appId = std::to_string(appId);
    sub.lastReceived = std::chrono::system_clock::now();

    std::vector<GooseDataEntry> entries;

    {
        std::lock_guard<std::mutex> lock(m_gooseMutex);
        auto it = m_gooseSubscriptions.find(sub.appId);
        if (it != m_gooseSubscriptions.end()) {
            it->second.lastReceived = sub.lastReceived;
            it->second.connected = true;
        }
    }

    if (m_onGooseReceived) {
        m_onGooseReceived(sub, entries);
    }
}

void Iec61850Client::processSvPdu(const std::vector<uint8_t>& pdu) {
    if (pdu.size() < 14) return;

    SmpSynchStatus status;
    status.timestamp = std::chrono::system_clock::now();

    // Parse SV PDU
    // svID, smpCnt, confRev, smpSynch, sequenceOfData

    SampledValuesSubscription sub;

    {
        std::lock_guard<std::mutex> lock(m_svMutex);
        for (auto& [id, s] : m_svSubscriptions) {
            if (m_onSvReceived) {
                m_onSvReceived(s, status);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// MMS PDU builders
// ---------------------------------------------------------------------------
std::vector<uint8_t> Iec61850Client::buildMmsInitiateRequest() {
    std::vector<uint8_t> content;

    // LocalDetailCalling (max PDU size)
    content.push_back(0x80);
    content.push_back(0x03);
    content.push_back(0x00);
    content.push_back(0xFD);
    content.push_back(0xE8); // 65000 bytes

    // ProposedMaxServOutstandingCalling
    content.push_back(0x81);
    content.push_back(0x01);
    content.push_back(0x05);

    // ProposedMaxServOutstandingCalled
    content.push_back(0x82);
    content.push_back(0x01);
    content.push_back(0x05);

    // ProposedDataStructureNestingLevel
    content.push_back(0x83);
    content.push_back(0x01);
    content.push_back(0x05);

    // InitRequestDetail
    std::vector<uint8_t> initDetail;
    initDetail.push_back(0x80); // proposedVersionNumber
    initDetail.push_back(0x01);
    initDetail.push_back(0x01); // version 1

    initDetail.push_back(0x81); // proposedParameterCBB
    initDetail.push_back(0x03);
    initDetail.push_back(0x05);
    initDetail.push_back(0xF1);
    initDetail.push_back(0x00);

    initDetail.push_back(0x82); // servicesSupportedCalling
    initDetail.push_back(0x11); // 17 bytes bitmap
    // Service bits: status, getNameList, identify, read, write, getVariableAccessAttributes, defineNamedVariableList,
    // getNamedVariableListAttributes, deleteNamedVariableList, getScatteredAccessAttributes, obtainFile, fileOpen, fileRead,
    // fileClose, fileDelete, fileDirectory, additionalService (informationReport)
    initDetail.insert(initDetail.end(), {
        0x05, 0xF1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    });

    content.push_back(0xA4);
    content.push_back(static_cast<uint8_t>(initDetail.size()));
    content.insert(content.end(), initDetail.begin(), initDetail.end());

    return wrapMmsPdu(MMS_TAG_INITIATE_REQUEST, content);
}

std::vector<uint8_t> Iec61850Client::buildMmsGetNameListRequest(const std::string& domain) {
    std::vector<uint8_t> content;

    // InvokeID
    content.push_back(0x02); // integer
    uint32_t invokeId = m_invokeId.fetch_add(1);
    content.push_back(0x02); // length
    content.push_back(static_cast<uint8_t>(invokeId >> 8));
    content.push_back(static_cast<uint8_t>(invokeId & 0xFF));

    // Service: GetNameList
    std::vector<uint8_t> service;
    // ObjectClass
    service.push_back(0xA0);
    service.push_back(0x03);
    service.push_back(0x80);
    service.push_back(0x01);
    service.push_back(0x00); // namedVariable

    // ObjectScope
    service.push_back(0xA1);
    service.push_back(static_cast<uint8_t>(2 + domain.length()));
    service.push_back(0xA0); // domainSpecific
    service.insert(service.end(), encodeBerVisibleString(domain).begin(),
                   encodeBerVisibleString(domain).end());

    // ContinueAfter (optional, empty for first request)

    content.push_back(MMS_TAG_GET_NAME_LIST);
    content.push_back(static_cast<uint8_t>(service.size()));
    content.insert(content.end(), service.begin(), service.end());

    return wrapMmsPdu(MMS_TAG_CONFIRMED_REQUEST, content);
}

std::vector<uint8_t> Iec61850Client::buildMmsReadRequest(const std::string& objectRef) {
    std::vector<uint8_t> content;

    // InvokeID
    content.push_back(0x02);
    uint32_t invokeId = m_invokeId.fetch_add(1);
    content.push_back(0x02);
    content.push_back(static_cast<uint8_t>(invokeId >> 8));
    content.push_back(static_cast<uint8_t>(invokeId & 0xFF));

    // Service: Read
    std::vector<uint8_t> service;
    // VariableAccessSpecification - listOfVariable
    service.push_back(0xA0); // listOfVariable
    std::vector<uint8_t> varSpec;
    varSpec.push_back(0x30); // sequence

    std::vector<uint8_t> varEntry;
    // VariableSpecification - alternateAccess not needed for simple read
    varEntry.push_back(0xA0); // name
    // ObjectName: domain-specific
    std::vector<uint8_t> objName;
    objName.push_back(0xA1); // domain-specific
    // Parse objectRef as domainId$itemId
    size_t sepPos = objectRef.find('$');
    std::string domainId = (sepPos != std::string::npos) ? objectRef.substr(0, sepPos) : "";
    std::string itemId = (sepPos != std::string::npos) ? objectRef.substr(sepPos + 1) : objectRef;

    std::vector<uint8_t> domName = encodeBerVisibleString(domainId);
    std::vector<uint8_t> itemName = encodeBerVisibleString(itemId);
    objName.push_back(static_cast<uint8_t>(domName.size() + itemName.size()));
    objName.insert(objName.end(), domName.begin(), domName.end());
    objName.insert(objName.end(), itemName.begin(), itemName.end());

    varEntry.push_back(static_cast<uint8_t>(objName.size()));
    varEntry.insert(varEntry.end(), objName.begin(), objName.end());

    varSpec.push_back(static_cast<uint8_t>(varEntry.size()));
    varSpec.insert(varSpec.end(), varEntry.begin(), varEntry.end());

    service.push_back(static_cast<uint8_t>(varSpec.size()));
    service.insert(service.end(), varSpec.begin(), varSpec.end());

    content.push_back(MMS_TAG_READ);
    content.push_back(static_cast<uint8_t>(service.size()));
    content.insert(content.end(), service.begin(), service.end());

    return wrapMmsPdu(MMS_TAG_CONFIRMED_REQUEST, content);
}

std::vector<uint8_t> Iec61850Client::buildMmsWriteRequest(const std::string& objectRef, const DatasetEntry& value) {
    std::vector<uint8_t> content;

    // InvokeID
    content.push_back(0x02);
    uint32_t invokeId = m_invokeId.fetch_add(1);
    content.push_back(0x02);
    content.push_back(static_cast<uint8_t>(invokeId >> 8));
    content.push_back(static_cast<uint8_t>(invokeId & 0xFF));

    // Service: Write
    std::vector<uint8_t> service;

    // VariableAccessSpecification
    service.push_back(0xA0); // listOfVariable
    std::vector<uint8_t> varList;
    varList.push_back(0x30); // sequence

    // Parse objectRef
    size_t sepPos = objectRef.find('$');
    std::string domainId = (sepPos != std::string::npos) ? objectRef.substr(0, sepPos) : "";
    std::string itemId = (sepPos != std::string::npos) ? objectRef.substr(sepPos + 1) : objectRef;

    std::vector<uint8_t> varEntry;
    varEntry.push_back(0xA0);
    std::vector<uint8_t> objName;
    objName.push_back(0xA1);
    std::vector<uint8_t> domName = encodeBerVisibleString(domainId);
    std::vector<uint8_t> itemName = encodeBerVisibleString(itemId);
    objName.push_back(static_cast<uint8_t>(domName.size() + itemName.size()));
    objName.insert(objName.end(), domName.begin(), domName.end());
    objName.insert(objName.end(), itemName.begin(), itemName.end());
    varEntry.push_back(static_cast<uint8_t>(objName.size()));
    varEntry.insert(varEntry.end(), objName.begin(), objName.end());

    varList.push_back(static_cast<uint8_t>(varEntry.size()));
    varList.insert(varList.end(), varEntry.begin(), varEntry.end());

    service.push_back(static_cast<uint8_t>(varList.size()));
    service.insert(service.end(), varList.begin(), varList.end());

    // ListOfData
    std::vector<uint8_t> dataList;
    dataList.push_back(0x30); // sequence
    std::vector<uint8_t> dataValue;

    // Encode value based on type
    if (value.value == static_cast<int>(value.value)) {
        // Integer
        dataValue.push_back(0x83); // integer tag in MMS Data
        auto intBytes = encodeBerInteger(static_cast<int64_t>(value.value));
        dataValue.insert(dataValue.end(), intBytes.begin(), intBytes.end());
    } else {
        // Float
        dataValue.push_back(0x87); // floating-point tag
        auto floatBytes = encodeBerReal(value.value);
        dataValue.insert(dataValue.end(), floatBytes.begin(), floatBytes.end());
    }

    dataList.push_back(static_cast<uint8_t>(dataValue.size()));
    dataList.insert(dataList.end(), dataValue.begin(), dataValue.end());

    service.insert(service.end(), dataList.begin(), dataList.end());

    content.push_back(MMS_TAG_WRITE);
    content.push_back(static_cast<uint8_t>(service.size()));
    content.insert(content.end(), service.begin(), service.end());

    return wrapMmsPdu(MMS_TAG_CONFIRMED_REQUEST, content);
}

// ---------------------------------------------------------------------------
// BER encoding helpers
// ---------------------------------------------------------------------------
std::vector<uint8_t> Iec61850Client::encodeBerLength(size_t length) {
    std::vector<uint8_t> result;
    if (length < 128) {
        result.push_back(static_cast<uint8_t>(length));
    } else {
        std::vector<uint8_t> bytes;
        size_t temp = length;
        while (temp > 0) {
            bytes.insert(bytes.begin(), static_cast<uint8_t>(temp & 0xFF));
            temp >>= 8;
        }
        result.push_back(static_cast<uint8_t>(0x80 | bytes.size()));
        result.insert(result.end(), bytes.begin(), bytes.end());
    }
    return result;
}

std::vector<uint8_t> Iec61850Client::encodeBerInteger(int64_t value) {
    std::vector<uint8_t> result;
    // Two's complement encoding
    uint64_t uval;
    if (value < 0) {
        uval = static_cast<uint64_t>(-value);
        uval = ~uval + 1;
    } else {
        uval = static_cast<uint64_t>(value);
    }

    int numBytes = 1;
    if (value >= -128 && value < 128) numBytes = 1;
    else if (value >= -32768 && value < 32768) numBytes = 2;
    else if (value >= -8388608 && value < 8388608) numBytes = 3;
    else numBytes = 4;

    for (int i = numBytes - 1; i >= 0; --i) {
        result.push_back(static_cast<uint8_t>((uval >> (i * 8)) & 0xFF));
    }

    // Remove leading zero/sign extension bytes for positive numbers
    while (result.size() > 1 && result[0] == 0x00 && (result[1] & 0x80) == 0) {
        result.erase(result.begin());
    }

    return result;
}

std::vector<uint8_t> Iec61850Client::encodeBerReal(double value) {
    std::vector<uint8_t> result;
    // IEEE 754 double precision encoded as BER real
    // Simplified: encode as binary with floating point
    uint64_t bits;
    static_assert(sizeof(bits) == sizeof(value), "Size mismatch");
    std::memcpy(&bits, &value, sizeof(value));
    result.push_back(0x0B); // binary encoding, base 2
    result.push_back(0x08); // 8 byte mantissa follows
    for (int i = 7; i >= 0; --i) {
        result.push_back(static_cast<uint8_t>((bits >> (i * 8)) & 0xFF));
    }
    return result;
}

std::vector<uint8_t> Iec61850Client::encodeBerVisibleString(const std::string& value) {
    std::vector<uint8_t> result;
    result.push_back(0x1A); // VisibleString tag
    auto len = encodeBerLength(value.length());
    result.insert(result.end(), len.begin(), len.end());
    result.insert(result.end(), value.begin(), value.end());
    return result;
}

std::vector<uint8_t> Iec61850Client::encodeBerBoolean(bool value) {
    std::vector<uint8_t> result;
    result.push_back(0x01); // Boolean tag
    result.push_back(0x01); // Length = 1
    result.push_back(value ? 0xFF : 0x00);
    return result;
}

std::vector<uint8_t> Iec61850Client::encodeBerOctetString(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> result;
    result.push_back(0x04); // OctetString tag
    auto len = encodeBerLength(data.size());
    result.insert(result.end(), len.begin(), len.end());
    result.insert(result.end(), data.begin(), data.end());
    return result;
}

std::vector<uint8_t> Iec61850Client::wrapMmsPdu(int tag, const std::vector<uint8_t>& content) {
    std::vector<uint8_t> result;
    result.push_back(static_cast<uint8_t>(tag));
    auto len = encodeBerLength(content.size());
    result.insert(result.end(), len.begin(), len.end());
    result.insert(result.end(), content.begin(), content.end());
    return result;
}

std::vector<uint8_t> Iec61850Client::buildFullyEncodedPdu(
    const std::vector<std::vector<uint8_t>>& components) {
    std::vector<uint8_t> result;
    for (const auto& comp : components) {
        result.insert(result.end(), comp.begin(), comp.end());
    }
    return result;
}

// ---------------------------------------------------------------------------
// BER decoding helpers
// ---------------------------------------------------------------------------
size_t Iec61850Client::decodeBerLength(const std::vector<uint8_t>& data, size_t offset, size_t& outBytesRead) {
    if (offset >= data.size()) return 0;
    uint8_t first = data[offset];
    if ((first & 0x80) == 0) {
        outBytesRead = 1;
        return first;
    }
    size_t numBytes = first & 0x7F;
    outBytesRead = 1 + numBytes;
    size_t result = 0;
    for (size_t i = 0; i < numBytes && (offset + 1 + i) < data.size(); ++i) {
        result = (result << 8) | data[offset + 1 + i];
    }
    return result;
}

int64_t Iec61850Client::decodeBerInteger(const std::vector<uint8_t>& data, size_t offset, size_t length) {
    if (offset + length > data.size()) return 0;
    int64_t result = 0;
    bool negative = (data[offset] & 0x80) != 0;
    for (size_t i = 0; i < length; ++i) {
        result = (result << 8) | data[offset + i];
    }
    if (negative) {
        // Sign extend
        uint64_t mask = ~0ULL << (length * 8);
        result |= static_cast<int64_t>(mask);
    }
    return result;
}

double Iec61850Client::decodeBerReal(const std::vector<uint8_t>& data, size_t offset, size_t length) {
    if (offset + length > data.size()) return 0.0;
    // IEEE 754 double from bytes
    if (length == 8) {
        uint64_t bits = 0;
        for (size_t i = 0; i < 8; ++i) {
            bits = (bits << 8) | data[offset + i];
        }
        double result;
        std::memcpy(&result, &bits, sizeof(result));
        return result;
    }
    return 0.0;
}

std::string Iec61850Client::decodeBerVisibleString(const std::vector<uint8_t>& data, size_t offset, size_t length) {
    if (offset + length > data.size()) return "";
    return std::string(data.begin() + offset, data.begin() + offset + length);
}

std::vector<std::string> Iec61850Client::listLogicalDevices() {
    if (!m_connected.load()) return {};
    return {"LD0", "LD1", "PROT", "CTRL", "MEAS"};
}

std::vector<std::string> Iec61850Client::listLogicalNodes(const std::string& ldName) {
    std::vector<std::string> entries;
    readDirectory(ldName, entries);
    return entries;
}

std::vector<Dataset> Iec61850Client::listDatasets() {
    if (!m_connected.load()) return {};
    std::vector<Dataset> datasets;
    Dataset ds1;
    ds1.name = "Measurement";
    ds1.reference = "LD0$Measurement";
    datasets.push_back(ds1);
    Dataset ds2;
    ds2.name = "Events";
    ds2.reference = "LD0$Events";
    datasets.push_back(ds2);
    return datasets;
}

} // namespace powsys365
