import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/**
 * @brief Toolbar - Main analysis toolbar
 *
 * Run buttons, zoom controls, method selector, status indicator.
 * Apple-style glassmorphism toolbar with rounded buttons.
 */
Rectangle {
    id: root

    // ── Properties ───────────────────────────────────────────────────────
    property var    theme: null
    property bool   isSolving: false
    property string statusMessage: "Ready"
    property string currentMethod: "NR"
    property bool   hasResults: false

    // ── Signals ───────────────────────────────────────────────────────────
    signal runLoadFlow()
    signal runShortCircuit()
    signal runStability()
    signal runOPF()
    signal zoomIn()
    signal zoomOut()
    signal zoomFit()
    signal methodChanged(string method)

    height: 52
    color: theme ? theme.toolbarBg : Qt.rgba(0.17, 0.17, 0.18, 0.94)
    border.color: theme ? theme.toolbarBorder : "#38383A"
    border.width: 0

    // Bottom subtle separator
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        color: theme ? theme.separator : "#38383A"
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 10

        // ── Run buttons ─────────────────────────────────────────────────
        Row {
            spacing: 6
            Layout.alignment: Qt.AlignVCenter

            // Run Load Flow button (primary action)
            Button {
                id: runBtn
                enabled: !isSolving

                contentItem: Row {
                    spacing: 6
                    anchors.centerIn: parent

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 8
                        height: 8
                        radius: 4
                        color: theme ? theme.accentGreen : "#32D74B"

                        // Pulse animation
                        Rectangle {
                            anchors.centerIn: parent
                            width: parent.width
                            height: parent.height
                            radius: parent.radius
                            color: parent.color
                            opacity: 0.5
                            NumberAnimation on scale {
                                from: 1; to: 2.5; duration: 1200
                                loops: Animation.Infinite
                                easing.type: Easing.OutQuad
                            }
                            NumberAnimation on opacity {
                                from: 0.5; to: 0; duration: 1200
                                loops: Animation.Infinite
                                easing.type: Easing.OutQuad
                            }
                            visible: !isSolving
                        }
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: isSolving ? "Running..." : "Run Load Flow"
                        font.family: theme ? theme.fontText : "SF Pro Text"
                        font.pixelSize: 13
                        font.weight: Font.Medium
                        color: theme ? theme.textPrimary : "#FFFFFF"
                    }
                }

                background: Rectangle {
                    implicitWidth: 130
                    implicitHeight: 32
                    radius: 6
                    color: {
                        if (!runBtn.enabled) return theme ? theme.buttonDisabled : "#2C2C2E"
                        if (runBtn.pressed)  return Qt.darker(theme ? theme.buttonBg : "#3A3A3C", 1.2)
                        if (runBtn.hovered)  return theme ? theme.buttonHover : "#48484A"
                        return theme ? theme.buttonBg : "#3A3A3C"
                    }
                    border.width: 1
                    border.color: theme ? theme.buttonBorder : "#48484A"
                    Behavior on color { ColorAnimation { duration: 100 } }
                }

                onClicked: root.runLoadFlow()
            }

            // Divider
            Rectangle {
                width: 1
                height: 24
                anchors.verticalCenter: parent.verticalCenter
                color: theme ? theme.separator : "#38383A"
            }

            // Quick analysis buttons
            ToolButton {
                id: scBtn
                enabled: !isSolving
                text: "Short Circuit"
                font.family: theme ? theme.fontText : "SF Pro Text"
                font.pixelSize: 12

                contentItem: Text {
                    text: scBtn.text
                    font: scBtn.font
                    color: scBtn.enabled
                        ? (scBtn.hovered ? (theme ? theme.textPrimary : "#FFFFFF") : (theme ? theme.textSecondary : "#98989D"))
                        : (theme ? theme.textTertiary : "#636366")
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                background: Rectangle {
                    implicitWidth: 100
                    implicitHeight: 28
                    radius: 6
                    color: {
                        if (scBtn.pressed) return Qt.darker(theme ? theme.buttonBg : "#3A3A3C", 1.2)
                        if (scBtn.hovered) return theme ? theme.buttonHover : "#48484A"
                        return "transparent"
                    }
                    Behavior on color { ColorAnimation { duration: 80 } }
                }

                onClicked: root.runShortCircuit()
            }

            ToolButton {
                id: stabBtn
                enabled: !isSolving
                text: "Stability"
                font.family: theme ? theme.fontText : "SF Pro Text"
                font.pixelSize: 12

                contentItem: Text {
                    text: stabBtn.text
                    font: stabBtn.font
                    color: stabBtn.enabled
                        ? (stabBtn.hovered ? (theme ? theme.textPrimary : "#FFFFFF") : (theme ? theme.textSecondary : "#98989D"))
                        : (theme ? theme.textTertiary : "#636366")
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                background: Rectangle {
                    implicitWidth: 80
                    implicitHeight: 28
                    radius: 6
                    color: {
                        if (stabBtn.pressed) return Qt.darker(theme ? theme.buttonBg : "#3A3A3C", 1.2)
                        if (stabBtn.hovered) return theme ? theme.buttonHover : "#48484A"
                        return "transparent"
                    }
                    Behavior on color { ColorAnimation { duration: 80 } }
                }

                onClicked: root.runStability()
            }

            ToolButton {
                id: opfBtn
                enabled: !isSolving
                text: "OPF"
                font.family: theme ? theme.fontText : "SF Pro Text"
                font.pixelSize: 12

                contentItem: Text {
                    text: opfBtn.text
                    font: opfBtn.font
                    color: opfBtn.enabled
                        ? (opfBtn.hovered ? (theme ? theme.textPrimary : "#FFFFFF") : (theme ? theme.textSecondary : "#98989D"))
                        : (theme ? theme.textTertiary : "#636366")
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                background: Rectangle {
                    implicitWidth: 60
                    implicitHeight: 28
                    radius: 6
                    color: {
                        if (opfBtn.pressed) return Qt.darker(theme ? theme.buttonBg : "#3A3A3C", 1.2)
                        if (opfBtn.hovered) return theme ? theme.buttonHover : "#48484A"
                        return "transparent"
                    }
                    Behavior on color { ColorAnimation { duration: 80 } }
                }

                onClicked: root.runOPF()
            }
        }

        // ── Method selector ─────────────────────────────────────────────
        Row {
            spacing: 8
            Layout.alignment: Qt.AlignVCenter

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "Method:"
                font.family: theme ? theme.fontText : "SF Pro Text"
                font.pixelSize: 12
                color: theme ? theme.textSecondary : "#98989D"
            }

            ComboBox {
                id: methodCombo
                anchors.verticalCenter: parent.verticalCenter
                enabled: !isSolving
                model: ["NR", "FD", "FDXB", "GS"]
                currentIndex: 0

                font.family: theme ? theme.fontMono : "SF Mono"
                font.pixelSize: 12

                contentItem: Text {
                    text: methodCombo.displayText
                    font: methodCombo.font
                    color: theme ? theme.textPrimary : "#FFFFFF"
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignHCenter
                    leftPadding: 8
                }

                background: Rectangle {
                    implicitWidth: 80
                    implicitHeight: 28
                    radius: 6
                    color: theme ? theme.inputBg : "#1C1C1E"
                    border.width: 1
                    border.color: methodCombo.hovered || methodCombo.down
                        ? (theme ? theme.inputFocused : "#0A84FF")
                        : (theme ? theme.inputBorder : "#38383A")
                    Behavior on border.color { ColorAnimation { duration: 100 } }
                }

                popup: Popup {
                    y: methodCombo.height + 4
                    width: methodCombo.width
                    padding: 4

                    background: Rectangle {
                        color: theme ? theme.backgroundSecondary : "#2C2C2E"
                        radius: 8
                        border.width: 1
                        border.color: theme ? theme.separator : "#38383A"
                    }

                    contentItem: ListView {
                        clip: true
                        implicitHeight: contentHeight
                        model: methodCombo.delegateModel
                        currentIndex: methodCombo.highlightedIndex
                        ScrollIndicator.vertical: ScrollIndicator {}
                    }
                }

                delegate: ItemDelegate {
                    width: methodCombo.width - 8
                    height: 28
                    highlighted: methodCombo.highlightedIndex === index

                    contentItem: Text {
                        text: modelData
                        font: methodCombo.font
                        color: highlighted
                            ? (theme ? theme.accentBlue : "#0A84FF")
                            : (theme ? theme.textPrimary : "#FFFFFF")
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: 4
                        color: highlighted
                            ? (theme ? theme.sidebarItemSelected : Qt.rgba(0.04, 0.52, 1, 0.2))
                            : "transparent"
                    }
                }

                onActivated: {
                    root.currentMethod = model[index]
                    root.methodChanged(root.currentMethod)
                }
            }
        }

        Item { Layout.fillWidth: true } // Spacer

        // ── Zoom controls ───────────────────────────────────────────────
        Row {
            spacing: 4
            Layout.alignment: Qt.AlignVCenter

            ToolButton {
                id: zoomInBtn
                text: "+"
                font.family: theme ? theme.fontMono : "SF Mono"
                font.pixelSize: 16
                font.bold: true

                contentItem: Text {
                    text: zoomInBtn.text
                    font: zoomInBtn.font
                    color: zoomInBtn.hovered ? (theme ? theme.textPrimary : "#FFFFFF") : (theme ? theme.textSecondary : "#98989D")
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                background: Rectangle {
                    implicitWidth: 32
                    implicitHeight: 32
                    radius: 6
                    color: zoomInBtn.pressed
                        ? Qt.darker(theme ? theme.buttonBg : "#3A3A3C", 1.3)
                        : (zoomInBtn.hovered ? (theme ? theme.buttonHover : "#48484A") : "transparent")
                    Behavior on color { ColorAnimation { duration: 80 } }
                }

                onClicked: root.zoomIn()
            }

            ToolButton {
                id: zoomOutBtn
                text: "\u2212" // minus sign
                font.family: theme ? theme.fontMono : "SF Mono"
                font.pixelSize: 16
                font.bold: true

                contentItem: Text {
                    text: zoomOutBtn.text
                    font: zoomOutBtn.font
                    color: zoomOutBtn.hovered ? (theme ? theme.textPrimary : "#FFFFFF") : (theme ? theme.textSecondary : "#98989D")
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                background: Rectangle {
                    implicitWidth: 32
                    implicitHeight: 32
                    radius: 6
                    color: zoomOutBtn.pressed
                        ? Qt.darker(theme ? theme.buttonBg : "#3A3A3C", 1.3)
                        : (zoomOutBtn.hovered ? (theme ? theme.buttonHover : "#48484A") : "transparent")
                    Behavior on color { ColorAnimation { duration: 80 } }
                }

                onClicked: root.zoomOut()
            }

            ToolButton {
                id: zoomFitBtn
                text: "Fit"
                font.family: theme ? theme.fontText : "SF Pro Text"
                font.pixelSize: 11

                contentItem: Text {
                    text: zoomFitBtn.text
                    font: zoomFitBtn.font
                    color: zoomFitBtn.hovered ? (theme ? theme.textPrimary : "#FFFFFF") : (theme ? theme.textSecondary : "#98989D")
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                background: Rectangle {
                    implicitWidth: 40
                    implicitHeight: 32
                    radius: 6
                    color: zoomFitBtn.pressed
                        ? Qt.darker(theme ? theme.buttonBg : "#3A3A3C", 1.3)
                        : (zoomFitBtn.hovered ? (theme ? theme.buttonHover : "#48484A") : "transparent")
                    Behavior on color { ColorAnimation { duration: 80 } }
                }

                onClicked: root.zoomFit()
            }
        }

        // ── Status ──────────────────────────────────────────────────────
        Row {
            spacing: 8
            Layout.alignment: Qt.AlignVCenter

            // Status dot
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 8
                height: 8
                radius: 4
                color: {
                    if (isSolving) return theme ? theme.statusBusy : "#FF9F0A"
                    if (hasResults) return theme ? theme.statusOnline : "#32D74B"
                    return theme ? theme.statusIdle : "#8E8E93"
                }

                // Busy spinner
                RotationAnimator on rotation {
                    running: isSolving
                    from: 0
                    to: 360
                    duration: 1000
                    loops: Animation.Infinite
                }
            }

            // Status text
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.statusMessage
                font.family: theme ? theme.fontMono : "SF Mono"
                font.pixelSize: 11
                color: theme ? theme.textSecondary : "#98989D"
                elide: Text.ElideRight
                maximumLineCount: 1
            }
        }
    }

    // Progress bar (shown during solving)
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        height: 2
        color: theme ? theme.accentBlue : "#0A84FF"
        width: parent.width * progressBar.progress
        visible: isSolving
        opacity: 0.8

        property double progress: 0
    }
}
