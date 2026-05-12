import QtQuick 2.15
import QtQuick.Shapes 1.15

Item {
    id: generatorComponent
    width: 40
    height: 40

    Shape {
        anchors.fill: parent

        ShapePath {
            strokeWidth: 2
            strokeColor: "red"
            fillColor: "transparent"
            startX: width / 2
            startY: 0
            PathLine { x: 0; y: height }
            PathLine { x: width; y: height }
            PathLine { x: width / 2; y: 0 }
        }
    }

    Text {
        anchors.centerIn: parent
        text: "G"
        font.pixelSize: 14
        font.bold: true
        color: "red"
    }

    // Properties
    property int bus: 1
    property real pGen: 100.0
    property real qGen: 20.0
}