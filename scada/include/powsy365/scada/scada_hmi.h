#pragma once

#include <QMainWindow>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QTimer>
#include <QDockWidget>
#include <QTableWidget>
#include <QTabWidget>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QSplitter>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsTextItem>
#include <QGraphicsPathItem>
#include <QPainterPath>
#include <memory>
#include <vector>
#include <string>
#include <chrono>
#include <map>
#include <functional>
#include <atomic>

// Forward declarations
namespace powsys365 {

class ProtocolGateway;
class AlarmManager;
class AnimationEngine;

// ---------------------------------------------------------------------------
// Data structures for electrical elements
// ---------------------------------------------------------------------------
struct ElectricalNode {
    std::string id;
    std::string name;
    double x = 0.0;
    double y = 0.0;
    double voltage_kv = 0.0;
    double voltage_base_kv = 138.0;
    double frequency_hz = 60.0;
    bool energized = false;
    std::string node_type; // "bus", "transformer", "generator", "load"
};

struct ElectricalBranch {
    std::string id;
    std::string name;
    std::string from_node;
    std::string to_node;
    double current_a = 0.0;
    double power_mw = 0.0;
    double power_mvar = 0.0;
    double loading_percent = 0.0;
    bool breaker_closed = true;
    bool faulted = false;
    std::string branch_type; // "line", "transformer", "breaker"
};

struct MeasurementPoint {
    std::string id;
    std::string element_id;
    std::string type; // "voltage", "current", "power", "frequency"
    double value = 0.0;
    std::string unit;
    std::chrono::system_clock::time_point timestamp;
    bool valid = false;
};

// ---------------------------------------------------------------------------
// HMI Mode
// ---------------------------------------------------------------------------
enum class HmiMode {
    VIEW,
    CONTROL,
    CONFIGURATION
};

// ---------------------------------------------------------------------------
// Graphics items for unifilar diagram
// ---------------------------------------------------------------------------
class BusGraphicsItem : public QGraphicsRectItem {
public:
    explicit BusGraphicsItem(const ElectricalNode& node, QGraphicsItem* parent = nullptr);
    void updateData(const ElectricalNode& node);
protected:
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
private:
    ElectricalNode m_node;
    QGraphicsTextItem* m_label = nullptr;
    QGraphicsTextItem* m_valueText = nullptr;
};

class LineGraphicsItem : public QGraphicsPathItem {
public:
    explicit LineGraphicsItem(const ElectricalBranch& branch, QGraphicsItem* parent = nullptr);
    void updateData(const ElectricalBranch& branch);
protected:
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
private:
    ElectricalBranch m_branch;
    QGraphicsTextItem* m_label = nullptr;
    QGraphicsTextItem* m_flowText = nullptr;
};

class BreakerGraphicsItem : public QGraphicsRectItem {
public:
    explicit BreakerGraphicsItem(const ElectricalBranch& branch, QGraphicsItem* parent = nullptr);
    void updateData(const ElectricalBranch& branch);
protected:
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
private:
    ElectricalBranch m_branch;
};

class GeneratorGraphicsItem : public QGraphicsEllipseItem {
public:
    explicit GeneratorGraphicsItem(const ElectricalNode& node, QGraphicsItem* parent = nullptr);
    void updateData(const ElectricalNode& node);
protected:
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
private:
    ElectricalNode m_node;
    QGraphicsTextItem* m_label = nullptr;
};

class LoadGraphicsItem : public QGraphicsEllipseItem {
public:
    explicit LoadGraphicsItem(const ElectricalNode& node, QGraphicsItem* parent = nullptr);
    void updateData(const ElectricalNode& node);
protected:
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
private:
    ElectricalNode m_node;
    QGraphicsTextItem* m_label = nullptr;
};

// ---------------------------------------------------------------------------
// Animation particle for power flow
// ---------------------------------------------------------------------------
class FlowParticle : public QGraphicsEllipseItem {
public:
    FlowParticle(const QPointF& start, const QPointF& end, double speed, QGraphicsItem* parent = nullptr);
    void advanceAnimation(double deltaMs);
    bool isFinished() const;
    void reset();
private:
    QPointF m_start;
    QPointF m_end;
    double m_speed = 1.0;
    double m_progress = 0.0;
};

// ---------------------------------------------------------------------------
// Main SCADA HMI class
// ---------------------------------------------------------------------------
class ScadaHmi : public QMainWindow {
    Q_OBJECT

public:
    explicit ScadaHmi(QWidget* parent = nullptr);
    ~ScadaHmi() override;

