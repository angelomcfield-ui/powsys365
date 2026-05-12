#include "sld_scene.h"
#include <QRandomGenerator>
#include <QtMath>
#include <QDebug>

SLDSceneController::SLDSceneController(QObject *parent)
    : QObject(parent)
{
}

SLDSceneController::~SLDSceneController() = default;

QPointF SLDSceneController::busPosition(int busId) const
{
    return m_positions.value(busId, QPointF());
}

QPointF SLDSceneController::mapToScene(double x, double y) const
{
    return QPointF(x, y);
}

QPointF SLDSceneController::mapFromScene(double x, double y) const
{
    return QPointF(x, y);
}

double SLDSceneController::zoomForFit(double viewWidth, double viewHeight) const
{
    if (!m_boundingRect.isValid() || viewWidth <= 0 || viewHeight <= 0)
        return 1.0;

    double zx = viewWidth / m_boundingRect.width();
    double zy = viewHeight / m_boundingRect.height();
    return qMin(zx, zy) * 0.9; // 90% fit with margin
}

QVariantMap SLDSceneController::busAt(int index) const
{
    if (index >= 0 && index < m_busNodes.size())
        return m_busNodes[index].toMap();
    return QVariantMap();
}

QVariantMap SLDSceneController::lineAt(int index) const
{
    if (index >= 0 && index < m_lineEdges.size())
        return m_lineEdges[index].toMap();
    return QVariantMap();
}

int SLDSceneController::busIndexAtPosition(double x, double y, double radius) const
{
    QPointF pt(x, y);
    for (int i = 0; i < m_busNodes.size(); ++i) {
        QVariantMap bus = m_busNodes[i].toMap();
        QPointF pos(bus["x"].toDouble(), bus["y"].toDouble());
        if (QLineF(pt, pos).length() < radius)
            return i;
    }
    return -1;
}

void SLDSceneController::setBusData(const QVariantList &buses)
{
    m_busNodes = buses;

    // Update positions hash
    m_positions.clear();
    for (const auto &item : buses) {
        QVariantMap bus = item.toMap();
        int id = bus["id"].toInt();
        double x = bus.contains("x") ? bus["x"].toDouble() : 0.0;
        double y = bus.contains("y") ? bus["y"].toDouble() : 0.0;
        m_positions[id] = QPointF(x, y);
    }

    emit busNodesChanged();
    emit nodeCountChanged();
    updateBoundingRect();
}

void SLDSceneController::setLineData(const QVariantList &lines)
{
    m_lineEdges = lines;
    emit lineEdgesChanged();
    emit edgeCountChanged();
}

void SLDSceneController::setGeneratorData(const QVariantList &generators)
{
    m_generatorNodes = generators;
    positionGenerators();
    emit generatorNodesChanged();
    emit nodeCountChanged();
}

void SLDSceneController::setLoadData(const QVariantList &loads)
{
    m_loadNodes = loads;
    positionLoads();
    emit loadNodesChanged();
    emit nodeCountChanged();
}

void SLDSceneController::computeAutoLayout(double width, double height)
{
    if (m_busNodes.isEmpty()) return;

    m_autoLayoutRunning = true;
    emit autoLayoutRunningChanged();

    // Identify slack bus (center), PV buses (inner), PQ buses (outer)
    QList<int> slackBuses, pvBuses, pqBuses;
    for (int i = 0; i < m_busNodes.size(); ++i) {
        QVariantMap bus = m_busNodes[i].toMap();
        QString type = bus["type"].toString();
        int id = bus["id"].toInt();
        if (type == "Slack")
            slackBuses.append(id);
        else if (type == "PV")
            pvBuses.append(id);
        else
            pqBuses.append(id);
    }

    // Use radial layout: slack center, PV inner ring, PQ outer ring
    double cx = width / 2.0;
    double cy = height / 2.0;

    // Place slack at center
    for (int id : slackBuses) {
        m_positions[id] = QPointF(cx, cy);
    }

    // Place PV buses on inner ring (30% of min dimension)
    double innerR = qMin(width, height) * 0.20;
    for (int i = 0; i < pvBuses.size(); ++i) {
        double angle = (2.0 * M_PI * i) / qMax(pvBuses.size(), 1);
        m_positions[pvBuses[i]] = QPointF(cx + innerR * cos(angle),
                                          cy + innerR * sin(angle));
    }

    // Place PQ buses on outer ring (50% of min dimension)
    double outerR = qMin(width, height) * 0.38;
    for (int i = 0; i < pqBuses.size(); ++i) {
        double angle = (2.0 * M_PI * i) / qMax(pqBuses.size(), 1) + 0.3; // offset
        m_positions[pqBuses[i]] = QPointF(cx + outerR * cos(angle),
                                          cy + outerR * sin(angle));
    }

    // Update bus nodes with positions
    QVariantList updatedBuses;
    for (const auto &item : m_busNodes) {
        QVariantMap bus = item.toMap();
        int id = bus["id"].toInt();
        if (m_positions.contains(id)) {
            bus["x"] = m_positions[id].x();
            bus["y"] = m_positions[id].y();
        }
        updatedBuses.append(bus);
    }
    m_busNodes = updatedBuses;

    // Position attached components
    positionGenerators();
    positionLoads();
    positionTransformers();

    m_autoLayoutRunning = false;
    emit autoLayoutRunningChanged();
    emit busNodesChanged();
    updateBoundingRect();
}

