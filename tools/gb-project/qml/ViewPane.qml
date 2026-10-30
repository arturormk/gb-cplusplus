import QtQuick

Rectangle {
    id: root
    property string title: "View"
    property url source
    color: "#10161d"
    border.color: "#31404d"
    border.width: 1
    radius: 4

    Text {
        id: heading
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 8
        text: root.title
        color: "#61dc97"
        font.bold: true
        font.pixelSize: 13
        z: 2
    }

    Image {
        anchors.fill: parent
        anchors.margins: 4
        source: root.source
        fillMode: Image.PreserveAspectFit
        asynchronous: false
        cache: false
        smooth: true
    }
}
