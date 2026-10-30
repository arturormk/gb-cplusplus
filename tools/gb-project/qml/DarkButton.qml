import QtQuick
import QtQuick.Controls

Button {
    id: control

    contentItem: Text {
        text: control.text
        color: control.enabled ? "#edf6fb" : "#71818d"
        font: control.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        implicitWidth: 100
        implicitHeight: 34
        radius: 4
        color: !control.enabled ? "#1b2832"
              : control.down ? "#174b6b"
              : control.hovered ? "#2f789f"
              : "#245f82"
        border.color: control.enabled ? "#4b94ba" : "#344550"
        border.width: 1
    }
}
