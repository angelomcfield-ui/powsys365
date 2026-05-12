import QtQuick
import QtQuick.Controls

ApplicationWindow {
    visible: true
    width: 1200
    height: 800
    title: "POWSYS365 IDE"

    menuBar: MenuBar {
        Menu {
            title: "File"
            MenuItem { text: "New Project" }
            MenuItem { text: "Open Project" }
            MenuItem { text: "Save" }
        }
        Menu {
            title: "Run"
            MenuItem { text: "Run Script" }
        }
    }

    Row {
        anchors.fill: parent

        ProjectTree {
            width: 200
            height: parent.height
        }

        Editor {
            width: parent.width - 400
            height: parent.height
        }

        OutputPanel {
            width: 200
            height: parent.height
        }
    }
}