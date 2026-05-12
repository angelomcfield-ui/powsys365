#include "powsy365/scada/scada_hmi.h"
#include "powsy365/scada/protocol_gateway.h"
#include "powsy365/scada/alarm_manager.h"
#include "powsy365/scada/animation_engine.h"
#include <QGraphicsSceneMouseEvent>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QInputDialog>
#include <QPainter>
#include <QWheelEvent>
#include <QScrollBar>
#include <QDateTime>
#include <cmath>

namespace powsys365 {

// ============================================================================
// BusGraphicsItem
// ============================================================================
BusGraphicsItem::BusGraphicsItem(const ElectricalNode& node, QGraphicsItem* parent)
    : QGraphicsRectItem(parent), m_node(node) {
    setRect(-40, -6, 80, 12);
    setPos(node.x, node.y);
    setAcceptHoverEvents(true);
    setFlag(QGraphicsItem::ItemIsSelectable);

    m_label = new QGraphicsTextItem(QString::fromStdString(node.name), this);
    m_label->setPos(-40, -30);

    m_valueText = new QGraphicsTextItem("--- kV", this);
    m_valueText->setPos(-40, 14);
}

void BusGraphicsItem::updateData(const ElectricalNode& node) {
    m_node = node;
    m_valueText->setPlainText(QString("%1 kV\n%2 Hz")
        .arg(node.voltage_kv, 0, 'f', 2)
        .arg(node.frequency_hz, 0, 'f', 2));
    update();
}

void BusGraphicsItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    QColor busColor = m_node.energized ? QColor(0, 200, 0) : QColor(150, 150, 150);
    if (isSelected()) {
        busColor = QColor(0, 150, 255);
    }
    painter->setPen(QPen(busColor, 3));
    painter->setBrush(busColor);
    painter->drawRect(rect());

    // Voltage indicator bar
    double vRatio = m_node.voltage_kv / m_node.voltage_base_kv;
    if (m_node.energized && vRatio > 0.1) {
        QColor vColor;
        if (vRatio >= 0.95 && vRatio <= 1.05) vColor = QColor(0, 255, 0, 180);
        else if (vRatio >= 0.90 && vRatio <= 1.10) vColor = QColor(255, 255, 0, 180);
        else vColor = QColor(255, 0, 0, 180);
        painter->setBrush(vColor);
        painter->setPen(Qt::NoPen);
        int barWidth = static_cast<int>(70 * std::min(vRatio, 1.2));
        painter->drawRect(-35, -4, barWidth, 8);
    }
}

// ============================================================================
// LineGraphicsItem
// ============================================================================
LineGraphicsItem::LineGraphicsItem(const ElectricalBranch& branch, QGraphicsItem* parent)
    : QGraphicsPathItem(parent), m_branch(branch) {
    QPainterPath path;
    // Line geometry - simplified, actual would be computed from node positions
    path.moveTo(0, 0);
    path.lineTo(100, 0);
    setPath(path);
    setAcceptHoverEvents(true);
    setFlag(QGraphicsItem::ItemIsSelectable);

    m_label = new QGraphicsTextItem(QString::fromStdString(branch.name), this);
    m_label->setPos(10, -25);

    m_flowText = new QGraphicsTextItem("--- MW", this);
    m_flowText->setPos(10, 5);
}

void LineGraphicsItem::updateData(const ElectricalBranch& branch) {
    m_branch = branch;
    m_flowText->setPlainText(QString("%1 MW / %2%")
        .arg(branch.power_mw, 0, 'f', 2)
        .arg(branch.loading_percent, 0, 'f', 1));
    update();
}

void LineGraphicsItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    QColor lineColor;
    if (!m_branch.breaker_closed) lineColor = QColor(80, 80, 80);
    else if (m_branch.faulted) lineColor = QColor(255, 0, 0);
    else if (m_branch.loading_percent > 100.0) lineColor = QColor(255, 0, 0);
    else if (m_branch.loading_percent > 80.0) lineColor = QColor(255, 200, 0);
    else lineColor = QColor(0, 200, 0);

    if (isSelected()) lineColor = QColor(0, 150, 255);

    QPen pen(lineColor, m_branch.faulted ? 4 : 2);
    if (m_branch.faulted) pen.setStyle(Qt::DashLine);
    painter->setPen(pen);
    painter->drawPath(path());
}

