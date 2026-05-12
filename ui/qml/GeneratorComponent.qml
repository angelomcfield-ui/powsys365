import QtQuick
import QtQuick.Controls

/**
 * @brief GeneratorComponent - Generator symbol for SLD
 *
 * Green circle with "G" letter, shows P/Q output and online status.
 */
Item {
    id: root

    // ── Public properties ────────────────────────────────────────────────
    property int    genId: 0
    property int    busId: 0
    property string genName: ""
    property double pGen: 0.0              // Generated P (MW)
    property double qGen: 0.0              // Generated Q (MVAr)
    property double pMax: 100.0            // Max P (MW)
    property double qMax: 50.0             // Max Q (MVAr)
    property string status: "Online"       // "Online", "Offline", "Limited"
    property double cost: 0.0              // Generation cost ($/MWh)
    property bool   isSelected: false
    property var    theme: null

    // Status color
    property color  statusColor: {
        switch (status) {
        case "Online":  return theme ? theme.genOnline  : "#32D74B"
        case "Limited": return theme ? theme.genLimited : "#FFD60A"
        default:        return theme ? theme.genOffline : "#8E8E93"
        }
    }

    // Position (offset from bus)
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
        id: genCircle
        anchors.horizontalCenter: parent.horizontalCenter
        width: 44
        height: 44
        radius: 22
        color: Qt.rgba(statusColor.r, statusColor.g, statusColor.b, 0.15)
        border.width: 2.5
        border.color: statusColor

        // "G" symbol
        Text {
            anchors.centerIn: parent
            text: "G"
            font.family: theme ? theme.fontMono : "SF Mono"
            font.pixelSize: 18
            font.bold: true
            color: statusColor
        }

        // Status dot (small indicator on the rim)
        Rectangle {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.rightMargin: -2
            anchors.topMargin: -2
            width: 12
            height: 12
            radius: 6
            color: statusColor
            border.width: 2
            border.color: theme ? theme.backgroundSecondary : "#2C2C2E"

            // Pulse animation when online
            Rectangle {
                anchors.centerIn: parent
                width: parent.width
                height: parent.height
                radius: width / 2
                color: statusColor
                opacity: 0.5

                NumberAnimation on scale {
                    from: 1
                    to: 2.5
                    duration: 1500
                    loops: Animation.Infinite
                    easing.type: Easing.OutQuad
                }
                NumberAnimation on opacity {
                    from: 0.5
                    to: 0
                    duration: 1500
                    loops: Animation.Infinite
                    easing.type: Easing.OutQuad
                }
                visible: root.status === "Online"
            }
        }
    }

    // Labels below
    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: genCircle.bottom
        anchors.topMargin: 3
        spacing: 1

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.genName || ("G" + root.genId)
            font.family: theme ? theme.fontText : "SF Pro Text"
            font.pixelSize: 9
            font.weight: Font.Medium
            color: theme ? theme.textSecondary : "#98989D"
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: Number(root.pGen).toFixed(1) + " MW"
            font.family: theme ? theme.fontMono : "SF Mono"
            font.pixelSize: 9
            font.weight: Font.Bold
            color: statusColor
        }
    }

    // Connection line to bus (drawn by parent, but we provide the anchor point)

    // Selection highlight
    Rectangle {
        anchors.centerIn: genCircle
        width: 50
        height: 50
        radius: 25
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
                text: "<b>" + (root.genName || "Generator " + root.genId) + "</b> [Bus " + root.busId + "]"
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
                text: "Pgen = " + Number(root.pGen).toFixed(2) + " / " + Number(root.pMax).toFixed(1) + " MW"
                font.family: theme ? theme.fontMono : "SF Mono"
                font.pixelSize: 11
                color: "#C7C7CC"
            }
            Text {
                text: "Qgen = " + Number(root.qGen).toFixed(2) + " / " + Number(root.qMax).toFixed(1) + " MVAr"
                font.family: theme ? theme.fontMono : "SF Mono"
                font.pixelSize: 11
                color: "#C7C7CC"
            }
            Text {
                text: "Cost: $" + Number(root.cost).toFixed(2) + "/MWh"
                font.family: theme ? theme.fontMono : "SF Mono"
                font.pixelSize: 11
                color: "#C7C7CC"
                visible: root.cost > 0
            }
            // Pgen bar
            Rectangle {
                width: 140
                height: 4
                radius: 2
                color: "#48484A"
                Rectangle {
                    width: Math.min(parent.width, parent.width * (root.pGen / root.pMax))
                    height: parent.height
                    radius: 2
                    color: statusColor
                    visible: root.pMax > 0
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
        anchors.fill: genCircle
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
        easing.overshoot: 1.5
    }
}
