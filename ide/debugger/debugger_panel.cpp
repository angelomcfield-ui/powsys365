#include "debugger_panel.h"
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
#include <QPlainTextEdit>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QFileInfo>
#include <QDebug>
#include <QTreeWidgetItem>
#include <QThread>

namespace powsys365::ide::debugger {

// ============================================================================
// StackTraceWidget
// ============================================================================

StackTraceWidget::StackTraceWidget(QWidget* parent)
    : QTreeWidget(parent)
{
    setColumnCount(4);
    setHeaderLabels(QStringList{tr("Function"), tr("File"), tr("Line"), tr("Module")});
    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(0, QHeaderView::Stretch);
    header()->setSectionResizeMode(1, QHeaderView::Stretch);
    header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    setRootIsDecorated(false);
    setAlternatingRowColors(true);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);

    connect(this, &QTreeWidget::itemClicked,
            this, &StackTraceWidget::onItemClicked);
}

void StackTraceWidget::setStackFrames(const QList<dap::StackFrame>& frames) {
    clearFrames();
    for (const auto& frame : frames) {
        QTreeWidgetItem* item = new QTreeWidgetItem(this);
        item->setText(0, frame.name);
        item->setText(1, QFileInfo(frame.source).fileName());
        item->setText(2, QString::number(frame.line));
        item->setText(3, frame.module);
        item->setData(0, Qt::UserRole, frame.id);
        item->setToolTip(0, frame.source);
    }
    if (topLevelItemCount() > 0) {
        setCurrentItem(topLevelItem(0));
    }
}

void StackTraceWidget::clearFrames() {
    clear();
}

int StackTraceWidget::selectedFrameId() const {
    QTreeWidgetItem* item = currentItem();
    if (item) {
        return item->data(0, Qt::UserRole).toInt();
    }
    return 0;
}

void StackTraceWidget::selectFrame(int frameId) {
    for (int i = 0; i < topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = topLevelItem(i);
        if (item->data(0, Qt::UserRole).toInt() == frameId) {
            setCurrentItem(item);
            return;
        }
    }
}

void StackTraceWidget::onItemClicked(QTreeWidgetItem* item, int /*column*/) {
    if (item) {
        int frameId = item->data(0, Qt::UserRole).toInt();
        Q_EMIT frameSelected(frameId);
    }
}

void StackTraceWidget::contextMenuEvent(QContextMenuEvent* event) {
    QTreeWidgetItem* item = itemAt(event->pos());
    if (!item) return;

    QMenu menu(this);
    QAction* gotoAct = menu.addAction(tr("Go to Source"));
    QAction* copyAct = menu.addAction(tr("Copy Frame Info"));
    menu.addSeparator();
    QAction* runToCursorAct = menu.addAction(tr("Run to Cursor (Set Temp Breakpoint)"));

    QAction* selected = menu.exec(event->globalPos());
    if (selected == gotoAct) {
        int frameId = item->data(0, Qt::UserRole).toInt();
        Q_EMIT frameSelected(frameId);
    } else if (selected == copyAct) {
        QString info = QString("%1 at %2:%3")
            .arg(item->text(0), item->text(1), item->text(2));
        QApplication::clipboard()->setText(info);
    } else if (selected == runToCursorAct) {
        // Set temporary breakpoint at this frame's location
        Q_EMIT frameSelected(item->data(0, Qt::UserRole).toInt());
    }
}

// ============================================================================
// VariablesWidget
// ============================================================================

VariablesWidget::VariablesWidget(QWidget* parent)
    : QTreeWidget(parent)
{
    setColumnCount(3);
    setHeaderLabels(QStringList{tr("Name"), tr("Value"), tr("Type")});
    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    header()->setSectionResizeMode(1, QHeaderView::Stretch);
    header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    setRootIsDecorated(true);
    setAlternatingRowColors(true);

    connect(this, &QTreeWidget::itemExpanded,
            this, &VariablesWidget::onItemExpanded);
    connect(this, &QTreeWidget::itemDoubleClicked,
            this, &VariablesWidget::onItemDoubleClicked);
}

void VariablesWidget::setVariables(const QList<dap::Variable>& variables) {
    clearVariables();
    for (const auto& var : variables) {
        QTreeWidgetItem* item = new QTreeWidgetItem(this);
        item->setText(0, var.name);
        item->setText(1, var.value);
        item->setText(2, var.type);
        item->setData(0, Qt::UserRole, var.variablesReference);
        item->setChildIndicatorPolicy(var.expandable ? QTreeWidgetItem::ShowIndicator
                                                      : QTreeWidgetItem::DontShowIndicator);
        if (!var.type.isEmpty()) {
            item->setToolTip(2, var.type);
        }
    }
}

