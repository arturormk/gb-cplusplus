import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Gb.Player

// Route map synchronized to the displayed frame. The marker is the nearest recorded GPS fix, as
// in the telemetry panel; nothing is interpolated. Clicking the route offers seek targets.
Rectangle {
    id: panel
    color: "#1a232b"
    radius: 3
    clip: true

    property alias map: map
    property var candidates: []

    MapView {
        id: map
        objectName: "routeMap"
        anchors.fill: parent
        controller: player
        onRouteClicked: function(latitude, longitude, tolerance) {
            var found = player.routeCandidates(latitude, longitude, tolerance)
            if (found.length === 1) {
                player.seekTicks(found[0].ticks)
            } else if (found.length > 1) {
                panel.candidates = found
                candidatePopup.open()
            }
        }
    }

    // Message for every state without a drawable fix: no media, loading, no GPS, invalid, gap.
    Rectangle {
        visible: player.mapMessage.length > 0
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: map.hasRoute ? 8 : (parent.height - height) / 2
        width: Math.min(parent.width - 24, messageText.implicitWidth + 20)
        height: messageText.implicitHeight + 12
        radius: 3
        color: "#d90b1015"
        Text {
            id: messageText
            anchors.centerIn: parent
            width: Math.min(implicitWidth, panel.width - 44)
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            text: player.mapMessage
            color: player.mapState === "invalid" || player.mapState === "gap" ? "#ffb35c" : "#b4c4cf"
            font.pixelSize: 12
        }
    }

    Column {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 8
        spacing: 4
        visible: map.hasRoute
        DarkButton {
            text: "+"; implicitWidth: 34
            enabled: map.zoom < map.maximumZoom
            onClicked: map.zoomBy(1)
        }
        DarkButton {
            text: "−"; implicitWidth: 34
            enabled: map.zoom > map.minimumZoom
            onClicked: map.zoomBy(-1)
        }
        DarkButton {
            text: "⤢"; implicitWidth: 34
            ToolTip.visible: hovered
            ToolTip.text: "Fit the whole route"
            onClicked: { map.follow = false; map.fitRoute() }
        }
        DarkButton {
            text: "◎"; implicitWidth: 34
            ToolTip.visible: hovered
            ToolTip.text: map.follow ? "Following the current position" : "Follow the current position"
            background: Rectangle {
                radius: 4
                color: map.follow ? "#c25a0c" : parent.hovered ? "#2f789f" : "#245f82"
                border.color: map.follow ? "#f76707" : "#4b94ba"
            }
            onClicked: map.follow = !map.follow
        }
    }

    Text {
        visible: map.hasRoute && map.tileStatus.length > 0
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 8
        anchors.topMargin: player.mapMessage.length > 0 ? 44 : 8
        width: parent.width - 70
        wrapMode: Text.Wrap
        text: map.tileStatus
        color: "#1d2a33"
        style: Text.Outline
        styleColor: "#e6ffffff"
        font.pixelSize: 11
    }

    // Required attribution: always visible on the map, bottom-right.
    Rectangle {
        visible: map.hasRoute
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: attribution.implicitWidth + 10
        height: attribution.implicitHeight + 4
        color: "#d9ffffff"
        Text {
            id: attribution
            anchors.centerIn: parent
            textFormat: Text.StyledText
            text: "<a href=\"" + player.mapAttributionUrl + "\">" + player.mapAttribution + "</a>"
                  + (player.mapAttributionUrl.indexOf("openstreetmap.org") >= 0
                     ? " · <a href=\"https://www.openstreetmap.org/fixthemap\">Report a map issue</a>" : "")
            linkColor: "#1c4f7a"
            font.pixelSize: 11
            onLinkActivated: function(link) { Qt.openUrlExternally(link) }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.NoButton
                cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor
            }
        }
    }

    Popup {
        id: candidatePopup
        anchors.centerIn: parent
        width: Math.min(parent.width - 20, 340)
        modal: true
        padding: 8
        background: Rectangle { color: "#17212a"; border.color: "#4b94ba"; radius: 4 }
        contentItem: ColumnLayout {
            spacing: 4
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: "The route passes here " + panel.candidates.length + " times. Seek to:"
                color: "#d9e2e8"
            }
            Repeater {
                model: panel.candidates
                DarkButton {
                    Layout.fillWidth: true
                    text: modelData.timeText + "   (" + modelData.distanceMetres.toFixed(0) + " m from click)"
                          + (modelData.nearestToNow ? "  · nearest to now" : "")
                    onClicked: {
                        candidatePopup.close()
                        player.seekTicks(modelData.ticks)
                    }
                }
            }
            DarkButton { Layout.fillWidth: true; text: "Cancel"; onClicked: candidatePopup.close() }
        }
    }
}
