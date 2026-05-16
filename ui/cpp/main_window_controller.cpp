#include "main_window_controller.h"

// ── Core power system engine headers ───────────────────────────────────────────
#include "powsy365/power_system.h"
#include "powsy365/load_flow.h"
#include "powsy365/short_circuit.h"
#include "powsy365/stability.h"
#include "powsy365/opf_solver.h"
#include "powsy365/data_manager.h"
#include "powsy365/ybus_builder.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QDir>
#include <QSettings>
#include <QMetaObject>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QCoreApplication>

#include <memory>
#include <functional>

using namespace powsys365;

// ═══════════════════════════════════════════════════════════════════════════════
//  Internal Worker Class  ─  runs heavy calculations in m_workerThread
// ═══════════════════════════════════════════════════════════════════════════════

class AnalysisWorker : public QObject
{
    Q_OBJECT
public:
    explicit AnalysisWorker(QObject *parent = nullptr) : QObject(parent) {}

    // ── Load Flow ─────────────────────────────────────────────────────────────
    void runLoadFlow(QString projectName, QString method,
                     std::function<void(int, const QString&)> progressCallback,
                     std::function<bool()> isCancelled)
    {
        QElapsedTimer timer;
        timer.start();

        // 1) Build the real PowerSystem
        auto ps = std::make_unique<PowerSystem>();
        if (!loadCaseIntoSystem(projectName, ps.get())) {
            emit loadFlowFinished(false, tr("Failed to load case: %1").arg(projectName),
                                  0, 0.0, 0.0, QVariantList(), QVariantList(),
                                  QVariantList(), QVariantList());
            return;
        }

        if (isCancelled && isCancelled()) {
            emit loadFlowFinished(false, tr("Operation cancelled by user"),
                                  0, 0.0, 0.0, QVariantList(), QVariantList(),
                                  QVariantList(), QVariantList());
            return;
        }

        // 2) Prepare solver config from method string
        SolverConfig config;
        config.method = methodFromString(method);
        config.tolerance = TOLERANCE_DEFAULT;
        config.maxIterations = MAX_ITERATIONS_DEFAULT;
        config.enforceQLimits = true;
        config.flatStart = true;
        config.verbose = false;
        config.progressCallback = [this, &isCancelled](int iter, double mismatch,
                                                       const std::string& msg) {
            QString qmsg = QString::fromStdString(msg);
            int percent = qMin(95, (iter * 100) / qMax(1, MAX_ITERATIONS_DEFAULT));
            emit progressUpdated(percent, qmsg);
            if (isCancelled && isCancelled()) {
                // Progress callback can't abort; we check before and after solve()
            }
        };

        emit progressUpdated(5, tr("Building Ybus matrix..."));
        ps->buildYbus();

        emit progressUpdated(10, tr("Initializing voltages..."));
        ps->initializeVoltages();

        if (!ps->isValid()) {
            emit loadFlowFinished(false, tr("Power system is invalid (no slack or disconnected)"),
                                  0, 0.0, 0.0, QVariantList(), QVariantList(),
                                  QVariantList(), QVariantList());
            return;
        }

        if (isCancelled && isCancelled()) {
            emit loadFlowFinished(false, tr("Operation cancelled by user"),
                                  0, 0.0, 0.0, QVariantList(), QVariantList(),
                                  QVariantList(), QVariantList());
            return;
        }

        // 3) Run the real LoadFlowSolver
        emit progressUpdated(15, tr("Running %1 solver...").arg(method));
        LoadFlowSolver solver(*ps);
        PowerFlowResult result = solver.solve(config);

        double elapsedMs = static_cast<double>(timer.nsecsElapsed()) / 1e6;

        if (isCancelled && isCancelled()) {
            emit loadFlowFinished(false, tr("Operation cancelled by user"),
                                  0, 0.0, 0.0, QVariantList(), QVariantList(),
                                  QVariantList(), QVariantList());
            return;
        }

        if (!result.converged()) {
            QVariantList empty;
            emit loadFlowFinished(false,
                tr("Load flow did not converge: %1")
                    .arg(QString::fromStdString(result.message)),
                result.iterations, result.finalMismatch, elapsedMs,
                empty, empty, empty, empty);
            return;
        }

        // 4) Convert real results to QVariantList for QML
        emit progressUpdated(90, tr("Processing results..."));

        QVariantList buses = convertBusResults(result.busResults, ps->getBaseMVA());
        QVariantList lines = convertLineResults(result.lineResults, ps->getBaseMVA());
        QVariantList generators = convertGeneratorResults(ps->getGenerators(),
                                                           result.busResults,
                                                           ps->getBaseMVA());
        QVariantList loads = convertLoadResults(ps->getLoads(),
                                                 result.busResults,
                                                 ps->getBaseMVA());

        emit progressUpdated(100, tr("Complete"));
        emit loadFlowFinished(true,
            tr("Converged in %1 iterations, error = %2")
                .arg(result.iterations).arg(result.finalMismatch, 0, 'e', 2),
            result.iterations, result.finalMismatch, elapsedMs,
            buses, lines, generators, loads);
    }

