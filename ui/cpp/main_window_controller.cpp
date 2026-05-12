#include "main_window_controller.h"
#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QDir>
#include <QSettings>
#include <random>

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
                        << "RTS 96 System"
                        << "Custom 118-Bus Network";
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

    // Simulate async work with progress notifications
    QElapsedTimer timer;
    timer.start();

    // Emit progress in steps via queued connection simulation
    for (int i = 0; i <= 10; ++i) {
        if (m_cancelRequested) {
            setIsSolving(false);
            setStatusMessage(tr("Cancelled"));
            emit loadFlowCompleted(false, tr("Operation cancelled by user"));
            return;
        }
        emit progressChanged(i * 10, tr("Iteration %1/10").arg(i));
    }

    // Generate realistic convergence data based on method
    std::mt19937 rng(QRandomGenerator::global()->generate());
    std::normal_distribution<double> iterDist(5.0, 2.0);
    int iterations = qMax(2, qMin(15, (int)std::round(iterDist(rng))));

    double error = 1e-6;
    double elapsedMs = timer.elapsed();

    // Generate sample bus results for QML
    QVariantList buses;
    for (int i = 1; i <= 14; ++i) {
        QVariantMap bus;
        bus["id"] = i;
        bus["name"] = QString("Bus %1").arg(i);
        bus["type"] = (i == 1) ? "Slack" : ((i % 3 == 0) ? "PV" : "PQ");
        bus["vm"] = 1.0 + (QRandomGenerator::global()->generateDouble() - 0.5) * 0.06;
        bus["va"] = (QRandomGenerator::global()->generateDouble() - 0.5) * 30.0;
        bus["pGen"] = (bus["type"].toString() != "PQ") ? 50.0 + QRandomGenerator::global()->generateDouble() * 100.0 : 0.0;
        bus["qGen"] = (bus["type"].toString() != "PQ") ? 10.0 + QRandomGenerator::global()->generateDouble() * 50.0 : 0.0;
        bus["pLoad"] = (bus["type"].toString() == "PQ" || i % 2 == 0) ? 20.0 + QRandomGenerator::global()->generateDouble() * 60.0 : 0.0;
        bus["qLoad"] = (bus["type"].toString() == "PQ" || i % 2 == 0) ? 5.0 + QRandomGenerator::global()->generateDouble() * 30.0 : 0.0;
        bus["hasViolation"] = (std::abs(bus["vm"].toDouble() - 1.0) > 0.05);
        buses.append(bus);
    }

    QVariantList lines;
    for (int i = 1; i <= 20; ++i) {
        QVariantMap line;
        line["id"] = i;
        line["fromBus"] = (i % 14) + 1;
        line["toBus"] = ((i + 1) % 14) + 1;
        line["pFlow"] = (QRandomGenerator::global()->generateDouble() - 0.5) * 80.0;
        line["qFlow"] = (QRandomGenerator::global()->generateDouble() - 0.5) * 40.0;
        line["loading"] = QRandomGenerator::global()->generateDouble() * 120.0;
        line["status"] = (QRandomGenerator::global()->generateDouble() > 0.1) ? "Closed" : "Open";
        lines.append(line);
    }

    QVariantList generators;
    for (int i = 1; i <= 5; ++i) {
        QVariantMap gen;
        gen["id"] = i;
        gen["busId"] = i * 3;
        gen["pGen"] = 50.0 + QRandomGenerator::global()->generateDouble() * 150.0;
        gen["qGen"] = 10.0 + QRandomGenerator::global()->generateDouble() * 60.0;
        gen["pMax"] = 200.0;
        gen["qMax"] = 100.0;
        gen["status"] = "Online";
        gen["cost"] = 20.0 + QRandomGenerator::global()->generateDouble() * 40.0;
        generators.append(gen);
    }

    QVariantList loads;
    for (int i = 1; i <= 10; ++i) {
        QVariantMap load;
        load["id"] = i;
        load["busId"] = i + 2;
        load["pLoad"] = 20.0 + QRandomGenerator::global()->generateDouble() * 60.0;
        load["qLoad"] = 5.0 + QRandomGenerator::global()->generateDouble() * 30.0;
        load["status"] = "Active";
        loads.append(load);
    }

    setIterationCount(iterations);
    setConvergenceError(error);
    setSolveTimeMs(elapsedMs);
    setHasResults(true);
    setIsSolving(false);
    setStatusMessage(tr("Load flow converged in %1 iterations (%.2f ms)")
                        .arg(iterations).arg(elapsedMs));

    emit busDataReady(buses);
    emit lineDataReady(lines);
    emit generatorDataReady(generators);
    emit loadDataReady(loads);
    emit loadFlowCompleted(true, tr("Converged in %1 iterations, error = %2")
                              .arg(iterations).arg(error, 0, 'e', 2));
    emit showNotification(tr("Load Flow Complete"),
                          tr("Converged in %1 iterations").arg(iterations), 0);
}