    // Initialization
    void initialize(std::shared_ptr<ProtocolGateway> gateway,
                    std::shared_ptr<AlarmManager> alarmManager);

    // Network data
    void setNodes(const std::vector<ElectricalNode>& nodes);
    void setBranches(const std::vector<ElectricalBranch>& branches);

    // Real-time updates
    void updateNodeValue(const std::string& nodeId, double voltage, double frequency, bool energized);
    void updateBranchValue(const std::string& branchId, double current, double powerMw,
                           double powerMvar, double loadingPercent, bool breakerClosed);
    void updateMeasurement(const MeasurementPoint& measurement);

    // Mode
    void setMode(HmiMode mode);
    HmiMode currentMode() const;

    // Control actions
    void sendBreakerCommand(const std::string& branchId, bool close);
    void sendTapChangerCommand(const std::string& transformerId, int tapPosition);
    void sendSetpointCommand(const std::string& generatorId, double mwSetpoint);

    // Zoom and navigation
    void zoomIn();
    void zoomOut();
    void zoomFit();
    void panToElement(const std::string& elementId);

    // Public accessors
    QGraphicsScene* diagramScene();

public slots:
    void onAnimationTick();
    void onDataRefresh();
    void onAlarmReceived(const std::string& alarmId, int severity, const std::string& message);

private slots:
    void onSceneSelectionChanged();
    void onBreakerDoubleClicked(const std::string& branchId);
    void onContextMenuRequested(const QPoint& pos);

private:
    void setupUi();
    void createMenuBar();
    void createToolBar();
    void createDockPanels();
    void buildUnifilarDiagram();
    void refreshValuesDisplay();
    void updateAnimation();
    QColor getVoltageColor(double voltage, double baseVoltage) const;
    QColor getLoadingColor(double loadingPercent) const;

    // Core members
    QGraphicsScene* m_scene = nullptr;
    QGraphicsView* m_view = nullptr;
    QTimer* m_animationTimer = nullptr;
    QTimer* m_dataRefreshTimer = nullptr;

    // Dock panels
    QDockWidget* m_alarmsDock = nullptr;
    QTableWidget* m_alarmsTable = nullptr;
    QDockWidget* m_measurementsDock = nullptr;
    QTableWidget* m_measurementsTable = nullptr;
    QDockWidget* m_detailsDock = nullptr;
    QLabel* m_detailsLabel = nullptr;

    // Mode
    std::atomic<HmiMode> m_mode{HmiMode::VIEW};

    // Data storage
    QMutex m_dataMutex;
    std::map<std::string, ElectricalNode> m_nodes;
    std::map<std::string, ElectricalBranch> m_branches;
    std::vector<MeasurementPoint> m_measurements;
    std::vector<std::unique_ptr<FlowParticle>> m_flowParticles;

    // Graphics item registry
    std::map<std::string, QGraphicsItem*> m_graphicsRegistry;

    // External services
    std::shared_ptr<ProtocolGateway> m_gateway;
    std::shared_ptr<AlarmManager> m_alarmManager;
    std::shared_ptr<AnimationEngine> m_animationEngine;

    // Frame rate control
    double m_frameRate = 30.0;
    std::chrono::steady_clock::time_point m_lastFrameTime;

    // UI controls
    QComboBox* m_modeSelector = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_zoomInBtn = nullptr;
    QPushButton* m_zoomOutBtn = nullptr;
    QPushButton* m_zoomFitBtn = nullptr;
};

} // namespace powsys365
