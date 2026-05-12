#ifndef MAIN_WINDOW_CONTROLLER_H
#define MAIN_WINDOW_CONTROLLER_H

#include <QObject>
#include <QQmlEngine>
#include <QVariantList>
#include <QThread>

/**
 * @brief MainWindowController - Central C++ controller exposed to QML
 *
 * Bridges the QML UI with the core C++ power system engine.
 * Provides Q_PROPERTY bindings for UI state, analysis methods,
 * and signals for async completion notifications.
 */
class MainWindowController : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // ── Properties exposed to QML ──────────────────────────────────────────
    Q_PROPERTY(bool isSolving READ isSolving WRITE setIsSolving NOTIFY isSolvingChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage WRITE setStatusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QString currentProject READ currentProject WRITE setCurrentProject NOTIFY currentProjectChanged)
    Q_PROPERTY(QString currentMethod READ currentMethod WRITE setCurrentMethod NOTIFY currentMethodChanged)
    Q_PROPERTY(bool hasResults READ hasResults WRITE setHasResults NOTIFY hasResultsChanged)
    Q_PROPERTY(int iterationCount READ iterationCount WRITE setIterationCount NOTIFY iterationCountChanged)
    Q_PROPERTY(double convergenceError READ convergenceError WRITE setConvergenceError NOTIFY convergenceErrorChanged)
    Q_PROPERTY(double solveTimeMs READ solveTimeMs WRITE setSolveTimeMs NOTIFY solveTimeMsChanged)
    Q_PROPERTY(QVariantList recentProjects READ recentProjects NOTIFY recentProjectsChanged)

public:
    explicit MainWindowController(QObject *parent = nullptr);
    ~MainWindowController() override;

    // ── Q_PROPERTY getters ────────────────────────────────────────────────
    bool isSolving() const { return m_isSolving; }
    QString statusMessage() const { return m_statusMessage; }
    QString currentProject() const { return m_currentProject; }
    QString currentMethod() const { return m_currentMethod; }
    bool hasResults() const { return m_hasResults; }
    int iterationCount() const { return m_iterationCount; }
    double convergenceError() const { return m_convergenceError; }
    double solveTimeMs() const { return m_solveTimeMs; }
    QVariantList recentProjects() const { return m_recentProjects; }

    // ── Q_PROPERTY setters ────────────────────────────────────────────────
    void setIsSolving(bool val);
    void setStatusMessage(const QString &msg);
    void setCurrentProject(const QString &project);
    void setCurrentMethod(const QString &method);
    void setHasResults(bool val);
    void setIterationCount(int count);
    void setConvergenceError(double err);
    void setSolveTimeMs(double ms);

public slots:
    // ── Analysis methods (async via worker thread) ────────────────────────
    void solveLoadFlow(const QString &method = "NR");
    void solveShortCircuit(const QString &faultType = "3PH");
    void solveStability(const QString &method = "euler");
    void solveOPF(const QString &objective = "cost");

    // ── Project management ────────────────────────────────────────────────
    void newProject();
    void openProject(const QString &filePath);
    void saveProject(const QString &filePath);
    void importCase(const QString &format, const QString &filePath);
    void exportCase(const QString &format, const QString &filePath);

    // ── Utility ───────────────────────────────────────────────────────────
    void cancelOperation();
    void requestReport(const QString &reportType);

signals:
    // ── Property changed notifications ────────────────────────────────────
    void isSolvingChanged();
    void statusMessageChanged();
    void currentProjectChanged();
    void currentMethodChanged();
    void hasResultsChanged();
    void iterationCountChanged();
    void convergenceErrorChanged();
    void solveTimeMsChanged();
    void recentProjectsChanged();

    // ── Async operation completion ────────────────────────────────────────
    void loadFlowCompleted(bool success, const QString &message);
    void shortCircuitCompleted(bool success, const QString &message);
    void stabilityCompleted(bool success, const QString &message);
    void opfCompleted(bool success, const QString &message);

    // ── Progress notifications ────────────────────────────────────────────
    void progressChanged(int percent, const QString &stage);

    // ── Data ready for QML consumption ────────────────────────────────────
    void busDataReady(const QVariantList &buses);
    void lineDataReady(const QVariantList &lines);
    void generatorDataReady(const QVariantList &generators);
    void loadDataReady(const QVariantList &loads);

    // ── Notifications ─────────────────────────────────────────────────────
    void showNotification(const QString &title, const QString &message, int type);

private slots:
    void onLoadFlowWorkerFinished(bool success, const QString &message,
                                  int iterations, double error, double elapsedMs);

private:
    bool m_isSolving = false;
    QString m_statusMessage = tr("Ready");
    QString m_currentProject = tr("Untitled");
    QString m_currentMethod = "NR";
    bool m_hasResults = false;
    int m_iterationCount = 0;
    double m_convergenceError = 0.0;
    double m_solveTimeMs = 0.0;
    QVariantList m_recentProjects;

    QThread m_workerThread;
    bool m_cancelRequested = false;

    void addRecentProject(const QString &path);
    void resetResults();
};

#endif // MAIN_WINDOW_CONTROLLER_H