void VariablesWidget::clearVariables() {
    itemToRef.clear();
    expandedVars.clear();
    clear();
}

void VariablesWidget::expandVariable(QTreeWidgetItem* item) {
    if (!item) return;
    int varRef = item->data(0, Qt::UserRole).toInt();
    if (varRef > 0) {
        Q_EMIT variableExpanded(varRef);
    }
}

void VariablesWidget::contextMenuEvent(QContextMenuEvent* event) {
    QTreeWidgetItem* item = itemAt(event->pos());
    if (!item) return;

    QMenu menu(this);
    QAction* copyNameAct = menu.addAction(tr("Copy Name"));
    QAction* copyValueAct = menu.addAction(tr("Copy Value"));
    QAction* copyTypeAct = menu.addAction(tr("Copy Type"));
    menu.addSeparator();
    QAction* setValueAct = menu.addAction(tr("Set Value..."));
    QAction* addWatchAct = menu.addAction(tr("Add to Watches"));

    QAction* selected = menu.exec(event->globalPos());
    if (selected == copyNameAct) {
        QApplication::clipboard()->setText(item->text(0));
    } else if (selected == copyValueAct) {
        QApplication::clipboard()->setText(item->text(1));
    } else if (selected == copyTypeAct) {
        QApplication::clipboard()->setText(item->text(2));
    } else if (selected == setValueAct) {
        bool ok;
        QString newValue = QInputDialog::getText(this, tr("Set Variable Value"),
            tr("New value for %1:").arg(item->text(0)),
            QLineEdit::Normal, item->text(1), &ok);
        if (ok) {
            Q_EMIT variableSet(item->text(0), newValue);
        }
    } else if (selected == addWatchAct) {
        // Signal up to add to watches
    }
}

void VariablesWidget::onItemExpanded(QTreeWidgetItem* item) {
    int varRef = item->data(0, Qt::UserRole).toInt();
    if (varRef > 0) {
        itemToRef[item] = varRef;
        if (!expandedVars.contains(varRef)) {
            Q_EMIT variableExpanded(varRef);
        }
    }
}

void VariablesWidget::onItemDoubleClicked(QTreeWidgetItem* item, int column) {
    if (column == 1) { // Value column - edit
        bool ok;
        QString newValue = QInputDialog::getText(this, tr("Edit Value"),
            tr("New value:"), QLineEdit::Normal, item->text(1), &ok);
        if (ok) {
            Q_EMIT variableSet(item->text(0), newValue);
        }
    }
}

void VariablesWidget::addVariableChildren(QTreeWidgetItem* parent, int variablesReference) {
    expandedVars[variablesReference] = QList<dap::Variable>();
}

// ============================================================================
// WatchesWidget
// ============================================================================

WatchesWidget::WatchesWidget(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
}

void WatchesWidget::setupUI() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    // Input row
    QHBoxLayout* inputLayout = new QHBoxLayout();
    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(tr("Enter watch expression..."));
    m_addBtn = new QPushButton(tr("+"), this);
    m_addBtn->setMaximumWidth(32);
    m_removeBtn = new QPushButton(tr("-"), this);
    m_removeBtn->setMaximumWidth(32);
    m_removeBtn->setEnabled(false);

    inputLayout->addWidget(m_input, 1);
    inputLayout->addWidget(m_addBtn);
    inputLayout->addWidget(m_removeBtn);
    layout->addLayout(inputLayout);

    // Tree
    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels(QStringList{tr("Expression"), tr("Value"), tr("Type")});
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tree->setRootIsDecorated(false);
    m_tree->setAlternatingRowColors(true);
    layout->addWidget(m_tree, 1);

    connect(m_addBtn, &QPushButton::clicked, this, &WatchesWidget::onAddWatch);
    connect(m_removeBtn, &QPushButton::clicked, this, &WatchesWidget::onRemoveWatch);
    connect(m_input, &QLineEdit::returnPressed, this, &WatchesWidget::onAddWatch);
    connect(m_tree, &QTreeWidget::itemDoubleClicked,
            this, &WatchesWidget::onItemDoubleClicked);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, [this]() {
        m_removeBtn->setEnabled(!m_tree->selectedItems().isEmpty());
    });
}

void WatchesWidget::setWatches(const QList<WatchExpression>& watches) {
    m_watches = watches;
    m_tree->clear();
    for (const auto& w : watches) {
        QTreeWidgetItem* item = new QTreeWidgetItem(m_tree);
        item->setText(0, w.expression);
        item->setText(1, w.value);
        item->setText(2, w.type);
        if (!w.evaluated) {
            item->setForeground(1, QColor("#888888"));
        }
    }
}

