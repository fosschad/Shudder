import QtQuick
import QtQuick.Controls

Button {
    id: control
    property bool prominent: false
    property bool quiet: false
    property bool compact: false

    Theme { id: theme }

    implicitHeight: compact ? theme.compactControlHeight : theme.controlHeight
    leftPadding: compact ? 10 : 16
    rightPadding: compact ? 10 : 16
    topPadding: compact ? 4 : 7
    bottomPadding: compact ? 4 : 7
    font.pixelSize: compact ? theme.fontCaption : theme.fontMeta
    font.weight: prominent ? Font.DemiBold : Font.Medium

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.enabled ? (control.prominent ? theme.black : theme.text) : theme.disabled
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: theme.radiusMd
        border.width: 1
        border.color: control.activeFocus ? theme.focusRing : (control.quiet && !control.hovered && !control.down && !control.prominent ? theme.transparent : (!control.enabled ? theme.border : (control.prominent ? theme.text : (control.hovered || control.down ? theme.borderStrong : theme.border))))
        color: !control.enabled ? theme.field : (control.prominent ? (control.down ? theme.textSoft : theme.text) : (control.down ? theme.surfacePressed : (control.hovered ? theme.surfaceHover : (control.quiet ? theme.transparent : theme.surface))))
        Behavior on color { ColorAnimation { duration: theme.animFast } }
        Behavior on border.color { ColorAnimation { duration: theme.animFast } }
    }
}