void SLDSceneController::applyRadialLayout(double centerX, double centerY)
{
    if (m_busNodes.isEmpty()) return;

    int count = m_busNodes.size();
    double maxR = 350.0;

    for (int i = 0; i < count; ++i) {
        QVariantMap bus = m_busNodes[i].toMap();
        int id = bus["id"].toInt();

        // Slack at center, others radial
        if (bus["type"].toString() == "Slack") {
            m_positions[id] = QPointF(centerX, centerY);
        } else {
            double angle = (2.0 * M_PI * i) / count;
            double r = (bus["type"].toString() == "PV") ? maxR * 0.5 : maxR;
            m_positions[id] = QPointF(centerX + r * cos(angle),
                                      centerY + r * sin(angle));
        }
        bus["x"] = m_positions[id].x();
        bus["y"] = m_positions[id].y();
        m_busNodes[i] = bus;
    }

    positionGenerators();
    positionLoads();
    emit busNodesChanged();
    updateBoundingRect();
}

void SLDSceneController::applyRingLayout(double centerX, double centerY)
{
    if (m_busNodes.isEmpty()) return;

    int count = m_busNodes.size();
    double radius = 300.0;

    for (int i = 0; i < count; ++i) {
        QVariantMap bus = m_busNodes[i].toMap();
        int id = bus["id"].toInt();
        double angle = (2.0 * M_PI * i) / count - M_PI / 2.0;
        m_positions[id] = QPointF(centerX + radius * cos(angle),
                                  centerY + radius * sin(angle));
        bus["x"] = m_positions[id].x();
        bus["y"] = m_positions[id].y();
        m_busNodes[i] = bus;
    }

    positionGenerators();
    positionLoads();
    emit busNodesChanged();
    updateBoundingRect();
}

void SLDSceneController::clearScene()
{
    m_busNodes.clear();
    m_lineEdges.clear();
    m_generatorNodes.clear();
    m_loadNodes.clear();
    m_transformerNodes.clear();
    m_positions.clear();
    m_boundingRect = QRectF();
    m_selectedBusId = -1;
    m_selectedLineId = -1;

    emit busNodesChanged();
    emit lineEdgesChanged();
    emit generatorNodesChanged();
    emit loadNodesChanged();
    emit transformerNodesChanged();
    emit boundingRectChanged();
    emit nodeCountChanged();
    emit edgeCountChanged();
}

void SLDSceneController::selectBus(int busId)
{
    m_selectedBusId = busId;
    m_selectedLineId = -1;
    emit selectionChanged(m_selectedBusId, m_selectedLineId);
}

void SLDSceneController::selectLine(int lineId)
{
    m_selectedLineId = lineId;
    m_selectedBusId = -1;
    emit selectionChanged(m_selectedBusId, m_selectedLineId);
}

void SLDSceneController::clearSelection()
{
    m_selectedBusId = -1;
    m_selectedLineId = -1;
    emit selectionChanged(-1, -1);
}

void SLDSceneController::startFlowAnimation()
{
    // Signal to QML to start flow dot animations
    emit lineEdgesChanged();
}

void SLDSceneController::stopFlowAnimation()
{
    emit lineEdgesChanged();
}

void SLDSceneController::updateBoundingRect()
{
    if (m_positions.isEmpty()) {
        m_boundingRect = QRectF();
        emit boundingRectChanged();
        return;
    }

    double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
    for (auto it = m_positions.begin(); it != m_positions.end(); ++it) {
        minX = qMin(minX, it.value().x());
        minY = qMin(minY, it.value().y());
        maxX = qMax(maxX, it.value().x());
        maxY = qMax(maxY, it.value().y());
    }

    m_boundingRect = QRectF(minX - 80, minY - 80, maxX - minX + 160, maxY - minY + 160);
    emit boundingRectChanged();
}

void SLDSceneController::positionGenerators()
{
    for (int i = 0; i < m_generatorNodes.size(); ++i) {
        QVariantMap gen = m_generatorNodes[i].toMap();
        int busId = gen["busId"].toInt();
        if (m_positions.contains(busId)) {
            QPointF bp = m_positions[busId];
            // Offset generator 50px to the upper-right of its bus
            gen["x"] = bp.x() + 50;
            gen["y"] = bp.y() - 50;
            m_generatorNodes[i] = gen;
        }
    }
}

void SLDSceneController::positionLoads()
{
    for (int i = 0; i < m_loadNodes.size(); ++i) {
        QVariantMap load = m_loadNodes[i].toMap();
        int busId = load["busId"].toInt();
        if (m_positions.contains(busId)) {
            QPointF bp = m_positions[busId];
            // Offset load 50px to the lower-left of its bus
            load["x"] = bp.x() - 50;
            load["y"] = bp.y() + 50;
            m_loadNodes[i] = load;
        }
    }
}

void SLDSceneController::positionTransformers()
{
    // Transformers positioned at line midpoints
    m_transformerNodes.clear();
    for (const auto &item : m_lineEdges) {
        QVariantMap line = item.toMap();
        int fromBus = line["fromBus"].toInt();
        int toBus = line["toBus"].toInt();

        if (m_positions.contains(fromBus) && m_positions.contains(toBus)) {
            QPointF p1 = m_positions[fromBus];
            QPointF p2 = m_positions[toBus];

            QVariantMap tx;
            tx["id"] = line["id"];
            tx["x"] = (p1.x() + p2.x()) / 2.0;
            tx["y"] = (p1.y() + p2.y()) / 2.0;
            tx["ratio"] = 1.0;
            tx["tap"] = 0;
            tx["loading"] = line.contains("loading") ? line["loading"] : 50.0;
            m_transformerNodes.append(tx);
        }
    }
    emit transformerNodesChanged();
}