void WatchesWidget::addWatch(const QString& expression, const QString& value,
                              const QString& type) {
    for (const auto& w : m_watches) {
        if (w.expression == expression) return;
    }
    WatchExpression we;
    we.expression = expression;
    we.value = value;
    we.type = type;
    we.evaluated = !value.isEmpty();
    m_watches.append(we);
    setWatches(m_watches);
    Q_EMIT watchAdded(expression);
}

void WatchesWidget::removeWatch(const QString& expression) {
    auto it = std::remove_if(m_watches.begin(), m_watches.end(),
        [&expression](const WatchExpression& w) { return w.expression == expression; });
    if (it != m_watches.end()) {
        m_watches.erase(it, m_watches.end());
        setWatches(m_watches);
        Q_EMIT watchRemoved(expression);
    }
}

void WatchesWidget::clearWatches() {
    m_watches.clear();
    m_tree->clear();
}

QList<WatchExpression> WatchesWidget::watches() const {
    return m_watches;
}

void WatchesWidget::onAddWatch() {
    QString expr = m_input->text().trimmed();
    if (!expr.isEmpty()) {
        addWatch(expr);
        m_input->clear();
    }
}

void WatchesWidget::onRemoveWatch() {
    QTreeWidgetItem* item = m_tree->currentItem();
    if (item) {
        removeWatch(item->text(0));
    }
}

void WatchesWidget::onEditWatch() {
    QTreeWidgetItem* item = m_tree->currentItem();
    if (!item) return;

    bool ok;
    QString newExpr = QInputDialog::getText(this, tr("Edit Watch"),
        tr("Expression:"), QLineEdit::Normal, item->text(0), &ok);
    if (ok && !newExpr.isEmpty()) {
        QString oldExpr = item->text(0);
        removeWatch(oldExpr);
        addWatch(newExpr);
        Q_EMIT watchEdited(oldExpr, newExpr);
    }
}

void WatchesWidget::onItemDoubleClicked(QTreeWidgetItem* item, int column) {
    if (column == 0) {
        onEditWatch();
    }
}

// ============================================================================
// BreakpointsWidget
// ============================================================================

BreakpointsWidget::BreakpointsWidget(QWidget* parent)
    : QTreeWidget(parent)
{
    setColumnCount(5);
    setHeaderLabels(QStringList{"", tr("File"), tr("Line"), tr("Condition"), tr("Hits")});
    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    header()->setSectionResizeMode(1, QHeaderView::Stretch);
    header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    header()->setSectionResizeMode(3, QHeaderView::Stretch);
    header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    setRootIsDecorated(false);
    setAlternatingRowColors(true);

    connect(this, &QTreeWidget::itemChanged,
            this, &BreakpointsWidget::onItemChanged);
}

void BreakpointsWidget::setBreakpoints(const QList<dap::Breakpoint>& breakpoints) {
    m_updating = true;
    clear();
    m_idToItem.clear();
    for (const auto& bp : breakpoints) {
        addBreakpoint(bp);
    }
    m_updating = false;
}

void BreakpointsWidget::addBreakpoint(const dap::Breakpoint& bp) {
    m_updating = true;
    QTreeWidgetItem* item = new QTreeWidgetItem(this);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(0, bp.enabled ? Qt::Checked : Qt::Unchecked);
    item->setText(1, QFileInfo(bp.file).fileName());
    item->setToolTip(1, bp.file);
    item->setText(2, QString::number(bp.line));
    item->setText(3, bp.condition);
    item->setText(4, QString::number(bp.hitCount));
    item->setData(0, Qt::UserRole, bp.id);

    if (!bp.verified) {
        item->setForeground(1, QColor("#FF8800"));
        item->setToolTip(0, tr("Unverified"));
    }

    m_idToItem[bp.id] = item;
    m_updating = false;
}

void BreakpointsWidget::removeBreakpoint(int breakpointId) {
    auto it = m_idToItem.find(breakpointId);
    if (it != m_idToItem.end()) {
        delete it.value();
        m_idToItem.erase(it);
    }
}

void BreakpointsWidget::updateBreakpoint(const dap::Breakpoint& bp) {
    auto it = m_idToItem.find(bp.id);
    if (it != m_idToItem.end()) {
        QTreeWidgetItem* item = it.value();
        item->setCheckState(0, bp.enabled ? Qt::Checked : Qt::Unchecked);
        item->setText(2, QString::number(bp.line));
        item->setText(3, bp.condition);
        item->setText(4, QString::number(bp.hitCount));
        if (bp.verified) {
            item->setForeground(1, QColor());
        }
    }
}