// ============================================================================
// BreakerGraphicsItem
// ============================================================================
BreakerGraphicsItem::BreakerGraphicsItem(const ElectricalBranch& branch, QGraphicsItem* parent)
    : QGraphicsRectItem(-10, -10, 20, 20, parent), m_branch(branch) {
    setAcceptHoverEvents(true);
    setFlag(QGraphicsItem::ItemIsSelectable);
}

void BreakerGraphicsItem::updateData(const ElectricalBranch& branch) {
    m_branch = branch;
    update();
}

void BreakerGraphicsItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    QColor breakerColor;
    if (m_branch.faulted) breakerColor = QColor(255, 0, 0);
    else if (m_branch.breaker_closed) breakerColor = QColor(0, 200, 0);
    else breakerColor = QColor(255, 100, 0);

    if (isSelected()) breakerColor = QColor(0, 150, 255);

    painter->setPen(QPen(Qt::black, 2));
    painter->setBrush(breakerColor);
    painter->drawRect(rect());

    // Draw breaker symbol (X when closed, | when open)
    painter->setPen(QPen(Qt::white, 2));
    QRectF r = rect().adjusted(4, 4, -4, -4);
    if (m_branch.breaker_closed) {
        painter->drawLine(r.topLeft(), r.bottomRight());
        painter->drawLine(r.topRight(), r.bottomLeft());
    } else {
        painter->drawLine(r.center().x(), r.top(), r.center().x(), r.bottom());
    }
}

// ============================================================================
// GeneratorGraphicsItem
// ============================================================================
GeneratorGraphicsItem::GeneratorGraphicsItem(const ElectricalNode& node, QGraphicsItem* parent)
    : QGraphicsEllipseItem(-20, -20, 40, 40, parent), m_node(node) {
    setPos(node.x, node.y);
    setAcceptHoverEvents(true);
    setFlag(QGraphicsItem::ItemIsSelectable);

    m_label = new QGraphicsTextItem(QString::fromStdString(node.name), this);
    m_label->setPos(-30, -40);
}

void GeneratorGraphicsItem::updateData(const ElectricalNode& node) {
    m_node = node;
    update();
}

void GeneratorGraphicsItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    QColor genColor = m_node.energized ? QColor(0, 150, 255) : QColor(100, 100, 100);
    if (isSelected()) genColor = QColor(0, 200, 255);

    painter->setPen(QPen(Qt::black, 2));
    painter->setBrush(genColor);
    painter->drawEllipse(rect());

    // Generator symbol (G)
    painter->setPen(QPen(Qt::white, 2));
    QFont font = painter->font();
    font.setBold(true);
    font.setPointSize(10);
    painter->setFont(font);
    painter->drawText(rect(), Qt::AlignCenter, "G");
}

// ============================================================================
// LoadGraphicsItem
// ============================================================================
LoadGraphicsItem::LoadGraphicsItem(const ElectricalNode& node, QGraphicsItem* parent)
    : QGraphicsEllipseItem(-15, -15, 30, 30, parent), m_node(node) {
    setPos(node.x, node.y);
    setAcceptHoverEvents(true);
    setFlag(QGraphicsItem::ItemIsSelectable);

    m_label = new QGraphicsTextItem(QString::fromStdString(node.name), this);
    m_label->setPos(-25, -35);
}

void LoadGraphicsItem::updateData(const ElectricalNode& node) {
    m_node = node;
    update();
}

void LoadGraphicsItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    QColor loadColor = m_node.energized ? QColor(255, 150, 0) : QColor(100, 100, 100);
    if (isSelected()) loadColor = QColor(255, 200, 50);

    painter->setPen(QPen(Qt::black, 2));
    painter->setBrush(loadColor);
    painter->drawEllipse(rect());

    // Load symbol (arrow)
    painter->setPen(QPen(Qt::white, 2));
    QRectF r = rect().adjusted(5, 5, -5, -5);
    painter->drawLine(r.center().x(), r.top() + 3, r.center().x(), r.bottom() - 3);
    painter->drawLine(r.center().x() - 4, r.bottom() - 7, r.center().x(), r.bottom() - 3);
    painter->drawLine(r.center().x() + 4, r.bottom() - 7, r.center().x(), r.bottom() - 3);
}

// ============================================================================
// FlowParticle
// ============================================================================
FlowParticle::FlowParticle(const QPointF& start, const QPointF& end, double speed, QGraphicsItem* parent)
    : QGraphicsEllipseItem(-3, -3, 6, 6, parent), m_start(start), m_end(end), m_speed(speed) {
    setPos(start);
    setBrush(QBrush(QColor(255, 255, 0, 200)));
    setPen(QPen(Qt::NoPen));
}

