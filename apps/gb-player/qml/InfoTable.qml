import QtQuick
import QtQuick.Layouts

// Label/value rows. A row's state is "ok", "invalid" (recorded but flagged) or "unavailable".
ColumnLayout {
    id: root
    property var rows: []
    property int labelWidth: 132
    spacing: 3

    Repeater {
        // Model by count, not by list: the rows list is replaced on every presented frame, and a
        // list model would make the Repeater destroy and recreate every delegate (visible flicker).
        // With a count model the delegates persist and only their bindings re-evaluate.
        model: root.rows.length
        RowLayout {
            readonly property var entry: root.rows[index] ?? ({})
            Layout.fillWidth: true
            spacing: 8
            Text {
                text: entry.label ?? ""
                color: "#8fa2b1"
                font.pixelSize: 12
                Layout.preferredWidth: root.labelWidth
                Layout.minimumWidth: root.labelWidth
                Layout.alignment: Qt.AlignTop
                elide: Text.ElideRight
            }
            Text {
                text: entry.value ?? ""
                color: entry.state === "unavailable" ? "#7a8894"
                     : entry.state === "invalid" ? "#ffb35c" : "#e3ebf0"
                font.pixelSize: 12
                font.italic: entry.state === "unavailable"
                font.family: "monospace"
                wrapMode: Text.WrapAnywhere
                // Take the remaining width only; never let a long value push into the label column.
                Layout.preferredWidth: 1
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
            }
        }
    }
}
