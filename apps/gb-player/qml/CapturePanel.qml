import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Capture mode's sidebar: frame, capture, range and the framing as gb-render settings.
Pane {
    id: panel
    padding: 10
    palette.window: "#121a21"
    palette.windowText: "#d9e2e8"
    palette.text: "#e3ebf0"
    palette.base: "#080b0e"
    palette.button: "#1b2833"
    palette.buttonText: "#edf6fb"
    palette.mid: "#40515e"
    palette.highlight: "#245f82"
    background: Rectangle { color: "#121a21"; radius: 3 }

    component Heading: Label {
        color: "#61dc97"
        font.bold: true
        Layout.topMargin: 6
    }
    component Note: Label {
        color: "#7f919f"
        wrapMode: Text.Wrap
        Layout.fillWidth: true
        font.pixelSize: 12
    }
    component Toggle: ToggleButton { implicitWidth: 64 }

    ScrollView {
        id: scroll
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth

        ColumnLayout {
            width: scroll.availableWidth
            spacing: 6

            Heading { text: "Frame"; Layout.topMargin: 0 }
            RowLayout {
                spacing: 4
                Repeater {
                    model: capture.aspectNames
                    Toggle {
                        text: modelData
                        selected: capture.aspectIndex === index
                        onClicked: capture.aspectIndex = index
                        implicitWidth: 60
                    }
                }
            }
            RowLayout {
                visible: capture.aspectIndex === capture.aspectNames.length - 1
                spacing: 6
                Label { text: "W:H" }
                SpinBox { from: 1; to: 100; editable: true; value: capture.freeWidth; onValueModified: capture.freeWidth = value }
                Label { text: ":" }
                SpinBox { from: 1; to: 100; editable: true; value: capture.freeHeight; onValueModified: capture.freeHeight = value }
            }
            RowLayout {
                spacing: 6
                Label { text: "Panel" }
                DarkComboBox {
                    id: panelBox
                    model: ["none", "left", "right", "bottom"]
                    currentIndex: Math.max(0, model.indexOf(capture.panelSide))
                    onActivated: function(index) { capture.panelSide = model[index] }
                    Layout.preferredWidth: 120
                }
                Label { text: "size"; visible: capture.panelSide !== "none" }
                SpinBox {
                    visible: capture.panelSide !== "none"
                    from: 5
                    to: 60
                    editable: true
                    value: Math.round(capture.panelSize * 100)
                    onValueModified: capture.panelSize = value / 100
                    textFromValue: function(value) { return value + " %" }
                    valueFromText: function(text) { return parseInt(text) }
                    ToolTip.visible: hovered
                    ToolTip.text: "Panel size, as a percentage of the frame height"
                }
            }
            RowLayout {
                visible: capture.panelSide !== "none"
                spacing: 12
                CheckBox {
                    text: "Map"
                    checked: capture.panelMap
                    onToggled: capture.panelMap = checked
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: "The route map at the map pane's zoom, centred on the position, north up"
                }
                CheckBox {
                    text: "Telemetry"
                    checked: capture.panelTelemetry
                    onToggled: capture.panelTelemetry = checked
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: "Date and time (local), latitude, longitude, altitude and speed at the frame"
                }
            }

            Heading { text: "Capture" }
            RowLayout {
                spacing: 6
                Label { text: "Size" }
                DarkComboBox {
                    id: sizeBox
                    property bool custom: false
                    readonly property var presets: capture.widthPresets
                    model: capture.widthPresetNames.concat(["Custom width"])
                    currentIndex: custom || presets.indexOf(capture.outputWidth) < 0 ? presets.length
                                                                                     : presets.indexOf(capture.outputWidth)
                    onActivated: function(index) {
                        custom = index >= presets.length
                        if (!custom) capture.outputWidth = presets[index]
                    }
                    Layout.preferredWidth: 170
                }
                SpinBox {
                    visible: sizeBox.currentIndex >= sizeBox.presets.length
                    from: 16
                    to: 16384
                    stepSize: 2
                    editable: true
                    value: capture.outputWidth
                    onValueModified: capture.outputWidth = value
                }
            }
            Note { text: "Output " + capture.outputSizeText }
            Note {
                visible: capture.resolutionWarning.length > 0
                text: capture.resolutionWarning
                color: "#ffb35c"
            }
            RowLayout {
                spacing: 4
                Toggle { text: "JPEG"; selected: !capture.png; onClicked: capture.png = false }
                Toggle { text: "PNG"; selected: capture.png; onClicked: capture.png = true }
                Label { text: "quality"; visible: !capture.png; Layout.leftMargin: 8 }
                SpinBox {
                    visible: !capture.png
                    from: 1
                    to: 100
                    editable: true
                    value: capture.jpegQuality
                    onValueModified: capture.jpegQuality = value
                }
            }
            CheckBox {
                id: gpsBox
                text: "Include GPS position (EXIF)"
                checked: capture.gpsExif
                enabled: capture.gpsAvailable && !capture.png
                onToggled: capture.gpsExif = checked
            }
            Note {
                visible: !gpsBox.enabled
                text: capture.png ? "PNG stills carry no GPS position."
                                  : "Unavailable: " + capture.gpsReason + "."
            }
            DarkButton {
                id: captureButton
                text: capture.busy ? "Fetching map tiles…" : "Capture"
                enabled: player.hasMedia && !capture.busy
                implicitHeight: 40
                Layout.fillWidth: true
                font.bold: true
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: "Write the displayed frame as a still in the folder below (Shift+click: choose a name)"
                // A MouseArea, because Button.clicked carries no keyboard modifiers.
                MouseArea {
                    anchors.fill: parent
                    enabled: parent.enabled
                    hoverEnabled: true
                    onClicked: function(mouse) { capture.capture((mouse.modifiers & Qt.ShiftModifier) !== 0) }
                }
            }
            RowLayout {
                spacing: 6
                Label {
                    text: capture.folder
                    color: "#9fb0bd"
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                    font.pixelSize: 12
                }
                DarkButton { text: "Folder…"; implicitWidth: 80; implicitHeight: 30; onClicked: capture.chooseFolder() }
            }

            Heading { text: "Range"; visible: capture.hasRange }
            GridLayout {
                visible: capture.hasRange
                columns: 3
                columnSpacing: 8
                rowSpacing: 4
                Label { text: "Position" }
                Label { text: capture.positionText; font.family: "monospace"; Layout.columnSpan: 2 }
                Label { text: "Start" }
                Label { text: capture.startText.length ? capture.startText : "—"; font.family: "monospace"; Layout.fillWidth: true }
                DarkButton { text: "Set start"; implicitWidth: 90; implicitHeight: 30; onClicked: capture.setStart() }
                Label { text: "End" }
                Label { text: capture.endText.length ? capture.endText : "—"; font.family: "monospace"; Layout.fillWidth: true }
                DarkButton { text: "Set end"; implicitWidth: 90; implicitHeight: 30; onClicked: capture.setEnd() }
            }
            Note {
                visible: capture.hasRange
                text: "Start and end go into the settings below, for a gb-render video export."
            }

            Heading { text: "Settings" }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: settingsText.implicitHeight + 12
                color: "#080b0e"
                border.color: "#26343f"
                radius: 3
                TextEdit {
                    id: settingsText
                    anchors.fill: parent
                    anchors.margins: 6
                    readOnly: true
                    selectByMouse: true
                    text: capture.settingsText
                    color: "#d2dde4"
                    selectionColor: "#245f82"
                    font.family: "monospace"
                    font.pixelSize: 12
                    wrapMode: TextEdit.WrapAnywhere
                }
            }
            RowLayout {
                spacing: 6
                DarkButton { text: "Copy"; implicitWidth: 80; implicitHeight: 30; onClicked: capture.copySettings() }
                DarkButton { text: "Save as…"; implicitWidth: 90; implicitHeight: 30; onClicked: capture.saveSettingsAs() }
            }
            Note {
                text: "Use with gb-render --settings for a video export, with an overlay settings file."
            }
        }
    }
}