    // ── Short Circuit ─────────────────────────────────────────────────────────
    void runShortCircuit(QString projectName, QString faultType,
                         std::function<void(int, const QString&)> progressCallback,
                         std::function<bool()> isCancelled)
    {
        QElapsedTimer timer;
        timer.start();

        auto ps = std::make_unique<PowerSystem>();
        if (!loadCaseIntoSystem(projectName, ps.get())) {
            emit shortCircuitFinished(false,
                tr("Failed to load case: %1").arg(projectName));
            return;
        }

        emit progressUpdated(10, tr("Building fault Ybus..."));
        ps->buildYbus();

        if (isCancelled && isCancelled()) {
            emit shortCircuitFinished(false, tr("Operation cancelled by user"));
            return;
        }

        // Parse fault type
        FaultType ftype = FaultType::ThreePhase;
        size_t faultBusId = 1;  // Default to bus 1
        if (faultType == "1PH" || faultType == "SLG")
            ftype = FaultType::SinglePhase;
        else if (faultType == "2PH" || faultType == "LL")
            ftype = FaultType::TwoPhase;
        else if (faultType == "2PH-G" || faultType == "LLG")
            ftype = FaultType::TwoPhaseG;

        emit progressUpdated(30, tr("Calculating %1 fault...").arg(faultType));

        ShortCircuitSolver scSolver(*ps);
        ShortCircuitResult result;

        try {
            switch (ftype) {
            case FaultType::ThreePhase:
                result = scSolver.calculateThreePhaseFault(faultBusId, 0.0);
                break;
            case FaultType::SinglePhase:
                result = scSolver.calculateSinglePhaseFault(faultBusId, 0.0);
                break;
            case FaultType::TwoPhase:
                result = scSolver.calculateTwoPhaseFault(faultBusId, 0.0);
                break;
            case FaultType::TwoPhaseG:
                result = scSolver.calculateTwoPhaseGroundFault(faultBusId, 0.0);
                break;
            default:
                result = scSolver.calculateThreePhaseFault(faultBusId, 0.0);
                break;
            }
        } catch (const std::exception& e) {
            emit shortCircuitFinished(false,
                tr("Short circuit calculation error: %1").arg(e.what()));
            return;
        }

        double elapsedMs = static_cast<double>(timer.nsecsElapsed()) / 1e6;
        Q_UNUSED(elapsedMs)

        emit progressUpdated(100, tr("Complete"));
        emit shortCircuitFinished(true,
            tr("%1 fault analysis completed at bus %2. Ik = %3 pu")
                .arg(faultType).arg(faultBusId).arg(result.ik_pu, 0, 'f', 4));
    }

    // ── Stability ─────────────────────────────────────────────────────────────
    void runStability(QString projectName, QString method,
                      std::function<void(int, const QString&)> progressCallback,
                      std::function<bool()> isCancelled)
    {
        QElapsedTimer timer;
        timer.start();

        auto ps = std::make_unique<PowerSystem>();
        if (!loadCaseIntoSystem(projectName, ps.get())) {
            emit stabilityFinished(false,
                tr("Failed to load case: %1").arg(projectName));
            return;
        }

        emit progressUpdated(5, tr("Building system model..."));
        ps->buildYbus();
        ps->initializeVoltages();

        if (!ps->isValid()) {
            emit stabilityFinished(false, tr("Power system is invalid"));
            return;
        }

        // Run power flow first to establish operating point
        emit progressUpdated(10, tr("Solving base case power flow..."));
        SolverConfig lfConfig;
        lfConfig.method = SolverMethod::NewtonRaphson;
        lfConfig.tolerance = TOLERANCE_DEFAULT;
        lfConfig.maxIterations = MAX_ITERATIONS_DEFAULT;
        LoadFlowSolver lfSolver(*ps);
        PowerFlowResult lfResult = lfSolver.solve(lfConfig);

        if (!lfResult.converged()) {
            emit stabilityFinished(false,
                tr("Base case power flow did not converge"));
            return;
        }

        emit progressUpdated(40, tr("Running stability analysis..."));

        StabilitySolver stabSolver(*ps);
        StabilityResult result;

        try {
            if (method == "euler" || method == "rk4") {
                // Transient stability simulation
                size_t faultBusId = 1;
                double faultClearingTime = 0.15;  // 150 ms
                double totalSimTime = 10.0;       // 10 seconds
                double timeStep = 0.01;

                emit progressUpdated(50, tr("Simulating transient response..."));

                std::vector<TransientResult> transients =
                    stabSolver.transientStability(faultBusId, faultClearingTime,
                                                   totalSimTime, timeStep);

                result.transientStable = !transients.empty() && transients.back().stable;
                result.timeSeries = std::move(transients);

                emit progressUpdated(80, tr("Computing eigenvalues..."));
                result = stabSolver.smallSignalStability();

            } else if (method == "eigenvalue" || method == "smallsignal") {
                emit progressUpdated(50, tr("Computing eigenvalues..."));
                result = stabSolver.smallSignalStability();
            } else {
                // Full analysis: both transient and small-signal
                emit progressUpdated(50, tr("Running transient simulation..."));
                size_t faultBusId = 1;
                double faultClearingTime = 0.15;
                double totalSimTime = 10.0;
                double timeStep = 0.01;

                std::vector<TransientResult> transients =
                    stabSolver.transientStability(faultBusId, faultClearingTime,
                                                   totalSimTime, timeStep);
                result.transientStable = !transients.empty() && transients.back().stable;
                result.timeSeries = std::move(transients);

                emit progressUpdated(75, tr("Computing eigenvalues..."));
                StabilityResult ssResult = stabSolver.smallSignalStability();
                result.eigenvalues = std::move(ssResult.eigenvalues);
                result.smallSignalStable = ssResult.smallSignalStable;
            }
        } catch (const std::exception& e) {
            emit stabilityFinished(false,
                tr("Stability analysis error: %1").arg(e.what()));
            return;
        }

        double elapsedMs = static_cast<double>(timer.nsecsElapsed()) / 1e6;
        Q_UNUSED(elapsedMs)

        QString msg;
        if (result.transientStable && result.smallSignalStable) {
            msg = tr("System is stable (transient + small-signal)");
        } else if (result.transientStable) {
            msg = tr("Transiently stable, but small-signal unstable");
        } else if (result.smallSignalStable) {
            msg = tr("Small-signal stable, but transiently unstable");
        } else {
            msg = tr("System is unstable in both domains");
        }

        emit progressUpdated(100, tr("Complete"));
        emit stabilityFinished(true, msg);
    }

