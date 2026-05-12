import QtQuick
import QtQuick.Controls

Rectangle {
    color: "#f0f0f0"

    ListView {
        anchors.fill: parent
        model: ["script1.py", "script2.py", "data.json"]
        delegate: Text {
            text: modelData
            padding: 5
        }
    }
}