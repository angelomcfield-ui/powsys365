#include "debug_adapter_manager.h"
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QElapsedTimer>
#include <QThread>
#include <QMutex>
#include <QMutexLocker>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QTimer>

namespace powsys365::ide::dap {

static const int DAP_TIMEOUT_MS = 30000;
static const int INITIALIZATION_TIMEOUT_MS = 15000;

class DebugAdapterManager::Impl {
public:
    DebugAdapterManager* q;
    DebugAdapterConfig currentConfig;
    DebugState currentState = DebugState::Idle;
    std::unique_ptr<QProcess> process;
    QTcpSocket* tcpSocket = nullptr;
    QByteArray readBuffer;
    int seqCounter = 1;
    int nextBreakpointId = 1;
    int currentThreadId_ = 0;
    QMap<int, std::function<void(const QJsonObject&)>> pendingRequests;
    QList<Breakpoint> breakpointList;
    QJsonObject capabilities;
    QMutex mutex;
    QMutex bpMutex;
    bool running = false;

    explicit Impl(DebugAdapterManager* parent) : q(parent) {}

    int nextSeq() { return seqCounter++; }

    void setState(DebugState s) {
        if (currentState != s) {
            currentState = s;
            Q_EMIT q->stateChanged(s);
        }
    }

    QByteArray encodeDapMessage(const QJsonObject& msg) {
        QJsonDocument doc(msg);
        QByteArray content = doc.toJson(QJsonDocument::Compact);
        return QString("Content-Length: %1\r\n\r\n").arg(content.size()).toUtf8() + content;
    }

