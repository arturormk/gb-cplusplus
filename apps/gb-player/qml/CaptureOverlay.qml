import QtQuick
import Gb.Player

// The capture rectangle over the canvas (the view in Capture mode). Outside it the view is dimmed;
// a side panel, when set, is previewed next to it as the still will have it. Drag inside to move
// it and the corner handles to resize it; its aspect ratio stays locked. Presses outside fall through to the view,
// which aims as in Play mode. Positions are canvas units, so the rectangle keeps its place on the
// picture when the window is resized.
Item {
    id: overlay

    readonly property rect area: Qt.rect(capture.crop.x * width, capture.crop.y * height,
                                         capture.crop.width * width, capture.crop.height * height)
    readonly property color dim: "#a6000000"
    readonly property real handleSize: 14

    // Dimming: four bands around the rectangle.
    Rectangle { color: overlay.dim; x: 0; y: 0; width: overlay.width; height: overlay.area.y }
    Rectangle {
        color: overlay.dim
        x: 0
        y: overlay.area.y + overlay.area.height
        width: overlay.width
        height: overlay.height - y
    }
    Rectangle { color: overlay.dim; x: 0; y: overlay.area.y; width: overlay.area.x; height: overlay.area.height }
    Rectangle {
        color: overlay.dim
        x: overlay.area.x + overlay.area.width
        y: overlay.area.y
        width: overlay.width - x
        height: overlay.area.height
    }

    // The panel takes part of the output frame next to the picture. Side panels are a fraction of
    // the frame height, which is the picture's; a top or bottom panel is a fraction of the taller
    // frame. It is previewed as the still will have it.
    Rectangle {
        readonly property real size: capture.panelSize
        readonly property string side: capture.panelSide
        readonly property bool across: side === "top" || side === "bottom"
        readonly property real frameHeight: across ? overlay.area.height / (1 - size) : overlay.area.height
        visible: side !== "none"
        color: "black"
        border.color: "#3a4a56"
        x: side === "left" ? overlay.area.x - size * frameHeight
           : side === "right" ? overlay.area.x + overlay.area.width : overlay.area.x
        y: side === "bottom" ? overlay.area.y + overlay.area.height
           : side === "top" ? overlay.area.y - size * frameHeight : overlay.area.y
        width: across ? overlay.area.width : size * frameHeight
        height: across ? size * frameHeight : overlay.area.height
        PanelPreview {
            anchors.fill: parent
            anchors.margins: 1
            controller: capture
            visible: parent.visible
        }
    }

    // The border.
    Rectangle {
        x: overlay.area.x - 1
        y: overlay.area.y - 1
        width: overlay.area.width + 2
        height: overlay.area.height + 2
        color: "transparent"
        border.color: "#61dc97"
        border.width: 1
    }

    // Move: positions are taken in overlay coordinates, because this area moves with the drag.
    MouseArea {
        x: overlay.area.x
        y: overlay.area.y
        width: overlay.area.width
        height: overlay.area.height
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        property point last
        onPressed: function(mouse) { last = mapToItem(overlay, mouse.x, mouse.y) }
        onPositionChanged: function(mouse) {
            var point = mapToItem(overlay, mouse.x, mouse.y)
            capture.moveCrop((point.x - last.x) / Math.max(1, overlay.width),
                             (point.y - last.y) / Math.max(1, overlay.height))
            last = point
        }
        onDoubleClicked: player.togglePlay()
    }

    Repeater {
        model: 4 // top left, top right, bottom right, bottom left
        Rectangle {
            readonly property bool atRight: index === 1 || index === 2
            readonly property bool atBottom: index === 2 || index === 3
            x: (atRight ? overlay.area.x + overlay.area.width : overlay.area.x) - width / 2
            y: (atBottom ? overlay.area.y + overlay.area.height : overlay.area.y) - height / 2
            width: overlay.handleSize
            height: overlay.handleSize
            radius: 2
            color: handleArea.pressed || handleArea.containsMouse ? "#61dc97" : "#0b1015"
            border.color: "#61dc97"
            border.width: 2
            MouseArea {
                id: handleArea
                anchors.fill: parent
                anchors.margins: -4
                hoverEnabled: true
                cursorShape: (index === 0 || index === 2) ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor
                onPositionChanged: function(mouse) {
                    if (!pressed)
                        return
                    var point = mapToItem(overlay, mouse.x, mouse.y)
                    capture.dragCorner(index, point.x / Math.max(1, overlay.width),
                                       point.y / Math.max(1, overlay.height))
                }
            }
        }
    }
}