void BreakpointsWidget::clearBreakpoints() {
    m_idToItem.clear();
    clear();
}

void BreakpointsWidget::contextMenuEvent(QContextMenuEvent* event) {
    QTreeWidgetItem* item = itemAt(event->pos());

    QMenu menu(this);
    QAction* removeAct = menu.addAction(tr("Remove Breakpoint"));
    QAction* removeAllAct = menu.addAction(tr("Remove All"));
    menu.addSeparator();
    QAction* editCondAct = menu.addAction(tr("Edit Condition..."));
    QAction* toggleAct = menu.addAction(tr("Toggle Enabled"));
    menu.addSeparator();
    QAction* gotoAct = menu.addAction(tr("Go to Source"));

    if (!item) {
        removeAct->setEnabled(false);
        editCondAct->setEnabled(false);
        toggleAct->setEnabled(false);
        gotoAct->setEnabled(false);
    }

    QAction* selected = menu.exec(event->globalPos());
    if (!item) {
        if (selected == removeAllAct) {
            clearBreakpoints();
        }
        return;
    }

    int bpId = item->data(0, Qt::UserRole).toInt();

    if (selected == removeAct) {
        Q_EMIT breakpointRemoved(bpId);
    } else if (selected == removeAllAct) {
        clearBreakpoints();
        for (auto it = m_idToItem.keyBegin(); it != m_idToItem.keyEnd(); ++it) {
            Q_EMIT breakpointRemoved(*it);
        }
    } else if (selected == editCondAct) {
        Q_EMIT breakpointEdited(bpId);
    } else if (selected == toggleAct) {
        bool enabled = item->checkState(0) == Qt::Checked;
        Q_EMIT breakpointEnabled(bpId, !enabled);
    } else if (selected == gotoAct) {
        Q_EMIT breakpointSelected(bpId);
    }
}

void BreakpointsWidget::onItemChanged(QTreeWidgetItem* item, int column) {
    if (m_updating || column != 0) return;
    int bpId = item->data(0, Qt::UserRole).toInt();
    bool enabled = item->checkState(0) == Qt::Checked;
    Q_EMIT breakpointEnabled(bpId, enabled);
}

// ============================================================================
// DebuggerToolbar
// ============================================================================

DebuggerToolbar::DebuggerToolbar(QWidget* parent)
    : QToolBar(tr("Debug"), parent)
{
    setFloatable(false);
    setupActions();
}

void DebuggerToolbar::setupActions() {
    m_startAct = addAction(QIcon::fromTheme("media-playback-start"), tr("Start (F5)"),
                           this, [this]() { Q_EMIT startRequested(); });
    m_startAct->setShortcut(Qt::Key_F5);

    m_stopAct = addAction(QIcon::fromTheme("media-playback-stop"), tr("Stop (Shift+F5)"),
                          this, [this]() { Q_EMIT stopRequested(); });
    m_stopAct->setShortcut(QKeySequence("Shift+F5"));
    m_stopAct->setEnabled(false);

    m_restartAct = addAction(QIcon::fromTheme("view-refresh"), tr("Restart (Ctrl+Shift+F5)"),
                             this, [this]() { Q_EMIT restartRequested(); });
    m_restartAct->setShortcut(QKeySequence("Ctrl+Shift+F5"));
    m_restartAct->setEnabled(false);

    addSeparator();

    m_pauseAct = addAction(QIcon::fromTheme("media-playback-pause"), tr("Pause"),
                           this, [this]() { Q_EMIT pauseRequested(); });
    m_pauseAct->setEnabled(false);

    m_continueAct = addAction(QIcon::fromTheme("media-playback-start"), tr("Continue (F5)"),
                              this, [this]() { Q_EMIT continueRequested(); });
    m_continueAct->setShortcut(Qt::Key_F5);
    m_continueAct->setVisible(false);

    addSeparator();

    m_stepOverAct = addAction(QIcon::fromTheme("debug-step-over"), tr("Step Over (F10)"),
                              this, [this]() { Q_EMIT stepOverRequested(); });
    m_stepOverAct->setShortcut(Qt::Key_F10);
    m_stepOverAct->setEnabled(false);

    m_stepIntoAct = addAction(QIcon::fromTheme("debug-step-into"), tr("Step Into (F11)"),
                              this, [this]() { Q_EMIT stepIntoRequested(); });
    m_stepIntoAct->setShortcut(Qt::Key_F11);
    m_stepIntoAct->setEnabled(false);

    m_stepOutAct = addAction(QIcon::fromTheme("debug-step-out"), tr("Step Out (Shift+F11)"),
                             this, [this]() { Q_EMIT stepOutRequested(); });
    m_stepOutAct->setShortcut(QKeySequence("Shift+F11"));
    m_stepOutAct->setEnabled(false);

    m_stepBackAct = addAction(QIcon::fromTheme("debug-step-back"), tr("Step Back"),
                              this, [this]() { Q_EMIT stepBackRequested(); });
    m_stepBackAct->setEnabled(false);
}