    QJsonObject parseDapMessage(const QByteArray& data) {
        int headerEnd = data.indexOf("\r\n\r\n");
        if (headerEnd < 0) return QJsonObject();

        static QRegularExpression re("Content-Length:\\s*(\\d+)",
                                     QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch match = re.match(QString::fromUtf8(data.left(headerEnd)));
        if (!match.hasMatch()) return QJsonObject();

        int len = match.captured(1).toInt();
        QByteArray payload = data.mid(headerEnd + 4, len);

        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(payload, &err);
        if (err.error != QJsonParseError::NoError) return QJsonObject();
        return doc.object();
    }

    void sendDapMessage(const QJsonObject& msg) {
        QMutexLocker lock(&mutex);
        if (!process || process->state() != QProcess::Running) return;
        QByteArray encoded = encodeDapMessage(msg);
        process->write(encoded);
        process->flush();
    }

    QJsonObject sendRequestAndWait(const QString& command, const QJsonObject& arguments,
                                    int timeoutMs = DAP_TIMEOUT_MS) {
        int seq = nextSeq();
        QJsonObject msg;
        msg["seq"] = seq;
        msg["type"] = "request";
        msg["command"] = command;
        if (!arguments.isEmpty()) {
            msg["arguments"] = arguments;
        }

        QJsonObject response;
        bool received = false;
        {
            QMutexLocker lock(&mutex);
            pendingRequests[seq] = [&response, &received](const QJsonObject& resp) {
                response = resp;
                received = true;
            };
        }

        sendDapMessage(msg);

        QElapsedTimer timer;
        timer.start();
        while (!received && timer.elapsed() < timeoutMs) {
            processEvents();
            QThread::msleep(10);
        }

        {
            QMutexLocker lock(&mutex);
            pendingRequests.remove(seq);
        }

        return response;
    }

    void processEvents() {
        if (!process || process->state() != QProcess::Running) return;

        while (process->bytesAvailable() > 0) {
            readBuffer.append(process->readAll());

            while (true) {
                int headerEnd = readBuffer.indexOf("\r\n\r\n");
                if (headerEnd < 0) break;

                static QRegularExpression re("Content-Length:\\s*(\\d+)",
                                              QRegularExpression::CaseInsensitiveOption);
                QRegularExpressionMatch match = re.match(
                    QString::fromUtf8(readBuffer.left(headerEnd)));
                if (!match.hasMatch()) {
                    readBuffer.remove(0, headerEnd + 4);
                    continue;
                }

                int contentLen = match.captured(1).toInt();
                int totalLen = headerEnd + 4 + contentLen;
                if (readBuffer.size() < totalLen) break;

                QByteArray content = readBuffer.mid(headerEnd + 4, contentLen);
                readBuffer.remove(0, totalLen);

                QJsonParseError err;
                QJsonDocument doc = QJsonDocument::fromJson(content, &err);
                if (err.error != QJsonParseError::NoError) continue;

                handleMessage(doc.object());
            }
        }
    }

    void handleMessage(const QJsonObject& msg) {
        QString type = msg.value("type").toString();
        if (type == "response") {
            int request_seq = msg.value("request_seq").toInt();
            QMutexLocker lock(&mutex);
            auto cb = pendingRequests.take(request_seq);
            lock.unlock();
            if (cb) {
                cb(msg);
            }
        } else if (type == "event") {
            handleEvent(msg);
        }
    }

    void handleEvent(const QJsonObject& msg) {
        QString event = msg.value("event").toString();
        QJsonObject body = msg.value("body").toObject();

        if (event == "initialized") {
            setState(DebugState::Stopped);
            Q_EMIT q->initialized();
            // Send configuration done after initialization
            sendConfigurationDone();
        } else if (event == "stopped") {
            setState(DebugState::Stopped);
            QString reason = body.value("reason").toString();
            QString description = body.value("description").toString();
            currentThreadId_ = body.value("threadId").toInt();
            Q_EMIT q->stopped(reason, description);

            if (reason == "breakpoint" || reason == "step" || reason == "exception") {
                QList<StackFrame> frames = getStackTraceInternal(currentThreadId_, 20);
                Q_EMIT q->stackTraceReceived(frames);
            }
        } else if (event == "continued") {
            setState(DebugState::Running);
            Q_EMIT q->continued();
        } else if (event == "thread") {
            int tid = body.value("threadId").toInt();
            bool started = body.value("started").toBool();
            if (started) {
                Q_EMIT q->threadStarted(tid);
            } else {
                Q_EMIT q->threadExited(tid);
            }
        } else if (event == "output") {
            QString category = body.value("category").toString("console");
            QString output = body.value("output").toString();
            Q_EMIT q->outputReceived(category, output);
        } else if (event == "breakpoint") {
            QJsonObject bpObj = body.value("breakpoint").toObject();
            Breakpoint bp;
            bp.id = bpObj.value("id").toInt();
            bp.verified = bpObj.value("verified").toBool();
            bp.line = bpObj.value("line").toInt();
            bp.column = bpObj.value("column").toInt();
            bp.message = bpObj.value("message").toString();
            bp.file = bpObj.value("source").toObject().value("path").toString();

            {
                QMutexLocker lock(&bpMutex);
                for (auto& existing : breakpointList) {
                    if (existing.id == bp.id) {
                        existing.verified = bp.verified;
                        existing.line = bp.line;
                        existing.message = bp.message;
                        break;
                    }
                }
            }
            Q_EMIT q->breakpointHit(bp);
        } else if (event == "terminated") {
            setState(DebugState::Terminated);
            Q_EMIT q->terminated();
        } else if (event == "exited") {
            int exitCode = body.value("exitCode").toInt();
            setState(DebugState::Terminated);
            Q_EMIT q->exited(exitCode);
        } else if (event == "module") {
            // Module loaded/unloaded
        } else if (event == "loadedSource") {
            // Source loaded
        } else if (event == "process") {
            QString procName = body.value("name").toString();
            Q_EMIT q->outputReceived("console", QString("Process started: %1").arg(procName));
        } else if (event == "capabilities") {
            QJsonObject caps = body.value("capabilities").toObject();
            capabilities = caps;
            Q_EMIT q->capabilitiesReceived(caps);
        } else if (event == "progressStart" || event == "progressUpdate" || event == "progressEnd") {
            QString pid = body.value("progressId").toString();
            int pct = body.value("percentage").toInt();
            QString msg = body.value("message").toString();
            Q_EMIT q->progressUpdate(pid, pct, msg);
        }
    }

    void sendConfigurationDone() {
        QJsonObject msg;
        msg["seq"] = nextSeq();
        msg["type"] = "request";
        msg["command"] = "configurationDone";
        sendDapMessage(msg);
    }

    QList<StackFrame> getStackTraceInternal(int threadId, int levels) {
        QList<StackFrame> frames;

        QJsonObject args;
        args["threadId"] = threadId;
        if (levels > 0) args["levels"] = levels;

        QJsonObject resp = sendRequestAndWait("stackTrace", args);
        QJsonObject body = resp.value("body").toObject();
        QJsonArray stackFrames = body.value("stackFrames").toArray();

        for (const auto& sfVal : stackFrames) {
            QJsonObject sf = sfVal.toObject();
            StackFrame frame;
            frame.id = sf.value("id").toInt();
            frame.name = sf.value("name").toString();
            frame.line = sf.value("line").toInt();
            frame.column = sf.value("column").toInt();
            frame.module = sf.value("moduleId").toVariant().toString();

            QJsonObject source = sf.value("source").toObject();
            if (!source.isEmpty()) {
                frame.source = source.value("path").toString();
                if (frame.source.isEmpty()) {
                    frame.source = source.value("name").toString();
                }
            }

            frames.append(frame);
        }

        return frames;
    }
};

DebugAdapterManager::DebugAdapterManager(QObject* parent)
    : QObject(parent), d(std::make_unique<Impl>(this))
{
}

DebugAdapterManager::~DebugAdapterManager() {
    stopDebugging();
}

bool DebugAdapterManager::startDebugging(const DebugAdapterConfig& config) {
    d->currentConfig = config;
    d->setState(DebugState::Initializing);
    d->breakpointList.clear();

    d->process = std::make_unique<QProcess>(this);
    d->process->setProgram(config.executable);
    d->process->setArguments(config.arguments);

    if (!config.workingDirectory.isEmpty()) {
        d->process->setWorkingDirectory(config.workingDirectory);
    }

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QJsonObject envObj = config.env;
    for (auto it = envObj.begin(); it != envObj.end(); ++it) {
        env.insert(it.key(), it.value().toString());
    }
    d->process->setProcessEnvironment(env);

    connect(d->process.get(), &QProcess::started, this, [this, config]() {
        Q_EMIT outputReceived("console", QString("Debug adapter started: %1").arg(config.executable));

        // Start the reader thread
        d->running = true;
        QThread* reader = QThread::create([this]() {
            while (d->running) {
                d->processEvents();
                QThread::msleep(20);
            }
        });
        connect(reader, &QThread::finished, reader, &QObject::deleteLater);
        reader->start();

        // Send initialize request
        QJsonObject initArgs;
        initArgs["clientID"] = "powsys365-ide";
        initArgs["clientName"] = "POWSYS365 IDE";
        initArgs["adapterID"] = config.type;
        initArgs["locale"] = "en-US";
        initArgs["linesStartAt1"] = true;
        initArgs["columnsStartAt1"] = true;
        initArgs["pathFormat"] = "path";
        initArgs["supportsVariableType"] = true;
        initArgs["supportsVariablePaging"] = true;
        initArgs["supportsRunInTerminalRequest"] = false;
        initArgs["supportsMemoryReferences"] = true;
        initArgs["supportsProgressReporting"] = true;
        initArgs["supportsInvalidatedEvent"] = true;
        initArgs["supportsMemoryEvent"] = true;
        initArgs["supportsArgsCanBeInterpretedByShell"] = false;
        initArgs["supportsStartDebuggingRequest"] = true;

        QJsonObject resp = d->sendRequestAndWait("initialize", initArgs, INITIALIZATION_TIMEOUT_MS);
        if (!resp.isEmpty() && resp.value("success").toBool()) {
            d->capabilities = resp.value("body").toObject();
            Q_EMIT capabilitiesReceived(d->capabilities);

            // Send launch/attach request
            QJsonObject launchArgs;
            launchArgs["program"] = config.program;
            if (!config.programArgs.isEmpty()) {
                launchArgs["args"] = QJsonArray::fromStringList(config.programArgs);
            }
            launchArgs["stopOnEntry"] = config.stopOnEntry;
            launchArgs["cwd"] = config.workingDirectory;
            launchArgs["env"] = config.env;
            if (!config.console.isEmpty()) {
                launchArgs["console"] = config.console;
            }

            // Merge additional config
            QJsonObject addConfig = config.additionalConfig;
            for (auto it = addConfig.begin(); it != addConfig.end(); ++it) {
                launchArgs[it.key()] = it.value();
            }

            QString launchCommand = (config.request == "attach") ? "attach" : "launch";
            if (config.request == "attach" && config.processId > 0) {
                launchArgs["processId"] = config.processId;
            }

            QJsonObject launchResp = d->sendRequestAndWait(launchCommand, launchArgs);
            if (launchResp.value("success").toBool()) {
                d->setState(DebugState::Running);
            } else {
                d->setState(DebugState::Error);
                Q_EMIT error(launchResp.value("message").toString("Launch failed"));
            }
        } else {
            d->setState(DebugState::Error);
            Q_EMIT error("Failed to initialize debug adapter");
        }
    });

    connect(d->process.get(), &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError err) {
        d->setState(DebugState::Error);
        Q_EMIT error(QString("Debug adapter process error: %1").arg(err));
    });

