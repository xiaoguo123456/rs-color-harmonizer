import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    visible: true
    width: 1280
    height: 760
    title: qsTr("RS Color Harmonizer")
    color: "#111827"

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            spacing: 12
            Label { text: qsTr("RS Color Harmonizer"); font.bold: true; color: "white"; Layout.leftMargin: 14 }
            ToolButton { text: qsTr("Import scenes") }
            ToolButton { text: qsTr("Reference") }
            ToolButton { text: qsTr("Harmonize") }
            ToolButton { text: qsTr("Export GeoTIFF") }
            Item { Layout.fillWidth: true }
            Label { text: qsTr("MVP preview"); color: "#9ca3af"; Layout.rightMargin: 14 }
        }
    }

    SplitView {
        anchors.fill: parent
        anchors.margins: 12
        orientation: Qt.Horizontal
        Pane {
            SplitView.preferredWidth: 270
            ColumnLayout {
                anchors.fill: parent
                Label { text: qsTr("Scenes"); font.pixelSize: 18; font.bold: true }
                ListView {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    model: ListModel { ListElement { name: qsTr("No scenes loaded") } }
                    delegate: Label { text: model.name; color: "#d1d5db"; padding: 8 }
                }
                Button { text: qsTr("Add GeoTIFF"); Layout.fillWidth: true }
            }
        }
        Pane {
            SplitView.fillWidth: true
            ColumnLayout {
                anchors.fill: parent
                Label { text: qsTr("Map canvas"); color: "#9ca3af" }
                Rectangle {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    color: "#1f2937"; radius: 6
                    Label { anchors.centerIn: parent; text: qsTr("Preview canvas\nConnect rsch_core for raster rendering"); horizontalAlignment: Text.AlignHCenter; color: "#9ca3af" }
                }
            }
        }
        Pane {
            SplitView.preferredWidth: 300
            ColumnLayout {
                anchors.fill: parent
                Label { text: qsTr("Harmonization"); font.pixelSize: 18; font.bold: true }
                Label { text: qsTr("Reference scene") }
                ComboBox { model: [qsTr("Automatic"), qsTr("Scene 1"), qsTr("Scene 2")] ; Layout.fillWidth: true }
                Label { text: qsTr("Transfer strength") }
                Slider { from: 0; to: 1; value: 1; Layout.fillWidth: true }
                CheckBox { text: qsTr("Match variance"); checked: true }
                Button { text: qsTr("Run harmonization"); Layout.fillWidth: true }
                Item { Layout.fillHeight: true }
                Label { text: qsTr("GDAL/OpenCV are optional. CLI remains available when Qt is not installed."); wrapMode: Text.WordWrap; color: "#9ca3af"; Layout.fillWidth: true }
            }
        }
    }
}
