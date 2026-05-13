#pragma once

#include <QObject>
#include <QProcess>
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QDateTime>
#include <memory>
#include <functional>

namespace powsys365::ide::dap {

/**
 * @brief Debug adapter configuration for a specific debugger
 */
struct DebugAdapterConfig {
    QString name;
    QString type;
    QString executable;
    QStringList arguments;
    QString program;
    QStringList programArgs;
    QString workingDirectory;
    QJsonObject env;
    bool stopOnEntry = false;
    QString console;
    int port = 0;
    QString host = "127.0.0.1";
    QString request; // "launch" or "attach"
    int processId = 0;
    QJsonObject additionalConfig;
};

/**
 * @brief Stack frame information
 */
struct StackFrame {
    int id = 0;
    QString name;
    QString source;
    int line = 0;
    int column = 0;
    QString module;
    bool presentationHint = false;
};

/**
 * @brief Scope information (Locals, Globals, Registers)
 */
struct Scope {
    int variablesReference = 0;
    QString name;
    bool expensive = false;
    int namedVariables = 0;
    int indexedVariables = 0;
};

/**
 * @brief Variable information
 */
struct Variable {
    QString name;
    QString value;
    QString type;
    int variablesReference = 0;
    int namedVariables = 0;
    int indexedVariables = 0;
    bool expandable = false;
};

/**
 * @brief Breakpoint information
 */
struct Breakpoint {
    int id = -1;
    bool verified = false;
    QString file;
    int line = 0;
    int column = 0;
    QString condition;
    QString hitCondition;
    int hitCount = 0;
    bool enabled = true;
    QString message;
};

/**
 * @brief Thread information
 */
struct ThreadInfo {
    int id = 0;
    QString name;
};

/**
 * @brief Debug session state
 */
enum class DebugState {
    Idle,
    Initializing,
    Running,
    Stopped,
    Paused,
    Stepping,
    Terminated,
    Error
};

/**
 * @brief Manages Debug Adapters for 20+ debuggers via DAP
 */
class DebugAdapterManager : public QObject {
    Q_OBJECT

public:
    explicit DebugAdapterManager(QObject* parent = nullptr);
    ~DebugAdapterManager();

    // === Session Lifecycle ===
    bool startDebugging(const DebugAdapterConfig& config);
    bool stopDebugging();
    bool restartDebugging();

    // === Execution Control ===
    bool pause();
    bool continue_();
    bool next();           // Step Over
    bool stepIn();         // Step Into
    bool stepOut();        // Step Out
    bool stepBack();
    bool reverseContinue();

    // === Breakpoints ===
    bool setBreakpoint(const QString& file, int line, const QString& condition = QString());
    bool setFunctionBreakpoint(const QString& functionName);
    bool setDataBreakpoint(const QString& dataId, const QString& accessType);
    bool setInstructionBreakpoint(quint64 address);
    bool removeBreakpoint(int breakpointId);
    bool removeAllBreakpoints();
    bool enableBreakpoint(int breakpointId, bool enable);
    bool updateBreakpoint(int breakpointId, const QString& condition);
    QList<Breakpoint> breakpoints() const;

    // === Stack and Variables ===
    QList<StackFrame> getStackTrace(int threadId = 0, int levels = 20);
    QList<Scope> getScopes(int frameId);
    QList<Variable> getVariables(int variablesReference);
    QList<Variable> evaluateWatch(const QString& expression, int frameId = 0);
    bool setVariable(int variablesReference, const QString& name, const QString& value);

    // === Threads ===
    QList<ThreadInfo> getThreads();

    // === State ===
    DebugState state() const;
    bool isRunning() const;
    bool isStopped() const;
    int currentThreadId() const;

    // === Configuration ===
    void registerAdapter(const QString& type, const DebugAdapterConfig& config);
    QStringList supportedAdapters() const;
    QStringList availableDebuggers() const;

    // === Source ===
    QString getSource(const QString& sourceReference);

Q_SIGNALS:
    void initialized();
    void stopped(const QString& reason, const QString& description);
    void continued();
    void threadStarted(int threadId);
    void threadExited(int threadId);
    void outputReceived(const QString& category, const QString& output);
    void breakpointHit(const Breakpoint& bp);
    void breakpointChanged(const QList<Breakpoint>& bps);
    void capabilitiesReceived(const QJsonObject& caps);
    void terminated();
    void exited(int exitCode);
    void error(const QString& message);
    void stateChanged(DebugState newState);
    void stackTraceReceived(const QList<StackFrame>& frames);
    void variablesReceived(const QList<Variable>& vars);
    void modulesLoaded(const QJsonArray& modules);
    void loadedSources(const QJsonArray& sources);
    void progressUpdate(const QString& progressId, int percentage, const QString& message);

private:
    class Impl;
    std::unique_ptr<Impl> d;
};

} // namespace powsys365::ide::dap
