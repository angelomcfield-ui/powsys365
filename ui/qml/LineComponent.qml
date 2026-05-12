import QtQuick 2.15
import QtQuick.Shapes 1.15

Shape {
    id: lineComponent
    width: 100
    height: 4

    ShapePath {
        strokeWidth: 3
        strokeColor: "black"
        fillColor: "transparent"
        startX: 0
        startY: height / 2
        PathLine { x: width; y: height / 2 }
    }

    // Properties
    property int fromBus: 1
    property int toBus: 2
    property real resistance: 0.01
    property real reactance: 0.03
}