    connect(d->process.get(),
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus) {
        d->running = false;
        if (d->currentState != DebugState::Terminated) {
            d->setState(DebugState::Terminated);
            Q_EMIT exited(exitCode);
            Q_EMIT terminated();
        }
    });

    d->process->start();
    return d->process->waitForStarted(DAP_TIMEOUT_MS);
}

bool DebugAdapterManager::stopDebugging() {
    if (!d->process || d->process->state() == QProcess::NotRunning) return false;

    d->running = false;

    QJsonObject args;
    args["terminateDebuggee"] = true;
    d->sendRequestAndWait("terminate", args, 5000);
    d->sendRequestAndWait("disconnect", QJsonObject(), 5000);

    d->process->terminate();
    if (!d->process->waitForFinished(3000)) {
        d->process->kill();
    }

    d->setState(DebugState::Idle);
    return true;
}

bool DebugAdapterManager::restartDebugging() {
    if (!d->process) return false;

    QJsonObject args;
    d->sendRequestAndWait("restart", args, DAP_TIMEOUT_MS);
    return true;
}

bool DebugAdapterManager::pause() {
    if (d->currentState == DebugState::Idle) return false;

    QJsonObject args;
    args["threadId"] = d->currentThreadId_;
    QJsonObject resp = d->sendRequestAndWait("pause", args);
    if (resp.value("success").toBool()) {
        d->setState(DebugState::Paused);
    }
    return resp.value("success").toBool();
}

