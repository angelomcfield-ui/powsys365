import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/**
 * @brief PropertiesPanel - Right-side properties panel
 *
 * Tabs: Resumen, Barras, Lineas, Generadores, Cargas
 * Tables with sortable/filterable data, violation highlighting.
 */
Rectangle {
    id: root

    // ── Properties ───────────────────────────────────────────────────────
    property var    theme: null
    property var    busData: []
    property var    lineData: []
    property var    generatorData: []
    property var    loadData: []
    property bool   hasResults: false

    width: 300
    color: theme ? theme.sidebarBg : Qt.rgba(0.12, 0.12, 0.13, 0.96)
    border.color: theme ? theme.sidebarBorder : "#38383A"
    border.width: 0

    // Left separator
    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: theme ? theme.separator : "#38383A"
    }

    // ── Tab Bar ──────────────────────────────────────────────────────────
    TabBar {
        id: tabBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 38
        background: Rectangle {
            color: "transparent"
        }

        // Custom tab button component
        Component {
            id: tabButtonComponent
            TabButton {
                id: tabBtn
                height: tabBar.height

                contentItem: Text {
                    text: tabBtn.text
                    font.family: theme ? theme.fontText : "SF Pro Text"
                    font.pixelSize: 11
                    font.weight: tabBtn.checked ? Font.SemiBold : Font.Medium
                    color: tabBtn.checked
                        ? (theme ? theme.accentBlue : "#0A84FF")
                        : (tabBtn.hovered ? (theme ? theme.textPrimary : "#FFFFFF") : (theme ? theme.textSecondary : "#98989D"))
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                background: Rectangle {
                    color: "transparent"
                    border.color: tabBtn.checked ? (theme ? theme.accentBlue : "#0A84FF") : "transparent"
                    border.width: tabBtn.checked ? 0 : 0

                    // Bottom indicator
                    Rectangle {
                        anchors.bottom: parent.bottom
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: tabBtn.checked ? parent.width * 0.6 : 0
                        height: 2
                        radius: 1
                        color: theme ? theme.accentBlue : "#0A84FF"
                        Behavior on width { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
                    }
                }
            }
        }

        TabButton {
            text: "Summary"
            implicitWidth: Math.max(60, contentItem.implicitWidth + 16)
            Component.onCompleted: {
                contentItem.font.family = theme ? theme.fontText : "SF Pro Text"
            }
        }
        TabButton {
            text: "Buses"
            implicitWidth: Math.max(50, contentItem.implicitWidth + 16)
        }
        TabButton {
            text: "Lines"
            implicitWidth: Math.max(50, contentItem.implicitWidth + 16)
        }
        TabButton {
            text: "Gen"
            implicitWidth: Math.max(45, contentItem.implicitWidth + 16)
        }
        TabButton {
            text: "Load"
            implicitWidth: Math.max(50, contentItem.implicitWidth + 16)
        }
    }

    // ── Content Stack ────────────────────────────────────────────────────
    StackLayout {
        anchors.top: tabBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        currentIndex: tabBar.currentIndex

        // ── Tab 0: Summary ─────────────────────────────────────────────
        SummaryView {
            theme: root.theme
            busData: root.busData
            lineData: root.lineData
            generatorData: root.generatorData
            loadData: root.loadData
            hasResults: root.hasResults
        }

        // ── Tab 1: Buses ───────────────────────────────────────────────
        BusTableView {
            theme: root.theme
            model: root.busData
            hasResults: root.hasResults
        }

        // ── Tab 2: Lines ───────────────────────────────────────────────
        LineTableView {
            theme: root.theme
            model: root.lineData
            hasResults: root.hasResults
        }

        // ── Tab 3: Generators ──────────────────────────────────────────
        GenTableView {
            theme: root.theme
            model: root.generatorData
            hasResults: root.hasResults
        }

        // ── Tab 4: Loads ───────────────────────────────────────────────
        LoadTableView {
            theme: root.theme
            model: root.loadData
            hasResults: root.hasResults
        }
    }

    // ── Summary View Component ──────────────────────────────────────────
    Component {
        id: summaryViewComp
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Summary View
// ═══════════════════════════════════════════════════════════════════════════

component SummaryView: ScrollView {
    property var theme: null
    property var busData: []
    property var lineData: []
    property var generatorData: []
    property var loadData: []
    property bool hasResults: false

    clip: true
    ScrollBar.vertical.policy: ScrollBar.AlwaysOff

    Column {
        width: parent.width
        spacing: 0
        padding: 16

        // System Overview Card
        SummaryCard {
            theme: summaryView.theme
            title: "System Overview"
            width: parent.width - 32

            Column {
                spacing: 8
                SummaryRow { label: "Buses"; value: summaryView.busData.length; theme: summaryView.theme }
                SummaryRow { label: "Lines"; value: summaryView.lineData.length; theme: summaryView.theme }
                SummaryRow { label: "Generators"; value: summaryView.generatorData.length; theme: summaryView.theme }
                SummaryRow { label: "Loads"; value: summaryView.loadData.length; theme: summaryView.theme }
            }
        }

        // Generation Card
        SummaryCard {
            theme: summaryView.theme
            title: "Generation"
            width: parent.width - 32
            visible: summaryView.hasResults

            Column {
                spacing: 8
                SummaryRow {
                    label: "Total Pgen"
                    value: {
                        var sum = 0
                        for (var i = 0; i < summaryView.generatorData.length; i++)
                            sum += (summaryView.generatorData[i].pGen || 0)
                        return sum.toFixed(1) + " MW"
                    }
                    theme: summaryView.theme
                }
                SummaryRow {
                    label: "Total Qgen"
                    value: {
                        var sum = 0
                        for (var i = 0; i < summaryView.generatorData.length; i++)
                            sum += (summaryView.generatorData[i].qGen || 0)
                        return sum.toFixed(1) + " MVAr"
                    }
                    theme: summaryView.theme
                }
            }
        }

        // Load Card
        SummaryCard {
            theme: summaryView.theme
            title: "Demand"
            width: parent.width - 32
            visible: summaryView.hasResults

            Column {
                spacing: 8
                SummaryRow {
                    label: "Total Pload"
                    value: {
                        var sum = 0
                        for (var i = 0; i < summaryView.loadData.length; i++)
                            sum += (summaryView.loadData[i].pLoad || 0)
                        return sum.toFixed(1) + " MW"
                    }
                    theme: summaryView.theme
                }
                SummaryRow {
                    label: "Total Qload"
                    value: {
                        var sum = 0
                        for (var i = 0; i < summaryView.loadData.length; i++)
                            sum += (summaryView.loadData[i].qLoad || 0)
                        return sum.toFixed(1) + " MVAr"
                    }
                    theme: summaryView.theme
                }
            }
        }

        // Losses estimate
        SummaryCard {
            theme: summaryView.theme
            title: "Losses"
            width: parent.width - 32
            visible: summaryView.hasResults

            Column {
                spacing: 8
                SummaryRow {
                    label: "P Losses"
                    value: {
                        var gen = 0, load = 0
                        for (var i = 0; i < summaryView.generatorData.length; i++)
                            gen += (summaryView.generatorData[i].pGen || 0)
                        for (var i = 0; i < summaryView.loadData.length; i++)
                            load += (summaryView.loadData[i].pLoad || 0)
                        return (gen - load).toFixed(1) + " MW"
                    }
                    valueColor: summaryView.theme ? summaryView.theme.accentOrange : "#FF9F0A"
                    theme: summaryView.theme
                }
            }
        }
    }
}

component SummaryCard: Rectangle {
    property var theme: null
    property string title: ""

    height: contentCol.implicitHeight + 28
    radius: 10
    color: theme ? theme.panelBg : "#2C2C2E"
    border.width: 1
    border.color: theme ? theme.panelBorder : "#38383A"

    Column {
        id: contentCol
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        Text {
            text: parent.parent.title
            font.family: theme ? theme.fontText : "SF Pro Text"
            font.pixelSize: 12
            font.weight: Font.SemiBold
            color: theme ? theme.textPrimary : "#FFFFFF"
        }

        // Child content
        Column {
            width: parent.width
            spacing: 6
            // Content injected by parent
        }
    }
}

component SummaryRow: Row {
    property string label: ""
    property var value: ""
    property var valueColor: null
    property var theme: null

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

// ═══════════════════════════════════════════════════════════════════════════
// Bus Table View
// ═══════════════════════════════════════════════════════════════════════════

component BusTableView: ScrollView {
    property var theme: null
    property var model: []
    property bool hasResults: false

    clip: true

    Column {
        width: parent.width
        spacing: 0

        // Header
        TableHeader {
            theme: busTableView.theme
            columns: ["ID", "Name", "Type", "Vm (pu)", "Va (deg)"]
            widths: [36, 80, 44, 68, 64]
        }

        // Rows
        Repeater {
            model: busTableView.model

            Rectangle {
                width: parent.width
                height: 32
                color: index % 2 === 0
                    ? (busTableView.theme ? busTableView.theme.tableBg : "#2C2C2E")
                    : (busTableView.theme ? busTableView.theme.tableAltRow : "#323234")

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    spacing: 0

                    TableCell {
                        text: modelData.id || ""
                        width: 36
                        fontSize: 11
                        theme: busTableView.theme
                    }
                    TableCell {
                        text: modelData.name || ""
                        width: 80
                        fontSize: 11
                        theme: busTableView.theme
                    }
                    TableCell {
                        text: modelData.type || ""
                        width: 44
                        fontSize: 10
                        color: {
                            switch (modelData.type) {
                            case "Slack": return busTableView.theme ? busTableView.theme.busSlack : "#FF453A"
                            case "PV":    return busTableView.theme ? busTableView.theme.busPV    : "#32D74B"
                            case "PQ":    return busTableView.theme ? busTableView.theme.busPQ    : "#0A84FF"
                            default:      return busTableView.theme ? busTableView.theme.busDead  : "#8E8E93"
                            }
                        }
                        fontSize: 10
                        theme: busTableView.theme
                    }
                    TableCell {
                        text: busTableView.hasResults ? Number(modelData.vm).toFixed(4) : "-"
                        width: 68
                        fontSize: 11
                        color: modelData.hasViolation
                            ? (busTableView.theme ? busTableView.theme.violation : "#FF453A")
                            : (busTableView.theme ? busTableView.theme.textPrimary : "#FFFFFF")
                        fontBold: modelData.hasViolation
                        theme: busTableView.theme
                    }
                    TableCell {
                        text: busTableView.hasResults ? Number(modelData.va).toFixed(2) : "-"
                        width: 64
                        fontSize: 11
                        theme: busTableView.theme
                    }
                }

                // Bottom border
                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: busTableView.theme ? busTableView.theme.tableBorder : "#38383A"
                }
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Line Table View
// ═══════════════════════════════════════════════════════════════════════════

component LineTableView: ScrollView {
    property var theme: null
    property var model: []
    property bool hasResults: false

    clip: true

    Column {
        width: parent.width
        spacing: 0

        TableHeader {
            theme: lineTableView.theme
            columns: ["ID", "From", "To", "P (MW)", "Load%"]
            widths: [32, 36, 36, 64, 56]
        }

        Repeater {
            model: lineTableView.model

            Rectangle {
                width: parent.width
                height: 32
                color: index % 2 === 0
                    ? (lineTableView.theme ? lineTableView.theme.tableBg : "#2C2C2E")
                    : (lineTableView.theme ? lineTableView.theme.tableAltRow : "#323234")

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    spacing: 0

                    TableCell { text: modelData.id || ""; width: 32; fontSize: 11; theme: lineTableView.theme }
                    TableCell { text: modelData.fromBus || ""; width: 36; fontSize: 11; theme: lineTableView.theme }
                    TableCell { text: modelData.toBus || ""; width: 36; fontSize: 11; theme: lineTableView.theme }
                    TableCell {
                        text: lineTableView.hasResults ? Number(modelData.pFlow).toFixed(1) : "-"
                        width: 64; fontSize: 11; theme: lineTableView.theme
                    }
                    TableCell {
                        text: lineTableView.hasResults ? Number(modelData.loading).toFixed(0) + "%" : "-"
                        width: 56; fontSize: 11
                        color: {
                            var l = modelData.loading || 0
                            if (l > 100) return lineTableView.theme ? lineTableView.theme.lineOverload : "#FF453A"
                            if (l > 80)  return lineTableView.theme ? lineTableView.theme.lineWarning : "#FFD60A"
                            return lineTableView.theme ? lineTableView.theme.lineNormal : "#32D74B"
                        }
                        fontBold: (modelData.loading || 0) > 100
                        theme: lineTableView.theme
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: lineTableView.theme ? lineTableView.theme.tableBorder : "#38383A"
                }
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Generator Table View
// ═══════════════════════════════════════════════════════════════════════════

component GenTableView: ScrollView {
    property var theme: null
    property var model: []
    property bool hasResults: false

    clip: true

    Column {
        width: parent.width
        spacing: 0

        TableHeader {
            theme: genTableView.theme
            columns: ["ID", "Bus", "Pgen", "Qgen", "Status"]
            widths: [32, 36, 56, 56, 60]
        }

        Repeater {
            model: genTableView.model

            Rectangle {
                width: parent.width
                height: 32
                color: index % 2 === 0
                    ? (genTableView.theme ? genTableView.theme.tableBg : "#2C2C2E")
                    : (genTableView.theme ? genTableView.theme.tableAltRow : "#323234")

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    spacing: 0

                    TableCell { text: modelData.id || ""; width: 32; fontSize: 11; theme: genTableView.theme }
                    TableCell { text: modelData.busId || ""; width: 36; fontSize: 11; theme: genTableView.theme }
                    TableCell {
                        text: genTableView.hasResults ? Number(modelData.pGen).toFixed(1) : "-"
                        width: 56; fontSize: 11; theme: genTableView.theme
                    }
                    TableCell {
                        text: genTableView.hasResults ? Number(modelData.qGen).toFixed(1) : "-"
                        width: 56; fontSize: 11; theme: genTableView.theme
                    }
                    TableCell {
                        text: modelData.status || ""
                        width: 60; fontSize: 10
                        color: {
                            switch (modelData.status) {
                            case "Online":  return genTableView.theme ? genTableView.theme.genOnline  : "#32D74B"
                            case "Limited": return genTableView.theme ? genTableView.theme.genLimited : "#FFD60A"
                            default:        return genTableView.theme ? genTableView.theme.genOffline : "#8E8E93"
                            }
                        }
                        theme: genTableView.theme
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: genTableView.theme ? genTableView.theme.tableBorder : "#38383A"
                }
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Load Table View
// ═══════════════════════════════════════════════════════════════════════════

component LoadTableView: ScrollView {
    property var theme: null
    property var model: []
    property bool hasResults: false

    clip: true

    Column {
        width: parent.width
        spacing: 0

        TableHeader {
            theme: loadTableView.theme
            columns: ["ID", "Bus", "Pload", "Qload", "Status"]
            widths: [32, 36, 56, 56, 52]
        }

        Repeater {
            model: loadTableView.model

            Rectangle {
                width: parent.width
                height: 32
                color: index % 2 === 0
                    ? (loadTableView.theme ? loadTableView.theme.tableBg : "#2C2C2E")
                    : (loadTableView.theme ? loadTableView.theme.tableAltRow : "#323234")

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    spacing: 0

                    TableCell { text: modelData.id || ""; width: 32; fontSize: 11; theme: loadTableView.theme }
                    TableCell { text: modelData.busId || ""; width: 36; fontSize: 11; theme: loadTableView.theme }
                    TableCell {
                        text: loadTableView.hasResults ? Number(modelData.pLoad).toFixed(1) : "-"
                        width: 56; fontSize: 11; theme: loadTableView.theme
                    }
                    TableCell {
                        text: loadTableView.hasResults ? Number(modelData.qLoad).toFixed(1) : "-"
                        width: 56; fontSize: 11; theme: loadTableView.theme
                    }
                    TableCell {
                        text: modelData.status || ""
                        width: 52; fontSize: 10
                        color: {
                            switch (modelData.status) {
                            case "Active": return loadTableView.theme ? loadTableView.theme.loadActive : "#FF9F0A"
                            case "Shed":   return loadTableView.theme ? loadTableView.theme.loadShed   : "#FF453A"
                            default:       return loadTableView.theme ? loadTableView.theme.busDead    : "#8E8E93"
                            }
                        }
                        theme: loadTableView.theme
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: loadTableView.theme ? loadTableView.theme.tableBorder : "#38383A"
                }
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Shared Table Components
// ═══════════════════════════════════════════════════════════════════════════

component TableHeader: Rectangle {
    property var theme: null
    property var columns: []
    property var widths: []

    width: parent.width
    height: 28
    color: theme ? theme.tableHeader : "#3A3A3C"

    Row {
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 8
        spacing: 0

        Repeater {
            model: columns.length
            Text {
                text: columns[index]
                width: widths[index] || 60
                font.family: theme ? theme.fontText : "SF Pro Text"
                font.pixelSize: 10
                font.weight: Font.Bold
                color: theme ? theme.textSecondary : "#98989D"
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
}

component TableCell: Text {
    property var theme: null
    property int fontSize: 11
    property bool fontBold: false

    font.family: theme ? theme.fontMono : "SF Mono"
    font.pixelSize: fontSize
    font.bold: fontBold
    color: theme ? theme.textPrimary : "#FFFFFF"
    verticalAlignment: Text.AlignVCenter
    elide: Text.ElideRight
}
