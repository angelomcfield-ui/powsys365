#include "json_rpc.h"
#include <QTcpSocket>
#include <QLocalSocket>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QRegularExpression>
#include <QElapsedTimer>
#include <QDebug>
#include <QThread>
#include <QEventLoop>
#include <QTimer>
#include <QMutexLocker>
#include <QWaitCondition>

namespace powsys365::ide::lsp {

class JsonRpc::Impl {
public:
    QIODevice* readDevice = nullptr;
    QIODevice* writeDevice = nullptr;
    QTcpSocket* tcpSocket = nullptr;
    QLocalSocket* localSocket = nullptr;
    QByteArray readBuffer;
    bool running = false;
    mutable QMutex writeMutex;
    mutable QMutex responseMutex;
    QMap<int, QJsonObject> pendingResponses;
    QWaitCondition responseCondition;
    int nextId = 1;

    QByteArray parseContentLength(const QByteArray& header) {
        static const QRegularExpression re("Content-Length:\\s*(\\d+)",
                                            QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch match = re.match(QString::fromUtf8(header));
        if (match.hasMatch()) {
            return match.captured(1).toUtf8();
        }
        return QByteArray();
    }

    bool readMessage(QByteArray& outMessage) {
        if (!readDevice || !readDevice->isReadable()) return false;

        while (readBuffer.size() < 16) {
            if (!readDevice->waitForReadyRead(100)) {
                if (!running) return false;
                continue;
            }
            readBuffer.append(readDevice->readAll());
        }

        // Parse header
        int headerEnd = readBuffer.indexOf("\r\n\r\n");
        if (headerEnd < 0) return false;

        QByteArray header = readBuffer.left(headerEnd);
        QByteArray lenStr = parseContentLength(header);
        if (lenStr.isEmpty()) {
            // Try to recover by skipping to next header
            readBuffer.remove(0, headerEnd + 4);
            return false;
        }

        int contentLength = lenStr.toInt();
        int totalNeeded = headerEnd + 4 + contentLength;

        if (readBuffer.size() < totalNeeded) return false;

        outMessage = readBuffer.mid(headerEnd + 4, contentLength);
        readBuffer.remove(0, totalNeeded);
        return true;
    }
};

JsonRpc::JsonRpc(QObject* parent)
    : QObject(parent), d(std::make_unique<Impl>())
{
}

JsonRpc::~JsonRpc() {
    stopReading();
}

void JsonRpc::setIODevice(QIODevice* readDevice, QIODevice* writeDevice) {
    d->readDevice = readDevice;
    d->writeDevice = writeDevice;
    d->tcpSocket = nullptr;
    d->localSocket = nullptr;
}

void JsonRpc::setTcpSocket(QTcpSocket* socket) {
    d->tcpSocket = socket;
    d->readDevice = socket;
    d->writeDevice = socket;
    d->localSocket = nullptr;
}

void JsonRpc::setLocalSocket(QLocalSocket* socket) {
    d->localSocket = socket;
    d->readDevice = socket;
    d->writeDevice = socket;
    d->tcpSocket = nullptr;
}

bool JsonRpc::startReading() {
    if (!d->readDevice || !d->writeDevice) return false;
    d->running = true;

    // Start a reading thread
    QThread* readerThread = QThread::create([this]() {
        while (d->running) {
            QByteArray message;
            if (d->readMessage(message)) {
                QJsonParseError parseError;
                QJsonDocument doc = QJsonDocument::fromJson(message, &parseError);
                if (parseError.error == QJsonParseError::NoError) {
                    QJsonObject msg = doc.object();

                    // Check if it's a response to a pending request
                    if (msg.contains("id") && !msg.contains("method")) {
                        int msgId = msg.value("id").toInt(-1);
                        if (msgId >= 0) {
                            QMutexLocker lock(&d->responseMutex);
                            d->pendingResponses[msgId] = msg;
                            d->responseCondition.wakeAll();
                            lock.unlock();
                        }
                    }

                    Q_EMIT messageReceived(msg);

                    if (msg.contains("method")) {
                        QString method = msg.value("method").toString();
                        QJsonObject params = msg.value("params").toObject();
                        if (msg.contains("id")) {
                            Q_EMIT requestReceived(method, params, msg.value("id").toInt());
                        } else {
                            Q_EMIT notificationReceived(method, params);
                        }
                    } else if (msg.contains("error")) {
                        QJsonObject err = msg.value("error").toObject();
                        int msgId = msg.value("id").toInt(-1);
                        Q_EMIT errorReceived(msgId, err.value("code").toInt(),
                                            err.value("message").toString());
                    } else if (msg.contains("result")) {
                        int msgId = msg.value("id").toInt(-1);
                        Q_EMIT responseReceived(msgId, msg.value("result").toObject());
                    }
                } else {
                    Q_EMIT error(QString("JSON parse error: %1").arg(parseError.errorString()));
                }
            }
        }
    });

    connect(readerThread, &QThread::finished, readerThread, &QObject::deleteLater);
    readerThread->start();
    return true;
}

void JsonRpc::stopReading() {
    d->running = false;
}

bool JsonRpc::isRunning() const {
    return d->running;
}

int JsonRpc::sendRequest(const QString& method, const QJsonObject& params, int id) {
    QMutexLocker lock(&d->writeMutex);

    int reqId = (id < 0) ? d->nextId++ : id;
    QJsonObject message = createRequest(method, params, reqId);
    QByteArray encoded = encodeMessage(message);

    if (d->writeDevice && d->writeDevice->isWritable()) {
        d->writeDevice->write(encoded);
        d->writeDevice->flush();
        return reqId;
    }
    return -1;
}

int JsonRpc::sendRequest(const QString& method, const QJsonArray& params, int id) {
    QJsonObject paramsObj;
    paramsObj["__array_params__"] = params;
    int reqId = sendRequest(method, paramsObj, id);
    return reqId;
}

void JsonRpc::sendNotification(const QString& method, const QJsonObject& params) {
    QMutexLocker lock(&d->writeMutex);

    QJsonObject message = createNotification(method, params);
    QByteArray encoded = encodeMessage(message);

    if (d->writeDevice && d->writeDevice->isWritable()) {
        d->writeDevice->write(encoded);
        d->writeDevice->flush();
    }
}

void JsonRpc::sendNotification(const QString& method, const QJsonArray& params) {
    QJsonObject paramsObj;
    paramsObj["__array_params__"] = params;
    sendNotification(method, paramsObj);
}

void JsonRpc::sendResponse(int id, const QJsonObject& result) {
    QMutexLocker lock(&d->writeMutex);

    QJsonObject message = createResponse(id, result);
    QByteArray encoded = encodeMessage(message);

    if (d->writeDevice && d->writeDevice->isWritable()) {
        d->writeDevice->write(encoded);
        d->writeDevice->flush();
    }
}

void JsonRpc::sendResponse(int id, const QJsonArray& result) {
    QJsonObject resultObj;
    resultObj["__array_result__"] = result;
    sendResponse(id, resultObj);
}

void JsonRpc::sendError(int id, int code, const QString& message, const QJsonValue& data) {
    QMutexLocker lock(&d->writeMutex);

    QJsonObject message = createError(id, code, message, data);
    QByteArray encoded = encodeMessage(message);

    if (d->writeDevice && d->writeDevice->isWritable()) {
        d->writeDevice->write(encoded);
        d->writeDevice->flush();
    }
}

void JsonRpc::sendBatch(const QList<QJsonObject>& messages) {
    QMutexLocker lock(&d->writeMutex);

    QJsonArray batch;
    for (const auto& msg : messages) {
        batch.append(msg);
    }

    QJsonDocument doc(batch);
    QByteArray content = doc.toJson(QJsonDocument::Compact);
    QByteArray header = QString("Content-Length: %1\r\n\r\n").arg(content.size()).toUtf8();

    if (d->writeDevice && d->writeDevice->isWritable()) {
        d->writeDevice->write(header + content);
        d->writeDevice->flush();
    }
}

QJsonObject JsonRpc::sendRequestSync(const QString& method, const QJsonObject& params,
                                      int timeoutMs) {
    int reqId = sendRequest(method, params);
    if (reqId < 0) return QJsonObject();

    QMutexLocker lock(&d->responseMutex);
    bool ok = d->responseCondition.wait(&d->responseMutex, timeoutMs);
    if (!ok) return QJsonObject();

    auto it = d->pendingResponses.find(reqId);
    if (it != d->pendingResponses.end()) {
        QJsonObject response = it.value();
        d->pendingResponses.erase(it);
        return response;
    }
    return QJsonObject();
}

QJsonObject JsonRpc::parseResponse(const QByteArray& data) const {
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    if (error.error == QJsonParseError::NoError) {
        return doc.object();
    }
    return QJsonObject();
}

bool JsonRpc::isResponse(const QJsonObject& msg) const {
    return msg.contains("id") && msg.contains("result");
}

bool JsonRpc::isNotification(const QJsonObject& msg) const {
    return msg.contains("method") && !msg.contains("id");
}

bool JsonRpc::isRequest(const QJsonObject& msg) const {
    return msg.contains("method") && msg.contains("id");
}

bool JsonRpc::isError(const QJsonObject& msg) const {
    return msg.contains("error");
}

QJsonObject JsonRpc::createRequest(const QString& method, const QJsonObject& params, int id) {
    QJsonObject msg;
    msg["jsonrpc"] = "2.0";
    msg["id"] = id;
    msg["method"] = method;
    if (!params.isEmpty()) {
        if (params.contains("__array_params__")) {
            msg["params"] = params.value("__array_params__");
        } else {
            msg["params"] = params;
        }
    }
    return msg;
}

QJsonObject JsonRpc::createNotification(const QString& method, const QJsonObject& params) {
    QJsonObject msg;
    msg["jsonrpc"] = "2.0";
    msg["method"] = method;
    if (!params.isEmpty()) {
        if (params.contains("__array_params__")) {
            msg["params"] = params.value("__array_params__");
        } else {
            msg["params"] = params;
        }
    }
    return msg;
}

QJsonObject JsonRpc::createResponse(int id, const QJsonObject& result) {
    QJsonObject msg;
    msg["jsonrpc"] = "2.0";
    msg["id"] = id;
    if (result.contains("__array_result__")) {
        msg["result"] = result.value("__array_result__");
    } else {
        msg["result"] = result;
    }
    return msg;
}

QJsonObject JsonRpc::createError(int id, int code, const QString& message,
                                  const QJsonValue& data) {
    QJsonObject msg;
    msg["jsonrpc"] = "2.0";
    msg["id"] = id;
    QJsonObject err;
    err["code"] = code;
    err["message"] = message;
    if (!data.isNull() && !data.isUndefined()) {
        err["data"] = data;
    }
    msg["error"] = err;
    return msg;
}

QByteArray JsonRpc::encodeMessage(const QJsonObject& message) {
    QJsonDocument doc(message);
    QByteArray content = doc.toJson(QJsonDocument::Compact);
    QByteArray header = QString("Content-Length: %1\r\n\r\n").arg(content.size()).toUtf8();
    return header + content;
}

QJsonObject JsonRpc::decodeMessage(const QByteArray& data, bool* ok) {
    if (ok) *ok = false;

    QByteArray payload = data;

    // Check if data has Content-Length header
    int headerEnd = payload.indexOf("\r\n\r\n");
    if (headerEnd >= 0) {
        QByteArray header = payload.left(headerEnd);
        static const QRegularExpression re("Content-Length:\\s*(\\d+)",
                                            QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch match = re.match(QString::fromUtf8(header));
        if (match.hasMatch()) {
            int contentLength = match.captured(1).toInt();
            payload = payload.mid(headerEnd + 4, contentLength);
        }
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(payload, &error);
    if (error.error == QJsonParseError::NoError) {
        if (ok) *ok = true;
        return doc.object();
    }
    return QJsonObject();
}

} // namespace powsys365::ide::lsp