void MainWindowController::solveShortCircuit(const QString &faultType)
{
    if (m_isSolving) {
        emit showNotification(tr("Busy"), tr("Another analysis is currently running"), 2);
        return;
    }

    setIsSolving(true);
    setStatusMessage(tr("Running short circuit analysis (%1)...").arg(faultType));

    // Simulate short circuit calculation
    QElapsedTimer timer;
    timer.start();

    for (int i = 0; i <= 5; ++i) {
        emit progressChanged(i * 20, tr("Building Ybus fault..."));
    }

    double elapsedMs = timer.elapsed();

    setIsSolving(false);
    setStatusMessage(tr("Short circuit analysis completed (%.2f ms)").arg(elapsedMs));
    emit shortCircuitCompleted(true, tr("%1 fault analysis completed").arg(faultType));
    emit showNotification(tr("Short Circuit Complete"),
                          tr("Fault currents calculated for %1 fault").arg(faultType), 0);
}

void MainWindowController::solveStability(const QString &method)
{
    if (m_isSolving) {
        emit showNotification(tr("Busy"), tr("Another analysis is currently running"), 2);
        return;
    }

    setIsSolving(true);
    setStatusMessage(tr("Running transient stability (%1)...").arg(method));

    QElapsedTimer timer;
    timer.start();

    for (int i = 0; i <= 10; ++i) {
        emit progressChanged(i * 10, tr("Time step %1/100").arg(i * 10));
    }

    double elapsedMs = timer.elapsed();

    setIsSolving(false);
    setStatusMessage(tr("Stability analysis completed (%.2f ms)").arg(elapsedMs));
    emit stabilityCompleted(true, tr("Simulation completed, system is stable"));
    emit showNotification(tr("Stability Complete"), tr("System remains stable"), 0);
}

void MainWindowController::solveOPF(const QString &objective)
{
    if (m_isSolving) {
        emit showNotification(tr("Busy"), tr("Another analysis is currently running"), 2);
        return;
    }

    setIsSolving(true);
    setStatusMessage(tr("Running OPF (%1)...").arg(objective));

    QElapsedTimer timer;
    timer.start();

    for (int i = 0; i <= 10; ++i) {
        emit progressChanged(i * 10, tr("OPF iteration %1").arg(i));
    }

    double elapsedMs = timer.elapsed();

    setIsSolving(false);
    setStatusMessage(tr("OPF converged (%.2f ms)").arg(elapsedMs));
    emit opfCompleted(true, tr("OPF converged, objective: %1").arg(objective));
    emit showNotification(tr("OPF Complete"), tr("Optimal dispatch calculated"), 0);
}

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

void MainWindowController::openProject(const QString &filePath)
{
    if (filePath.isEmpty()) return;
    QFileInfo fi(filePath);
    setCurrentProject(fi.baseName());
    addRecentProject(fi.baseName());
    setStatusMessage(tr("Opened: %1").arg(fi.fileName()));

    // Load demo data
    QVariantList buses;
    for (int i = 1; i <= 14; ++i) {
        QVariantMap bus;
        bus["id"] = i;
        bus["name"] = QString("Bus %1").arg(i);
        bus["type"] = (i == 1) ? "Slack" : ((i % 3 == 0) ? "PV" : "PQ");
        bus["vm"] = 1.0;
        bus["va"] = 0.0;
        buses.append(bus);
    }
    emit busDataReady(buses);
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

void MainWindowController::onLoadFlowWorkerFinished(bool success, const QString &message,
                                                      int iterations, double error, double elapsedMs)
{
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
