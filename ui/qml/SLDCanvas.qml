import QtQuick 2.15
import QtQuick.Controls 2.15

Rectangle {
    id: sldCanvas
    color: "#f0f0f0"

    // Canvas for drawing the single line diagram
    Canvas {
        id: canvas
        anchors.fill: parent
        onPaint: {
            var ctx = getContext("2d");
            ctx.clearRect(0, 0, width, height);

            // Draw grid
            ctx.strokeStyle = "#e0e0e0";
            ctx.lineWidth = 1;
            for (var x = 0; x < width; x += 20) {
                ctx.beginPath();
                ctx.moveTo(x, 0);
                ctx.lineTo(x, height);
                ctx.stroke();
            }
            for (var y = 0; y < height; y += 20) {
                ctx.beginPath();
                ctx.moveTo(0, y);
                ctx.lineTo(width, y);
                ctx.stroke();
            }
        }
    }

    // Container for components
    Item {
        id: componentsContainer
        anchors.fill: parent
    }

    // Mouse area for interaction
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton

        onClicked: {
            if (mouse.button === Qt.RightButton) {
                // Show context menu
                contextMenu.popup();
            }
        }
    }

    // Context menu
    Menu {
        id: contextMenu
        MenuItem { text: "Add Bus"; onTriggered: addBus() }
        MenuItem { text: "Add Line"; onTriggered: addLine() }
        MenuItem { text: "Add Generator"; onTriggered: addGenerator() }
        MenuItem { text: "Add Load"; onTriggered: addLoad() }
    }

    function addBus() {
        var component = Qt.createComponent("BusComponent.qml");
        if (component.status === Component.Ready) {
            var bus = component.createObject(componentsContainer);
            bus.x = Math.random() * (width - 50);
            bus.y = Math.random() * (height - 50);
        }
    }

    function addLine() {
        // Implement line addition
    }

    function addGenerator() {
        var component = Qt.createComponent("GeneratorComponent.qml");
        if (component.status === Component.Ready) {
            var gen = component.createObject(componentsContainer);
            gen.x = Math.random() * (width - 50);
            gen.y = Math.random() * (height - 50);
        }
    }

    function addLoad() {
        var component = Qt.createComponent("LoadComponent.qml");
        if (component.status === Component.Ready) {
            var load = component.createObject(componentsContainer);
            load.x = Math.random() * (width - 50);
            load.y = Math.random() * (height - 50);
        }
    }
}