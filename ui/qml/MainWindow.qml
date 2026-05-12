import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: mainWindow

    // Toolbar at top
    Toolbar {
        id: toolbar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 50
    }

    // Main content area
    SplitView {
        anchors.top: toolbar.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        orientation: Qt.Horizontal

        // SLD Canvas
        SLDCanvas {
            id: sldCanvas
            SplitView.minimumWidth: 600
            SplitView.preferredWidth: parent.width * 0.7
        }

        // Properties Panel
        PropertiesPanel {
            id: propertiesPanel
            SplitView.minimumWidth: 300
            SplitView.preferredWidth: parent.width * 0.3
        }
    }
}