import QtQuick 2.15
import QtQuick.Shapes 1.15

Item {
    id: loadComponent
    width: 30
    height: 30

    Shape {
        anchors.fill: parent

        ShapePath {
            strokeWidth: 2
            strokeColor: "green"
            fillColor: "transparent"
            startX: 0
            startY: 0
            PathLine { x: width; y: 0 }
            PathLine { x: width / 2; y: height }
            PathLine { x: 0; y: 0 }
        }
    }

    Text {
        anchors.centerIn: parent
        text: "L"
        font.pixelSize: 12
        font.bold: true
        color: "green"
    }

    // Properties
    property int bus: 1
    property real pLoad: 50.0
    property real qLoad: 10.0
}