    // ── OPF ───────────────────────────────────────────────────────────────────
    void runOPF(QString projectName, QString objective,
                std::function<void(int, const QString&)> progressCallback,
                std::function<bool()> isCancelled)
    {
        QElapsedTimer timer;
        timer.start();

        auto ps = std::make_unique<PowerSystem>();
        if (!loadCaseIntoSystem(projectName, ps.get())) {
            emit opfFinished(false,
                tr("Failed to load case: %1").arg(projectName));
            return;
        }

        emit progressUpdated(5, tr("Building system model..."));
        ps->buildYbus();
        ps->initializeVoltages();

        if (!ps->isValid()) {
            emit opfFinished(false, tr("Power system is invalid"));
            return;
        }

        ObjectiveType objType = ObjectiveType::MinCost;
        if (objective == "losses")
            objType = ObjectiveType::MinLosses;
        else if (objective == "emissions")
            objType = ObjectiveType::MinEmissions;

        emit progressUpdated(15, tr("Setting up OPF with objective: %1...").arg(objective));

        OptimalPowerFlow opf(*ps);
        OPFResult result;

        try {
            emit progressUpdated(30, tr("Solving OPF..."));
            result = opf.solveACOPF(objType, 30);
        } catch (const std::exception& e) {
            emit opfFinished(false,
                tr("OPF solver error: %1").arg(e.what()));
            return;
        }

        double elapsedMs = static_cast<double>(timer.nsecsElapsed()) / 1e6;
        Q_UNUSED(elapsedMs)

        if (!result.converged) {
            emit opfFinished(false,
                tr("OPF did not converge: %1")
                    .arg(QString::fromStdString(result.message)));
            return;
        }

        emit progressUpdated(100, tr("Complete"));
        emit opfFinished(true,
            tr("OPF converged. Total cost: %1 $/h, Losses: %2 pu")
                .arg(result.totalCost_h, 0, 'f', 2)
                .arg(result.totalLosses_pu, 0, 'e', 4));
    }

signals:
    void progressUpdated(int percent, const QString &stage);
    void loadFlowFinished(bool success, const QString &message, int iterations,
                          double error, double elapsedMs,
                          const QVariantList &buses, const QVariantList &lines,
                          const QVariantList &generators, const QVariantList &loads);
    void shortCircuitFinished(bool success, const QString &message);
    void stabilityFinished(bool success, const QString &message);
    void opfFinished(bool success, const QString &message);

private:
    // ── Helper: load case from project name ───────────────────────────────────
    static bool loadCaseIntoSystem(const QString &projectName, PowerSystem *ps)
    {
        if (!ps) return false;

        ps->clear();

        // Map common project/case names to IEEE test systems
        QString name = projectName.toLower().trimmed();

        if (name.contains("14")) {
            ps->loadIEEE14();
        } else if (name.contains("30")) {
            ps->loadIEEE30();
        } else if (name.contains("57")) {
            ps->loadIEEE57();
        } else if (name.contains("118")) {
            ps->loadIEEE118();
        } else if (name == "untitled" || name.isEmpty()) {
            // Default to IEEE 14 for untitled projects
            ps->loadIEEE14();
        } else {
            // Try to match by number in name
            bool ok;
            int num = name.toInt(&ok);
            if (ok && num == 14) ps->loadIEEE14();
            else if (ok && num == 30) ps->loadIEEE30();
            else if (ok && num == 57) ps->loadIEEE57();
            else if (ok && num == 118) ps->loadIEEE118();
            else ps->loadIEEE14();  // Default fallback
        }

        return ps->numBuses() > 0;
    }

    // ── Helper: map method string to enum ─────────────────────────────────────
    static SolverMethod methodFromString(const QString &method)
    {
        QString m = method.toLower();
        if (m == "fd" || m == "fastdecoupled" || m == "fdbx")
            return SolverMethod::FastDecoupledBX;
        if (m == "fdxb" || m == "fastdecoupled_xb")
            return SolverMethod::FastDecoupledXB;
        if (m == "gs" || m == "gaussseidel" || m == "gauss")
            return SolverMethod::GaussSeidel;
        // Default: Newton-Raphson
        return SolverMethod::NewtonRaphson;
    }

    // ── Helper: convert bus results ───────────────────────────────────────────
    static QVariantList convertBusResults(const std::vector<PowerFlowBusResult> &busResults,
                                          double baseMVA)
    {
        Q_UNUSED(baseMVA)
        QVariantList buses;
        for (const auto &br : busResults) {
            QVariantMap bus;
            bus["id"] = static_cast<int>(br.busId);
            bus["name"] = QString::fromStdString(br.busName);
            switch (br.type) {
            case BusType::Slack:  bus["type"] = "Slack";  break;
            case BusType::PV:     bus["type"] = "PV";     break;
            default:              bus["type"] = "PQ";     break;
            }
            bus["vm"] = br.vm_pu;
            bus["va"] = br.va_deg;
            bus["pGen"] = br.pg_pu * baseMVA;
            bus["qGen"] = br.qg_pu * baseMVA;
            bus["pLoad"] = br.pl_pu * baseMVA;
            bus["qLoad"] = br.ql_pu * baseMVA;
            bus["pNet"] = br.pInyected_pu * baseMVA;
            bus["qNet"] = br.qInyected_pu * baseMVA;
            bus["pMismatch"] = br.pMismatch_pu * baseMVA;
            bus["qMismatch"] = br.qMismatch_pu * baseMVA;
            bus["hasViolation"] = br.voltageViolation;
            buses.append(bus);
        }
        return buses;
    }

    // ── Helper: convert line results ──────────────────────────────────────────
    static QVariantList convertLineResults(const std::vector<PowerFlowLineResult> &lineResults,
                                           double baseMVA)
    {
        QVariantList lines;
        for (const auto &lr : lineResults) {
            QVariantMap line;
            line["id"] = static_cast<int>(lr.lineId);
            line["fromBus"] = static_cast<int>(lr.fromBus);
            line["toBus"] = static_cast<int>(lr.toBus);
            line["pFrom"] = lr.pFrom_pu * baseMVA;
            line["qFrom"] = lr.qFrom_pu * baseMVA;
            line["pTo"] = lr.pTo_pu * baseMVA;
            line["qTo"] = lr.qTo_pu * baseMVA;
            line["pFlow"] = (lr.pFrom_pu - lr.pTo_pu) * 0.5 * baseMVA;
            line["qFlow"] = (lr.qFrom_pu - lr.qTo_pu) * 0.5 * baseMVA;
            line["pLoss"] = lr.pLoss_pu * baseMVA;
            line["qLoss"] = lr.qLoss_pu * baseMVA;
            line["loading"] = lr.loading_pu * 100.0;  // percentage
            line["currentFrom"] = lr.currentFrom_pu;
            line["currentTo"] = lr.currentTo_pu;
            line["overload"] = lr.overload;
            line["status"] = "Closed";
            lines.append(line);
        }
        return lines;
    }

