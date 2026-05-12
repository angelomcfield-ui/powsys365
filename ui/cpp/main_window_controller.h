#ifndef MAIN_WINDOW_CONTROLLER_H
#define MAIN_WINDOW_CONTROLLER_H

#include <QObject>
#include <QQmlApplicationEngine>

class MainWindowController : public QObject
{
    Q_OBJECT

public:
    explicit MainWindowController(QQmlApplicationEngine* engine, QObject* parent = nullptr);

public slots:
    void newProject();
    void openProject();
    void saveProject();
    void runLoadFlow();
    void runShortCircuit();
    void runStabilityAnalysis();

private:
    QQmlApplicationEngine* m_engine;
};

#endif // MAIN_WINDOW_CONTROLLER_H