import QtQuick
import QtQuick.Controls

// One of a set of exclusive choices (Play | Capture, aspect ratios, JPEG | PNG): the checked one
// stands out. Not checkable: clicking the selected choice again must not deselect it, so the
// owner binds `selected` and sets its state in onClicked.
Button {
    id: control
    property bool selected
    implicitHeight: 30

    contentItem: Text {
        text: control.text
        color: !control.enabled ? "#71818d" : control.selected ? "#61dc97" : "#c9d6de"
        font.bold: control.selected
        font.pixelSize: control.font.pixelSize
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        implicitWidth: 60
        radius: 4
        color: !control.enabled ? "#1b2832"
              : control.selected ? "#16323f"
              : control.down ? "#174b6b"
              : control.hovered ? "#1b2833" : "#0d1419"
        border.color: control.selected ? "#61dc97" : control.hovered ? "#4b94ba" : "#40515e"
        border.width: 1
    }
}