void DebuggerToolbar::setRunningState(bool running) {
    m_startAct->setEnabled(!running);
    m_stopAct->setEnabled(running);
    m_restartAct->setEnabled(running);
    m_pauseAct->setEnabled(running);
    m_continueAct->setVisible(false);
    m_stepOverAct->setEnabled(false);
    m_stepIntoAct->setEnabled(false);
    m_stepOutAct->setEnabled(false);
    m_stepBackAct->setEnabled(false);
}

void DebuggerToolbar::setStoppedState(bool stopped) {
    m_continueAct->setVisible(stopped);
    m_continueAct->setEnabled(stopped);
    m_stepOverAct->setEnabled(stopped);
    m_stepIntoAct->setEnabled(stopped);
    m_stepOutAct->setEnabled(stopped);
    m_stepBackAct->setEnabled(stopped);
    m_pauseAct->setEnabled(!stopped);
}

// ============================================================================
// ThreadSelector
// ============================================================================

ThreadSelector::ThreadSelector(QWidget* parent)
    : QWidget(parent)
{
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 2, 4, 2);

    QLabel* label = new QLabel(tr("Thread:"), this);
    m_combo = new QComboBox(this);
    m_combo->setMinimumWidth(200);

    layout->addWidget(label);
    layout->addWidget(m_combo, 1);

    connect(m_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        int threadId = m_combo->itemData(index).toInt();
        Q_EMIT threadSelected(threadId);
    });
}

void ThreadSelector::setThreads(const QList<dap::ThreadInfo>& threads) {
    m_threads.clear();
    m_combo->clear();
    for (const auto& t : threads) {
        QString label = QString("#%1: %2").arg(t.id).arg(t.name);
        m_combo->addItem(label, t.id);
        m_threads[t.id] = t.name;
    }
}

void ThreadSelector::setCurrentThread(int threadId) {
    for (int i = 0; i < m_combo->count(); ++i) {
        if (m_combo->itemData(i).toInt() == threadId) {
            m_combo->setCurrentIndex(i);
            return;
        }
    }
}

// ============================================================================
// DebugConsole
// ============================================================================

DebugConsole::DebugConsole(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
}

void DebugConsole::setupUI() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    // Category filter
    QHBoxLayout* filterLayout = new QHBoxLayout();
    QLabel* label = new QLabel(tr("Filter:"), this);
    m_categoryFilter = new QComboBox(this);
    m_categoryFilter->addItem(tr("All"), QString());
    m_categoryFilter->addItem(tr("Console"), "console");
    m_categoryFilter->addItem(tr("Stdout"), "stdout");
    m_categoryFilter->addItem(tr("Stderr"), "stderr");
    m_categoryFilter->addItem(tr("Telemetry"), "telemetry");
    filterLayout->addWidget(label);
    filterLayout->addWidget(m_categoryFilter);
    filterLayout->addStretch();
    layout->addLayout(filterLayout);

    // Output
    m_output = new QPlainTextEdit(this);
    m_output->setReadOnly(true);
    m_output->setMaximumBlockCount(5000);
    QFont monoFont("Consolas", 9);
    if (!QFontDatabase::families().contains("Consolas")) {
        monoFont = QFont("Courier New", 9);
    }
    monoFont.setStyleHint(QFont::Monospace);
    m_output->setFont(monoFont);
    layout->addWidget(m_output, 1);

    // Input
    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(tr("Enter debugger command (e.g., 'expr x = 5')..."));
    layout->addWidget(m_input);

    connect(m_input, &QLineEdit::returnPressed, this, &DebugConsole::onCommandEntered);
}

