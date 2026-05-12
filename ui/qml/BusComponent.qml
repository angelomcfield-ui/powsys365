import QtQuick
import QtQuick.Controls

/**
 * @brief BusComponent - Electric bus (node) symbol for SLD
 *
 * Renders a bus as a colored circle with voltage label,
 * type indicator, and violation warning animation.
 */
Item {
    id: root

    // ── Public properties ────────────────────────────────────────────────
    property int    busId: 0
    property string busName: ""
    property string busType: "PQ"          // "PQ", "PV", "Slack", "Dead"
    property double vm: 1.0                // Voltage magnitude (p.u.)
    property double va: 0.0                // Voltage angle (deg)
    property double pGen: 0.0              // Generated P (MW)
    property double qGen: 0.0              // Generated Q (MVAr)
    property double pLoad: 0.0             // Load P (MW)
    property double qLoad: 0.0             // Load Q (MVAr)
    property bool   hasViolation: false
    property bool   isSelected: false
    property bool   showAnimation: false

    // Theme colors
    property color  typeColor: {
        switch (busType) {
        case "Slack": return theme ? theme.busSlack : "#FF453A"
        case "PV":    return theme ? theme.busPV    : "#32D74B"
        case "PQ":    return theme ? theme.busPQ    : "#0A84FF"
        default:      return theme ? theme.busDead  : "#8E8E93"
        }
    }
    property var    theme: null

    // Size
    width: 64
    height: 80

    // Center the item on its coordinate
    x: xPos - width / 2
    y: yPos - height / 2
    property double xPos: 0
    property double yPos: 0

    // ── Signals ───────────────────────────────────────────────────────────
    signal clicked(var mouse)
    signal doubleClicked(var mouse)
    signal contextMenuRequested(var mouse)

    // ── Visual ────────────────────────────────────────────────────────────
    Rectangle {
        id: outerRing
        anchors.centerIn: parent
        width: 52
        height: 52
        radius: 26
        color: "transparent"
        border.width: isSelected ? 3 : 2
        border.color: isSelected ? Qt.lighter(typeColor, 1.3) : typeColor
        opacity: 0.8

        Behavior on border.width {
            NumberAnimation { duration: 150; easing.type: Easing.OutCubic }
        }
    }

    Rectangle {
        id: innerCircle
        anchors.centerIn: outerRing
        width: 40
        height: 40
        radius: 20
        color: Qt.rgba(typeColor.r, typeColor.g, typeColor.b, 0.15)
        border.width: 1
        border.color: Qt.rgba(typeColor.r, typeColor.g, typeColor.b, 0.3)

        // Type indicator letter
        Text {
            anchors.centerIn: parent
            text: {
                switch (root.busType) {
                case "Slack": return "S"
                case "PV":    return "V"
                default:      return "Q"
                }
            }
            font.family: theme ? theme.fontMono : "SF Mono"
            font.pixelSize: 14
            font.bold: true
            color: typeColor
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    // Voltage label below
    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: outerRing.bottom
        anchors.topMargin: 4
        spacing: 1

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.busName || ("Bus " + root.busId)
            font.family: theme ? theme.fontText : "SF Pro Text"
            font.pixelSize: 10
            font.weight: Font.Medium
            color: theme ? theme.textSecondary : "#98989D"
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: Number(root.vm).toFixed(4) + " pu"
            font.family: theme ? theme.fontMono : "SF Mono"
            font.pixelSize: 10
            font.weight: Font.Bold
            color: root.hasViolation
                   ? (theme ? theme.violation : "#FF453A")
                   : (theme ? theme.textPrimary : "#FFFFFF")
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: Number(root.va).toFixed(2) + " deg"
            font.family: theme ? theme.fontMono : "SF Mono"
            font.pixelSize: 9
            color: theme ? theme.textTertiary : "#636366"
            visible: root.hasResults
            property bool hasResults: root.va !== 0.0
        }
    }

    // Violation warning animation
    Rectangle {
        anchors.centerIn: outerRing
        width: 56
        height: 56
        radius: 28
        color: "transparent"
        border.width: 2
        border.color: theme ? theme.violation : "#FF453A"
        opacity: root.hasViolation ? (0.4 + 0.6 * Math.abs(Math.sin(animationPhase * Math.PI * 2))) : 0
        visible: root.hasViolation

        property double animationPhase: 0

        NumberAnimation on animationPhase {
            running: root.hasViolation
            from: 0
            to: 1
            loops: Animation.Infinite
            duration: 1200
        }
    }

    // Selection glow
    Rectangle {
        anchors.centerIn: outerRing
        width: 60
        height: 60
        radius: 30
        color: "transparent"
        border.width: 1
        border.color: typeColor
        opacity: isSelected ? 0.25 : 0

        Behavior on opacity {
            NumberAnimation { duration: 200 }
        }
    }

    // Tooltip on hover
    ToolTip {
        id: tooltip
        visible: mouseArea.containsMouse && !mouseArea.pressed
        delay: 500
        timeout: 5000

        contentItem: Column {
            spacing: 2
            Text {
                text: "<b>" + (root.busName || "Bus " + root.busId) + "</b> [" + root.busType + "]"
                font.family: theme ? theme.fontText : "SF Pro Text"
                font.pixelSize: 12
                color: "#FFFFFF"
            }
            Text {
                text: "V = " + Number(root.vm).toFixed(4) + " pu / " + Number(root.va).toFixed(2) + " deg"
                font.family: theme ? theme.fontMono : "SF Mono"
                font.pixelSize: 11
                color: "#C7C7CC"
            }
            Text {
                text: "Gen: P=" + Number(root.pGen).toFixed(1) + " Q=" + Number(root.qGen).toFixed(1)
                font.family: theme ? theme.fontMono : "SF Mono"
                font.pixelSize: 11
                color: "#C7C7CC"
                visible: root.pGen > 0 || root.qGen > 0
            }
            Text {
                text: "Load: P=" + Number(root.pLoad).toFixed(1) + " Q=" + Number(root.qLoad).toFixed(1)
                font.family: theme ? theme.fontMono : "SF Mono"
                font.pixelSize: 11
                color: "#C7C7CC"
                visible: root.pLoad > 0 || root.qLoad > 0
            }
        }

        background: Rectangle {
            color: theme ? theme.tooltipBg : "#3A3A3C"
            radius: 8
            border.width: 1
            border.color: theme ? theme.tooltipBorder : "#48484A"
        }
    }

    // ── Interaction ───────────────────────────────────────────────────────
    MouseArea {
        id: mouseArea
        anchors.fill: outerRing
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton

        onClicked: function(mouse) {
            if (mouse.button === Qt.LeftButton) {
                root.clicked(mouse)
            } else {
                root.contextMenuRequested(mouse)
            }
        }
        onDoubleClicked: root.doubleClicked(mouse)

        cursorShape: Qt.PointingHandCursor
    }

    // Drag behavior
    Drag.active: mouseArea.drag.active
    Drag.hotSpot.x: width / 2
    Drag.hotSpot.y: height / 2

    MouseArea {
        anchors.fill: parent
        enabled: false // Only for drag propagation
    }

    // Entrance animation
    NumberAnimation on opacity {
        from: 0
        to: 1
        duration: 300
        easing.type: Easing.OutCubic
    }
    NumberAnimation on scale {
        from: 0.5
        to: 1.0
        duration: 300
        easing.type: Easing.OutBack
    }
}
