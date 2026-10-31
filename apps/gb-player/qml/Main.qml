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
    // MAX dual-stream video, equirectangular photo, anything else flat.
    readonly property var availableModes: player.sourceLayout === 1 ? [0, 1, 2, 3, 4, 5, 6]
                                        : player.sourceLayout === 2 ? [0, 1, 2, 3, 4, 7] : [7]
    readonly property bool spherical: view.mode <= 3
    // Fisheye, stereographic and Little Planet normalize each axis separately; a square view
    // makes their image circle round.
    readonly property bool circular: view.mode >= 1 && view.mode <= 3
    property bool squareView: initialSquare
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

    // Capture mode (docs/plans/export/capture.md). The canvas aspect ratio is frozen on entry.
    function setCaptureMode(enabled) {
        if (enabled)
            capture.freezeCanvas((videoArea.width - 2) / Math.max(1, videoArea.height - 2))
        capture.active = enabled
    }
    // Nudges the capture rectangle by screen pixels.
    function nudge(dx, dy) {
        capture.moveCrop(dx / Math.max(1, view.width), dy / Math.max(1, view.height))
    }
    Binding { target: capture; property: "view"; value: view }
    Binding { target: capture; property: "square"; value: view.square }
    Binding { target: capture; property: "mapView"; value: mapPanel.map }

    Shortcut { sequence: "Space"; onActivated: player.togglePlay() }
    Shortcut { sequence: "Left"; enabled: !capture.active; onActivated: player.seekSeconds(player.position - 5) }
    Shortcut { sequence: "Right"; enabled: !capture.active; onActivated: player.seekSeconds(player.position + 5) }
    Shortcut { sequence: "Left"; enabled: capture.active; onActivated: window.nudge(-1, 0) }
    Shortcut { sequence: "Right"; enabled: capture.active; onActivated: window.nudge(1, 0) }
    Shortcut { sequence: "Up"; enabled: capture.active; onActivated: window.nudge(0, -1) }
    Shortcut { sequence: "Down"; enabled: capture.active; onActivated: window.nudge(0, 1) }
    Shortcut { sequence: "Shift+Left"; enabled: capture.active; onActivated: window.nudge(-10, 0) }
    Shortcut { sequence: "Shift+Right"; enabled: capture.active; onActivated: window.nudge(10, 0) }
    Shortcut { sequence: "Shift+Up"; enabled: capture.active; onActivated: window.nudge(0, -10) }
    Shortcut { sequence: "Shift+Down"; enabled: capture.active; onActivated: window.nudge(0, 10) }
    Shortcut { sequence: ","; onActivated: player.stepFrames(-1) }
    Shortcut { sequence: "."; onActivated: player.stepFrames(1) }
    Shortcut { sequence: "Ctrl+Left"; onActivated: player.stepFrames(-1) }
    Shortcut { sequence: "Ctrl+Right"; onActivated: player.stepFrames(1) }
    Shortcut { sequence: "M"; onActivated: player.audioMuted = !player.audioMuted }
    Shortcut { sequence: "F"; onActivated: window.visibility = window.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen }
    Shortcut { sequence: "Escape"; onActivated: if (window.visibility === Window.FullScreen) window.visibility = Window.Windowed }
    Shortcut { sequence: "R"; onActivated: window.resetView() }
    Shortcut { sequence: "S"; onActivated: window.squareView = !window.squareView }
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
            // Play | Capture
            Row {
                spacing: 2
                ToggleButton {
                    text: "Play"
                    selected: !capture.active
                    implicitWidth: 72
                    implicitHeight: 34
                    onClicked: window.setCaptureMode(false)
                }
                ToggleButton {
                    text: "Capture"
                    selected: capture.active
                    enabled: player.hasMedia || capture.active
                    implicitWidth: 80
                    implicitHeight: 34
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: "Frame and capture stills, and get the framing as gb-render settings"
                    onClicked: window.setCaptureMode(true)
                }
            }
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
            DarkButton {
                text: "Square"
                visible: window.circular
                checkable: true
                checked: window.squareView
                enabled: player.hasMedia
                implicitWidth: 76
                ToolTip.visible: hovered
                ToolTip.text: "Square view, so the image circle is round (S)"
                onToggled: window.squareView = checked
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
            id: videoArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#090d11"
            border.color: "#26343f"
            radius: 3
            clip: true

            // Capture mode only: room beyond the area on the panel's side, so the panel preview next
            // to the capture rectangle can be scrolled into view when the window is not wide (or
            // tall) enough. The room is what the panel can take at most for its side and size,
            // wherever the rectangle is, so dragging the rectangle never moves the content. Drags
            // and the wheel keep aiming the view; the edge scroll bars appear on hover.
            Flickable {
                id: canvasScroll
                readonly property bool panel: capture.active && player.hasMedia && capture.panelSide !== "none"
                readonly property bool across: capture.panelSide === "top" || capture.panelSide === "bottom"
                // A side panel is a fraction of the picture's height; a top or bottom one of the
                // taller frame.
                readonly property real reach: !panel ? 0
                    : across ? view.height * capture.panelSize / (1 - capture.panelSize)
                             : view.height * capture.panelSize
                readonly property real freeX: (width - view.width) / 2
                readonly property real freeY: (height - view.height) / 2
                readonly property real margin: 6
                function room(side, free) {
                    return capture.panelSide === side && reach > free ? reach - free + margin : 0
                }
                readonly property real padLeft: room("left", freeX)
                readonly property real padRight: room("right", freeX)
                readonly property real padTop: room("top", freeY)
                readonly property real padBottom: room("bottom", freeY)
                // How far the scroll bars have moved the content from where the view is in place.
                // Only the bars set it; everything else keeps the view where it is on screen.
                property real offsetX: 0
                property real offsetY: 0
                // A new side (or none, or leaving Capture mode) starts with the view in place.
                readonly property string side: panel ? capture.panelSide : ""
                onSideChanged: { offsetX = 0; offsetY = 0; Qt.callLater(settle) }
                function settle() {
                    contentX = Math.max(0, Math.min(contentWidth - width, padLeft + offsetX))
                    contentY = Math.max(0, Math.min(contentHeight - height, padTop + offsetY))
                }
                onPadLeftChanged: Qt.callLater(settle)
                onPadTopChanged: Qt.callLater(settle)
                onContentWidthChanged: Qt.callLater(settle)
                onContentHeightChanged: Qt.callLater(settle)
                onContentXChanged: if (horizontalBar.hovered || horizontalBar.pressed) offsetX = contentX - padLeft
                onContentYChanged: if (verticalBar.hovered || verticalBar.pressed) offsetY = contentY - padTop

                anchors.fill: parent
                anchors.margins: 1
                clip: true
                interactive: false
                boundsBehavior: Flickable.StopAtBounds
                contentWidth: width + padLeft + padRight
                contentHeight: height + padTop + padBottom
                ScrollBar.horizontal: EdgeScrollBar { id: horizontalBar }
                ScrollBar.vertical: EdgeScrollBar { id: verticalBar }

                VideoView {
                    id: view
                    readonly property bool square: window.squareView && window.circular
                    readonly property real availableWidth: canvasScroll.width
                    readonly property real availableHeight: canvasScroll.height
                    // Capture mode letterboxes the view to the frozen canvas, so resizing the window
                    // scales the canvas but never changes what is in it.
                    readonly property real captureWidth: availableWidth / Math.max(1, availableHeight) > capture.canvasAspect
                                                         ? availableHeight * capture.canvasAspect : availableWidth
                    x: canvasScroll.padLeft + (availableWidth - width) / 2
                    y: canvasScroll.padTop + (availableHeight - height) / 2
                    width: capture.active ? captureWidth
                           : square ? Math.min(availableWidth, availableHeight) : availableWidth
                    height: capture.active ? captureWidth / capture.canvasAspect
                            : square ? width : availableHeight
                    engine: player.engine
                    sourceLayout: player.sourceLayout
                    overlap: player.overlap
                    panoramaCrop: player.panoramaCrop
                    Component.onCompleted: window.selectMode(initialView)
                }

                MouseArea {
                    width: canvasScroll.contentWidth
                    height: canvasScroll.contentHeight
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
                        if (capture.active && (wheel.modifiers & Qt.ControlModifier)) {
                            capture.scaleCrop(Math.pow(1.1, steps))
                        } else if (window.spherical) {
                            var maximum = view.mode === 0 ? 150 : 330
                            view.fieldOfView = Math.max(20, Math.min(maximum, view.fieldOfView - steps * 5))
                        } else {
                            view.zoom = Math.max(1, Math.min(16, view.zoom * Math.pow(1.15, steps)))
                            if (view.zoom === 1) { view.panX = 0; view.panY = 0 }
                        }
                        wheel.accepted = true
                    }
                }

                CaptureOverlay {
                    visible: capture.active && player.hasMedia
                    x: view.x
                    y: view.y
                    width: view.width
                    height: view.height
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
                                             : "Open a GoPro .360 or MP4 recording, or a JPG photo (Ctrl+O)"
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

        CapturePanel {
            visible: capture.active
            Layout.preferredWidth: 440
            Layout.fillHeight: true
        }

        // Hidden, not destroyed, in Capture mode, so the map and graphs keep their state.
        SplitView {
            visible: !capture.active
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
                                    text: player.isPhoto
                                          ? "The position recorded in the photo's EXIF GPS fields. A photo has "
                                            + "one position and no track."
                                          : "Synchronized to the displayed frame. Values are the nearest recorded "
                                            + "GPS5 record; nothing is interpolated."
                                    color: "#7f919f"
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                }
                                InfoTable { rows: player.telemetryRows; Layout.fillWidth: true }
                                // Photo capture settings from EXIF and the GoPro APP6 segment.
                                Label {
                                    visible: player.isPhoto
                                    text: "Camera"
                                    color: "#61dc97"
                                    font.bold: true
                                }
                                InfoTable {
                                    visible: player.isPhoto
                                    rows: player.photoRows
                                    labelWidth: 150
                                    Layout.fillWidth: true
                                }
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
                                        // The graphs are smoothed with a 3 s sliding mean; this shows the raw records instead.
                                        Button {
                                            id: rawButton
                                            checkable: true
                                            implicitWidth: 24
                                            implicitHeight: 24
                                            padding: 0
                                            Layout.bottomMargin: 3
                                            hoverEnabled: true
                                            ToolTip.visible: hovered
                                            ToolTip.delay: 400
                                            ToolTip.text: checked ? "Show smoothed height and speed (3 s mean)" : "Show raw GPS records"
                                            background: Rectangle {
                                                radius: 3
                                                color: rawButton.down ? "#174b6b" : rawButton.checked ? "#16323f"
                                                       : rawButton.hovered ? "#1b2833" : "#0d1419"
                                                border.color: rawButton.hovered || rawButton.checked ? "#4b94ba" : "#26343f"
                                            }
                                            // A jagged, noisy trace, drawn so it needs no icon font.
                                            contentItem: Canvas {
                                                readonly property color ink: rawButton.checked || rawButton.hovered ? "#61dc97" : "#9fb0bd"
                                                onInkChanged: requestPaint()
                                                onPaint: {
                                                    var context = getContext("2d")
                                                    context.reset()
                                                    context.strokeStyle = ink
                                                    context.lineWidth = 1.25
                                                    context.lineCap = "round"
                                                    context.lineJoin = "round"
                                                    context.beginPath()
                                                    context.moveTo(4, 14); context.lineTo(6, 9); context.lineTo(8, 16)
                                                    context.lineTo(10, 6); context.lineTo(12, 15); context.lineTo(14, 8)
                                                    context.lineTo(16, 17); context.lineTo(18, 10); context.lineTo(20, 13)
                                                    context.stroke()
                                                }
                                            }
                                        }
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
                                            smoothed: !rawButton.checked
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
                                Label { visible: !player.isPhoto; text: "Tracks"; color: "#61dc97"; font.bold: true }
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
                                Label {
                                    text: player.isPhoto ? "GoPro metadata (GPMF)" : "GPMF at playback time"
                                    color: "#61dc97"
                                    font.bold: true
                                }
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
            // A photo has no time: no transport, timeline or audio.
            RowLayout {
                Layout.fillWidth: true
                visible: !player.isPhoto
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