bool DebugAdapterManager::continue_() {
    if (d->currentState == DebugState::Idle) return false;

    QJsonObject args;
    args["threadId"] = d->currentThreadId_;
    args["singleThread"] = false;
    QJsonObject resp = d->sendRequestAndWait("continue", args);
    if (resp.value("success").toBool()) {
        d->setState(DebugState::Running);
    }
    return resp.value("success").toBool();
}

bool DebugAdapterManager::next() {
    if (d->currentState != DebugState::Stopped) return false;

    QJsonObject args;
    args["threadId"] = d->currentThreadId_;
    args["singleThread"] = false;
    d->setState(DebugState::Stepping);
    QJsonObject resp = d->sendRequestAndWait("next", args);
    return resp.value("success").toBool();
}

bool DebugAdapterManager::stepIn() {
    if (d->currentState != DebugState::Stopped) return false;

    QJsonObject args;
    args["threadId"] = d->currentThreadId_;
    args["singleThread"] = false;
    d->setState(DebugState::Stepping);
    QJsonObject resp = d->sendRequestAndWait("stepIn", args);
    return resp.value("success").toBool();
}

bool DebugAdapterManager::stepOut() {
    if (d->currentState != DebugState::Stopped) return false;

    QJsonObject args;
    args["threadId"] = d->currentThreadId_;
    args["singleThread"] = false;
    d->setState(DebugState::Stepping);
    QJsonObject resp = d->sendRequestAndWait("stepOut", args);
    return resp.value("success").toBool();
}

bool DebugAdapterManager::stepBack() {
    if (d->currentState != DebugState::Stopped) return false;

    QJsonObject args;
    args["threadId"] = d->currentThreadId_;
    QJsonObject resp = d->sendRequestAndWait("stepBack", args);
    return resp.value("success").toBool();
}

bool DebugAdapterManager::reverseContinue() {
    if (d->currentState != DebugState::Stopped) return false;

    QJsonObject args;
    args["threadId"] = d->currentThreadId_;
    QJsonObject resp = d->sendRequestAndWait("reverseContinue", args);
    return resp.value("success").toBool();
}

