import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Gb.Player

ApplicationWindow {
    id: window
    width: 1600
    height: 960
    minimumWidth: 1100
    minimumHeight: 680
    visible: true
    color: "#0b1015"
    title: player.displayName + (player.inputName.length ? " — " + player.inputName : "")

    readonly property var modeNames: ["Rectilinear", "Fisheye", "Stereographic", "Little Planet",
                                      "Panorama (equirectangular)", "Raw track 1", "Raw track 2",
                                      "Original"]
    readonly property var availableModes: player.sourceLayout === 1 ? [0, 1, 2, 3, 4, 5, 6] : [7]
    readonly property bool spherical: view.mode <= 3
    property int requestedMode: initialView

    function selectMode(mode) {
        requestedMode = mode
        view.mode = availableModes.indexOf(mode) >= 0 ? mode : availableModes[0]
        if (view.mode === 3 && view.fieldOfView < 180) view.fieldOfView = 240
        else if (view.mode === 0 && view.fieldOfView > 150) view.fieldOfView = 90
        else if ((view.mode === 1 || view.mode === 2) && view.fieldOfView < 120) view.fieldOfView = 180
    }
    function resetView() {
        view.yaw = 0
        view.pitch = 0
        view.roll = 0
        view.zoom = 1
        view.panX = 0
        view.panY = 0
        view.fieldOfView = view.mode === 3 ? 240 : (view.mode === 1 || view.mode === 2) ? 180 : 90
    }

    Connections {
        target: player
        function onFileChanged() {
            window.selectMode(window.requestedMode)
            // Choosing an entry breaks the currentIndex binding; follow chapter changes anyway.
            chapterBox.currentIndex = player.sequenceIndex
        }
    }

    Shortcut { sequence: "Space"; onActivated: player.togglePlay() }
    Shortcut { sequence: "Left"; onActivated: player.seekSeconds(player.position - 5) }
    Shortcut { sequence: "Right"; onActivated: player.seekSeconds(player.position + 5) }
    Shortcut { sequence: ","; onActivated: player.stepFrames(-1) }
    Shortcut { sequence: "."; onActivated: player.stepFrames(1) }
    Shortcut { sequence: "Ctrl+Left"; onActivated: player.stepFrames(-1) }
    Shortcut { sequence: "Ctrl+Right"; onActivated: player.stepFrames(1) }
    Shortcut { sequence: "M"; onActivated: player.audioMuted = !player.audioMuted }
    Shortcut { sequence: "F"; onActivated: window.visibility = window.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen }
    Shortcut { sequence: "Escape"; onActivated: if (window.visibility === Window.FullScreen) window.visibility = Window.Windowed }
    Shortcut { sequence: "R"; onActivated: window.resetView() }
    Shortcut { sequence: "Ctrl+O"; onActivated: player.openDialog() }
    Repeater {
        model: 8
        Item {
            Shortcut { sequence: String(index + 1); onActivated: window.selectMode(index) }
        }
    }

    header: ToolBar {
        background: Rectangle { color: "#17212a" }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            spacing: 8

            DarkButton { text: "Open…"; onClicked: player.openDialog() }
            Label {
                visible: player.sequenceNames.length < 2
                text: player.inputName.length ? player.inputName : "No file"
                color: "#d9e2e8"
                elide: Text.ElideMiddle
                Layout.maximumWidth: 260
            }
            // Chapters of a recording split by the camera: pick one to open it.
            DarkComboBox {
                id: chapterBox
                visible: player.sequenceNames.length >= 2
                model: player.sequenceNames
                currentIndex: player.sequenceIndex
                Layout.preferredWidth: 170
                ToolTip.visible: hovered
                ToolTip.text: "Chapter " + (player.sequenceIndex + 1) + " of " + player.sequenceNames.length
                onActivated: function(index) { player.openSequenceIndex(index) }
            }
            Label {
                text: player.layoutDescription
                color: "#7f919f"
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Label { text: "View"; color: "#9fb0bd" }
            DarkComboBox {
                id: modeBox
                model: window.availableModes.map(function(mode) { return window.modeNames[mode] })
                currentIndex: Math.max(0, window.availableModes.indexOf(view.mode))
                enabled: player.hasMedia
                Layout.preferredWidth: 230
                onActivated: function(index) { window.selectMode(window.availableModes[index]) }
            }
            DarkButton { text: "Reset view"; enabled: player.hasMedia; onClicked: window.resetView() }
            DarkButton {
                text: window.visibility === Window.FullScreen ? "Exit full screen" : "Full screen"
                onClicked: window.visibility = window.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 6

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#090d11"
            border.color: "#26343f"
            radius: 3
            clip: true

            VideoView {
                id: view
                anchors.fill: parent
                anchors.margins: 1
                engine: player.engine
                sourceLayout: player.sourceLayout
                overlap: player.overlap
                Component.onCompleted: window.selectMode(initialView)
            }

            MouseArea {
                anchors.fill: parent
                enabled: player.hasMedia
                acceptedButtons: Qt.LeftButton
                property real lastX: 0
                property real lastY: 0
                onPressed: function(mouse) { lastX = mouse.x; lastY = mouse.y }
                onPositionChanged: function(mouse) {
                    var dx = mouse.x - lastX
                    var dy = mouse.y - lastY
                    lastX = mouse.x
                    lastY = mouse.y
                    if (window.spherical) {
                        var degreesPerPixel = view.fieldOfView / Math.max(1, view.width)
                        view.yaw = ((view.yaw - dx * degreesPerPixel + 540) % 360) - 180
                        view.pitch = Math.max(-90, Math.min(90, view.pitch + dy * degreesPerPixel))
                    } else {
                        view.panX -= dx / Math.max(1, view.width) / view.zoom
                        view.panY -= dy / Math.max(1, view.height) / view.zoom
                    }
                }
                onDoubleClicked: player.togglePlay()
                onWheel: function(wheel) {
                    var steps = wheel.angleDelta.y / 120
                    if (window.spherical) {
                        var maximum = view.mode === 0 ? 150 : 330
                        view.fieldOfView = Math.max(20, Math.min(maximum, view.fieldOfView - steps * 5))
                    } else {
                        view.zoom = Math.max(1, Math.min(16, view.zoom * Math.pow(1.15, steps)))
                        if (view.zoom === 1) { view.panX = 0; view.panY = 0 }
                    }
                    wheel.accepted = true
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.margins: 10
                visible: player.hasMedia
                color: "#b30b1015"
                radius: 3
                width: overlayText.implicitWidth + 16
                height: overlayText.implicitHeight + 10
                Text {
                    id: overlayText
                    anchors.centerIn: parent
                    color: "#cfe0ea"
                    font.pixelSize: 12
                    font.family: "monospace"
                    text: window.modeNames[view.mode] + (window.spherical
                          ? "   yaw " + view.yaw.toFixed(1) + "°  pitch " + view.pitch.toFixed(1)
                            + "°  roll " + view.roll.toFixed(1) + "°  FOV " + view.fieldOfView.toFixed(0) + "°"
                          : "   zoom " + view.zoom.toFixed(2) + "×")
                }
            }

            Column {
                anchors.centerIn: parent
                spacing: 8
                visible: !player.hasMedia || !view.gpuAvailable
                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: !view.gpuAvailable ? "GPU view unavailable: OpenGL 3.3 is required"
                                             : "Open a GoPro .360 or MP4 recording (Ctrl+O)"
                    color: "#8193a1"
                    font.pixelSize: 16
                }
                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "Original files are opened read-only."
                    color: "#5d6e7b"
                }
            }
        }

        SplitView {
            Layout.preferredWidth: 440
            Layout.fillHeight: true
            orientation: Qt.Vertical
            handle: Rectangle {
                implicitHeight: 6
                color: SplitHandle.pressed ? "#4b94ba" : SplitHandle.hovered ? "#2f789f" : "#0b1015"
            }

            MapPanel {
                id: mapPanel
                SplitView.preferredHeight: 380
                SplitView.minimumHeight: 160
            }

            Rectangle {
                SplitView.fillHeight: true
                SplitView.minimumHeight: 180
                color: "#121a21"
                radius: 3

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6

                    TabBar {
                        id: tabs
                        Layout.fillWidth: true
                        background: Rectangle { color: "#121a21" }
                        Repeater {
                            model: ["Telemetry", "Metadata", "Performance"]
                            TabButton {
                                text: modelData
                                contentItem: Text {
                                    text: parent.text
                                    color: tabs.currentIndex === index ? "#61dc97" : "#9fb0bd"
                                    font.bold: tabs.currentIndex === index
                                    horizontalAlignment: Text.AlignHCenter
                                }
                                background: Rectangle {
                                    color: tabs.currentIndex === index ? "#1b2833" : "#121a21"
                                    border.color: "#26343f"
                                }
                            }
                        }
                    }

                    StackLayout {
                        currentIndex: tabs.currentIndex
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        // A plain Flickable: ScrollView's automatic content sizing loops with a
                        // graph that takes the height the text leaves.
                        Flickable {
                            id: telemetryScroll
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            contentWidth: width
                            contentHeight: telemetryText.height + (profileBlock.visible ? 8 + profileBlock.height : 0)
                            ScrollBar.vertical: ScrollBar {}
                            ColumnLayout {
                                id: telemetryText
                                width: telemetryScroll.width
                                spacing: 8
                                Label {
                                    text: "Synchronized to the displayed frame. Values are the nearest recorded "
                                          + "GPS5 record; nothing is interpolated."
                                    color: "#7f919f"
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                }
                                InfoTable { rows: player.telemetryRows; Layout.fillWidth: true }
                            }
                            // GPS profile over the whole recording; press or drag to seek. It takes
                            // the height the text leaves, down to a minimum; below that the tab scrolls.
                            // Shown only once the recording's GPS records have loaded.
                            Item {
                                id: profileBlock
                                visible: player.telemetryState === "ready"
                                y: telemetryText.height + 8
                                width: telemetryScroll.width
                                height: Math.max(150, telemetryScroll.height - y - 2)
                                ColumnLayout {
                                    anchors.fill: parent
                                    spacing: -1  // the selected tab overlaps the frame's top border
                                    RowLayout {
                                        Layout.fillWidth: true
                                        z: 1
                                        spacing: 6
                                        TabBar {
                                            id: profileTabs
                                            spacing: 2
                                            background: Item {}
                                            Repeater {
                                                model: ["Height", "Speed"]
                                                TabButton {
                                                    readonly property bool selected: profileTabs.currentIndex === index
                                                    text: modelData
                                                    width: 80
                                                    implicitHeight: 26
                                                    contentItem: Text {
                                                        text: parent.text
                                                        color: parent.selected ? "#61dc97" : "#9fb0bd"
                                                        font.bold: parent.selected
                                                        horizontalAlignment: Text.AlignHCenter
                                                        verticalAlignment: Text.AlignVCenter
                                                    }
                                                    background: Rectangle {
                                                        color: parent.selected ? "#121a21" : "#0d1419"
                                                        border.color: "#26343f"
                                                        // Open towards the graph: hide the bottom border when selected.
                                                        Rectangle {
                                                            visible: parent.parent.selected
                                                            anchors.left: parent.left
                                                            anchors.right: parent.right
                                                            anchors.bottom: parent.bottom
                                                            anchors.margins: 1
                                                            anchors.bottomMargin: 0
                                                            height: 1
                                                            color: parent.color
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                        Item { Layout.fillWidth: true }
                                        // Export the GPS track of the whole recording (every chapter).
                                        Button {
                                            id: exportButton
                                            enabled: !player.exportingTrack
                                            implicitWidth: 24
                                            implicitHeight: 24
                                            padding: 0
                                            Layout.bottomMargin: 3
                                            hoverEnabled: true
                                            ToolTip.visible: hovered
                                            ToolTip.delay: 400
                                            ToolTip.text: player.exportingTrack ? "Exporting GPS track…"
                                                          : player.sequenceNames.length > 1
                                                            ? "Export GPS track of all " + player.sequenceNames.length + " chapters…"
                                                            : "Export GPS track…"
                                            onClicked: player.exportTrackDialog()
                                            background: Rectangle {
                                                radius: 3
                                                color: exportButton.down ? "#174b6b" : exportButton.hovered ? "#1b2833" : "#0d1419"
                                                border.color: exportButton.hovered ? "#4b94ba" : "#26343f"
                                            }
                                            contentItem: Item {
                                                // Arrow into a tray, drawn so it needs no icon font.
                                                Canvas {
                                                    anchors.fill: parent
                                                    visible: !player.exportingTrack
                                                    readonly property color ink: exportButton.hovered ? "#61dc97" : "#9fb0bd"
                                                    onInkChanged: requestPaint()
                                                    onPaint: {
                                                        var context = getContext("2d")
                                                        context.reset()
                                                        context.strokeStyle = ink
                                                        context.lineWidth = 1.5
                                                        context.lineCap = "round"
                                                        context.lineJoin = "round"
                                                        var cx = width / 2
                                                        context.beginPath()
                                                        context.moveTo(cx, 5); context.lineTo(cx, 14)
                                                        context.moveTo(cx - 4, 10); context.lineTo(cx, 14); context.lineTo(cx + 4, 10)
                                                        context.moveTo(cx - 7, 15); context.lineTo(cx - 7, 18)
                                                        context.lineTo(cx + 7, 18); context.lineTo(cx + 7, 15)
                                                        context.stroke()
                                                    }
                                                }
                                                // Spinner while the export runs: a three-quarter arc turning once a second.
                                                Canvas {
                                                    id: exportSpinner
                                                    anchors.fill: parent
                                                    visible: player.exportingTrack
                                                    onPaint: {
                                                        var context = getContext("2d")
                                                        context.reset()
                                                        context.strokeStyle = "#61dc97"
                                                        context.lineWidth = 2
                                                        context.lineCap = "round"
                                                        context.beginPath()
                                                        context.arc(width / 2, height / 2, 6.5, 0, 1.5 * Math.PI)
                                                        context.stroke()
                                                    }
                                                    RotationAnimator on rotation {
                                                        running: exportSpinner.visible
                                                        from: 0
                                                        to: 360
                                                        duration: 1000
                                                        loops: Animation.Infinite
                                                    }
                                                }
                                            }
                                        }
                                    }
                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        color: "#121a21"
                                        border.color: "#26343f"
                                        ProfileView {
                                            anchors.fill: parent
                                            anchors.margins: 1
                                            controller: player
                                            quantity: profileTabs.currentIndex === 0 ? ProfileView.Height : ProfileView.Speed
                                        }
                                    }
                                }
                            }
                        }

                        ScrollView {
                            clip: true
                            contentWidth: availableWidth
                            ColumnLayout {
                                width: parent.width
                                spacing: 8
                                Label { text: "File"; color: "#61dc97"; font.bold: true }
                                InfoTable { rows: player.fileRows; Layout.fillWidth: true }
                                Label { text: "Tracks"; color: "#61dc97"; font.bold: true }
                                Repeater {
                                    model: tabs.currentIndex === 1 ? player.trackRows : []
                                    Text {
                                        Layout.fillWidth: true
                                        wrapMode: Text.WrapAnywhere
                                        font.family: "monospace"
                                        font.pixelSize: 11
                                        color: "#d2dde4"
                                        text: "#" + modelData.id + " " + modelData.handler + " " + modelData.codec
                                              + " — " + modelData.name + "\n   " + modelData.samples + " samples, "
                                              + modelData.duration + " ticks @ " + modelData.timeBase
                                              + (modelData.geometry.length ? ", " + modelData.geometry : "")
                                    }
                                }
                                Label { text: "GPMF at playback time"; color: "#61dc97"; font.bold: true }
                                Label {
                                    text: player.gpmfTitle
                                    color: "#9fb0bd"
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                }
                                Repeater {
                                    // Hundreds of rows: only instantiate while the tab is visible.
                                    model: tabs.currentIndex === 1 ? player.gpmfRows : []
                                    Text {
                                        Layout.fillWidth: true
                                        leftPadding: modelData.depth * 14
                                        wrapMode: Text.WrapAnywhere
                                        font.family: "monospace"
                                        font.pixelSize: 11
                                        textFormat: Text.PlainText
                                        color: modelData.known ? "#d2dde4" : "#ffb35c"
                                        text: modelData.key + " [" + modelData.type
                                              + (modelData.repeat > 1 ? "×" + modelData.repeat : "") + "] "
                                              + modelData.detail + (modelData.unit.length ? " " + modelData.unit : "")
                                              + (modelData.known ? "" : "   (unknown key)")
                                    }
                                }
                            }
                        }

                        ScrollView {
                            clip: true
                            contentWidth: availableWidth
                            ColumnLayout {
                                width: parent.width
                                spacing: 8
                                Label {
                                    text: "Updated every second. Seek latency is measured from request to the "
                                          + "first drawn frame. Run gb-player --benchmark for a reproducible report."
                                    color: "#7f919f"
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                }
                                InfoTable { rows: player.diagnosticsRows; labelWidth: 120; Layout.fillWidth: true }
                            }
                        }
                    }
                }
            }
        }
    }

    footer: Rectangle {
        color: "#17212a"
        implicitHeight: footerColumn.implicitHeight + 12
        ColumnLayout {
            id: footerColumn
            anchors.fill: parent
            anchors.margins: 6
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            spacing: 2
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                DarkButton { text: "◀|"; implicitWidth: 44; enabled: player.hasMedia; onClicked: player.stepFrames(-1) }
                DarkButton {
                    text: player.playing ? "Pause" : "Play"
                    implicitWidth: 80
                    enabled: player.hasMedia
                    onClicked: player.togglePlay()
                }
                DarkButton { text: "|▶"; implicitWidth: 44; enabled: player.hasMedia; onClicked: player.stepFrames(1) }
                Label {
                    text: player.positionText
                    color: "#e3ebf0"
                    font.family: "monospace"
                }
                Timeline {
                    Layout.fillWidth: true
                    position: player.position
                    duration: player.duration
                    samples: player.timelineSamples
                    gaps: player.timelineGaps
                    coverage: player.gpsCoverage
                    onSeekRequested: function(fraction) { player.seekFraction(fraction) }
                    onScrubStarted: player.beginScrub()
                    onScrubFinished: player.endScrub()
                }
                Label {
                    text: player.durationText
                    color: "#9fb0bd"
                    font.family: "monospace"
                }
                DarkButton {
                    text: !player.hasAudio ? "No audio" : player.audioMuted ? "Unmute" : "Mute"
                    implicitWidth: 80
                    enabled: player.hasAudio
                    onClicked: player.audioMuted = !player.audioMuted
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: player.status
                    color: player.statusIsError ? "#ff7d7d" : "#9fb0bd"
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Label {
                    text: player.ptsText
                    color: "#6f8292"
                    font.family: "monospace"
                }
                Label {
                    text: "Independent open-source toolkit — not affiliated with GoPro"
                    color: "#56656f"
                    font.pixelSize: 11
                }
            }
        }
    }
}
