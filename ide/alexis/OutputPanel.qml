import QtQuick
import QtQuick.Controls

Rectangle {
    color: "#e0e0e0"

    TextArea {
        anchors.fill: parent
        readOnly: true
        placeholderText: "Output will appear here..."
    }
}