bool DebugAdapterManager::setBreakpoint(const QString& file, int line,
                                        const QString& condition) {
    QJsonObject args;

    QJsonObject source;
    source["path"] = file;
    source["name"] = QFileInfo(file).fileName();
    args["source"] = source;

    QJsonObject bp;
    bp["line"] = line;
    if (!condition.isEmpty()) bp["condition"] = condition;
    bp["verified"] = false;

    args["breakpoints"] = QJsonArray{bp};
    args["lines"] = QJsonArray{line};
    args["sourceModified"] = false;

    QJsonObject resp = d->sendRequestAndWait("setBreakpoints", args);
    QJsonObject body = resp.value("body").toObject();
    QJsonArray bps = body.value("breakpoints").toArray();

    QMutexLocker lock(&d->bpMutex);
    for (const auto& bpVal : bps) {
        QJsonObject bpObj = bpVal.toObject();
        Breakpoint bp;
        bp.id = bpObj.value("id").toInt(d->nextBreakpointId++);
        bp.verified = bpObj.value("verified").toBool();
        bp.file = file;
        bp.line = bpObj.value("line").toInt(line);
        bp.column = bpObj.value("column").toInt();
        bp.condition = condition;
        bp.enabled = true;
        bp.message = bpObj.value("message").toString();
        d->breakpointList.append(bp);
    }

    Q_EMIT breakpointChanged(d->breakpointList);
    return resp.value("success").toBool();
}

bool DebugAdapterManager::setFunctionBreakpoint(const QString& functionName) {
    QJsonObject args;
    QJsonObject bp;
    bp["name"] = functionName;
    args["breakpoints"] = QJsonArray{bp};

    QJsonObject resp = d->sendRequestAndWait("setFunctionBreakpoints", args);
    return resp.value("success").toBool();
}

bool DebugAdapterManager::setDataBreakpoint(const QString& dataId, const QString& accessType) {
    QJsonObject args;
    QJsonObject bp;
    bp["dataId"] = dataId;
    bp["accessType"] = accessType; // "read", "write", "readWrite"
    args["breakpoints"] = QJsonArray{bp};

    QJsonObject resp = d->sendRequestAndWait("setDataBreakpoints", args);
    return resp.value("success").toBool();
}

bool DebugAdapterManager::setInstructionBreakpoint(quint64 address) {
    QJsonObject args;
    QJsonObject bp;
    bp["instructionReference"] = QString::number(address, 16);
    args["breakpoints"] = QJsonArray{bp};

    QJsonObject resp = d->sendRequestAndWait("setInstructionBreakpoints", args);
    return resp.value("success").toBool();
}

bool DebugAdapterManager::removeBreakpoint(int breakpointId) {
    QMutexLocker lock(&d->bpMutex);
    auto it = std::remove_if(d->breakpointList.begin(), d->breakpointList.end(),
        [breakpointId](const Breakpoint& bp) { return bp.id == breakpointId; });
    bool removed = it != d->breakpointList.end();
    d->breakpointList.erase(it, d->breakpointList.end());
    lock.unlock();

    if (removed) {
        // Re-send all breakpoints for affected files
        QMap<QString, QList<Breakpoint>> byFile;
        for (const auto& bp : d->breakpointList) {
            byFile[bp.file].append(bp);
        }
        for (auto it = byFile.begin(); it != byFile.end(); ++it) {
            QJsonObject args;
            QJsonObject source;
            source["path"] = it.key();
            args["source"] = source;
            QJsonArray bps;
            for (const auto& bp : it.value()) {
                QJsonObject bpObj;
                bpObj["line"] = bp.line;
                if (!bp.condition.isEmpty()) bpObj["condition"] = bp.condition;
                bps.append(bpObj);
            }
            args["breakpoints"] = bps;
            d->sendRequestAndWait("setBreakpoints", args);
        }
        Q_EMIT breakpointChanged(d->breakpointList);
    }
    return removed;
}

bool DebugAdapterManager::removeAllBreakpoints() {
    QMutexLocker lock(&d->bpMutex);
    d->breakpointList.clear();
    lock.unlock();

    QJsonObject args;
    args["breakpoints"] = QJsonArray();
    d->sendRequestAndWait("setBreakpoints", args);
    Q_EMIT breakpointChanged(d->breakpointList);
    return true;
}