    // ── Helper: convert generator results ─────────────────────────────────────
    static QVariantList convertGeneratorResults(
        const std::vector<Generator> &generators,
        const std::vector<PowerFlowBusResult> &busResults,
        double baseMVA)
    {
        QVariantList gens;
        for (const auto &gen : generators) {
            QVariantMap g;
            g["id"] = static_cast<int>(gen.id);
            g["busId"] = static_cast<int>(gen.busId);
            g["name"] = QString::fromStdString(gen.name);

            // Find the bus result for this generator to get actual Pg/Qg
            double pg_actual = gen.pg_pu;
            double qg_actual = gen.qg_pu;
            for (const auto &br : busResults) {
                if (br.busId == gen.busId) {
                    pg_actual = br.pg_pu;
                    qg_actual = br.qg_pu;
                    break;
                }
            }

            g["pGen"] = pg_actual * baseMVA;
            g["qGen"] = qg_actual * baseMVA;
            g["pMax"] = gen.pgMax_pu * baseMVA;
            g["qMax"] = gen.qmax_pu * baseMVA;
            g["qMin"] = gen.qmin_pu * baseMVA;
            g["pMin"] = gen.pgMin_pu * baseMVA;
            g["vmSet"] = gen.vmSet_pu;
            g["status"] = (gen.status == 1) ? "Online" : "Offline";
            // Cost: c0 + c1*P + c2*P^2 [$/h]
            double pMW = pg_actual * baseMVA;
            g["cost"] = gen.cost_c0 + gen.cost_c1 * pMW + gen.cost_c2 * pMW * pMW;
            gens.append(g);
        }
        return gens;
    }

    // ── Helper: convert load results ──────────────────────────────────────────
    static QVariantList convertLoadResults(
        const std::vector<Load> &loads,
        const std::vector<PowerFlowBusResult> &busResults,
        double baseMVA)
    {
        QVariantList loadsList;
        for (const auto &ld : loads) {
            QVariantMap load;
            load["id"] = static_cast<int>(ld.id);
            load["busId"] = static_cast<int>(ld.busId);
            load["name"] = QString::fromStdString(ld.name);

            // Find the bus result for this load to get actual Pd/Qd
            double pl_actual = ld.pl_pu;
            double ql_actual = ld.ql_pu;
            for (const auto &br : busResults) {
                if (br.busId == ld.busId) {
                    pl_actual = br.pl_pu;
                    ql_actual = br.ql_pu;
                    break;
                }
            }

            load["pLoad"] = pl_actual * baseMVA;
            load["qLoad"] = ql_actual * baseMVA;
            load["status"] = (ld.status == 1) ? "Active" : "Inactive";
            loadsList.append(load);
        }
        return loadsList;
    }
};

#include "main_window_controller.moc"

// ═══════════════════════════════════════════════════════════════════════════════
//  MainWindowController Implementation
// ═══════════════════════════════════════════════════════════════════════════════

MainWindowController::MainWindowController(QObject *parent)
    : QObject(parent)
{
    // Load recent projects from settings
    QSettings settings("POWSYS365", "POWSYS365");
    m_recentProjects = settings.value("recentProjects").toList();

    // Pre-populate demo recent projects if empty
    if (m_recentProjects.isEmpty()) {
        m_recentProjects << "IEEE 14-Bus Test Case"
                        << "IEEE 30-Bus Test Case"
                        << "IEEE 57-Bus Test Case"
                        << "IEEE 118-Bus Test Case";
    }

    m_workerThread.setObjectName("AnalysisWorker");
    m_workerThread.start();
}

MainWindowController::~MainWindowController()
{
    m_workerThread.quit();
    m_workerThread.wait(5000);
}

void MainWindowController::setIsSolving(bool val)
{
    if (m_isSolving != val) {
        m_isSolving = val;
        emit isSolvingChanged();
    }
}

void MainWindowController::setStatusMessage(const QString &msg)
{
    if (m_statusMessage != msg) {
        m_statusMessage = msg;
        emit statusMessageChanged();
    }
}

void MainWindowController::setCurrentProject(const QString &project)
{
    if (m_currentProject != project) {
        m_currentProject = project;
        emit currentProjectChanged();
    }
}

void MainWindowController::setCurrentMethod(const QString &method)
{
    if (m_currentMethod != method) {
        m_currentMethod = method;
        emit currentMethodChanged();
    }
}

void MainWindowController::setHasResults(bool val)
{
    if (m_hasResults != val) {
        m_hasResults = val;
        emit hasResultsChanged();
    }
}

void MainWindowController::setIterationCount(int count)
{
    if (m_iterationCount != count) {
        m_iterationCount = count;
        emit iterationCountChanged();
    }
}

void MainWindowController::setConvergenceError(double err)
{
    if (!qFuzzyCompare(m_convergenceError, err)) {
        m_convergenceError = err;
        emit convergenceErrorChanged();
    }
}

