import QtQuick 2.15
import QtQuick.Controls 2.15

Rectangle {
    id: busComponent
    width: 60
    height: 30
    color: "transparent"
    border.color: "blue"
    border.width: 2
    radius: 5

    Text {
        anchors.centerIn: parent
        text: "Bus " + (index + 1)
        font.pixelSize: 12
    }

    // Drag functionality
    MouseArea {
        anchors.fill: parent
        drag.target: parent
        drag.axis: Drag.XAndYAxis
        acceptedButtons: Qt.LeftButton
    }

    // Properties
    property int busNumber: 1
    property string busName: "Bus"
    property real voltage: 1.0
    property int busType: 1 // 1=PQ, 2=PV, 3=Slack
}