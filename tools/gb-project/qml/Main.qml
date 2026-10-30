import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    width: 1600
    height: 960
    minimumWidth: 1100
    minimumHeight: 700
    visible: true
    title: "gb-project — MAX Projection Laboratory"
    color: "#0b1015"

    property real yaw: 0
    property real pitch: 0
    property real roll: 0
    property real fieldOfView: 90
    property real dragYaw: 0
    property real dragPitch: 0

    function refreshCpuReference() {
        projectController.updateProjection(projectionBox.currentIndex, yaw, pitch, roll,
                                           fieldOfView)
    }

    header: ToolBar {
        background: Rectangle { color: "#17212a" }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            spacing: 8

            DarkButton {
                text: "Open…"
                onClicked: projectController.openFile(Number(timeField.text),
                                                       Number(widthBox.currentText))
            }
            Label {
                text: projectController.inputName.length ? projectController.inputName : "No file"
                color: "#d9e2e8"
                Layout.maximumWidth: 240
                elide: Text.ElideMiddle
            }
            Label { text: "Time (s)"; color: "#9fb0bd" }
            TextField {
                id: timeField
                text: Number(initialSeconds).toFixed(3)
                validator: DoubleValidator { bottom: 0 }
                Layout.preferredWidth: 92
            }
            Label { text: "Panorama"; color: "#9fb0bd" }
            DarkComboBox {
                id: widthBox
                model: ["1024", "2048", "4096", "5376"]
                Component.onCompleted: {
                    var wanted = String(initialPanoramaWidth)
                    var found = model.indexOf(wanted)
                    currentIndex = found >= 0 ? found : 1
                }
            }
            DarkButton {
                text: "Load timestamp"
                enabled: projectController.inputPath.length > 0
                onClicked: projectController.loadFile(projectController.inputPath,
                                                       Number(timeField.text),
                                                       Number(widthBox.currentText))
            }
            Item { Layout.fillWidth: true }
            DarkButton {
                text: "Save CPU reference…"
                enabled: projectController.ready
                onClicked: projectController.saveProjection(projectionBox.currentIndex,
                                                              window.yaw, window.pitch,
                                                              window.roll, window.fieldOfView)
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        GridLayout {
            columns: 2
            rowSpacing: 6
            columnSpacing: 6
            Layout.fillWidth: true
            Layout.fillHeight: true

            ViewPane {
                title: "RAW TRACK 1 — LEFT / FRONT / RIGHT"
                source: projectController.rawFrontSource
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
            ViewPane {
                title: "RAW TRACK 2 — DOWN / BACK / TOP"
                source: projectController.rawRearSource
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
            ViewPane {
                title: "RECONSTRUCTED EQUIRECTANGULAR"
                source: projectController.panoramaSource
                Layout.fillWidth: true
                Layout.fillHeight: true
            }

            Rectangle {
                id: outputPane
                color: "#10161d"
                border.color: "#31404d"
                border.width: 1
                radius: 4
                Layout.fillWidth: true
                Layout.fillHeight: true

                Text {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.margins: 8
                    text: "GPU VIRTUAL CAMERA — drag to look, wheel to zoom"
                    color: "#61dc97"
                    font.bold: true
                    font.pixelSize: 13
                    z: 3
                }

                Image {
                    id: panoramaTexture
                    width: Math.max(1, sourceSize.width)
                    height: Math.max(1, sourceSize.height)
                    source: projectController.panoramaSource
                    visible: true
                    cache: false
                }
                ShaderEffectSource {
                    id: panoramaSource
                    sourceItem: panoramaTexture
                    hideSource: true
                    live: true
                    textureSize: Qt.size(panoramaTexture.width, panoramaTexture.height)
                }
                ShaderEffect {
                    id: projectionShader
                    anchors.fill: parent
                    property variant source: panoramaSource
                    property real yaw: window.yaw * Math.PI / 180
                    property real pitch: window.pitch * Math.PI / 180
                    property real roll: window.roll * Math.PI / 180
                    property real fieldOfView: window.fieldOfView * Math.PI / 180
                    property real aspect: width / Math.max(1, height)
                    property int mode: projectionBox.currentIndex
                    fragmentShader: "qrc:/shaders/projection.frag.qsb"
                    visible: projectController.ready
                    z: 1
                }
                Image {
                    anchors.fill: parent
                    anchors.margins: 4
                    source: projectController.outputSource
                    fillMode: Image.PreserveAspectFit
                    cache: false
                    smooth: true
                    z: 0
                }
                Label {
                    anchors.centerIn: parent
                    text: "Open a two-stream MAX .360 file"
                    color: "#7f919f"
                    visible: !projectController.ready
                }
                MouseArea {
                    anchors.fill: parent
                    enabled: projectController.ready
                    z: 2
                    acceptedButtons: Qt.LeftButton
                    onPressed: function(mouse) {
                        pressX = mouse.x
                        pressY = mouse.y
                        window.dragYaw = window.yaw
                        window.dragPitch = window.pitch
                    }
                    onPositionChanged: function(mouse) {
                        if (!pressed) return
                        window.yaw = window.dragYaw - (mouse.x - pressX) * 0.2
                        window.pitch = Math.max(-90, Math.min(90,
                            window.dragPitch + (mouse.y - pressY) * 0.2))
                    }
                    onWheel: function(wheel) {
                        window.fieldOfView = Math.max(20, Math.min(220,
                            window.fieldOfView - wheel.angleDelta.y / 24))
                        wheel.accepted = true
                        refreshTimer.restart()
                    }
                    property real pressX: 0
                    property real pressY: 0
                    onReleased: window.refreshCpuReference()
                }
                Timer {
                    id: refreshTimer
                    interval: 200
                    onTriggered: window.refreshCpuReference()
                }
            }
        }

        Pane {
            Layout.preferredWidth: 300
            Layout.fillHeight: true
            background: Rectangle { color: "#121a21"; radius: 4 }
            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                Label { text: "Projection controls"; color: "#61dc97"; font.bold: true }
                DarkComboBox {
                    id: projectionBox
                    model: ["Rectilinear", "Fisheye", "Stereographic", "Little Planet"]
                    Layout.fillWidth: true
                    onCurrentIndexChanged: {
                        if (currentIndex === 3 && window.fieldOfView < 180)
                            window.fieldOfView = 240
                        else if (currentIndex === 0 && window.fieldOfView > 160)
                            window.fieldOfView = 120
                        if (projectController.ready) refreshTimer.restart()
                    }
                }

                Label { text: "Yaw  " + window.yaw.toFixed(1) + "°"; color: "#d9e2e8" }
                Slider {
                    from: -180; to: 180; value: window.yaw
                    Layout.fillWidth: true
                    onMoved: window.yaw = value
                    onPressedChanged: if (!pressed && projectController.ready)
                                          window.refreshCpuReference()
                }
                Label { text: "Pitch  " + window.pitch.toFixed(1) + "°"; color: "#d9e2e8" }
                Slider {
                    from: -90; to: 90; value: window.pitch
                    Layout.fillWidth: true
                    onMoved: window.pitch = value
                    onPressedChanged: if (!pressed && projectController.ready)
                                          window.refreshCpuReference()
                }
                Label { text: "Roll  " + window.roll.toFixed(1) + "°"; color: "#d9e2e8" }
                Slider {
                    from: -180; to: 180; value: window.roll
                    Layout.fillWidth: true
                    onMoved: window.roll = value
                    onPressedChanged: if (!pressed && projectController.ready)
                                          window.refreshCpuReference()
                }
                Label { text: "Field of view  " + window.fieldOfView.toFixed(1) + "°"; color: "#d9e2e8" }
                Slider {
                    from: 20; to: projectionBox.currentIndex === 0 ? 160 : 300
                    value: window.fieldOfView
                    Layout.fillWidth: true
                    onMoved: window.fieldOfView = value
                    onPressedChanged: if (!pressed && projectController.ready)
                                          window.refreshCpuReference()
                }
                DarkButton {
                    text: "Reset view"
                    Layout.fillWidth: true
                    onClicked: {
                        window.yaw = 0
                        window.pitch = 0
                        window.roll = 0
                        window.fieldOfView = 90
                        window.refreshCpuReference()
                    }
                }
                Rectangle { height: 1; color: "#31404d"; Layout.fillWidth: true }
                Label { text: "Diagnostics"; color: "#61dc97"; font.bold: true }
                Label {
                    text: projectController.status
                    color: projectController.status.startsWith("Error") ? "#ff7d7d" : "#c0ccd5"
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                Label {
                    text: "Raw → face-specific MAX EAC reconstruction → GPU virtual camera. " +
                          "Saved views use the deterministic CPU reference."
                    color: "#7f919f"
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                Item { Layout.fillHeight: true }
            }
        }
    }
}
