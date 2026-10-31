import QtQuick
import QtQuick.Controls

// A scroll bar along an edge that is hidden until the pointer is over it or it is dragged, for
// a view whose own drags and wheel do something else. It takes no presses while there is nothing
// to scroll.
ScrollBar {
    id: control
    readonly property bool shown: control.size < 1.0 && (control.hovered || control.pressed)
    enabled: control.size < 1.0
    hoverEnabled: true
    policy: ScrollBar.AsNeeded
    minimumSize: 0.05
    padding: 2
    // The style's own show-and-fade animations would fight the opacity bindings below.
    states: []
    transitions: []

    contentItem: Rectangle {
        implicitWidth: 8
        implicitHeight: 8
        radius: 4
        color: control.pressed ? "#61dc97" : "#4b94ba"
        opacity: control.shown ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 120 } }
    }

    background: Rectangle {
        implicitWidth: 12
        implicitHeight: 12
        color: "#cc0b1015"
        opacity: control.shown ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 120 } }
    }
}