bool DebugAdapterManager::enableBreakpoint(int breakpointId, bool enable) {
    QMutexLocker lock(&d->bpMutex);
    for (auto& bp : d->breakpointList) {
        if (bp.id == breakpointId) {
            bp.enabled = enable;
            lock.unlock();
            Q_EMIT breakpointChanged(d->breakpointList);
            return true;
        }
    }
    return false;
}

bool DebugAdapterManager::updateBreakpoint(int breakpointId, const QString& condition) {
    QMutexLocker lock(&d->bpMutex);
    for (auto& bp : d->breakpointList) {
        if (bp.id == breakpointId) {
            bp.condition = condition;
            lock.unlock();
            Q_EMIT breakpointChanged(d->breakpointList);
            return true;
        }
    }
    return false;
}

QList<Breakpoint> DebugAdapterManager::breakpoints() const {
    QMutexLocker lock(&d->bpMutex);
    return d->breakpointList;
}

QList<StackFrame> DebugAdapterManager::getStackTrace(int threadId, int levels) {
    int tid = (threadId == 0) ? d->currentThreadId_ : threadId;
    return d->getStackTraceInternal(tid, levels);
}

QList<Scope> DebugAdapterManager::getScopes(int frameId) {
    QList<Scope> scopes;

    QJsonObject resp = d->sendRequestAndWait("scopes", QJsonObject{{"frameId", frameId}});
    QJsonObject body = resp.value("body").toObject();
    QJsonArray scopesArr = body.value("scopes").toArray();

    for (const auto& scVal : scopesArr) {
        QJsonObject sc = scVal.toObject();
        Scope scope;
        scope.variablesReference = sc.value("variablesReference").toInt();
        scope.name = sc.value("name").toString();
        scope.expensive = sc.value("expensive").toBool();
        scope.namedVariables = sc.value("namedVariables").toInt();
        scope.indexedVariables = sc.value("indexedVariables").toInt();
        scopes.append(scope);
    }

    return scopes;
}

QList<Variable> DebugAdapterManager::getVariables(int variablesReference) {
    QList<Variable> vars;

    QJsonObject args;
    args["variablesReference"] = variablesReference;
    QJsonObject resp = d->sendRequestAndWait("variables", args);
    QJsonObject body = resp.value("body").toObject();
    QJsonArray varsArr = body.value("variables").toArray();

    for (const auto& varVal : varsArr) {
        QJsonObject v = varVal.toObject();
        Variable var;
        var.name = v.value("name").toString();
        var.value = v.value("value").toString();
        var.type = v.value("type").toString();
        var.variablesReference = v.value("variablesReference").toInt();
        var.namedVariables = v.value("namedVariables").toInt();
        var.indexedVariables = v.value("indexedVariables").toInt();
        var.expandable = var.variablesReference > 0;
        vars.append(var);
    }

    return vars;
}

QList<Variable> DebugAdapterManager::evaluateWatch(const QString& expression, int frameId) {
    QList<Variable> vars;

    QJsonObject args;
    args["expression"] = expression;
    args["frameId"] = frameId;
    args["context"] = "watch";

    QJsonObject resp = d->sendRequestAndWait("evaluate", args);
    QJsonObject body = resp.value("body").toObject();

    if (resp.value("success").toBool()) {
        Variable var;
        var.name = expression;
        var.value = body.value("result").toString();
        var.type = body.value("type").toString();
        var.variablesReference = body.value("variablesReference").toInt();
        var.expandable = var.variablesReference > 0;
        vars.append(var);
    }

    return vars;
}

bool DebugAdapterManager::setVariable(int variablesReference, const QString& name,
                                       const QString& value) {
    QJsonObject args;
    args["variablesReference"] = variablesReference;
    args["name"] = name;
    args["value"] = value;

    QJsonObject resp = d->sendRequestAndWait("setVariable", args);
    return resp.value("success").toBool();
}

QList<ThreadInfo> DebugAdapterManager::getThreads() {
    QList<ThreadInfo> threads;

    QJsonObject resp = d->sendRequestAndWait("threads", QJsonObject());
    QJsonObject body = resp.value("body").toObject();
    QJsonArray threadsArr = body.value("threads").toArray();

    for (const auto& tVal : threadsArr) {
        QJsonObject t = tVal.toObject();
        ThreadInfo ti;
        ti.id = t.value("id").toInt();
        ti.name = t.value("name").toString();
        threads.append(ti);
    }

    return threads;
}

