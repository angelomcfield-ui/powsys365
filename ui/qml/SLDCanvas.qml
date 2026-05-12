import QtQuick
import QtQuick.Controls

/**
 * @brief SLDCanvas - Interactive Single Line Diagram canvas
 *
 * Zoom (0.1x-5x), pan, fit-to-view.
 * Renders buses, lines, generators, loads, transformers.
 * Power flow animation support.
 */
Rectangle {
    id: root

    // ── Properties ───────────────────────────────────────────────────────
    property var    theme: null
    property var    controller: null       // SLDSceneController reference
    property bool   showFlowAnimation: false
    property bool   enableEditing: false

    // Zoom/Pan state
    property double zoomLevel: 1.0
    property double minZoom: 0.1
    property double maxZoom: 5.0
    property double panX: 0
    property double panY: 0

    // Data from C++
    property var busData: []
    property var lineData: []
    property var generatorData: []
    property var loadData: []
    property var transformerData: []

    // Selection
    property int selectedBusId: -1
    property int selectedLineId: -1

    color: theme ? theme.backgroundPrimary : "#1C1C1E"
    clip: true

    // ── Signals ───────────────────────────────────────────────────────────
    signal busClicked(int busId)
    signal lineClicked(int lineId)
    signal canvasContextMenu(var mouse)

    // ── Zoom helpers ──────────────────────────────────────────────────────
    function zoomTo(newZoom, centerX, centerY) {
        var oldZoom = zoomLevel
        zoomLevel = Math.max(minZoom, Math.min(maxZoom, newZoom))
        // Zoom toward point
        var factor = zoomLevel / oldZoom
        panX = centerX - (centerX - panX) * factor
        panY = centerY - (centerY - panY) * factor
    }

    function zoomIn() {
        zoomTo(zoomLevel * 1.25, width / 2, height / 2)
    }

    function zoomOut() {
        zoomTo(zoomLevel * 0.8, width / 2, height / 2)
    }

    function zoomFit() {
        if (!controller || busData.length === 0) {
            zoomLevel = 1.0
            panX = 0
            panY = 0
            return
        }
        var fitZoom = controller.zoomForView(width, height)
        zoomLevel = isFinite(fitZoom) ? fitZoom : 1.0
        panX = width / 2
        panY = height / 2
    }

    function resetView() {
        zoomLevel = 1.0
        panX = width / 2
        panY = height / 2
    }

    // ── Background grid ──────────────────────────────────────────────────
    Canvas {
        id: gridCanvas
        anchors.fill: parent
        antialiasing: false

        property double gridSize: 40 * zoomLevel
        property double offsetX: panX % gridSize
        property double offsetY: panY % gridSize

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)

            var gs = gridSize
            if (gs < 10) return // Don't draw if too dense

            ctx.strokeStyle = theme ? theme.gridColor || "#38383A" : "#38383A"
            ctx.lineWidth = 0.5
            ctx.globalAlpha = 0.3

            // Vertical lines
            for (var x = offsetX; x < width; x += gs) {
                ctx.beginPath()
                ctx.moveTo(x, 0)
                ctx.lineTo(x, height)
                ctx.stroke()
            }
            // Horizontal lines
            for (var y = offsetY; y < height; y += gs) {
                ctx.beginPath()
                ctx.moveTo(0, y)
                ctx.lineTo(width, y)
                ctx.stroke()
            }
        }

        onOffsetXChanged: requestPaint()
        onOffsetYChanged: requestPaint()
        onGridSizeChanged: requestPaint()
    }

    // ── Transform container ──────────────────────────────────────────────
    Item {
        id: sceneContainer
        x: panX
        y: panY
        transform: Scale {
            origin.x: 0
            origin.y: 0
            xScale: zoomLevel
            yScale: zoomLevel
        }

        // Bus position lookup
        function getBusPosition(busId) {
            for (var i = 0; i < busData.length; i++) {
                if (busData[i].id === busId) {
                    return Qt.point(busData[i].x || 0, busData[i].y || 0)
                }
            }
            return Qt.point(0, 0)
        }

        // ── Lines (drawn first, behind buses) ──────────────────────────
        Repeater {
            model: root.lineData

            LineComponent {
                theme: root.theme
                lineId: modelData.id || 0
                fromBus: modelData.fromBus || 0
                toBus: modelData.toBus || 0
                pFlow: modelData.pFlow || 0
                qFlow: modelData.qFlow || 0
                loading: modelData.loading || 0
                status: modelData.status || "Closed"
                isSelected: root.selectedLineId === lineId
                showFlowAnimation: root.showFlowAnimation

                // Coordinates from bus positions
                x1: sceneContainer.getBusPosition(fromBus).x
                y1: sceneContainer.getBusPosition(fromBus).y
                x2: sceneContainer.getBusPosition(toBus).x
                y2: sceneContainer.getBusPosition(toBus).y

                onClicked: {
                    root.selectedLineId = lineId
                    root.selectedBusId = -1
                    root.lineClicked(lineId)
                }
            }
        }

        // ── Buses ──────────────────────────────────────────────────────
        Repeater {
            model: root.busData

            BusComponent {
                theme: root.theme
                busId: modelData.id || 0
                busName: modelData.name || ""
                busType: modelData.type || "PQ"
                vm: modelData.vm || 1.0
                va: modelData.va || 0.0
                pGen: modelData.pGen || 0
                qGen: modelData.qGen || 0
                pLoad: modelData.pLoad || 0
                qLoad: modelData.qLoad || 0
                hasViolation: modelData.hasViolation || false
                isSelected: root.selectedBusId === busId
                showAnimation: root.showFlowAnimation

                xPos: modelData.x || (index * 80 + 100)
                yPos: modelData.y || (index * 60 + 100)

                onClicked: {
                    root.selectedBusId = busId
                    root.selectedLineId = -1
                    root.busClicked(busId)
                }
            }
        }

        // ── Generators ─────────────────────────────────────────────────
        Repeater {
            model: root.generatorData

            GeneratorComponent {
                theme: root.theme
                genId: modelData.id || 0
                busId: modelData.busId || 0
                pGen: modelData.pGen || 0
                qGen: modelData.qGen || 0
                pMax: modelData.pMax || 100
                qMax: modelData.qMax || 50
                status: modelData.status || "Online"
                cost: modelData.cost || 0

                // Position offset from parent bus
                xPos: {
                    var bp = sceneContainer.getBusPosition(busId)
                    return bp.x + 55
                }
                yPos: {
                    var bp = sceneContainer.getBusPosition(busId)
                    return bp.y - 55
                }

                onClicked: root.busClicked(busId)
            }
        }

        // ── Loads ──────────────────────────────────────────────────────
        Repeater {
            model: root.loadData

            LoadComponent {
                theme: root.theme
                loadId: modelData.id || 0
                busId: modelData.busId || 0
                pLoad: modelData.pLoad || 0
                qLoad: modelData.qLoad || 0
                status: modelData.status || "Active"

                xPos: {
                    var bp = sceneContainer.getBusPosition(busId)
                    return bp.x - 55
                }
                yPos: {
                    var bp = sceneContainer.getBusPosition(busId)
                    return bp.y + 55
                }

                onClicked: root.busClicked(busId)
            }
        }

        // ── Transformers ───────────────────────────────────────────────
        Repeater {
            model: root.transformerData

            TransformerComponent {
                theme: root.theme
                txId: modelData.id || 0
                ratio: modelData.ratio || 1.0
                tapPosition: modelData.tap || 0
                loading: modelData.loading || 0
                sBase: modelData.sBase || 100

                xPos: modelData.x || 0
                yPos: modelData.y || 0
            }
        }
    }

    // ── Pan interaction ──────────────────────────────────────────────────
    MouseArea {
        id: panArea
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
        scrollGestureEnabled: false

        property double lastX: 0
        property double lastY: 0
        property bool isPanning: false

        onPressed: function(mouse) {
            lastX = mouse.x
            lastY = mouse.y
            isPanning = (mouse.button === Qt.MiddleButton) ||
                       (mouse.button === Qt.LeftButton && (mouse.modifiers & Qt.ControlModifier))
        }

        onPositionChanged: function(mouse) {
            if (isPanning) {
                panX += mouse.x - lastX
                panY += mouse.y - lastY
                lastX = mouse.x
                lastY = mouse.y
            }
        }

        onReleased: {
            isPanning = false
        }

        onWheel: function(wheel) {
            var factor = wheel.angleDelta.y > 0 ? 1.15 : 0.87
            zoomTo(zoomLevel * factor, wheel.x, wheel.y)
            wheel.accepted = true
        }

        onClicked: function(mouse) {
            if (mouse.button === Qt.RightButton) {
                root.canvasContextMenu(mouse)
            }
        }
    }

    // ── Zoom level indicator ─────────────────────────────────────────────
    Rectangle {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 16
        width: zoomText.implicitWidth + 16
        height: zoomText.implicitHeight + 8
        radius: 6
        color: theme ? theme.backgroundSecondary : "#2C2C2E"
        border.width: 1
        border.color: theme ? theme.separator : "#38383A"
        opacity: 0.8

        Text {
            id: zoomText
            anchors.centerIn: parent
            text: Math.round(root.zoomLevel * 100) + "%"
            font.family: theme ? theme.fontMono : "SF Mono"
            font.pixelSize: 11
            color: theme ? theme.textSecondary : "#98989D"
        }
    }

    // ── Coordinate display ───────────────────────────────────────────────
    Rectangle {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 16
        width: coordText.implicitWidth + 16
        height: coordText.implicitHeight + 8
        radius: 6
        color: theme ? theme.backgroundSecondary : "#2C2C2E"
        border.width: 1
        border.color: theme ? theme.separator : "#38383A"
        opacity: 0.8
        visible: panArea.containsMouse

        Text {
            id: coordText
            anchors.centerIn: parent
            text: {
                var sx = (panArea.mouseX - panX) / zoomLevel
                var sy = (panArea.mouseY - panY) / zoomLevel
                return "(" + Math.round(sx) + ", " + Math.round(sy) + ")"
            }
            font.family: theme ? theme.fontMono : "SF Mono"
            font.pixelSize: 10
            color: theme ? theme.textTertiary : "#636366"
        }
    }

    // ── Empty state ──────────────────────────────────────────────────────
    Column {
        anchors.centerIn: parent
        spacing: 12
        visible: root.busData.length === 0

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "No system loaded"
            font.family: theme ? theme.fontDisplay : "SF Pro Display"
            font.pixelSize: 20
            font.weight: Font.Medium
            color: theme ? theme.textSecondary : "#98989D"
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Open a project or create a new one to start"
            font.family: theme ? theme.fontText : "SF Pro Text"
            font.pixelSize: 13
            color: theme ? theme.textTertiary : "#636366"
        }

        Button {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "New Project"
            onClicked: if (controller) controller.newProject()

            contentItem: Text {
                text: parent.text
                font.family: theme ? theme.fontText : "SF Pro Text"
                font.pixelSize: 13
                font.weight: Font.Medium
                color: theme ? theme.accentBlue : "#0A84FF"
                horizontalAlignment: Text.AlignHCenter
            }

            background: Rectangle {
                implicitWidth: 120
                implicitHeight: 32
                radius: 6
                color: parent.hovered ? Qt.rgba(0.04, 0.52, 1, 0.15) : "transparent"
                border.width: 1
                border.color: theme ? theme.accentBlue : "#0A84FF"
            }
        }
    }
}