void MainWindowController::setSolveTimeMs(double ms)
{
    if (!qFuzzyCompare(m_solveTimeMs, ms)) {
        m_solveTimeMs = ms;
        emit solveTimeMsChanged();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  solveLoadFlow  ─  REAL engine via worker thread
// ═══════════════════════════════════════════════════════════════════════════════

void MainWindowController::solveLoadFlow(const QString &method)
{
    if (m_isSolving) {
        emit showNotification(tr("Busy"), tr("Another analysis is currently running"), 2);
        return;
    }

    setIsSolving(true);
    setCurrentMethod(method);
    setStatusMessage(tr("Running load flow (%1)...").arg(method));
    resetResults();
    m_cancelRequested = false;

    // Create worker, move to thread, connect signals
    AnalysisWorker *worker = new AnalysisWorker();
    worker->moveToThread(&m_workerThread);

    // Connect worker signals to controller slots/UI
    connect(worker, &AnalysisWorker::progressUpdated,
            this, [this](int percent, const QString &stage) {
                emit progressChanged(percent, stage);
            }, Qt::QueuedConnection);

    connect(worker, &AnalysisWorker::loadFlowFinished,
            this, [this, worker](bool success, const QString &message, int iterations,
                                 double error, double elapsedMs,
                                 const QVariantList &buses, const QVariantList &lines,
                                 const QVariantList &generators, const QVariantList &loads) {
                // Update state
                setIterationCount(iterations);
                setConvergenceError(error);
                setSolveTimeMs(elapsedMs);
                setHasResults(success);
                setIsSolving(false);

                if (success) {
                    setStatusMessage(tr("Load flow converged in %1 iterations (%.2f ms)")
                                        .arg(iterations).arg(elapsedMs));
                    emit busDataReady(buses);
                    emit lineDataReady(lines);
                    emit generatorDataReady(generators);
                    emit loadDataReady(loads);
                    emit showNotification(tr("Load Flow Complete"),
                                          tr("Converged in %1 iterations").arg(iterations), 0);
                } else {
                    setStatusMessage(tr("Load flow failed: %1").arg(message));
                    emit showNotification(tr("Load Flow Failed"), message, 2);
                }

                emit loadFlowCompleted(success, message);
                worker->deleteLater();
            }, Qt::QueuedConnection);

    // Launch computation in worker thread
    QMetaObject::invokeMethod(worker, [this, worker, method]() {
        worker->runLoadFlow(m_currentProject, method,
            [](int, const QString&) {},  // progress via signal
            [this]() { return m_cancelRequested; });
    }, Qt::QueuedConnection);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  solveShortCircuit  ─  REAL engine via worker thread
// ═══════════════════════════════════════════════════════════════════════════════

void MainWindowController::solveShortCircuit(const QString &faultType)
{
    if (m_isSolving) {
        emit showNotification(tr("Busy"), tr("Another analysis is currently running"), 2);
        return;
    }

    setIsSolving(true);
    setStatusMessage(tr("Running short circuit analysis (%1)...").arg(faultType));
    m_cancelRequested = false;

    AnalysisWorker *worker = new AnalysisWorker();
    worker->moveToThread(&m_workerThread);

    connect(worker, &AnalysisWorker::progressUpdated,
            this, [this](int percent, const QString &stage) {
                emit progressChanged(percent, stage);
            }, Qt::QueuedConnection);

    connect(worker, &AnalysisWorker::shortCircuitFinished,
            this, [this, worker](bool success, const QString &message) {
                setIsSolving(false);

                if (success) {
                    setStatusMessage(tr("Short circuit analysis completed"));
                    emit showNotification(tr("Short Circuit Complete"), message, 0);
                } else {
                    setStatusMessage(tr("Short circuit failed: %1").arg(message));
                    emit showNotification(tr("Short Circuit Failed"), message, 2);
                }

                emit shortCircuitCompleted(success, message);
                worker->deleteLater();
            }, Qt::QueuedConnection);

    QMetaObject::invokeMethod(worker, [this, worker, faultType]() {
        worker->runShortCircuit(m_currentProject, faultType,
            [](int, const QString&) {},
            [this]() { return m_cancelRequested; });
    }, Qt::QueuedConnection);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  solveStability  ─  REAL engine via worker thread
// ═══════════════════════════════════════════════════════════════════════════════

void MainWindowController::solveStability(const QString &method)
{
    if (m_isSolving) {
        emit showNotification(tr("Busy"), tr("Another analysis is currently running"), 2);
        return;
    }

    setIsSolving(true);
    setStatusMessage(tr("Running transient stability (%1)...").arg(method));
    m_cancelRequested = false;

    AnalysisWorker *worker = new AnalysisWorker();
    worker->moveToThread(&m_workerThread);

    connect(worker, &AnalysisWorker::progressUpdated,
            this, [this](int percent, const QString &stage) {
                emit progressChanged(percent, stage);
            }, Qt::QueuedConnection);

    connect(worker, &AnalysisWorker::stabilityFinished,
            this, [this, worker](bool success, const QString &message) {
                setIsSolving(false);

                if (success) {
                    setStatusMessage(tr("Stability analysis completed"));
                    emit showNotification(tr("Stability Complete"), message, 0);
                } else {
                    setStatusMessage(tr("Stability analysis failed: %1").arg(message));
                    emit showNotification(tr("Stability Failed"), message, 2);
                }

                emit stabilityCompleted(success, message);
                worker->deleteLater();
            }, Qt::QueuedConnection);

    QMetaObject::invokeMethod(worker, [this, worker, method]() {
        worker->runStability(m_currentProject, method,
            [](int, const QString&) {},
            [this]() { return m_cancelRequested; });
    }, Qt::QueuedConnection);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  solveOPF  ─  REAL engine via worker thread
// ═══════════════════════════════════════════════════════════════════════════════

void MainWindowController::solveOPF(const QString &objective)
{
    if (m_isSolving) {
        emit showNotification(tr("Busy"), tr("Another analysis is currently running"), 2);
        return;
    }

    setIsSolving(true);
    setStatusMessage(tr("Running OPF (%1)...").arg(objective));
    m_cancelRequested = false;

    AnalysisWorker *worker = new AnalysisWorker();
    worker->moveToThread(&m_workerThread);

    connect(worker, &AnalysisWorker::progressUpdated,
            this, [this](int percent, const QString &stage) {
                emit progressChanged(percent, stage);
            }, Qt::QueuedConnection);

    connect(worker, &AnalysisWorker::opfFinished,
            this, [this, worker](bool success, const QString &message) {
                setIsSolving(false);

                if (success) {
                    setStatusMessage(tr("OPF converged"));
                    emit showNotification(tr("OPF Complete"), message, 0);
                } else {
                    setStatusMessage(tr("OPF failed: %1").arg(message));
                    emit showNotification(tr("OPF Failed"), message, 2);
                }

                emit opfCompleted(success, message);
                worker->deleteLater();
            }, Qt::QueuedConnection);

    QMetaObject::invokeMethod(worker, [this, worker, objective]() {
        worker->runOPF(m_currentProject, objective,
            [](int, const QString&) {},
            [this]() { return m_cancelRequested; });
    }, Qt::QueuedConnection);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Project Management
// ═══════════════════════════════════════════════════════════════════════════════

void MainWindowController::newProject()
{
    setCurrentProject(tr("Untitled"));
    setHasResults(false);
    resetResults();
    setStatusMessage(tr("New project created"));

    QVariantList empty;
    emit busDataReady(empty);
    emit lineDataReady(empty);
    emit generatorDataReady(empty);
    emit loadDataReady(empty);
}

// ── Forward declarations: file I/O helpers (free functions, not class methods) ─
static QStringList parseRawLine(const QString &line);
static void doParseRawFile(const QString &filePath, MainWindowController *ctrl);
static void doParseJsonFile(const QString &filePath, MainWindowController *ctrl);
static void doLoadBuiltInCase(const QString &projectName, MainWindowController *ctrl);

void MainWindowController::openProject(const QString &filePath)
{
    if (filePath.isEmpty()) return;

    QFileInfo fi(filePath);
    setCurrentProject(fi.baseName());
    addRecentProject(fi.baseName());

    // ── REAL file parsing: .raw (PSS/E) and .json formats ────────────────────
    QString ext = fi.suffix().toLower();

    if (ext == "raw") {
        // Parse PSS/E RAW format
        setStatusMessage(tr("Parsing RAW file: %1...").arg(fi.fileName()));
        doParseRawFile(filePath, this);
    } else if (ext == "json") {
        // Parse JSON format
        setStatusMessage(tr("Parsing JSON file: %1...").arg(fi.fileName()));
        doParseJsonFile(filePath, this);
    } else {
        // For IEEE named projects, load built-in test case
        setStatusMessage(tr("Loading case: %1").arg(fi.fileName()));
        doLoadBuiltInCase(fi.baseName(), this);
    }
}

// ── PSS/E RAW file parser ─────────────────────────────────────────────────────
static void doParseRawFile(const QString &filePath, MainWindowController *ctrl)
{
    QFile file(filePath);
    QFileInfo fi(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        ctrl->setStatusMessage(QObject::tr("Cannot open file: %1").arg(filePath));
        emit ctrl->showNotification(QObject::tr("Open Failed"),
                              QObject::tr("Cannot open file: %1").arg(filePath), 2);
        return;
    }

    PowerSystem ps;
    QTextStream in(&file);
    int lineNum = 0;
    int section = 0;  // 0=header, 1=buses, 2=loads, 3=gen, 4=branches, 5=trafos
    bool inSection = false;

    try {
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            lineNum++;

            if (line.isEmpty()) continue;
            if (line.startsWith("0")) {
                section++;
                inSection = false;
                continue;
            }

            if (section == 0) {
                // Header line: contains case name, base MVA
                continue;
            } else if (section == 1 && !inSection) {
                // Bus data section
                inSection = true;
                continue;
            } else if (section == 1 && inSection) {
                // Parse bus record (IEEE RAW format)
                // I, 'NAME', BASKV, IDE, GL, BL, AREA, ZONE, VM, VA, OWNER
                QStringList parts = parseRawLine(line);
                if (parts.size() >= 10) {
                    Bus bus;
                    bus.id = parts[0].toULongLong();
                    bus.name = parts[1].trimmed().mid(1, parts[1].length()-2).toStdString();
                    bus.baseVoltage_kV = parts[2].toDouble();
                    int ide = parts[3].toInt();
                    switch (ide) {
                    case 3: bus.type = BusType::Slack; break;
                    case 2: bus.type = BusType::PV;    break;
                    default: bus.type = BusType::PQ;    break;
                    }
                    bus.gsh_pu = parts[4].toDouble();
                    bus.bsh_pu = parts[5].toDouble();
                    bus.area = parts[6].toInt();
                    bus.zone = parts[7].toInt();
                    bus.vm_pu = parts[8].toDouble();
                    bus.va_deg = parts[9].toDouble();
                    ps.addBus(bus);
                }
            } else if (section == 2 && !inSection) {
                inSection = true;
                continue;
            } else if (section == 2 && inSection) {
                // Load data: I, ID, STATUS, AREA, ZONE, PL, QL, IP, IQ, YP, YQ
                QStringList parts = parseRawLine(line);
                if (parts.size() >= 7) {
                    Load load;
                    load.busId = parts[0].toULongLong();
                    load.id = load.busId * 1000 + parts[1].toULongLong();
                    load.status = parts[2].toInt();
                    load.pl_pu = parts[5].toDouble() / 100.0;  // MW to pu on 100 MVA
                    load.ql_pu = parts[6].toDouble() / 100.0;
                    ps.addLoad(load);
                }
            } else if (section == 3 && !inSection) {
                inSection = true;
                continue;
            } else if (section == 3 && inSection) {
                // Generator data: I, ID, PG, QG, QT, QB, VS, IREG, MBASE, ZR, ZX, etc.
                QStringList parts = parseRawLine(line);
                if (parts.size() >= 8) {
                    Generator gen;
                    gen.busId = parts[0].toULongLong();
                    gen.id = gen.busId * 1000 + parts[1].toULongLong();
                    gen.pg_pu = parts[2].toDouble() / 100.0;
                    gen.qg_pu = parts[3].toDouble() / 100.0;
                    gen.qmax_pu = parts[4].toDouble() / 100.0;
                    gen.qmin_pu = parts[5].toDouble() / 100.0;
                    gen.vmSet_pu = parts[6].toDouble();
                    ps.addGenerator(gen);
                }
            } else if (section == 4 && !inSection) {
                inSection = true;
                continue;
            } else if (section == 4 && inSection) {
                // Branch data: I, J, CKT, R, X, B, RATEA, RATEB, RATEC, GI, BI, GJ, BJ, ST
                QStringList parts = parseRawLine(line);
                if (parts.size() >= 14) {
                    Line lineData;
                    static size_t lineCounter = 1;
                    lineData.id = lineCounter++;
                    lineData.fromBus = parts[0].toULongLong();
                    lineData.toBus = parts[1].toULongLong();
                    lineData.r_pu = parts[3].toDouble();
                    lineData.x_pu = parts[4].toDouble();
                    lineData.bch_pu = parts[5].toDouble();
                    lineData.rateA_pu = parts[6].toDouble() / 100.0;
                    lineData.rateB_pu = parts[7].toDouble() / 100.0;
                    lineData.rateC_pu = parts[8].toDouble() / 100.0;
                    lineData.status = parts[13].toInt();
                    ps.addLine(lineData);
                }
            }
        }
    } catch (const std::exception& e) {
        ctrl->setStatusMessage(QObject::tr("Error parsing RAW file: %1").arg(e.what()));
        emit ctrl->showNotification(QObject::tr("Parse Error"),
                              QObject::tr("Error parsing RAW: %1").arg(e.what()), 2);
        return;
    }

    file.close();

    if (ps.numBuses() > 0) {
        // Emit loaded bus data for preview
        QVariantList buses;
        for (const auto &bus : ps.getBuses()) {
            QVariantMap b;
            b["id"] = static_cast<int>(bus.id);
            b["name"] = QString::fromStdString(bus.name);
            switch (bus.type) {
            case BusType::Slack: b["type"] = "Slack"; break;
            case BusType::PV:    b["type"] = "PV";    break;
            default:             b["type"] = "PQ";    break;
            }
            b["vm"] = bus.vm_pu;
            b["va"] = bus.va_deg;
            b["baseKV"] = bus.baseVoltage_kV;
            buses.append(b);
        }
        emit ctrl->busDataReady(buses);
        emit ctrl->lineDataReady(QVariantList());
        emit ctrl->generatorDataReady(QVariantList());
        emit ctrl->loadDataReady(QVariantList());

        ctrl->setStatusMessage(QObject::tr("Loaded %1 buses, %2 lines, %3 generators, %4 loads from RAW")
                            .arg(ps.numBuses())
                            .arg(ps.numLines())
                            .arg(ps.numGenerators())
                            .arg(ps.numLoads()));
        emit ctrl->showNotification(QObject::tr("Project Loaded"),
                              QObject::tr("Loaded %1 buses from %2")
                                  .arg(ps.numBuses()).arg(fi.baseName()), 0);
    } else {
        ctrl->setStatusMessage(QObject::tr("No buses found in RAW file"));
        emit ctrl->showNotification(QObject::tr("Parse Warning"), QObject::tr("No bus data found"), 1);
    }
}

// ── JSON file parser ──────────────────────────────────────────────────────────
static void doParseJsonFile(const QString &filePath, MainWindowController *ctrl)
{
    QFile file(filePath);
    QFileInfo fi(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        ctrl->setStatusMessage(QObject::tr("Cannot open JSON file: %1").arg(filePath));
        emit ctrl->showNotification(QObject::tr("Open Failed"), QObject::tr("Cannot open JSON file"), 2);
        return;
    }

    QByteArray rawData = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(rawData, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        ctrl->setStatusMessage(QObject::tr("JSON parse error: %1").arg(parseError.errorString()));
        emit ctrl->showNotification(QObject::tr("Parse Error"), parseError.errorString(), 2);
        return;
    }

    PowerSystem ps;
    QJsonObject root = doc.object();

    // Parse buses
    QJsonArray busesArr = root["buses"].toArray();
    for (const auto &bv : busesArr) {
        QJsonObject b = bv.toObject();
        Bus bus;
        bus.id = static_cast<size_t>(b["id"].toInt());
        bus.name = b["name"].toString().toStdString();
        bus.baseVoltage_kV = b["baseVoltage_kV"].toDouble();
        QString t = b["type"].toString().toLower();
        if (t == "slack") bus.type = BusType::Slack;
        else if (t == "pv") bus.type = BusType::PV;
        else bus.type = BusType::PQ;
        bus.vm_pu = b["vm_pu"].toDouble(1.0);
        bus.va_deg = b["va_deg"].toDouble(0.0);
        bus.vmin_pu = b["vmin_pu"].toDouble(0.9);
        bus.vmax_pu = b["vmax_pu"].toDouble(1.1);
        bus.area = b["area"].toInt(1);
        bus.zone = b["zone"].toInt(1);
        ps.addBus(bus);
    }

    // Parse lines
    QJsonArray linesArr = root["lines"].toArray();
    for (const auto &lv : linesArr) {
        QJsonObject l = lv.toObject();
        Line lineData;
        lineData.id = static_cast<size_t>(l["id"].toInt());
        lineData.fromBus = static_cast<size_t>(l["fromBus"].toInt());
        lineData.toBus = static_cast<size_t>(l["toBus"].toInt());
        lineData.r_pu = l["r_pu"].toDouble();
        lineData.x_pu = l["x_pu"].toDouble();
        lineData.bch_pu = l["bch_pu"].toDouble();
        lineData.rateA_pu = l["rateA_pu"].toDouble();
        lineData.rateB_pu = l["rateB_pu"].toDouble();
        lineData.rateC_pu = l["rateC_pu"].toDouble();
        lineData.status = l["status"].toInt(1);
        ps.addLine(lineData);
    }

    // Parse generators
    QJsonArray gensArr = root["generators"].toArray();
    for (const auto &gv : gensArr) {
        QJsonObject g = gv.toObject();
        Generator gen;
        gen.id = static_cast<size_t>(g["id"].toInt());
        gen.busId = static_cast<size_t>(g["busId"].toInt());
        gen.name = g["name"].toString().toStdString();
        gen.pg_pu = g["pg_pu"].toDouble();
        gen.qg_pu = g["qg_pu"].toDouble();
        gen.qmax_pu = g["qmax_pu"].toDouble(999.0);
        gen.qmin_pu = g["qmin_pu"].toDouble(-999.0);
        gen.pgMax_pu = g["pgMax_pu"].toDouble(999.0);
        gen.pgMin_pu = g["pgMin_pu"].toDouble(0.0);
        gen.vmSet_pu = g["vmSet_pu"].toDouble(1.0);
        gen.cost_c0 = g["cost_c0"].toDouble();
        gen.cost_c1 = g["cost_c1"].toDouble();
        gen.cost_c2 = g["cost_c2"].toDouble();
        gen.status = g["status"].toInt(1);
        ps.addGenerator(gen);
    }

    // Parse loads
    QJsonArray loadsArr = root["loads"].toArray();
    for (const auto &lv : loadsArr) {
        QJsonObject ld = lv.toObject();
        Load load;
        load.id = static_cast<size_t>(ld["id"].toInt());
        load.busId = static_cast<size_t>(ld["busId"].toInt());
        load.pl_pu = ld["pl_pu"].toDouble();
        load.ql_pu = ld["ql_pu"].toDouble();
        load.status = ld["status"].toInt(1);
        ps.addLoad(load);
    }

    if (ps.numBuses() > 0) {
        QVariantList buses;
        for (const auto &bus : ps.getBuses()) {
            QVariantMap b;
            b["id"] = static_cast<int>(bus.id);
            b["name"] = QString::fromStdString(bus.name);
            switch (bus.type) {
            case BusType::Slack: b["type"] = "Slack"; break;
            case BusType::PV:    b["type"] = "PV";    break;
            default:             b["type"] = "PQ";    break;
            }
            b["vm"] = bus.vm_pu;
            b["va"] = bus.va_deg;
            b["baseKV"] = bus.baseVoltage_kV;
            buses.append(b);
        }
        emit ctrl->busDataReady(buses);
        emit ctrl->lineDataReady(QVariantList());
        emit ctrl->generatorDataReady(QVariantList());
        emit ctrl->loadDataReady(QVariantList());

        ctrl->setStatusMessage(QObject::tr("Loaded %1 buses, %2 lines, %3 generators from JSON")
                            .arg(ps.numBuses())
                            .arg(ps.numLines())
                            .arg(ps.numGenerators()));
        emit ctrl->showNotification(QObject::tr("Project Loaded"),
                              QObject::tr("Loaded %1 buses from %2")
                                  .arg(ps.numBuses()).arg(fi.baseName()), 0);
    } else {
        ctrl->setStatusMessage(QObject::tr("No buses found in JSON file"));
        emit ctrl->showNotification(QObject::tr("Parse Warning"), QObject::tr("No bus data found in JSON"), 1);
    }
}

// ── Helper: parse a RAW format line into tokens ───────────────────────────────
static QStringList parseRawLine(const QString &line)
{
    QStringList result;
    QString current;
    bool inQuotes = false;

    for (int i = 0; i < line.length(); ++i) {
        QChar c = line[i];
        if (c == '\'') {
            inQuotes = !inQuotes;
            current.append(c);
        } else if (c == ',' && !inQuotes) {
            result.append(current.trimmed());
            current.clear();
        } else {
            current.append(c);
        }
    }
    if (!current.isEmpty()) {
        result.append(current.trimmed());
    }
    return result;
}

// ── Load built-in IEEE case from project name ─────────────────────────────────
static void doLoadBuiltInCase(const QString &projectName, MainWindowController *ctrl)
{
    PowerSystem ps;
    QString name = projectName.toLower();

    if (name.contains("14"))
        ps.loadIEEE14();
    else if (name.contains("30"))
        ps.loadIEEE30();
    else if (name.contains("57"))
        ps.loadIEEE57();
    else if (name.contains("118"))
        ps.loadIEEE118();
    else
        ps.loadIEEE14();  // Default

    if (ps.numBuses() > 0) {
        QVariantList buses;
        for (const auto &bus : ps.getBuses()) {
            QVariantMap b;
            b["id"] = static_cast<int>(bus.id);
            b["name"] = QString::fromStdString(bus.name);
            switch (bus.type) {
            case BusType::Slack: b["type"] = "Slack"; break;
            case BusType::PV:    b["type"] = "PV";    break;
            default:             b["type"] = "PQ";    break;
            }
            b["vm"] = bus.vm_pu;
            b["va"] = bus.va_deg;
            b["baseKV"] = bus.baseVoltage_kV;
            buses.append(b);
        }
        emit ctrl->busDataReady(buses);
        emit ctrl->lineDataReady(QVariantList());
        emit ctrl->generatorDataReady(QVariantList());
        emit ctrl->loadDataReady(QVariantList());

        ctrl->setStatusMessage(QObject::tr("Loaded IEEE %1-bus system (%2 buses, %3 lines)")
                            .arg(ps.numBuses())
                            .arg(ps.numBuses())
                            .arg(ps.numLines()));
        emit ctrl->showNotification(QObject::tr("Case Loaded"),
                              QObject::tr("IEEE %1-bus system loaded").arg(ps.numBuses()), 0);
    }
}

void MainWindowController::saveProject(const QString &filePath)
{
    if (!filePath.isEmpty()) {
        QFileInfo fi(filePath);
        setCurrentProject(fi.baseName());
        addRecentProject(fi.baseName());
        setStatusMessage(tr("Saved: %1").arg(fi.fileName()));
    }
}

void MainWindowController::importCase(const QString &format, const QString &filePath)
{
    Q_UNUSED(format)
    Q_UNUSED(filePath)
    setStatusMessage(tr("Import not yet implemented"));
}

void MainWindowController::exportCase(const QString &format, const QString &filePath)
{
    Q_UNUSED(format)
    Q_UNUSED(filePath)
    setStatusMessage(tr("Export not yet implemented"));
}

void MainWindowController::cancelOperation()
{
    m_cancelRequested = true;
    setStatusMessage(tr("Cancelling..."));
}

void MainWindowController::requestReport(const QString &reportType)
{
    setStatusMessage(tr("Generating %1 report...").arg(reportType));
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Worker finished handler (kept for backward compat, real logic in lambdas)
// ═══════════════════════════════════════════════════════════════════════════════

void MainWindowController::onLoadFlowWorkerFinished(bool success, const QString &message,
                                                      int iterations, double error, double elapsedMs)
{
    // NOTE: This slot is retained for backward compatibility.
    // The actual result handling is done inline in the solveLoadFlow() lambda
    // connected to AnalysisWorker::loadFlowFinished signal.
    Q_UNUSED(success)
    Q_UNUSED(message)
    Q_UNUSED(iterations)
    Q_UNUSED(error)
    Q_UNUSED(elapsedMs)
}

void MainWindowController::addRecentProject(const QString &path)
{
    // Remove if already exists
    for (int i = 0; i < m_recentProjects.size(); ++i) {
        if (m_recentProjects[i].toString() == path) {
            m_recentProjects.removeAt(i);
            break;
        }
    }
    // Add to front
    m_recentProjects.prepend(path);
    // Keep max 10
    while (m_recentProjects.size() > 10) {
        m_recentProjects.removeLast();
    }

    QSettings settings("POWSYS365", "POWSYS365");
    settings.setValue("recentProjects", m_recentProjects);

    emit recentProjectsChanged();
}

void MainWindowController::resetResults()
{
    setIterationCount(0);
    setConvergenceError(0.0);
    setSolveTimeMs(0.0);
    setHasResults(false);
}
