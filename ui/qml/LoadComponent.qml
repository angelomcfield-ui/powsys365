import QtQuick
import QtQuick.Controls

/**
 * @brief LoadComponent - Load symbol for SLD
 *
 * Orange square with downward arrow, shows P/Q consumption.
 */
Item {
    id: root

    // ── Public properties ────────────────────────────────────────────────
    property int    loadId: 0
    property int    busId: 0
    property string loadName: ""
    property double pLoad: 0.0             // Active power (MW)
    property double qLoad: 0.0             // Reactive power (MVAr)
    property string status: "Active"       // "Active", "Shed", "Off"
    property double pf: 0.85              // Power factor
    property bool   isSelected: false
    property var    theme: null

    // Status color
    property color  statusColor: {
        switch (status) {
        case "Active": return theme ? theme.loadActive : "#FF9F0A"
        case "Shed":   return theme ? theme.loadShed   : "#FF453A"
        default:       return theme ? theme.busDead    : "#8E8E93"
        }
    }

    // Position
    property double xPos: 0
    property double yPos: 0

    x: xPos - width / 2
    y: yPos - height / 2

    width: 56
    height: 72

    // ── Signals ───────────────────────────────────────────────────────────
    signal clicked(var mouse)
    signal contextMenuRequested(var mouse)

    // ── Visual ────────────────────────────────────────────────────────────
    Rectangle {
        id: loadBox
        anchors.horizontalCenter: parent.horizontalCenter
        width: 40
        height: 40
        radius: 8
        color: Qt.rgba(statusColor.r, statusColor.g, statusColor.b, 0.12)
        border.width: 2
        border.color: statusColor

        // Downward arrow (consumption indicator)
        Canvas {
            anchors.centerIn: parent
            width: 20
            height: 20
            antialiasing: true
            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                ctx.strokeStyle = statusColor
                ctx.fillStyle = statusColor
                ctx.lineWidth = 2.5
                ctx.lineCap = "round"
                ctx.lineJoin = "round"

                // Arrow pointing down
                ctx.beginPath()
                ctx.moveTo(10, 2)
                ctx.lineTo(10, 16)
                ctx.stroke()

                // Arrow head
                ctx.beginPath()
                ctx.moveTo(5, 12)
                ctx.lineTo(10, 18)
                ctx.lineTo(15, 12)
                ctx.closePath()
                ctx.fill()
            }
        }
    }

    // Labels
    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: loadBox.bottom
        anchors.topMargin: 3
        spacing: 1

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.loadName || ("L" + root.loadId)
            font.family: theme ? theme.fontText : "SF Pro Text"
            font.pixelSize: 9
            font.weight: Font.Medium
            color: theme ? theme.textSecondary : "#98989D"
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: Number(root.pLoad).toFixed(1) + " MW"
            font.family: theme ? theme.fontMono : "SF Mono"
            font.pixelSize: 9
            font.weight: Font.Bold
            color: statusColor
        }
    }

    // Selection highlight
    Rectangle {
        anchors.centerIn: loadBox
        width: 46
        height: 46
        radius: 11
        color: "transparent"
        border.width: 1.5
        border.color: statusColor
        opacity: isSelected ? 0.3 : 0
        Behavior on opacity { NumberAnimation { duration: 150 } }
    }

    // Tooltip
    ToolTip {
        visible: mouseArea.containsMouse
        delay: 500
        contentItem: Column {
            spacing: 2
            Text {
                text: "<b>" + (root.loadName || "Load " + root.loadId) + "</b> [Bus " + root.busId + "]"
                font.family: theme ? theme.fontText : "SF Pro Text"
                font.pixelSize: 12
                color: "#FFFFFF"
            }
            Text {
                text: "Status: <font color='" + statusColor + "'>" + root.status + "</font>"
                font.family: theme ? theme.fontText : "SF Pro Text"
                font.pixelSize: 11
                color: "#C7C7CC"
                textFormat: Text.RichText
            }
            Text {
                text: "P = " + Number(root.pLoad).toFixed(2) + " MW"
                font.family: theme ? theme.fontMono : "SF Mono"
                font.pixelSize: 11
                color: "#C7C7CC"
            }
            Text {
                text: "Q = " + Number(root.qLoad).toFixed(2) + " MVAr"
                font.family: theme ? theme.fontMono : "SF Mono"
                font.pixelSize: 11
                color: "#C7C7CC"
            }
            Text {
                text: "PF = " + Number(root.pf).toFixed(3)
                font.family: theme ? theme.fontMono : "SF Mono"
                font.pixelSize: 11
                color: "#C7C7CC"
            }
            Text {
                text: "S = " + Number(Math.sqrt(root.pLoad * root.pLoad + root.qLoad * root.qLoad)).toFixed(2) + " MVA"
                font.family: theme ? theme.fontMono : "SF Mono"
                font.pixelSize: 11
                color: "#98989D"
            }
        }
        background: Rectangle {
            color: theme ? theme.tooltipBg : "#3A3A3C"
            radius: 8
            border.width: 1
            border.color: theme ? theme.tooltipBorder : "#48484A"
        }
    }

    // Interaction
    MouseArea {
        id: mouseArea
        anchors.fill: loadBox
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

    // Entrance
    scale: 0
    NumberAnimation on scale {
        to: 1
        duration: 300
        easing.type: Easing.OutBack
        easing.overshoot: 1.5
    }
}