void FlowParticle::advanceAnimation(double deltaMs) {
    m_progress += (m_speed * deltaMs / 1000.0);
    if (m_progress > 1.0) m_progress = 1.0;

    QPointF newPos = m_start + (m_end - m_start) * m_progress;
    setPos(newPos);
}

bool FlowParticle::isFinished() const {
    return m_progress >= 1.0;
}

void FlowParticle::reset() {
    m_progress = 0.0;
    setPos(m_start);
}

// ============================================================================
// ScadaHmi
// ============================================================================
ScadaHmi::ScadaHmi(QWidget* parent) : QMainWindow(parent) {
    setupUi();
    createMenuBar();
    createToolBar();
    createDockPanels();

    m_animationTimer = new QTimer(this);
    connect(m_animationTimer, &QTimer::timeout, this, &ScadaHmi::onAnimationTick);
    m_animationTimer->setInterval(static_cast<int>(1000.0 / m_frameRate));

    m_dataRefreshTimer = new QTimer(this);
    connect(m_dataRefreshTimer, &QTimer::timeout, this, &ScadaHmi::onDataRefresh);
    m_dataRefreshTimer->setInterval(500); // 2 Hz data refresh

    m_lastFrameTime = std::chrono::steady_clock::now();
}

ScadaHmi::~ScadaHmi() = default;

void ScadaHmi::initialize(std::shared_ptr<ProtocolGateway> gateway,
                          std::shared_ptr<AlarmManager> alarmManager) {
    m_gateway = gateway;
    m_alarmManager = alarmManager;

    if (m_gateway && m_gateway->isRunning()) {
        m_gateway->setOnMessageReceived([this](const ProtocolMessage& msg) {
            // Process incoming protocol data
            Q_UNUSED(msg)
        });
    }
}

void ScadaHmi::setupUi() {
    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* layout = new QVBoxLayout(central);

    m_scene = new QGraphicsScene(this);
    m_scene->setSceneRect(-500, -400, 2000, 1200);

    m_view = new QGraphicsView(m_scene, this);
    m_view->setRenderHint(QPainter::Antialiasing);
    m_view->setDragMode(QGraphicsView::ScrollHandDrag);
    m_view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    m_view->setResizeAnchor(QGraphicsView::AnchorViewCenter);
    m_view->setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    layout->addWidget(m_view);

    setWindowTitle("POWSYS365 SCADA HMI");
    resize(1400, 900);
}

void ScadaHmi::createMenuBar() {
    auto* fileMenu = menuBar()->addMenu("&Archivo");
    fileMenu->addAction("&Abrir Diagrama", this, [](){});
    fileMenu->addAction("&Guardar Layout", this, [](){});
    fileMenu->addSeparator();
    fileMenu->addAction("&Salir", this, &QWidget::close);

    auto* viewMenu = menuBar()->addMenu("&Vista");
    viewMenu->addAction("Zoom &In", this, &ScadaHmi::zoomIn, QKeySequence::ZoomIn);
    viewMenu->addAction("Zoom &Out", this, &ScadaHmi::zoomOut, QKeySequence::ZoomOut);
    viewMenu->addAction("Zoom &Fit", this, &ScadaHmi::zoomFit);

    auto* controlMenu = menuBar()->addMenu("&Control");
    controlMenu->addAction("&Reconectar Protocolos", this, [this](){
        if (m_gateway) m_gateway->start();
    });

    auto* modeMenu = menuBar()->addMenu("&Modo");
}

