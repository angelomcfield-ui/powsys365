#pragma once

#include <QWidget>
#include <QTreeWidget>
#include <QTableWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QSplitter>
#include <QTabWidget>
#include <QToolBar>
#include <QAction>
#include <QHeaderView>
#include <QContextMenuEvent>
#include <QMenu>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QInputDialog>
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include <memory>

#include "../dap/debug_adapter_manager.h"

namespace powsys365::ide::debugger {

/**
 * @brief Watch expression item
 */
struct WatchExpression {
    QString expression;
    QString value;
    QString type;
    bool evaluated = false;
};

/**
 * @brief Panel widget showing stack frames
 */
class StackTraceWidget : public QTreeWidget {
    Q_OBJECT
public:
    explicit StackTraceWidget(QWidget* parent = nullptr);
    void setStackFrames(const QList<dap::StackFrame>& frames);
    void clearFrames();
    int selectedFrameId() const;
    void selectFrame(int frameId);

Q_SIGNALS:
    void frameSelected(int frameId);

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    void onItemClicked(QTreeWidgetItem* item, int column);
};

/**
 * @brief Panel widget showing variables
 */
class VariablesWidget : public QTreeWidget {
    Q_OBJECT
public:
    explicit VariablesWidget(QWidget* parent = nullptr);
    void setVariables(const QList<dap::Variable>& variables);
    void clearVariables();
    void expandVariable(QTreeWidgetItem* item);

Q_SIGNALS:
    void variableExpanded(int variablesReference);
    void variableSet(const QString& name, const QString& value);

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    void onItemExpanded(QTreeWidgetItem* item);
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);

private:
    void addVariableChildren(QTreeWidgetItem* parent, int variablesReference);
    QMap<QTreeWidgetItem*, int> itemToRef;
    QMap<int, QList<dap::Variable>> expandedVars;
};

/**
 * @brief Panel widget showing watch expressions
 */
class WatchesWidget : public QWidget {
    Q_OBJECT
public:
    explicit WatchesWidget(QWidget* parent = nullptr);
    void setWatches(const QList<WatchExpression>& watches);
    void addWatch(const QString& expression, const QString& value = QString(),
                  const QString& type = QString());
    void removeWatch(const QString& expression);
    void clearWatches();
    QList<WatchExpression> watches() const;

Q_SIGNALS:
    void watchAdded(const QString& expression);
    void watchRemoved(const QString& expression);
    void watchEdited(const QString& oldExpr, const QString& newExpr);

private:
    void setupUI();
    void onAddWatch();
    void onRemoveWatch();
    void onEditWatch();
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);

    QTreeWidget* m_tree;
    QLineEdit* m_input;
    QPushButton* m_addBtn;
    QPushButton* m_removeBtn;
    QList<WatchExpression> m_watches;
};

/**
 * @brief Panel widget showing breakpoints
 */
class BreakpointsWidget : public QTreeWidget {
    Q_OBJECT
public:
    explicit BreakpointsWidget(QWidget* parent = nullptr);
    void setBreakpoints(const QList<dap::Breakpoint>& breakpoints);
    void addBreakpoint(const dap::Breakpoint& bp);
    void removeBreakpoint(int breakpointId);
    void updateBreakpoint(const dap::Breakpoint& bp);
    void clearBreakpoints();

Q_SIGNALS:
    void breakpointSelected(int breakpointId);
    void breakpointEnabled(int breakpointId, bool enabled);
    void breakpointRemoved(int breakpointId);
    void breakpointEdited(int breakpointId);

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    void onItemChanged(QTreeWidgetItem* item, int column);

private:
    QMap<int, QTreeWidgetItem*> m_idToItem;
    bool m_updating = false;
};

/**
 * @brief Toolbar with debugging controls
 */
class DebuggerToolbar : public QToolBar {
    Q_OBJECT
public:
    explicit DebuggerToolbar(QWidget* parent = nullptr);

Q_SIGNALS:
    void startRequested();
    void stopRequested();
    void restartRequested();
    void pauseRequested();
    void continueRequested();
    void stepOverRequested();
    void stepIntoRequested();
    void stepOutRequested();
    void stepBackRequested();

public Q_SLOTS:
    void setRunningState(bool running);
    void setStoppedState(bool stopped);

private:
    void setupActions();

