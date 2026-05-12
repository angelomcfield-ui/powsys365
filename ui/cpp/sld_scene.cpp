#include "sld_scene.h"
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsRectItem>
#include <QPen>
#include <QBrush>

SLDScene::SLDScene(QObject* parent)
    : QGraphicsScene(parent)
{
}

void SLDScene::addBus(qreal x, qreal y)
{
    auto* bus = new QGraphicsEllipseItem(x - 15, y - 15, 30, 30);
    bus->setPen(QPen(Qt::blue, 2));
    bus->setBrush(QBrush(Qt::transparent));
    addItem(bus);
}

void SLDScene::addLine(qreal x1, qreal y1, qreal x2, qreal y2)
{
    auto* line = new QGraphicsLineItem(x1, y1, x2, y2);
    line->setPen(QPen(Qt::black, 3));
    addItem(line);
}

void SLDScene::addGenerator(qreal x, qreal y)
{
    auto* gen = new QGraphicsRectItem(x - 20, y - 20, 40, 40);
    gen->setPen(QPen(Qt::red, 2));
    gen->setBrush(QBrush(Qt::transparent));
    addItem(gen);
}

void SLDScene::addLoad(qreal x, qreal y)
{
    auto* load = new QGraphicsRectItem(x - 15, y - 15, 30, 30);
    load->setPen(QPen(Qt::green, 2));
    load->setBrush(QBrush(Qt::transparent));
    addItem(load);
}