import QtQuick
import QtQuick.Controls

/**
 * @brief TransformerComponent - Transformer symbol for SLD
 *
 * Two interconnected circles representing windings,
 * with ratio label, tap position, and loading indicator.
 */
Item {
    id: root

    // ── Public properties ────────────────────────────────────────────────
    property int    txId: 0
    property double ratio: 1.0             // Turns ratio
    property int    tapPosition: 0         // Tap changer position
    property int    tapMin: -16
    property int    tapMax: 16
    property double loading: 0.0           // Loading percentage
    property double sBase: 100.0           // Base MVA
    property bool   isSelected: false
    property var    theme: null

    // Status color
    property color  statusColor: {
        if (root.loading > 100) return theme ? theme.txOverloaded : "#FF453A"
        return theme ? theme.txNormal : "#64D2FF"
    }

    // Position
    property double xPos: 0
    property double yPos: 0

    x: xPos - width / 2
    y: yPos - height / 2

    width: 64
    height: 76

    // ── Signals ───────────────────────────────────────────────────────────
    signal clicked(var mouse)
    signal contextMenuRequested(var mouse)

    // ── Visual ────────────────────────────────────────────────────────────
    // Two overlapping circles (classic transformer symbol)
    Item {
        anchors.horizontalCenter: parent.horizontalCenter
        width: 52
        height: 44

        // Primary winding (left circle)
        Rectangle {
            id: winding1
            x: 4
            y: 2
            width: 28
            height: 28
            radius: 14
            color: Qt.rgba(statusColor.r, statusColor.g, statusColor.b, 0.1)
            border.width: 2.5
            border.color: statusColor
        }

        // Secondary winding (right circle, offset down)
        Rectangle {
            id: winding2
            x: 20
            y: 14
            width: 28
            height: 28
            radius: 14
            color: Qt.rgba(statusColor.r, statusColor.g, statusColor.b, 0.1)
            border.width: 2.5
            border.color: statusColor
        }
    }

    // Winding dots (polarity marks)
    Rectangle {
        x: winding1.x + winding1.width / 2 - 3
        y: winding1.y - 5
        width: 6
        height: 6
        radius: 3
        color: statusColor
        visible: true
    }

    // Labels below
    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.children[0].bottom
        anchors.topMargin: 8
        spacing: 1

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "TX" + root.txId
            font.family: theme ? theme.fontText : "SF Pro Text"
            font.pixelSize: 9
            font.weight: Font.Medium
            color: theme ? theme.textSecondary : "#98989D"
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "1:" + Number(root.ratio).toFixed(2)
            font.family: theme ? theme.fontMono : "SF Mono"
            font.pixelSize: 10
            font.weight: Font.Bold
            color: statusColor
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Tap " + root.tapPosition
            font.family: theme ? theme.fontMono : "SF Mono"
            font.pixelSize: 9
            color: theme ? theme.textTertiary : "#636366"
            visible: root.tapPosition !== 0
        }
    }

    // Loading bar
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        y: parent.height - 8
        width: 40
        height: 3
        radius: 1.5
        color: theme ? theme.fillTertiary : Qt.rgba(0.46, 0.46, 0.50, 0.10)
        visible: root.loading > 0

        Rectangle {
            width: Math.min(parent.width, parent.width * (root.loading / 100))
            height: parent.height
            radius: 1.5
            color: statusColor
        }
    }

    // Selection highlight
    Rectangle {
        anchors.centerIn: parent
        width: 58
        height: 54
        radius: 12
        color: "transparent"
        border.width: 1.5
        border.color: statusColor
        opacity: isSelected ? 0.3 : 0
        y: 8
        Behavior on opacity { NumberAnimation { duration: 150 } }
    }

    // Tooltip
    ToolTip {
        visible: mouseArea.containsMouse
        delay: 500
        contentItem: Column {
            spacing: 3
            Text {
                text: "<b>Transformer " + root.txId + "</b>"
                font.family: theme ? theme.fontText : "SF Pro Text"
                font.pixelSize: 12
                color: "#FFFFFF"
            }
            Text {
                text: "Ratio: 1:" + Number(root.ratio).toFixed(3)
                font.family: theme ? theme.fontMono : "SF Mono"
                font.pixelSize: 11
                color: "#C7C7CC"
            }
            Text {
                text: "Tap: " + root.tapPosition + " / [" + root.tapMin + " .. +" + root.tapMax + "]"
                font.family: theme ? theme.fontMono : "SF Mono"
                font.pixelSize: 11
                color: "#C7C7CC"
            }
            Text {
                text: "Loading: " + Number(root.loading).toFixed(1) + "%"
                font.family: theme ? theme.fontMono : "SF Mono"
                font.pixelSize: 11
                font.bold: root.loading > 100
                color: root.loading > 100 ? "#FF453A" : (root.loading > 80 ? "#FFD60A" : "#64D2FF")
            }
            Text {
                text: "Sbase: " + Number(root.sBase).toFixed(1) + " MVA"
                font.family: theme ? theme.fontMono : "SF Mono"
                font.pixelSize: 11
                color: "#98989D"
            }
            // Loading bar
            Rectangle {
                width: 140
                height: 4
                radius: 2
                color: "#48484A"
                Rectangle {
                    width: Math.min(parent.width, parent.width * (root.loading / 100))
                    height: parent.height
                    radius: 2
                    color: root.loading > 100 ? "#FF453A" : (root.loading > 80 ? "#FFD60A" : "#64D2FF")
                }
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
    scale: 0
    NumberAnimation on scale {
        to: 1
        duration: 300
        easing.type: Easing.OutBack
        easing.overshoot: 1.2
    }
}
