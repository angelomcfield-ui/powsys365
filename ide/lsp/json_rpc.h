#pragma once

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QIODevice>
#include <QByteArray>
#include <QMutex>
#include <QThread>
#include <QQueue>
#include <QWaitCondition>
#include <memory>

namespace powsys365::ide::lsp {

/**
 * @brief JSON-RPC 2.0 protocol implementation for LSP communication
 *
 * Handles the complete JSON-RPC 2.0 protocol including:
 * - Request/Response message pairs with unique IDs
 * - Notification messages (no response required)
 * - Batch message processing
 * - Content-Length header framing (per LSP spec)
 * - Error handling with proper error codes
 */
class JsonRpc : public QObject {
    Q_OBJECT

public:
    explicit JsonRpc(QObject* parent = nullptr);
    ~JsonRpc();

    // I/O device setup
    void setIODevice(QIODevice* readDevice, QIODevice* writeDevice);
    void setTcpSocket(QTcpSocket* socket);
    void setLocalSocket(QLocalSocket* socket);

    // Lifecycle
    bool startReading();
    void stopReading();
    bool isRunning() const;

    // JSON-RPC 2.0 Message Sending
    int sendRequest(const QString& method, const QJsonObject& params, int id = -1);
    int sendRequest(const QString& method, const QJsonArray& params, int id = -1);
    void sendNotification(const QString& method, const QJsonObject& params);
    void sendNotification(const QString& method, const QJsonArray& params);
    void sendResponse(int id, const QJsonObject& result);
    void sendResponse(int id, const QJsonArray& result);
    void sendError(int id, int code, const QString& message, const QJsonValue& data = QJsonValue());
    void sendBatch(const QList<QJsonObject>& messages);

    // Synchronous request (blocking with timeout)
    QJsonObject sendRequestSync(const QString& method, const QJsonObject& params,
                                int timeoutMs = 30000);

    // Response handling
    QJsonObject parseResponse(const QByteArray& data) const;
    bool isResponse(const QJsonObject& msg) const;
    bool isNotification(const QJsonObject& msg) const;
    bool isRequest(const QJsonObject& msg) const;
    bool isError(const QJsonObject& msg) const;

    // Message factory
    static QJsonObject createRequest(const QString& method, const QJsonObject& params, int id);
    static QJsonObject createNotification(const QString& method, const QJsonObject& params);
    static QJsonObject createResponse(int id, const QJsonObject& result);
    static QJsonObject createError(int id, int code, const QString& message,
                                    const QJsonValue& data = QJsonValue());

    // Framing
    static QByteArray encodeMessage(const QJsonObject& message);
    static QJsonObject decodeMessage(const QByteArray& data, bool* ok = nullptr);

    // Error codes (JSON-RPC 2.0 + LSP extensions)
    enum ErrorCode {
        ParseError = -32700,
        InvalidRequest = -32600,
        MethodNotFound = -32601,
        InvalidParams = -32602,
        InternalError = -32603,
        ServerErrorStart = -32099,
        ServerErrorEnd = -32000,
        ServerNotInitialized = -32002,
        UnknownErrorCode = -32001,
        RequestCancelled = -32800,
        ContentModified = -32801,
        // LSP-specific
        LspServerNotInitialized = -32002,
        LspRequestFailed = -32803,
    };

Q_SIGNALS:
    void messageReceived(const QJsonObject& message);
    void notificationReceived(const QString& method, const QJsonObject& params);
    void requestReceived(const QString& method, const QJsonObject& params, int id);
    void responseReceived(int id, const QJsonObject& result);
    void errorReceived(int id, int code, const QString& message);
    void error(const QString& errorString);
    void connectionClosed();

private:
    class Impl;
    std::unique_ptr<Impl> d;
};

} // namespace powsys365::ide::lsp