void ScadaHmi::createToolBar() {
    auto* toolBar = addToolBar("Principal");

    m_modeSelector = new QComboBox(this);
    m_modeSelector->addItem("Vista", static_cast<int>(HmiMode::VIEW));
    m_modeSelector->addItem("Control", static_cast<int>(HmiMode::CONTROL));
    m_modeSelector->addItem("Configuracion", static_cast<int>(HmiMode::CONFIGURATION));
    connect(m_modeSelector, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        setMode(static_cast<HmiMode>(m_modeSelector->itemData(index).toInt()));
    });
    toolBar->addWidget(new QLabel("Modo: "));
    toolBar->addWidget(m_modeSelector);
    toolBar->addSeparator();

    m_zoomInBtn = new QPushButton("+", this);
    m_zoomOutBtn = new QPushButton("-", this);
    m_zoomFitBtn = new QPushButton("Fit", this);
    connect(m_zoomInBtn, &QPushButton::clicked, this, &ScadaHmi::zoomIn);
    connect(m_zoomOutBtn, &QPushButton::clicked, this, &ScadaHmi::zoomOut);
    connect(m_zoomFitBtn, &QPushButton::clicked, this, &ScadaHmi::zoomFit);
    toolBar->addWidget(m_zoomInBtn);
    toolBar->addWidget(m_zoomOutBtn);
    toolBar->addWidget(m_zoomFitBtn);
    toolBar->addSeparator();

    m_statusLabel = new QLabel("Desconectado", this);
    toolBar->addWidget(m_statusLabel);
}

void ScadaHmi::createDockPanels() {
    // Alarms panel
    m_alarmsDock = new QDockWidget("Alarmas Activas", this);
    m_alarmsTable = new QTableWidget(0, 6, this);
    m_alarmsTable->setHorizontalHeaderLabels(
        QStringList() << "Hora" << "Severidad" << "Equipo" << "Descripcion" << "Valor" << "Estado");
    m_alarmsDock->setWidget(m_alarmsTable);
    addDockWidget(Qt::RightDockWidgetArea, m_alarmsDock);

    // Measurements panel
    m_measurementsDock = new QDockWidget("Mediciones", this);
    m_measurementsTable = new QTableWidget(0, 5, this);
    m_measurementsTable->setHorizontalHeaderLabels(
        QStringList() << "Punto" << "Valor" << "Unidad" << "Estado" << "Timestamp");
    m_measurementsDock->setWidget(m_measurementsTable);
    addDockWidget(Qt::RightDockWidgetArea, m_measurementsDock);

    // Details panel
    m_detailsDock = new QDockWidget("Detalles", this);
    m_detailsLabel = new QLabel("Seleccione un elemento del diagrama", this);
    m_detailsDock->setWidget(m_detailsLabel);
    addDockWidget(Qt::BottomDockWidgetArea, m_detailsDock);

    connect(m_scene, &QGraphicsScene::selectionChanged,
            this, &ScadaHmi::onSceneSelectionChanged);
}

void ScadaHmi::setMode(HmiMode mode) {
    m_mode.store(mode);
    QString modeStr;
    switch (mode) {
        case HmiMode::VIEW: modeStr = "MODO VISTA"; break;
        case HmiMode::CONTROL: modeStr = "MODO CONTROL"; break;
        case HmiMode::CONFIGURATION: modeStr = "MODO CONFIGURACION"; break;
    }
    m_statusLabel->setText(modeStr);
}

HmiMode ScadaHmi::currentMode() const {
    return m_mode.load();
}

void ScadaHmi::setNodes(const std::vector<ElectricalNode>& nodes) {
    QMutexLocker lock(&m_dataMutex);
    m_nodes.clear();
    for (const auto& node : nodes) {
        m_nodes[node.id] = node;
    }
}

void ScadaHmi::setBranches(const std::vector<ElectricalBranch>& branches) {
    QMutexLocker lock(&m_dataMutex);
    m_branches.clear();
    for (const auto& branch : branches) {
        m_branches[branch.id] = branch;
    }
}

void ScadaHmi::updateNodeValue(const std::string& nodeId, double voltage, double frequency, bool energized) {
    QMutexLocker lock(&m_dataMutex);
    auto it = m_nodes.find(nodeId);
    if (it != m_nodes.end()) {
        it->second.voltage_kv = voltage;
        it->second.frequency_hz = frequency;
        it->second.energized = energized;
    }
}

void ScadaHmi::updateBranchValue(const std::string& branchId, double current, double powerMw,
                                   double powerMvar, double loadingPercent, bool breakerClosed) {
    QMutexLocker lock(&m_dataMutex);
    auto it = m_branches.find(branchId);
    if (it != m_branches.end()) {
        it->second.current_a = current;
        it->second.power_mw = powerMw;
        it->second.power_mvar = powerMvar;
        it->second.loading_percent = loadingPercent;
        it->second.breaker_closed = breakerClosed;
    }
}

void ScadaHmi::updateMeasurement(const MeasurementPoint& measurement) {
    QMutexLocker lock(&m_dataMutex);
    auto it = std::find_if(m_measurements.begin(), m_measurements.end(),
        [&measurement](const MeasurementPoint& mp) { return mp.id == measurement.id; });
    if (it != m_measurements.end()) {
        *it = measurement;
    } else {
        m_measurements.push_back(measurement);
    }
}

