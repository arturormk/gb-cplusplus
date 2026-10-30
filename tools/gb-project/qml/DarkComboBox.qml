import QtQuick
import QtQuick.Controls

ComboBox {
    id: control
    implicitWidth: Math.max(120, contentItem.implicitWidth + indicator.width + 28)
    implicitHeight: 34

    contentItem: Text {
        leftPadding: 10
        rightPadding: control.indicator.width + 18
        text: control.displayText
        color: control.enabled ? "#ffffff" : "#71818d"
        font: control.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Text {
        x: control.width - width - 9
        y: Math.round((control.height - height) / 2)
        text: "▾"
        color: control.enabled ? "#ffffff" : "#71818d"
        font.pixelSize: 14
    }

    background: Rectangle {
        color: control.enabled ? "#080b0e" : "#151c22"
        border.color: control.activeFocus ? "#4b94ba" : "#40515e"
        border.width: 1
        radius: 3
    }

    delegate: ItemDelegate {
        id: option
        required property int index
        width: control.width
        highlighted: control.highlightedIndex === index

        contentItem: Text {
            text: control.textAt(option.index)
            color: "#ffffff"
            font: control.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Rectangle {
            color: option.highlighted ? "#245f82"
                  : option.hovered ? "#173c52"
                  : "#080b0e"
        }
    }

    popup: Popup {
        y: control.height - 1
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + 2, 240)
        padding: 1

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }

        background: Rectangle {
            color: "#080b0e"
            border.color: "#4b94ba"
            border.width: 1
            radius: 3
        }
    }
}