DebugState DebugAdapterManager::state() const {
    return d->currentState;
}

bool DebugAdapterManager::isRunning() const {
    return d->currentState == DebugState::Running;
}

bool DebugAdapterManager::isStopped() const {
    return d->currentState == DebugState::Stopped || d->currentState == DebugState::Paused;
}

int DebugAdapterManager::currentThreadId() const {
    return d->currentThreadId_;
}

void DebugAdapterManager::registerAdapter(const QString& type,
                                           const DebugAdapterConfig& config) {
    QMutexLocker lock(&d->mutex);
    // Store in a static map if needed
    Q_UNUSED(type)
    Q_UNUSED(config)
}

QStringList DebugAdapterManager::supportedAdapters() const {
    return QStringList{
        "cppdbg",       // GDB/LLDB for C/C++ (VS Code cppdbg)
        "gdb",          // Native GDB
        "lldb",         // Native LLDB
        "python",       // Python debugger (debugpy)
        "node",         // Node.js
        "node2",        // Node.js legacy
        "chrome",       // Chrome DevTools Protocol
        "msedge",       // Edge DevTools Protocol
        "java",         // Java Debug Server
        "go",           // Go Delve
        "ruby",         // Ruby debugger
        "php",          // PHP XDebug
        "dart",         // Dart/Flutter
        "flutter",      // Flutter
        "native",       // Native debug
        "coreclr",      // .NET Core
        "mono",         // Mono
        "lldb-vscode",  // LLDB VS Code
        "mock",         // Mock adapter for testing
        "rust",         // Rust (via cpptools or native)
        "swift",        // Swift LLDB
        "kotlin",       // Kotlin JVM
        "scala",        // Scala
        "bash",         // Bash
        "powershell",   // PowerShell
    };
}

QStringList DebugAdapterManager::availableDebuggers() const {
    QStringList debuggers;

    // Check for common debugger executables
    struct DbgCheck {
        QString name;
        QStringList executables;
    };

    QList<DbgCheck> checks = {
        {"GDB", {"gdb", "gdb.exe"}},
        {"LLDB", {"lldb", "lldb.exe"}},
        {"Python (debugpy)", {"debugpy", "python"}},
        {"Node.js", {"node"}},
        {"Java", {"java"}},
        {"Go (Delve)", {"dlv"}},
        {"Rust (GDB)", {"gdb", "rust-gdb", "rust-lldb"}},
        {"Ruby", {"ruby", "rdebug-ide"}},
        {"PHP (XDebug)", {"php"}},
        {".NET Core", {"dotnet"}},
        {"Mono", {"mono"}},
        {"Dart", {"dart"}},
        {"Flutter", {"flutter"}},
        {"Kotlin", {"kotlin"}},
        {"Swift", {"swift", "lldb"}},
        {"Chrome DevTools", {"node"}},
        {"Bash", {"bashdb", "bash"}},
        {"PowerShell", {"powershell", "pwsh"}},
        {"C++ (cppdbg)", {"gdb", "lldb"}},
    };

    QString pathEnv = QProcessEnvironment::systemEnvironment().value("PATH");
    QStringList pathDirs = pathEnv.split(QDir::listSeparator());

    for (const auto& check : checks) {
        for (const QString& exe : check.executables) {
            bool found = false;
            for (const QString& dir : pathDirs) {
                if (QFile::exists(QDir(dir).absoluteFilePath(exe)) ||
                    QFile::exists(QDir(dir).absoluteFilePath(exe + ".exe"))) {
                    debuggers.append(check.name);
                    found = true;
                    break;
                }
            }
            if (found) break;
        }
    }

    return debuggers;
}

QString DebugAdapterManager::getSource(const QString& sourceReference) {
    QJsonObject args;
    QJsonObject source;
    source["sourceReference"] = sourceReference.toInt();
    args["source"] = source;

    QJsonObject resp = d->sendRequestAndWait("source", args);
    QJsonObject body = resp.value("body").toObject();
    return body.value("content").toString();
}

} // namespace powsys365::ide::dap
