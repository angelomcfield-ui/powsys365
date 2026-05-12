import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

ScrollView {
    id: propertiesPanel

    ColumnLayout {
        width: parent.width
        spacing: 10

        GroupBox {
            title: "Selected Component"
            Layout.fillWidth: true

            ColumnLayout {
                width: parent.width

                Label { text: "Type: Bus" }
                Label { text: "Number: 1" }
                Label { text: "Name: Bus 1" }

                TextField {
                    placeholderText: "Voltage (pu)"
                    text: "1.0"
                }

                TextField {
                    placeholderText: "Angle (deg)"
                    text: "0.0"
                }

                ComboBox {
                    model: ["PQ", "PV", "Slack"]
                    currentIndex: 0
                }
            }
        }

        GroupBox {
            title: "System Summary"
            Layout.fillWidth: true

            ColumnLayout {
                width: parent.width

                Label { text: "Buses: 14" }
                Label { text: "Lines: 20" }
                Label { text: "Generators: 5" }
                Label { text: "Loads: 11" }
                Label { text: "Total Generation: 272.4 MW" }
                Label { text: "Total Load: 259.0 MW" }
            }
        }

        GroupBox {
            title: "Analysis Results"
            Layout.fillWidth: true

            ColumnLayout {
                width: parent.width

                Label { text: "Converged: Yes" }
                Label { text: "Iterations: 4" }
                Label { text: "Total Loss: 3.2 MW" }
            }
        }
    }
}