void DebugConsole::appendOutput(const QString& category, const QString& text) {
    QString filter = m_categoryFilter->currentData().toString();
    if (!filter.isEmpty() && category != filter) return;

    QTextCursor cursor(m_output->textCursor());
    cursor.movePosition(QTextCursor::End);

    // Color code by category
    QTextCharFormat fmt;
    if (category == "stderr") {
        fmt.setForeground(QColor("#FF4444"));
    } else if (category == "stdout") {
        fmt.setForeground(QColor("#44FF44"));
    } else if (category == "telemetry") {
        fmt.setForeground(QColor("#8888FF"));
    }

    QString prefix;
    if (!category.isEmpty()) {
        prefix = QString("[%1] ").arg(category);
        fmt.setFontWeight(QFont::Bold);
    }

    cursor.insertText(prefix, fmt);
    fmt.setFontWeight(QFont::Normal);
    cursor.insertText(text, fmt);
    if (!text.endsWith('\n')) {
        cursor.insertText("\n");
    }

    m_output->setTextCursor(cursor);
    m_output->ensureCursorVisible();
}

void DebugConsole::clear() {
    m_output->clear();
}

void DebugConsole::onCommandEntered() {
    QString cmd = m_input->text().trimmed();
    if (cmd.isEmpty()) return;

    appendOutput("", QString("> %1").arg(cmd));
    executeCommand(cmd);
    m_input->clear();
}

void DebugConsole::executeCommand(const QString& command) {
    Q_UNUSED(command)
    // Commands are forwarded to the debug adapter via evaluate request
    // This is handled by the DebuggerPanel
}

// ============================================================================
// DebuggerPanel (Main)
// ============================================================================

DebuggerPanel::DebuggerPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
    setupConnections();
}

DebuggerPanel::~DebuggerPanel() = default;

void DebuggerPanel::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(2);

    // Toolbar + thread selector row
    QHBoxLayout* topLayout = new QHBoxLayout();
    m_toolbar = new DebuggerToolbar(this);
    m_threadSelector = new ThreadSelector(this);
    topLayout->addWidget(m_toolbar);
    topLayout->addWidget(m_threadSelector, 1);
    mainLayout->addLayout(topLayout);

    // Main splitter
    m_mainSplitter = new QSplitter(Qt::Horizontal, this);

    // Left: Stack trace
    m_stackTrace = new StackTraceWidget(this);
    m_mainSplitter->addWidget(m_stackTrace);

    // Right: Tabs for Variables, Watches
    m_rightTabs = new QTabWidget(this);
    m_variables = new VariablesWidget(this);
    m_watches = new WatchesWidget(this);
    m_rightTabs->addTab(m_variables, tr("Variables"));
    m_rightTabs->addTab(m_watches, tr("Watches"));
    m_mainSplitter->addWidget(m_rightTabs);

    m_mainSplitter->setStretchFactor(0, 2);
    m_mainSplitter->setStretchFactor(1, 3);
    m_mainSplitter->setSizes({400, 500});

    // Bottom tabs: Breakpoints, Console
    m_bottomTabs = new QTabWidget(this);
    m_breakpoints = new BreakpointsWidget(this);
    m_console = new DebugConsole(this);
    m_bottomTabs->addTab(m_breakpoints, tr("Breakpoints"));
    m_bottomTabs->addTab(m_console, tr("Console"));

    // Vertical splitter
    QSplitter* vSplitter = new QSplitter(Qt::Vertical, this);
    vSplitter->addWidget(m_mainSplitter);
    vSplitter->addWidget(m_bottomTabs);
    vSplitter->setStretchFactor(0, 3);
    vSplitter->setStretchFactor(1, 1);
    vSplitter->setSizes({500, 200});

    mainLayout->addWidget(vSplitter, 1);
}

