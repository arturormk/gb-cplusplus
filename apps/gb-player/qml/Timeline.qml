import QtQuick

// Seek bar with metadata sample positions, GPS coverage and reported telemetry gaps.
Item {
    id: root
    property real position: 0
    property real duration: 0
    property var samples: []
    property var gaps: []
    property var coverage: ({})
    signal seekRequested(real fraction)
    implicitHeight: 34

    readonly property real fraction: duration > 0 ? Math.min(1, Math.max(0, position / duration)) : 0

    Rectangle {
        id: track
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        height: 10
        radius: 3
        color: "#1d2a34"

        Rectangle {
            visible: root.coverage.start !== undefined
            x: (root.coverage.start || 0) * track.width
            width: Math.max(1, ((root.coverage.end || 0) - (root.coverage.start || 0)) * track.width)
            height: 3
            anchors.bottom: parent.bottom
            color: "#2f7d55"
        }
        Repeater {
            model: root.gaps
            Rectangle {
                x: modelData.start * track.width
                width: Math.max(2, (modelData.end - modelData.start) * track.width)
                height: track.height
                color: "#8a4b22"
                opacity: 0.85
            }
        }
        Rectangle {
            width: root.fraction * track.width
            height: parent.height
            radius: 3
            color: "#2d6f95"
        }
    }

    Item {
        anchors.left: track.left
        anchors.right: track.right
        anchors.bottom: track.top
        anchors.bottomMargin: 2
        height: 6
        Repeater {
            model: root.samples
            Rectangle {
                x: modelData * parent.width
                width: 1
                height: 6
                color: "#5b7384"
            }
        }
    }

    Rectangle {
        x: root.fraction * track.width - width / 2
        anchors.verticalCenter: track.verticalCenter
        width: 14
        height: 14
        radius: 7
        color: mouse.pressed ? "#9fdcff" : "#e6f2f8"
        border.color: "#2d6f95"
        border.width: 2
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        enabled: root.duration > 0
        function request(x) { root.seekRequested(Math.min(1, Math.max(0, x / width))) }
        onPressed: function(mouse) { request(mouse.x) }
        onPositionChanged: function(mouse) { if (pressed) request(mouse.x) }
    }
}
