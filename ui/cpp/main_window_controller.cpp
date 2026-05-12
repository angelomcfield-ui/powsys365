#include "main_window_controller.h"
#include <QDebug>

MainWindowController::MainWindowController(QQmlApplicationEngine* engine, QObject* parent)
    : QObject(parent), m_engine(engine)
{
}

void MainWindowController::newProject()
{
    qDebug() << "New project";
    // Implement new project logic
}

void MainWindowController::openProject()
{
    qDebug() << "Open project";
    // Implement open project logic
}

void MainWindowController::saveProject()
{
    qDebug() << "Save project";
    // Implement save project logic
}

void MainWindowController::runLoadFlow()
{
    qDebug() << "Run load flow";
    // Implement load flow execution
}

void MainWindowController::runShortCircuit()
{
    qDebug() << "Run short circuit";
    // Implement short circuit analysis
}

void MainWindowController::runStabilityAnalysis()
{
    qDebug() << "Run stability analysis";
    // Implement stability analysis
}