void DebuggerPanel::setupConnections() {
    // Toolbar actions
    connect(m_toolbar, &DebuggerToolbar::startRequested, this, [this]() {
        if (m_adapter) {
            onDebugStarted();
        }
    });
    connect(m_toolbar, &DebuggerToolbar::stopRequested, this, [this]() {
        if (m_adapter) {
            m_adapter->stopDebugging();
            onDebugStopped();
        }
    });
    connect(m_toolbar, &DebuggerToolbar::restartRequested, this, [this]() {
        if (m_adapter) {
            m_adapter->restartDebugging();
        }
    });
    connect(m_toolbar, &DebuggerToolbar::pauseRequested, this, [this]() {
        if (m_adapter) m_adapter->pause();
    });
    connect(m_toolbar, &DebuggerToolbar::continueRequested, this, [this]() {
        if (m_adapter) {
            m_adapter->continue_();
            onDebugContinued();
        }
    });
    connect(m_toolbar, &DebuggerToolbar::stepOverRequested, this, [this]() {
        if (m_adapter) m_adapter->next();
    });
    connect(m_toolbar, &DebuggerToolbar::stepIntoRequested, this, [this]() {
        if (m_adapter) m_adapter->stepIn();
    });
    connect(m_toolbar, &DebuggerToolbar::stepOutRequested, this, [this]() {
        if (m_adapter) m_adapter->stepOut();
    });

    // Stack trace
    connect(m_stackTrace, &StackTraceWidget::frameSelected,
            this, &DebuggerPanel::onFrameSelected);

    // Variables
    connect(m_variables, &VariablesWidget::variableExpanded,
            this, [this](int varRef) {
        if (m_adapter) {
            QList<dap::Variable> vars = m_adapter->getVariables(varRef);
            // Update display with expanded variables
        }
    });
    connect(m_variables, &VariablesWidget::variableSet,
            this, [this](const QString& name, const QString& value) {
        // Set variable value through adapter
        Q_UNUSED(name)
        Q_UNUSED(value)
    });

    // Watches
    connect(m_watches, &WatchesWidget::watchAdded,
            this, &DebuggerPanel::onWatchAdded);

    // Breakpoints
    connect(m_breakpoints, &BreakpointsWidget::breakpointRemoved,
            this, [this](int bpId) {
        if (m_adapter) m_adapter->removeBreakpoint(bpId);
    });
    connect(m_breakpoints, &BreakpointsWidget::breakpointEnabled,
            this, [this](int bpId, bool enabled) {
        if (m_adapter) m_adapter->enableBreakpoint(bpId, enabled);
    });
    connect(m_breakpoints, &BreakpointsWidget::breakpointEdited,
            this, [this](int bpId) {
        bool ok;
        QString cond = QInputDialog::getText(this, tr("Edit Condition"),
            tr("Condition:"), QLineEdit::Normal, QString(), &ok);
        if (ok && m_adapter) {
            m_adapter->updateBreakpoint(bpId, cond);
        }
    });
    connect(m_breakpoints, &BreakpointsWidget::breakpointSelected,
            this, [this](int bpId) {
        auto bps = m_adapter->breakpoints();
        for (const auto& bp : bps) {
            if (bp.id == bpId) {
                Q_EMIT frameNavigationRequested(bp.file, bp.line);
                return;
            }
        }
    });

    // Thread selector
    connect(m_threadSelector, &ThreadSelector::threadSelected,
            this, [this](int threadId) {
        m_currentThreadId = threadId;
        if (m_adapter) {
            QList<dap::StackFrame> frames = m_adapter->getStackTrace(threadId);
            updateStackTrace(frames);
        }
    });
}

void DebuggerPanel::setDebugAdapter(dap::DebugAdapterManager* adapter) {
    if (m_adapter) {
        disconnect(m_adapter, nullptr, this, nullptr);
    }
    m_adapter = adapter;

    if (m_adapter) {
        connect(m_adapter, &dap::DebugAdapterManager::stopped, this,
                [this](const QString& reason, const QString&) {
            appendConsoleOutput("console", QString("Stopped: %1").arg(reason));
            onDebugPaused();
            onStackTraceReceived();
            onVariablesReceived();
        });
        connect(m_adapter, &dap::DebugAdapterManager::continued, this,
                &DebuggerPanel::onDebugContinued);
        connect(m_adapter, &dap::DebugAdapterManager::terminated, this,
                &DebuggerPanel::onDebugStopped);
        connect(m_adapter, &dap::DebugAdapterManager::outputReceived, this,
                &DebuggerPanel::appendConsoleOutput);
        connect(m_adapter, &dap::DebugAdapterManager::breakpointChanged, this,
                [this](const QList<dap::Breakpoint>& bps) {
            updateBreakpoints(bps);
        });
        connect(m_adapter, &dap::DebugAdapterManager::stackTraceReceived, this,
                &DebuggerPanel::onStackTraceReceived);
    }
}

void DebuggerPanel::updateStackTrace(const QList<dap::StackFrame>& frames) {
    m_stackTrace->setStackFrames(frames);
}

void DebuggerPanel::updateVariables(const QList<dap::Variable>& variables) {
    m_variables->setVariables(variables);
}

void DebuggerPanel::updateBreakpoints(const QList<dap::Breakpoint>& breakpoints) {
    m_breakpoints->setBreakpoints(breakpoints);
}

void DebuggerPanel::updateThreads(const QList<dap::ThreadInfo>& threads) {
    m_threadSelector->setThreads(threads);
    if (m_adapter) {
        m_threadSelector->setCurrentThread(m_adapter->currentThreadId());
    }
}

void DebuggerPanel::updateWatches() {
    refreshWatches();
}

void DebuggerPanel::addWatchExpression(const QString& expression) {
    m_watches->addWatch(expression);
}