void ScadaHmi::buildUnifilarDiagram() {
    m_scene->clear();
    m_graphicsRegistry.clear();

    // Build nodes
    for (const auto& [id, node] : m_nodes) {
        QGraphicsItem* item = nullptr;
        if (node.node_type == "bus") {
            item = new BusGraphicsItem(node);
        } else if (node.node_type == "generator") {
            item = new GeneratorGraphicsItem(node);
        } else if (node.node_type == "load") {
            item = new LoadGraphicsItem(node);
        }
        if (item) {
            m_scene->addItem(item);
            m_graphicsRegistry[id] = item;
        }
    }

    // Build branches
    for (const auto& [id, branch] : m_branches) {
        QGraphicsItem* item = nullptr;
        if (branch.branch_type == "breaker") {
            item = new BreakerGraphicsItem(branch);
        } else {
            item = new LineGraphicsItem(branch);
        }
        if (item) {
            m_scene->addItem(item);
            m_graphicsRegistry[id] = item;
        }
    }
}

void ScadaHmi::refreshValuesDisplay() {
    QMutexLocker lock(&m_dataMutex);

    for (const auto& [id, node] : m_nodes) {
        auto* item = dynamic_cast<BusGraphicsItem*>(m_graphicsRegistry[id]);
        if (item) {
            item->updateData(node);
            continue;
        }
        auto* genItem = dynamic_cast<GeneratorGraphicsItem*>(m_graphicsRegistry[id]);
        if (genItem) genItem->updateData(node);
    }

    for (const auto& [id, branch] : m_branches) {
        auto* item = dynamic_cast<LineGraphicsItem*>(m_graphicsRegistry[id]);
        if (item) {
            item->updateData(branch);
            continue;
        }
        auto* brItem = dynamic_cast<BreakerGraphicsItem*>(m_graphicsRegistry[id]);
        if (brItem) brItem->updateData(branch);
    }
}

void ScadaHmi::sendBreakerCommand(const std::string& branchId, bool close) {
    if (m_mode.load() != HmiMode::CONTROL) {
        QMessageBox::warning(this, "Modo Incorrecto",
            "Cambie a modo CONTROL para enviar comandos.");
        return;
    }
    if (m_gateway) {
        ProtocolMessage msg;
        msg.sourceId = "scada_hmi";
        msg.destinationId = "dnp3_master";
        msg.protocol = ProtocolType::DNP3_TCP;
        std::string cmd = close ? "CLOSE:" : "OPEN:";
        cmd += branchId;
        msg.payload.assign(cmd.begin(), cmd.end());
        m_gateway->sendMessage(msg);
    }
}

void ScadaHmi::sendTapChangerCommand(const std::string&, int) {
    if (m_mode.load() != HmiMode::CONTROL) {
        QMessageBox::warning(this, "Modo Incorrecto",
            "Cambie a modo CONTROL para enviar comandos.");
        return;
    }
    // Implementation would send Modbus write to tap changer register
}

void ScadaHmi::sendSetpointCommand(const std::string&, double) {
    if (m_mode.load() != HmiMode::CONTROL) {
        QMessageBox::warning(this, "Modo Incorrecto",
            "Cambie a modo CONTROL para enviar comandos.");
        return;
    }
    // Implementation would send appropriate protocol command
}

void ScadaHmi::zoomIn() {
    m_view->scale(1.2, 1.2);
}

void ScadaHmi::zoomOut() {
    m_view->scale(1.0 / 1.2, 1.0 / 1.2);
}

void ScadaHmi::zoomFit() {
    m_view->fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
}

void ScadaHmi::panToElement(const std::string& elementId) {
    auto it = m_graphicsRegistry.find(elementId);
    if (it != m_graphicsRegistry.end()) {
        m_view->centerOn(it->second);
    }
}

QGraphicsScene* ScadaHmi::diagramScene() {
    return m_scene;
}

void ScadaHmi::onAnimationTick() {
    auto now = std::chrono::steady_clock::now();
    double deltaMs = std::chrono::duration<double, std::milli>(now - m_lastFrameTime).count();
    m_lastFrameTime = now;

    // Update flow particles
    for (auto& particle : m_flowParticles) {
        particle->advanceAnimation(deltaMs);
        if (particle->isFinished()) {
            particle->reset();
        }
    }

    m_scene->update();
}

