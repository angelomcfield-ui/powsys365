import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

ToolBar {
    id: toolbar

    RowLayout {
        anchors.fill: parent
        spacing: 10

        ToolButton {
            text: "New Project"
            icon.source: "qrc:/icons/new.png"
        }

        ToolButton {
            text: "Open"
            icon.source: "qrc:/icons/open.png"
        }

        ToolButton {
            text: "Save"
            icon.source: "qrc:/icons/save.png"
        }

        ToolSeparator {}

        ToolButton {
            text: "Run Load Flow"
            icon.source: "qrc:/icons/run.png"
        }

        ToolButton {
            text: "Run Short Circuit"
            icon.source: "qrc:/icons/short_circuit.png"
        }

        ToolButton {
            text: "Run Stability"
            icon.source: "qrc:/icons/stability.png"
        }

        ToolSeparator {}

        ToolButton {
            text: "Add Bus"
            icon.source: "qrc:/icons/bus.png"
        }

        ToolButton {
            text: "Add Line"
            icon.source: "qrc:/icons/line.png"
        }

        ToolButton {
            text: "Add Generator"
            icon.source: "qrc:/icons/generator.png"
        }

        ToolButton {
            text: "Add Load"
            icon.source: "qrc:/icons/load.png"
        }

        Item { Layout.fillWidth: true }

        Label {
            text: "POWSYS365 v0.1.0"
            font.pixelSize: 12
        }
    }
}