import QtQuick
import QtQuick.Controls

Slider {
    id: control
    property int trackHeight: 3
    property int handleSize: 14

    Theme { id: theme }

    implicitHeight: 28

    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth
        height: control.trackHeight
        radius: height / 2
        color: theme.border

        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: parent.radius
            color: control.enabled ? theme.text : theme.disabled
        }
    }

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.handleSize
        height: control.handleSize
        radius: width / 2
        color: control.enabled ? theme.text : theme.disabled
        border.color: theme.black
        border.width: 1
    }
}