void ScadaHmi::onDataRefresh() {
    refreshValuesDisplay();
}

void ScadaHmi::onAlarmReceived(const std::string&, int severity, const std::string& message) {
    QString sevStr;
    QColor bgColor;
    switch (severity) {
        case 3: sevStr = "CRITICA"; bgColor = QColor(255, 0, 0); break;
        case 2: sevStr = "ADVERTENCIA"; bgColor = QColor(255, 200, 0); break;
        default: sevStr = "INFO"; bgColor = QColor(0, 200, 0); break;
    }

    int row = m_alarmsTable->rowCount();
    m_alarmsTable->insertRow(row);
    m_alarmsTable->setItem(row, 0, new QTableWidgetItem(
        QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss")));
    auto* sevItem = new QTableWidgetItem(sevStr);
    sevItem->setBackground(bgColor);
    m_alarmsTable->setItem(row, 1, sevItem);
    m_alarmsTable->setItem(row, 3, new QTableWidgetItem(QString::fromStdString(message)));
    m_alarmsTable->setItem(row, 5, new QTableWidgetItem("ACTIVA"));
}

void ScadaHmi::onSceneSelectionChanged() {
    auto items = m_scene->selectedItems();
    if (items.isEmpty()) {
        m_detailsLabel->setText("Seleccione un elemento del diagrama");
        return;
    }
    // Find which element was selected and show details
    for (const auto& [id, gfxItem] : m_graphicsRegistry) {
        if (gfxItem == items.first()) {
            QMutexLocker lock(&m_dataMutex);
            auto nodeIt = m_nodes.find(id);
            if (nodeIt != m_nodes.end()) {
                QString details = QString("<b>%1</b><br>Tipo: %2<br>V: %3 kV<br>f: %4 Hz<br>Energizado: %5")
                    .arg(QString::fromStdString(nodeIt->second.name))
                    .arg(QString::fromStdString(nodeIt->second.node_type))
                    .arg(nodeIt->second.voltage_kv, 0, 'f', 2)
                    .arg(nodeIt->second.frequency_hz, 0, 'f', 2)
                    .arg(nodeIt->second.energized ? "SI" : "NO");
                m_detailsLabel->setText(details);
                return;
            }
            auto branchIt = m_branches.find(id);
            if (branchIt != m_branches.end()) {
                QString details = QString("<b>%1</b><br>Tipo: %2<br>I: %3 A<br>P: %4 MW<br>Carga: %5%<br>Breaker: %6")
                    .arg(QString::fromStdString(branchIt->second.name))
                    .arg(QString::fromStdString(branchIt->second.branch_type))
                    .arg(branchIt->second.current_a, 0, 'f', 1)
                    .arg(branchIt->second.power_mw, 0, 'f', 2)
                    .arg(branchIt->second.loading_percent, 0, 'f', 1)
                    .arg(branchIt->second.breaker_closed ? "CERRADO" : "ABIERTO");
                m_detailsLabel->setText(details);
                return;
            }
        }
    }
}

void ScadaHmi::onBreakerDoubleClicked(const std::string& branchId) {
    if (m_mode.load() != HmiMode::CONTROL) return;

    auto it = m_branches.find(branchId);
    if (it != m_branches.end()) {
        QString action = it->second.breaker_closed ? "Abrir" : "Cerrar";
        int reply = QMessageBox::question(this, "Comando de Breaker",
            QString("%1 breaker %2?").arg(action).arg(QString::fromStdString(it->second.name)),
            QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            sendBreakerCommand(branchId, !it->second.breaker_closed);
        }
    }
}

void ScadaHmi::onContextMenuRequested(const QPoint&) {
    // Context menu implementation
}

QColor ScadaHmi::getVoltageColor(double voltage, double baseVoltage) const {
    double ratio = voltage / baseVoltage;
    if (ratio >= 0.95 && ratio <= 1.05) return QColor(0, 255, 0);
    if (ratio >= 0.90 && ratio <= 1.10) return QColor(255, 200, 0);
    return QColor(255, 0, 0);
}

QColor ScadaHmi::getLoadingColor(double loadingPercent) const {
    if (loadingPercent < 80.0) return QColor(0, 255, 0);
    if (loadingPercent < 100.0) return QColor(255, 200, 0);
    return QColor(255, 0, 0);
}

} // namespace powsys365