void DebuggerPanel::removeWatchExpression(const QString& expression) {
    m_watches->removeWatch(expression);
}

void DebuggerPanel::setDebugState(dap::DebugState state) {
    m_currentState = state;
    switch (state) {
    case dap::DebugState::Running:
        m_toolbar->setRunningState(true);
        break;
    case dap::DebugState::Stopped:
    case dap::DebugState::Paused:
        m_toolbar->setStoppedState(true);
        break;
    case dap::DebugState::Idle:
    case dap::DebugState::Terminated:
    case dap::DebugState::Error:
        m_toolbar->setRunningState(false);
        break;
    default:
        break;
    }
}

void DebuggerPanel::appendConsoleOutput(const QString& category, const QString& text) {
    m_console->appendOutput(category, text);
}

bool DebuggerPanel::startSession(const dap::DebugAdapterConfig& config) {
    if (!m_adapter) return false;
    bool ok = m_adapter->startDebugging(config);
    if (ok) {
        onDebugStarted();
    }
    return ok;
}

bool DebuggerPanel::stopSession() {
    if (!m_adapter) return false;
    bool ok = m_adapter->stopDebugging();
    onDebugStopped();
    return ok;
}

void DebuggerPanel::onDebugStarted() {
    setDebugState(dap::DebugState::Running);
    appendConsoleOutput("console", tr("Debug session started"));
}

void DebuggerPanel::onDebugStopped() {
    setDebugState(dap::DebugState::Idle);
    m_stackTrace->clearFrames();
    m_variables->clearVariables();
    m_currentThreadId = 0;
    m_selectedFrameId = 0;
    appendConsoleOutput("console", tr("Debug session stopped"));
}

void DebuggerPanel::onDebugPaused() {
    setDebugState(dap::DebugState::Stopped);
    if (m_adapter) {
        m_currentThreadId = m_adapter->currentThreadId();
    }
    refreshWatches();
}

void DebuggerPanel::onDebugContinued() {
    setDebugState(dap::DebugState::Running);
}

void DebuggerPanel::onStackTraceReceived() {
    if (!m_adapter) return;
    QList<dap::StackFrame> frames = m_adapter->getStackTrace(m_currentThreadId);
    updateStackTrace(frames);

    if (!frames.isEmpty()) {
        m_selectedFrameId = frames.first().id;
        Q_EMIT frameNavigationRequested(frames.first().source, frames.first().line);

        // Update variables for current frame
        QList<dap::Scope> scopes = m_adapter->getScopes(m_selectedFrameId);
        QList<dap::Variable> allVars;
        for (const auto& scope : scopes) {
            QList<dap::Variable> vars = m_adapter->getVariables(scope.variablesReference);
            allVars.append(vars);
        }
        updateVariables(allVars);
    }
}

void DebuggerPanel::onVariablesReceived() {
    // Variables are already updated in onStackTraceReceived
}

void DebuggerPanel::onBreakpointHit() {
    appendConsoleOutput("console", tr("Breakpoint hit"));
}

void DebuggerPanel::onFrameSelected(int frameId) {
    m_selectedFrameId = frameId;
    if (m_adapter) {
        QList<dap::Scope> scopes = m_adapter->getScopes(frameId);
        QList<dap::Variable> allVars;
        for (const auto& scope : scopes) {
            QList<dap::Variable> vars = m_adapter->getVariables(scope.variablesReference);
            allVars.append(vars);
        }
        updateVariables(allVars);
    }
}

void DebuggerPanel::onWatchAdded(const QString& expression) {
    if (!m_adapter) return;
    QList<dap::Variable> result = m_adapter->evaluateWatch(expression, m_selectedFrameId);
    if (!result.isEmpty()) {
        m_watches->addWatch(expression, result.first().value, result.first().type);
    } else {
        m_watches->addWatch(expression, "<not available>", "");
    }
}

void DebuggerPanel::refreshWatches() {
    if (!m_adapter) return;
    QList<WatchExpression> currentWatches = m_watches->watches();
    for (auto& watch : currentWatches) {
        QList<dap::Variable> result = m_adapter->evaluateWatch(watch.expression, m_selectedFrameId);
        if (!result.isEmpty()) {
            watch.value = result.first().value;
            watch.type = result.first().type;
            watch.evaluated = true;
        } else {
            watch.value = "<error>";
            watch.evaluated = false;
        }
    }
    m_watches->setWatches(currentWatches);
}

void DebuggerPanel::onBreakpointContextMenu(const QPoint& pos) {
    Q_UNUSED(pos)
    // Handled by BreakpointsWidget
}

} // namespace powsys365::ide::debugger
