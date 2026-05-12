#ifndef SLD_SCENE_H
#define SLD_SCENE_H

#include <QObject>
#include <QGraphicsScene>

class SLDScene : public QGraphicsScene
{
    Q_OBJECT

public:
    explicit SLDScene(QObject* parent = nullptr);

public slots:
    void addBus(qreal x, qreal y);
    void addLine(qreal x1, qreal y1, qreal x2, qreal y2);
    void addGenerator(qreal x, qreal y);
    void addLoad(qreal x, qreal y);

signals:
    void componentSelected(int type, int id);
};

#endif // SLD_SCENE_H