    QAction* m_startAct = nullptr;
    QAction* m_stopAct = nullptr;
    QAction* m_restartAct = nullptr;
    QAction* m_pauseAct = nullptr;
    QAction* m_continueAct = nullptr;
    QAction* m_stepOverAct = nullptr;
    QAction* m_stepIntoAct = nullptr;
    QAction* m_stepOutAct = nullptr;
    QAction* m_stepBackAct = nullptr;
};

/**
 * @brief Thread selector widget
 */
class ThreadSelector : public QWidget {
    Q_OBJECT
public:
    explicit ThreadSelector(QWidget* parent = nullptr);
    void setThreads(const QList<dap::ThreadInfo>& threads);
    void setCurrentThread(int threadId);

Q_SIGNALS:
    void threadSelected(int threadId);

private:
    QComboBox* m_combo;
    QMap<int, QString> m_threads;
};

/**
 * @brief Debugger output console
 */
class DebugConsole : public QWidget {
    Q_OBJECT
public:
    explicit DebugConsole(QWidget* parent = nullptr);
    void appendOutput(const QString& category, const QString& text);
    void clear();

private:
    void setupUI();
    void onCommandEntered();
    void executeCommand(const QString& command);

    class QPlainTextEdit* m_output;
    QLineEdit* m_input;
    QComboBox* m_categoryFilter;
};

/**
 * @brief Main debugger panel integrating all debug views
 */
class DebuggerPanel : public QWidget {
    Q_OBJECT
public:
    explicit DebuggerPanel(QWidget* parent = nullptr);
    ~DebuggerPanel();

    // Set the debug adapter manager
    void setDebugAdapter(dap::DebugAdapterManager* adapter);

    // Update displays
    void updateStackTrace(const QList<dap::StackFrame>& frames);
    void updateVariables(const QList<dap::Variable>& variables);
    void updateBreakpoints(const QList<dap::Breakpoint>& breakpoints);
    void updateThreads(const QList<dap::ThreadInfo>& threads);
    void updateWatches();
    void addWatchExpression(const QString& expression);
    void removeWatchExpression(const QString& expression);

    // State
    void setDebugState(dap::DebugState state);
    void appendConsoleOutput(const QString& category, const QString& text);

    // Session management
    bool startSession(const dap::DebugAdapterConfig& config);
    bool stopSession();

Q_SIGNALS:
    void frameNavigationRequested(const QString& file, int line);
    void breakpointToggleRequested(const QString& file, int line);
    void evaluationRequested(const QString& expression);

public Q_SLOTS:
    void onDebugStarted();
    void onDebugStopped();
    void onDebugPaused();
    void onDebugContinued();
    void onStackTraceReceived();
    void onVariablesReceived();
    void onBreakpointHit();

private:
    void setupUI();
    void setupConnections();
    void onFrameSelected(int frameId);
    void onWatchAdded(const QString& expression);
    void onBreakpointContextMenu(const QPoint& pos);
    void refreshWatches();

    dap::DebugAdapterManager* m_adapter = nullptr;

    // UI Components
    DebuggerToolbar* m_toolbar = nullptr;
    ThreadSelector* m_threadSelector = nullptr;
    StackTraceWidget* m_stackTrace = nullptr;
    VariablesWidget* m_variables = nullptr;
    WatchesWidget* m_watches = nullptr;
    BreakpointsWidget* m_breakpoints = nullptr;
    DebugConsole* m_console = nullptr;
    QTabWidget* m_rightTabs = nullptr;
    QTabWidget* m_bottomTabs = nullptr;
    QSplitter* m_mainSplitter = nullptr;
    QSplitter* m_rightSplitter = nullptr;

    dap::DebugState m_currentState = dap::DebugState::Idle;
    QList<WatchExpression> m_watchList;
    int m_selectedFrameId = 0;
    int m_currentThreadId = 0;
};

} // namespace powsys365::ide::debugger
