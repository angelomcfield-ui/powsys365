import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Qt.labs.platform as Platform

import POWSYS365.Controllers 1.0

/**
 * @brief POWSYS365 Main Window - Apple-style dark mode interface
 *
 * Three-panel layout:
 *   - Left sidebar (280px): project, system summary, quick analysis
 *   - Center: toolbar + SLD canvas
 *   - Right panel (300px): properties
 *
 * Dark mode default. Glassmorphism. SF Pro typography.
 */
ApplicationWindow {
    id: mainWindow

    // ── Window config ────────────────────────────────────────────────────
    visible: true
    width: 1440
    height: 900
    minimumWidth: 1280
    minimumHeight: 720
    title: (mainController ? mainController.currentProject : "Untitled") + " - POWSYS365"
    color: theme.backgroundPrimary

    // Use system font fallbacks (SF Pro on macOS, system sans-serif elsewhere)
    font.family: "SF Pro Display"

    // ── Theme ────────────────────────────────────────────────────────────
    property var theme: (themeManager && themeManager.darkMode !== undefined)
                        ? themeManager : fallbackTheme

    // Inline fallback theme for when C++ singleton isn't registered
    property var fallbackTheme: QtObject {
        property bool darkMode: true
        property color backgroundPrimary:   "#1C1C1E"
        property color backgroundSecondary: "#2C2C2E"
        property color backgroundTertiary:  "#3A3A3C"
        property color textPrimary:     "#FFFFFF"
        property color textSecondary:   "#98989D"
        property color textTertiary:    "#636366"
        property color accentBlue:   "#0A84FF"
        property color accentGreen:  "#32D74B"
        property color accentRed:    "#FF453A"
        property color accentOrange: "#FF9F0A"
        property color accentYellow: "#FFD60A"
        property color accentTeal:   "#64D2FF"
        property color accentPurple: "#BF5AF2"
        property color accentIndigo: "#5E5CE6"
        property color accentPink:   "#FF375F"
        property color separator:       "#38383A"
        property color fillPrimary:   Qt.rgba(0.47, 0.47, 0.50, 0.14)
        property color fillSecondary: Qt.rgba(0.47, 0.47, 0.50, 0.12)
        property color buttonBg:        "#3A3A3C"
        property color buttonHover:     "#48484A"
        property color buttonPressed:   "#525254"
        property color buttonDisabled:  "#2C2C2E"
        property color buttonBorder:    "#48484A"
        property color inputBg:        "#1C1C1E"
        property color inputBorder:    "#38383A"
        property color inputFocused:   "#0A84FF"
        property color tableBg:        "#2C2C2E"
        property color tableAltRow:    "#323234"
        property color tableSelected:  Qt.rgba(0.04, 0.52, 1.0, 0.24)
        property color tableHeader:    "#3A3A3C"
        property color tableBorder:    "#38383A"
        property color panelBg:        "#2C2C2E"
        property color panelBorder:    "#38383A"
        property color panelHeader:    "#3A3A3C"
        property color toolbarBg:      Qt.rgba(0.17, 0.17, 0.18, 0.94)
        property color toolbarBorder:  "#38383A"
        property color sidebarBg:      Qt.rgba(0.12, 0.12, 0.13, 0.96)
        property color sidebarBorder:  "#38383A"
        property color sidebarItemHover:  "#3A3A3C"
        property color sidebarItemSelected: Qt.rgba(0.04, 0.52, 1.0, 0.20)
        property color glassBg:        Qt.rgba(0.16, 0.16, 0.17, 0.72)
        property color glassBorder:    Qt.rgba(1.0, 1.0, 1.0, 0.08)
        property color glassHighlight: Qt.rgba(1.0, 1.0, 1.0, 0.05)
        property color tooltipBg:      "#3A3A3C"
        property color tooltipBorder:  "#48484A"
        property color overlay:        Qt.rgba(0, 0, 0, 0.45)
        property color busPQ:          "#0A84FF"
        property color busPV:          "#32D74B"
        property color busSlack:       "#FF453A"
        property color busDead:        "#8E8E93"
        property color lineNormal:     "#32D74B"
        property color lineWarning:    "#FFD60A"
        property color lineOverload:   "#FF453A"
        property color lineOpen:       "#8E8E93"
        property color genOnline:      "#32D74B"
        property color genOffline:     "#8E8E93"
        property color genLimited:     "#FFD60A"
        property color loadActive:     "#FF9F0A"
        property color loadShed:       "#FF453A"
        property color txNormal:       "#64D2FF"
        property color txOverloaded:   "#FF453A"
        property color violation:      "#FF453A"
        property color warning:        "#FFD60A"
        property color info:           "#0A84FF"
        property color success:        "#32D74B"
        property color statusOnline:   "#32D74B"
        property color statusBusy:     "#FF9F0A"
        property color statusError:    "#FF453A"
        property color statusIdle:     "#8E8E93"
        property string fontDisplay:   "SF Pro Display"
        property string fontText:      "SF Pro Text"
        property string fontMono:      "SF Mono"
    }

    // ── C++ Controllers ─────────────────────────────────────────────────
    property var mainController: null
    property var sldController: null
    property var themeManager: null

    // ── State ────────────────────────────────────────────────────────────
    property var busList: []
    property var lineList: []
    property var genList: []
    property var loadList: []
    property bool hasResults: mainController ? mainController.hasResults : false

    // ── Connections ──────────────────────────────────────────────────────
    Connections {
        target: mainController || null
        function onBusDataReady(buses) {
            mainWindow.busList = buses
            if (sldController) {
                sldController.setBusData(buses)
                sldController.computeAutoLayout(sldCanvas.width, sldCanvas.height)
            }
        }
        function onLineDataReady(lines) {
            mainWindow.lineList = lines
            if (sldController) sldController.setLineData(lines)
        }
        function onGeneratorDataReady(generators) {
            mainWindow.genList = generators
            if (sldController) sldController.setGeneratorData(generators)
        }
        function onLoadDataReady(loads) {
            mainWindow.loadList = loads
            if (sldController) sldController.setLoadData(loads)
        }
        function onShowNotification(title, message, type) {
            notificationManager.show(title, message, type)
        }
        function onLoadFlowCompleted(success, message) {
            statusText.text = message
        }
    }

    // ── MenuBar ──────────────────────────────────────────────────────────
    Platform.MenuBar {
        Platform.Menu {
            title: "File"
            Platform.MenuItem {
                text: "New Project"
                shortcut: "Ctrl+N"
                onTriggered: if (mainController) mainController.newProject()
            }
            Platform.MenuItem {
                text: "Open Project..."
                shortcut: "Ctrl+O"
                onTriggered: openFileDialog.open()
            }
            Platform.MenuItem {
                text: "Save Project"
                shortcut: "Ctrl+S"
                onTriggered: saveFileDialog.open()
            }
            Platform.MenuSeparator {}
            Platform.MenuItem {
                text: "Import..."
                onTriggered: importMenu.open()
            }
            Platform.MenuItem {
                text: "Export..."
                onTriggered: exportMenu.open()
            }
            Platform.MenuSeparator {}
            Platform.MenuItem {
                text: "Exit"
                shortcut: "Ctrl+Q"
                onTriggered: Qt.quit()
            }
        }
        Platform.Menu {
            title: "Analysis"
            Platform.MenuItem {
                text: "Load Flow"
                shortcut: "Ctrl+R"
                onTriggered: toolbar.runLoadFlow()
            }
            Platform.MenuItem {
                text: "Short Circuit"
                shortcut: "Ctrl+Shift+S"
                onTriggered: toolbar.runShortCircuit()
            }
            Platform.MenuItem {
                text: "Transient Stability"
                shortcut: "Ctrl+Shift+T"
                onTriggered: toolbar.runStability()
            }
            Platform.MenuItem {
                text: "Optimal Power Flow"
                shortcut: "Ctrl+Shift+O"
                onTriggered: toolbar.runOPF()
            }
            Platform.MenuSeparator {}
            Platform.Menu {
                id: methodMenu
                title: "Method"
                Platform.MenuItem { text: "Newton-Raphson"; onTriggered: toolbar.methodChanged("NR") }
                Platform.MenuItem { text: "Fast Decoupled"; onTriggered: toolbar.methodChanged("FD") }
                Platform.MenuItem { text: "FD XB"; onTriggered: toolbar.methodChanged("FDXB") }
                Platform.MenuItem { text: "Gauss-Seidel"; onTriggered: toolbar.methodChanged("GS") }
            }
            Platform.MenuSeparator {}
            Platform.MenuItem {
                text: "Cancel"
                onTriggered: if (mainController) mainController.cancelOperation()
            }
        }
        Platform.Menu {
            title: "View"
            Platform.MenuItem {
                text: "Zoom In"
                shortcut: "Ctrl++"
                onTriggered: sldCanvas.zoomIn()
            }
            Platform.MenuItem {
                text: "Zoom Out"
                shortcut: "Ctrl+-"
                onTriggered: sldCanvas.zoomOut()
            }
            Platform.MenuItem {
                text: "Fit to View"
                shortcut: "Ctrl+0"
                onTriggered: sldCanvas.zoomFit()
            }
            Platform.MenuSeparator {}
            Platform.MenuItem {
                text: "Toggle Theme"
                shortcut: "Ctrl+Shift+D"
                onTriggered: if (themeManager) themeManager.toggleTheme()
            }
            Platform.MenuItem {
                text: "Animate Flow"
                checkable: true
                checked: sldCanvas.showFlowAnimation
                onTriggered: sldCanvas.showFlowAnimation = !sldCanvas.showFlowAnimation
            }
        }
        Platform.Menu {
            title: "Tools"
            Platform.MenuItem {
                text: "Auto Layout"
                onTriggered: {
                    if (sldController && busList.length > 0)
                        sldController.computeAutoLayout(sldCanvas.width, sldCanvas.height)
                }
            }
            Platform.MenuItem {
                text: "Radial Layout"
                onTriggered: {
                    if (sldController) sldController.applyRadialLayout(sldCanvas.width/2, sldCanvas.height/2)
                }
            }
            Platform.MenuItem {
                text: "Ring Layout"
                onTriggered: {
                    if (sldController) sldController.applyRingLayout(sldCanvas.width/2, sldCanvas.height/2)
                }
            }
            Platform.MenuSeparator {}
            Platform.MenuItem {
                text: "Generate Report..."
                onTriggered: if (mainController) mainController.requestReport("full")
            }
        }
        Platform.Menu {
            title: "Help"
            Platform.MenuItem {
                text: "Documentation"
                shortcut: "F1"
                onTriggered: notificationManager.show("Help", "Documentation will open in browser", 0)
            }
            Platform.MenuItem {
                text: "About POWSYS365"
                onTriggered: aboutDialog.open()
            }
        }
    }

    // ── Main Layout ──────────────────────────────────────────────────────
    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ═════════════════════════════════════════════════════════════════
        // LEFT SIDEBAR (280px)
        // ═════════════════════════════════════════════════════════════════
        Rectangle {
            Layout.preferredWidth: 280
            Layout.fillHeight: true
            color: theme.sidebarBg

            // Right border
            Rectangle {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 1
                color: theme.sidebarBorder
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // ── Logo / Title ──────────────────────────────────────
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 60
                    color: "transparent"

                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 20
                        spacing: 10

                        // App icon (colored circle)
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 36
                            height: 36
                            radius: 10
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: "#0A84FF" }
                                GradientStop { position: 1.0; color: "#5E5CE6" }
                            }

                            Text {
                                anchors.centerIn: parent
                                text: "P"
                                font.family: theme.fontDisplay
                                font.pixelSize: 18
                                font.bold: true
                                color: "#FFFFFF"
                            }
                        }

                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2

                            Text {
                                text: "POWSYS365"
                                font.family: theme.fontDisplay
                                font.pixelSize: 16
                                font.weight: Font.Bold
                                color: theme.textPrimary
                            }

                            Text {
                                text: "Power System Analysis"
                                font.family: theme.fontText
                                font.pixelSize: 10
                                color: theme.textSecondary
                            }
                        }
                    }

                    // Theme toggle button
                    Button {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        width: 32
                        height: 32

                        contentItem: Text {
                            text: themeManager && themeManager.darkMode ? "\u2600" : "\u263E"
                            font.pixelSize: 14
                            color: parent.hovered ? theme.textPrimary : theme.textSecondary
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        background: Rectangle {
                            radius: 6
                            color: parent.hovered ? theme.buttonHover : "transparent"
                        }

                        onClicked: if (themeManager) themeManager.toggleTheme()
                    }
                }

                // Divider
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: theme.sidebarBorder
                }

                // ── Project Selector ──────────────────────────────────
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 56
                    color: "transparent"

                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 16
                        anchors.right: parent.right
                        anchors.rightMargin: 16
                        spacing: 8

                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2

                            Text {
                                text: "Project"
                                font.family: theme.fontText
                                font.pixelSize: 10
                                color: theme.textSecondary
                            }

                            Text {
                                text: mainController ? mainController.currentProject : "Untitled"
                                font.family: theme.fontText
                                font.pixelSize: 13
                                font.weight: Font.Medium
                                color: theme.textPrimary
                                elide: Text.ElideRight
                                maximumLineCount: 1
                                width: 220
                            }
                        }
                    }
                }

                // Divider
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: theme.sidebarBorder
                }

                // ── Quick Analysis Buttons ────────────────────────────
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 160
                    color: "transparent"

                    Column {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 6

                        Text {
                            text: "Quick Analysis"
                            font.family: theme.fontText
                            font.pixelSize: 11
                            font.weight: Font.SemiBold
                            color: theme.textSecondary
                        }

                        // NR button
                        QuickActionButton {
                            theme: mainWindow.theme
                            text: "Load Flow (NR)"
                            iconColor: theme.accentBlue
                            shortcut: "Ctrl+R"
                            enabled: !mainController.isSolving
                            onClicked: {
                                toolbar.methodChanged("NR")
                                toolbar.runLoadFlow()
                            }
                        }

                        // Short Circuit button
                        QuickActionButton {
                            theme: mainWindow.theme
                            text: "Short Circuit"
                            iconColor: theme.accentRed
                            shortcut: "Ctrl+Shift+S"
                            enabled: !mainController.isSolving
                            onClicked: toolbar.runShortCircuit()
                        }

                        // Stability button
                        QuickActionButton {
                            theme: mainWindow.theme
                            text: "Stability"
                            iconColor: theme.accentPurple
                            shortcut: "Ctrl+Shift+T"
                            enabled: !mainController.isSolving
                            onClicked: toolbar.runStability()
                        }
                    }
                }

                // Divider
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: theme.sidebarBorder
                }

                // ── System Stats ──────────────────────────────────────
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 120
                    color: "transparent"

                    Column {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 8

                        Text {
                            text: "System"
                            font.family: theme.fontText
                            font.pixelSize: 11
                            font.weight: Font.SemiBold
                            color: theme.textSecondary
                        }

                        StatRow {
                            theme: mainWindow.theme
                            label: "Buses"
                            value: busList.length
                        }
                        StatRow {
                            theme: mainWindow.theme
                            label: "Lines"
                            value: lineList.length
                        }
                        StatRow {
                            theme: mainWindow.theme
                            label: "Generators"
                            value: genList.length
                            valueColor: genList.length > 0 ? theme.accentGreen : theme.textSecondary
                        }
                        StatRow {
                            theme: mainWindow.theme
                            label: "Loads"
                            value: loadList.length
                            valueColor: loadList.length > 0 ? theme.accentOrange : theme.textSecondary
                        }
                    }
                }

                Item { Layout.fillHeight: true } // Spacer

                // ── Solver Status ─────────────────────────────────────
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    color: theme.sidebarItemSelected
                    visible: mainController ? mainController.hasResults : false

                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 16
                        spacing: 8

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 8
                            height: 8
                            radius: 4
                            color: theme.accentGreen
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: mainController
                                ? "Converged: " + mainController.iterationCount + " it, "
                                  + Number(mainController.convergenceError).toExponential(1) + ", "
                                  + Number(mainController.solveTimeMs).toFixed(0) + " ms"
                                : ""
                            font.family: theme.fontMono
                            font.pixelSize: 10
                            color: theme.textSecondary
                        }
                    }
                }

                // ── Footer ────────────────────────────────────────────
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    color: "transparent"

                    Text {
                        anchors.centerIn: parent
                        text: "POWSYS365 v1.0"
                        font.family: theme.fontText
                        font.pixelSize: 10
                        color: theme.textTertiary
                    }
                }
            }
        }

        // ═════════════════════════════════════════════════════════════════
        // CENTER AREA
        // ═════════════════════════════════════════════════════════════════
        ColumnLayout {
            spacing: 0
            Layout.fillWidth: true
            Layout.fillHeight: true

            // ── Toolbar ───────────────────────────────────────────────
            Toolbar {
                id: toolbar
                Layout.fillWidth: true
                theme: mainWindow.theme
                isSolving: mainController ? mainController.isSolving : false
                statusMessage: mainController ? mainController.statusMessage : "Ready"
                currentMethod: mainController ? mainController.currentMethod : "NR"
                hasResults: mainWindow.hasResults

                onRunLoadFlow: {
                    if (mainController) mainController.solveLoadFlow(toolbar.currentMethod)
                }
                onRunShortCircuit: {
                    if (mainController) mainController.solveShortCircuit("3PH")
                }
                onRunStability: {
                    if (mainController) mainController.solveStability("euler")
                }
                onRunOPF: {
                    if (mainController) mainController.solveOPF("cost")
                }
                onZoomIn: sldCanvas.zoomIn()
                onZoomOut: sldCanvas.zoomOut()
                onZoomFit: sldCanvas.zoomFit()
                onMethodChanged: function(method) {
                    if (mainController) mainController.currentMethod = method
                }
            }

            // ── SLD Canvas ────────────────────────────────────────────
            SLDCanvas {
                id: sldCanvas
                Layout.fillWidth: true
                Layout.fillHeight: true
                theme: mainWindow.theme
                controller: sldController
                busData: mainWindow.busList
                lineData: mainWindow.lineList
                generatorData: mainWindow.genList
                loadData: mainWindow.loadList
                transformerData: {
                    if (sldController) return sldController.transformerNodes
                    return []
                }
                showFlowAnimation: false

                onBusClicked: function(busId) {
                    if (sldController) sldController.selectBus(busId)
                }
                onLineClicked: function(lineId) {
                    if (sldController) sldController.selectLine(lineId)
                }
            }
        }

        // ═════════════════════════════════════════════════════════════════
        // RIGHT PROPERTIES PANEL (300px)
        // ═════════════════════════════════════════════════════════════════
        PropertiesPanel {
            theme: mainWindow.theme
            busData: mainWindow.busList
            lineData: mainWindow.lineList
            generatorData: mainWindow.genList
            loadData: mainWindow.loadList
            hasResults: mainWindow.hasResults
        }
    }

    // ── Notification Manager ─────────────────────────────────────────────
    Item {
        id: notificationManager
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 20
        width: 320
        height: parent.height * 0.5
        z: 100

        function show(title, message, type) {
            notificationModel.insert(0, { title: title, message: message, type: type, time: Date.now() })
            if (notificationModel.count > 5) notificationModel.remove(notificationModel.count - 1)
        }

        ListModel {
            id: notificationModel
        }

        Column {
            anchors.fill: parent
            spacing: 8

            Repeater {
                model: notificationModel

                Rectangle {
                    width: parent.width
                    height: notifCol.implicitHeight + 20
                    radius: 10
                    color: {
                        switch (type) {
                        case 1: return Qt.rgba(1, 0.27, 0.23, 0.9)     // error
                        case 2: return Qt.rgba(1, 0.84, 0.04, 0.9)     // warning
                        case 0: default: return Qt.rgba(0.17, 0.17, 0.18, 0.92)
                        }
                    }
                    border.width: 1
                    border.color: type === 1 ? "#FF453A" : (type === 2 ? "#FFD60A" : "#48484A")

                    // Glassmorphism
                    Rectangle {
                        anchors.fill: parent
                        radius: parent.radius
                        color: type === 0 ? theme.glassBg : "transparent"
                        border.width: 1
                        border.color: theme.glassBorder
                    }

                    Column {
                        id: notifCol
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 4

                        Row {
                            spacing: 8
                            width: parent.width

                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 6
                                height: 6
                                radius: 3
                                color: type === 1 ? "#FF453A" : (type === 2 ? "#FFD60A" : "#32D74B")
                            }

                            Text {
                                text: title
                                font.family: theme.fontText
                                font.pixelSize: 13
                                font.weight: Font.Bold
                                color: "#FFFFFF"
                            }

                            Item { width: parent.width - titleText.implicitWidth - 20; height: 1 }
                        }

                        Text {
                            text: message
                            font.family: theme.fontText
                            font.pixelSize: 12
                            color: "#C7C7CC"
                            wrapMode: Text.WordWrap
                            width: parent.width
                        }
                    }

                    // Auto-dismiss timer
                    Timer {
                        interval: 5000
                        running: true
                        onTriggered: notificationModel.remove(index)
                    }

                    // Entrance animation
                    opacity: 0
                    x: 30
                    NumberAnimation on opacity { to: 1; duration: 300; easing.type: Easing.OutCubic }
                    NumberAnimation on x { to: 0; duration: 300; easing.type: Easing.OutCubic }
                }
            }
        }
    }

    // ── Status Bar ───────────────────────────────────────────────────────
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 24
        color: theme.backgroundSecondary

        // Top border
        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: theme.separator
        }

        Row {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 16
            spacing: 16

            Text {
                id: statusText
                text: mainController ? mainController.statusMessage : "Ready"
                font.family: theme.fontMono
                font.pixelSize: 10
                color: theme.textTertiary
            }

            Text {
                text: "Method: " + (mainController ? mainController.currentMethod : "NR")
                font.family: theme.fontMono
                font.pixelSize: 10
                color: theme.textTertiary
                visible: mainController ? mainController.hasResults : false
            }

            Text {
                text: "Buses: " + busList.length
                font.family: theme.fontMono
                font.pixelSize: 10
                color: theme.textTertiary
            }

            Text {
                text: "Lines: " + lineList.length
                font.family: theme.fontMono
                font.pixelSize: 10
                color: theme.textTertiary
            }
        }

        Row {
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            anchors.rightMargin: 16
            spacing: 12

            Text {
                text: "POWSYS365 v1.0"
                font.family: theme.fontText
                font.pixelSize: 10
                color: theme.textTertiary
            }
        }
    }

    // ── Dialogs ──────────────────────────────────────────────────────────
    FileDialog {
        id: openFileDialog
        title: "Open Project"
        nameFilters: ["POWSYS files (*.powsys)", "MATPOWER files (*.m)", "PSS/E files (*.raw)", "All files (*)"]
        onAccepted: {
            if (mainController) mainController.openProject(selectedFile.toString())
        }
    }

    FileDialog {
        id: saveFileDialog
        title: "Save Project"
        nameFilters: ["POWSYS files (*.powsys)"]
        fileMode: FileDialog.SaveFile
        onAccepted: {
            if (mainController) mainController.saveProject(selectedFile.toString())
        }
    }

    Menu {
        id: importMenu
        title: "Import"
        MenuItem { text: "MATPOWER (.m)"; onTriggered: notificationManager.show("Import", "MATPOWER import not yet implemented", 2) }
        MenuItem { text: "PSS/E (.raw)"; onTriggered: notificationManager.show("Import", "PSS/E import not yet implemented", 2) }
        MenuItem { text: "IEEE CDF"; onTriggered: notificationManager.show("Import", "IEEE CDF import not yet implemented", 2) }
    }

    Menu {
        id: exportMenu
        title: "Export"
        MenuItem { text: "MATPOWER (.m)"; onTriggered: notificationManager.show("Export", "MATPOWER export not yet implemented", 2) }
        MenuItem { text: "CSV"; onTriggered: notificationManager.show("Export", "CSV export not yet implemented", 2) }
        MenuItem { text: "PDF Report"; onTriggered: notificationManager.show("Export", "PDF export not yet implemented", 2) }
    }

    Dialog {
        id: aboutDialog
        title: "About POWSYS365"
        modal: true
        anchors.centerIn: parent
        width: 400
        height: 220

        contentItem: Column {
            spacing: 12
            anchors.fill: parent
            anchors.margins: 20

            Row {
                spacing: 12
                anchors.horizontalCenter: parent.horizontalCenter

                Rectangle {
                    width: 48
                    height: 48
                    radius: 12
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#0A84FF" }
                        GradientStop { position: 1.0; color: "#5E5CE6" }
                    }
                    Text {
                        anchors.centerIn: parent
                        text: "P"
                        font.family: theme.fontDisplay
                        font.pixelSize: 24
                        font.bold: true
                        color: "#FFFFFF"
                    }
                }

                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2

                    Text {
                        text: "POWSYS365"
                        font.family: theme.fontDisplay
                        font.pixelSize: 18
                        font.weight: Font.Bold
                        color: theme.textPrimary
                    }

                    Text {
                        text: "Power System Analysis Suite"
                        font.family: theme.fontText
                        font.pixelSize: 12
                        color: theme.textSecondary
                    }
                }
            }

            Rectangle {
                width: parent.width
                height: 1
                color: theme.separator
            }

            Text {
                text: "A modern power system analysis tool for load flow, short circuit, transient stability, and optimal power flow studies."
                font.family: theme.fontText
                font.pixelSize: 12
                color: theme.textSecondary
                wrapMode: Text.WordWrap
                width: parent.width
            }

            Text {
                text: "Built with Qt 6 + QML | C++ Backend"
                font.family: theme.fontMono
                font.pixelSize: 11
                color: theme.textTertiary
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "\u00A9 2025 POWSYS365 Team"
                font.family: theme.fontText
                font.pixelSize: 11
                color: theme.textTertiary
            }
        }

        standardButtons: Dialog.Ok
    }

    // ── Keyboard shortcuts ───────────────────────────────────────────────
    Shortcut {
        sequence: "Ctrl+R"
        onActivated: toolbar.runLoadFlow()
    }
    Shortcut {
        sequence: "Ctrl++"
        onActivated: sldCanvas.zoomIn()
    }
    Shortcut {
        sequence: "Ctrl+-"
        onActivated: sldCanvas.zoomOut()
    }
    Shortcut {
        sequence: "Ctrl+0"
        onActivated: sldCanvas.zoomFit()
    }
    Shortcut {
        sequence: "Ctrl+Shift+D"
        onActivated: if (themeManager) themeManager.toggleTheme()
    }
    Shortcut {
        sequence: "Escape"
        onActivated: {
            if (sldController) sldController.clearSelection()
        }
    }

    // ── Component on completed ───────────────────────────────────────────
    Component.onCompleted: {
        // Find C++ controllers registered as context properties
        if (!mainController && typeof mainWindowController !== "undefined") {
            mainController = mainWindowController
        }
        if (!sldController && typeof sldSceneController !== "undefined") {
            sldController = sldSceneController
        }
        if (!themeManager && typeof themeMgr !== "undefined") {
            themeManager = themeMgr
        }

        // Auto-load demo data
        if (mainController) {
            mainController.openProject("IEEE 14-Bus")
        }

        console.log("POWSYS365 UI loaded. Controllers:",
                    mainController ? "Main OK" : "Main MISSING",
                    sldController ? "SLD OK" : "SLD MISSING",
                    themeManager ? "Theme OK" : "Theme MISSING")
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Inline Components
// ═══════════════════════════════════════════════════════════════════════════

component QuickActionButton: Rectangle {
    property var theme: null
    property string text: ""
    property color iconColor: "#0A84FF"
    property string shortcut: ""
    property bool enabled: true
    signal clicked()

    width: parent.width
    height: 34
    radius: 8
    color: enabled ? (mouseArea.containsMouse ? theme.sidebarItemHover : "transparent") : "transparent"
    opacity: enabled ? 1 : 0.4

    Row {
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 8
        spacing: 10

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 8
            height: 8
            radius: 4
            color: iconColor
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: text
            font.family: theme ? theme.fontText : "SF Pro Text"
            font.pixelSize: 12
            color: theme ? theme.textPrimary : "#FFFFFF"
        }

        Item { width: parent.parent.width - 100; height: 1 }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: shortcut
            font.family: theme ? theme.fontMono : "SF Mono"
            font.pixelSize: 10
            color: theme ? theme.textTertiary : "#636366"
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        enabled: parent.enabled
        onClicked: parent.clicked()
        cursorShape: Qt.PointingHandCursor
    }
}

component StatRow: Row {
    property var theme: null
    property string label: ""
    property var value: 0
    property color valueColor: null

    spacing: 8

    Text {
        text: label + ":"
        font.family: theme ? theme.fontText : "SF Pro Text"
        font.pixelSize: 12
        color: theme ? theme.textSecondary : "#98989D"
        width: 100
    }

    Text {
        text: value.toString()
        font.family: theme ? theme.fontMono : "SF Mono"
        font.pixelSize: 12
        font.weight: Font.Medium
        color: valueColor || (theme ? theme.textPrimary : "#FFFFFF")
    }
}
