#ifndef SLD_SCENE_H
#define SLD_SCENE_H

#include <QObject>
#include <QQmlEngine>
#include <QVariantList>
#include <QVariantMap>
#include <QHash>

/**
 * @brief SLDSceneController - Manages Single Line Diagram scene data
 *
 * Provides bus/line/generator/load geometry data to QML for rendering.
 * Handles automatic layout (force-directed positioning), coordinate
 * transformations, and result-to-visual mapping.
 */
class SLDSceneController : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // ── Properties exposed to QML ──────────────────────────────────────────
    Q_PROPERTY(QVariantList busNodes READ busNodes NOTIFY busNodesChanged)
    Q_PROPERTY(QVariantList lineEdges READ lineEdges NOTIFY lineEdgesChanged)
    Q_PROPERTY(QVariantList generatorNodes READ generatorNodes NOTIFY generatorNodesChanged)
    Q_PROPERTY(QVariantList loadNodes READ loadNodes NOTIFY loadNodesChanged)
    Q_PROPERTY(QVariantList transformerNodes READ transformerNodes NOTIFY transformerNodesChanged)
    Q_PROPERTY(QRectF boundingRect READ boundingRect NOTIFY boundingRectChanged)
    Q_PROPERTY(bool autoLayoutRunning READ autoLayoutRunning NOTIFY autoLayoutRunningChanged)
    Q_PROPERTY(int nodeCount READ nodeCount NOTIFY nodeCountChanged)
    Q_PROPERTY(int edgeCount READ edgeCount NOTIFY edgeCountChanged)

public:
    explicit SLDSceneController(QObject *parent = nullptr);
    ~SLDSceneController() override;

    // ── Q_PROPERTY getters ────────────────────────────────────────────────
    QVariantList busNodes() const { return m_busNodes; }
    QVariantList lineEdges() const { return m_lineEdges; }
    QVariantList generatorNodes() const { return m_generatorNodes; }
    QVariantList loadNodes() const { return m_loadNodes; }
    QVariantList transformerNodes() const { return m_transformerNodes; }
    QRectF boundingRect() const { return m_boundingRect; }
    bool autoLayoutRunning() const { return m_autoLayoutRunning; }
    int nodeCount() const { return m_busNodes.size() + m_generatorNodes.size() + m_loadNodes.size(); }
    int edgeCount() const { return m_lineEdges.size(); }

    // ── Coordinate helpers (invokable from QML) ───────────────────────────
    Q_INVOKABLE QPointF busPosition(int busId) const;
    Q_INVOKABLE QPointF mapToScene(double x, double y) const;
    Q_INVOKABLE QPointF mapFromScene(double x, double y) const;
    Q_INVOKABLE double zoomForFit(double viewWidth, double viewHeight) const;
    Q_INVOKABLE QVariantMap busAt(int index) const;
    Q_INVOKABLE QVariantMap lineAt(int index) const;
    Q_INVOKABLE int busIndexAtPosition(double x, double y, double radius) const;

public slots:
    // ── Data population ───────────────────────────────────────────────────
    void setBusData(const QVariantList &buses);
    void setLineData(const QVariantList &lines);
    void setGeneratorData(const QVariantList &generators);
    void setLoadData(const QVariantList &loads);

    // ── Layout ────────────────────────────────────────────────────────────
    void computeAutoLayout(double width = 1200, double height = 800);
    void applyRadialLayout(double centerX = 600, double centerY = 400);
    void applyRingLayout(double centerX = 600, double centerY = 400);
    void clearScene();

    // ── Selection ─────────────────────────────────────────────────────────
    void selectBus(int busId);
    void selectLine(int lineId);
    void clearSelection();

    // ── Animation helpers ─────────────────────────────────────────────────
    void startFlowAnimation();
    void stopFlowAnimation();

signals:
    void busNodesChanged();
    void lineEdgesChanged();
    void generatorNodesChanged();
    void loadNodesChanged();
    void transformerNodesChanged();
    void boundingRectChanged();
    void autoLayoutRunningChanged();
    void nodeCountChanged();
    void edgeCountChanged();
    void selectionChanged(int selectedBusId, int selectedLineId);

private:
    QVariantList m_busNodes;
    QVariantList m_lineEdges;
    QVariantList m_generatorNodes;
    QVariantList m_loadNodes;
    QVariantList m_transformerNodes;
    QRectF m_boundingRect;
    bool m_autoLayoutRunning = false;

    // Internal position map: busId -> QPointF
    QHash<int, QPointF> m_positions;
    int m_selectedBusId = -1;
    int m_selectedLineId = -1;

    void updateBoundingRect();
    void positionGenerators();
    void positionLoads();
    void positionTransformers();
};

#endif // SLD_SCENE_H
