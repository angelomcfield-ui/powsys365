import QtQuick
import QtQuick.Controls

/**
 * @brief LineComponent - Transmission line between two buses
 *
 * Draws a line with loading-based color, flow labels,
 * and animated power flow dots.
 */
Item {
    id: root

    // ── Public properties ────────────────────────────────────────────────
    property int    lineId: 0
    property int    fromBus: 0
    property int    toBus: 0
    property double pFlow: 0.0             // Active power flow (MW)
    property double qFlow: 0.0             // Reactive power flow (MVAr)
    property double loading: 0.0           // Percentage loading
    property string status: "Closed"       // "Closed" or "Open"
    property bool   isSelected: false
    property bool   showFlowAnimation: false

    // Theme
    property var    theme: null

    // Colors based on loading
    property color  lineColor: {
        if (status === "Open")
            return theme ? theme.lineOpen : "#8E8E93"
        if (loading > 100)
            return theme ? theme.lineOverload : "#FF453A"
        if (loading > 80)
            return theme ? theme.lineWarning : "#FFD60A"
        return theme ? theme.lineNormal : "#32D74B"
    }

    // Coordinates (will be set by parent)
    property double x1: 0
    property double y1: 0
    property double x2: 0
    property double y2: 0

    // Computed
    property double length: Math.sqrt(Math.pow(x2 - x1, 2) + Math.pow(y2 - y1, 2))
    property double angle: Math.atan2(y2 - y1, x2 - x1) * 180 / Math.PI

    width: Math.abs(x2 - x1) + 20
    height: Math.abs(y2 - y1) + 20
    x: Math.min(x1, x2) - 10
    y: Math.min(y1, y2) - 10

    // ── Signals ───────────────────────────────────────────────────────────
    signal clicked(var mouse)
    signal contextMenuRequested(var mouse)

    // ── Visual ────────────────────────────────────────────────────────────
    Canvas {
        id: lineCanvas
        anchors.fill: parent
        antialiasing: true
        renderStrategy: Canvas.Threaded

        property double localX1: root.x1 - root.x
        property double localY1: root.y1 - root.y
        property double localX2: root.x2 - root.x
        property double localY2: root.y2 - root.y

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)

            if (root.status === "Open") {
                // Dashed line for open
                ctx.setLineDash([8, 4])
                ctx.strokeStyle = lineColor
                ctx.lineWidth = 2
                ctx.beginPath()
                ctx.moveTo(localX1, localY1)
                ctx.lineTo(localX2, localY2)
                ctx.stroke()
                ctx.setLineDash([])

                // X mark at midpoint
                var mx = (localX1 + localX2) / 2
                var my = (localY1 + localY2) / 2
                ctx.strokeStyle = theme ? theme.lineOpen : "#8E8E93"
                ctx.lineWidth = 2
                ctx.beginPath()
                ctx.moveTo(mx - 6, my - 6)
                ctx.lineTo(mx + 6, my + 6)
                ctx.moveTo(mx + 6, my - 6)
                ctx.lineTo(mx - 6, my + 6)
                ctx.stroke()
            } else {
                // Main line
                ctx.strokeStyle = lineColor
                ctx.lineWidth = isSelected ? 4 : 3
                ctx.lineCap = "round"
                ctx.beginPath()
                ctx.moveTo(localX1, localY1)
                ctx.lineTo(localX2, localY2)
                ctx.stroke()

                // Subtle shadow/glow for overload
                if (root.loading > 100) {
                    ctx.shadowColor = lineColor
                    ctx.shadowBlur = 6
                    ctx.strokeStyle = Qt.rgba(lineColor.r, lineColor.g, lineColor.b, 0.3)
                    ctx.lineWidth = 8
                    ctx.beginPath()
                    ctx.moveTo(localX1, localY1)
                    ctx.lineTo(localX2, localY2)
                    ctx.stroke()
                    ctx.shadowBlur = 0
                }
            }
        }
    }

    // Flow animation dots
    Repeater {
        model: root.showFlowAnimation && root.status === "Closed" ? 3 : 0

        Rectangle {
            width: 6
            height: 6
            radius: 3
            color: lineColor
            opacity: 0.7

            // Position along the line
            property double t: 0
            property double speed: 0.6 + index * 0.3

            x: localX1 + (localX2 - localX1) * t - width / 2
            y: localY1 + (localY2 - localY1) * t - height / 2

            property double localX1: root.x1 - root.x
            property double localY1: root.y1 - root.y
            property double localX2: root.x2 - root.x
            property double localY2: root.y2 - root.y

            NumberAnimation on t {
                from: 0
                to: 1
                duration: 3000 / speed
                loops: Animation.Infinite
                easing.type: Easing.Linear
            }
        }
    }

    // Flow label at midpoint
    Rectangle {
        visible: root.loading > 0
        x: (root.x1 - root.x + root.x2 - root.x) / 2 - width / 2
        y: (root.y1 - root.y + root.y2 - root.y) / 2 - height / 2 - 16
        width: flowLabel.implicitWidth + 12
        height: flowLabel.implicitHeight + 6
        radius: 4
        color: theme ? theme.backgroundSecondary : "#2C2C2E"
        border.width: 1
        border.color: theme ? theme.separator : "#38383A"
        opacity: 0.9

        Text {
            id: flowLabel
            anchors.centerIn: parent
            text: Number(root.pFlow).toFixed(1) + "+j" + Number(Math.abs(root.qFlow)).toFixed(1) + " MW"
            font.family: theme ? theme.fontMono : "SF Mono"
            font.pixelSize: 9
            color: lineColor
        }
    }

    // Loading percentage badge
    Rectangle {
        x: (root.x1 - root.x + root.x2 - root.x) / 2 - width / 2
        y: (root.y1 - root.y + root.y2 - root.y) / 2 - height / 2 + 8
        width: loadingLabel.implicitWidth + 10
        height: loadingLabel.implicitHeight + 4
        radius: 4
        color: {
            if (root.loading > 100) return Qt.rgba(1, 0.27, 0.23, 0.2)
            if (root.loading > 80)  return Qt.rgba(1, 0.84, 0.04, 0.2)
            return Qt.rgba(0.2, 0.84, 0.29, 0.2)
        }
        border.width: 1
        border.color: lineColor
        opacity: 0.9

        Text {
            id: loadingLabel
            anchors.centerIn: parent
            text: Number(root.loading).toFixed(1) + "%"
            font.family: theme ? theme.fontMono : "SF Mono"
            font.pixelSize: 9
            font.bold: true
            color: lineColor
        }
    }

    // Selection highlight
    Canvas {
        anchors.fill: parent
        visible: isSelected
        antialiasing: true
        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            var lx1 = root.x1 - root.x
            var ly1 = root.y1 - root.y
            var lx2 = root.x2 - root.x
            var ly2 = root.y2 - root.y
            ctx.strokeStyle = Qt.rgba(0.04, 0.52, 1, 0.3)
            ctx.lineWidth = 10
            ctx.lineCap = "round"
            ctx.beginPath()
            ctx.moveTo(lx1, ly1)
            ctx.lineTo(lx2, ly2)
            ctx.stroke()
        }
    }

    // Tooltip
    ToolTip {
        visible: mouseArea.containsMouse
        delay: 500
        contentItem: Column {
            spacing: 2
            Text {
                text: "<b>Line " + root.lineId + ":</b> Bus " + root.fromBus + " - Bus " + root.toBus
                font.family: theme ? theme.fontText : "SF Pro Text"
                font.pixelSize: 12
                color: "#FFFFFF"
            }
            Text {
                text: "P = " + Number(root.pFlow).toFixed(2) + " MW"
                font.family: theme ? theme.fontMono : "SF Mono"
                font.pixelSize: 11
                color: "#C7C7CC"
            }
            Text {
                text: "Q = " + Number(root.qFlow).toFixed(2) + " MVAr"
                font.family: theme ? theme.fontMono : "SF Mono"
                font.pixelSize: 11
                color: "#C7C7CC"
            }
            Text {
                text: "Loading: " + Number(root.loading).toFixed(1) + "%"
                font.family: theme ? theme.fontMono : "SF Mono"
                font.pixelSize: 11
                color: root.loading > 100 ? "#FF453A" : (root.loading > 80 ? "#FFD60A" : "#32D74B")
                font.bold: root.loading > 100
            }
        }
        background: Rectangle {
            color: theme ? theme.tooltipBg : "#3A3A3C"
            radius: 8
            border.width: 1
            border.color: theme ? theme.tooltipBorder : "#48484A"
        }
    }

    // Interaction hit area (invisible, wider than line)
    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton

        onClicked: function(mouse) {
            if (mouse.button === Qt.LeftButton)
                root.clicked(mouse)
            else
                root.contextMenuRequested(mouse)
        }
        cursorShape: Qt.PointingHandCursor
    }

    // Entrance animation
    opacity: 0
    NumberAnimation on opacity {
        to: 1
        duration: 400
        easing.type: Easing.OutCubic
    }
}
