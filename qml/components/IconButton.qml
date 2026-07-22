import QtQuick
import QtQuick.Controls

Button {
    id: control
    property bool prominent: false
    property string iconName: text

    Theme { id: theme }

    implicitWidth: 42
    implicitHeight: 42
    padding: 0
    font.pixelSize: 13
    font.weight: Font.DemiBold

    contentItem: Item {
        Canvas {
            id: iconCanvas
            anchors.fill: parent
            Connections {
                target: control
                function onIconNameChanged() { iconCanvas.requestPaint() }
                function onProminentChanged() { iconCanvas.requestPaint() }
                function onEnabledChanged() { iconCanvas.requestPaint() }
                function onHoveredChanged() { iconCanvas.requestPaint() }
            }
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const ink = control.enabled ? (control.prominent ? theme.black : theme.text) : theme.disabled
                const cx = width / 2
                const cy = height / 2
                ctx.strokeStyle = ink
                ctx.fillStyle = ink
                ctx.lineWidth = 2
                ctx.lineCap = "round"
                ctx.lineJoin = "round"
                if (control.iconName === "play") {
                    ctx.beginPath()
                    ctx.moveTo(cx - 5, cy - 9)
                    ctx.lineTo(cx - 5, cy + 9)
                    ctx.lineTo(cx + 9, cy)
                    ctx.closePath()
                    ctx.fill()
                } else if (control.iconName === "pause") {
                    ctx.fillRect(cx - 8, cy - 9, 4, 18)
                    ctx.fillRect(cx + 4, cy - 9, 4, 18)
                } else if (control.iconName === "stats") {
                    ctx.fillRect(cx - 11, cy + 2, 4, 8)
                    ctx.fillRect(cx - 2, cy - 5, 4, 15)
                    ctx.fillRect(cx + 7, cy - 10, 4, 20)
                } else if (control.iconName === "fullscreen") {
                    const s = 10
                    ctx.beginPath()
                    ctx.moveTo(cx - s, cy - 4); ctx.lineTo(cx - s, cy - s); ctx.lineTo(cx - 4, cy - s)
                    ctx.moveTo(cx + 4, cy - s); ctx.lineTo(cx + s, cy - s); ctx.lineTo(cx + s, cy - 4)
                    ctx.moveTo(cx + s, cy + 4); ctx.lineTo(cx + s, cy + s); ctx.lineTo(cx + 4, cy + s)
                    ctx.moveTo(cx - 4, cy + s); ctx.lineTo(cx - s, cy + s); ctx.lineTo(cx - s, cy + 4)
                    ctx.stroke()
                } else if (control.iconName === "volume" || control.iconName === "muted") {
                    ctx.lineWidth = 1.8
                    ctx.beginPath()
                    ctx.moveTo(cx - 10, cy - 5)
                    ctx.lineTo(cx - 6, cy - 5)
                    ctx.lineTo(cx, cy - 10)
                    ctx.lineTo(cx, cy + 10)
                    ctx.lineTo(cx - 6, cy + 5)
                    ctx.lineTo(cx - 10, cy + 5)
                    ctx.closePath()
                    ctx.stroke()
                    if (control.iconName === "muted") {
                        ctx.beginPath()
                        ctx.moveTo(cx + 6, cy - 5.5)
                        ctx.lineTo(cx + 12, cy + 5.5)
                        ctx.moveTo(cx + 12, cy - 5.5)
                        ctx.lineTo(cx + 6, cy + 5.5)
                        ctx.stroke()
                    } else {
                        ctx.beginPath()
                        ctx.arc(cx + 1, cy, 6, -0.72, 0.72)
                        ctx.stroke()
                        ctx.beginPath()
                        ctx.arc(cx + 2, cy, 10, -0.62, 0.62)
                        ctx.stroke()
                    }
                }
            }
        }
        Text {
            anchors.centerIn: parent
            visible: control.iconName !== "play" && control.iconName !== "pause" && control.iconName !== "stats" && control.iconName !== "fullscreen" && control.iconName !== "volume" && control.iconName !== "muted"
            text: control.text
            color: control.enabled ? (control.prominent ? theme.black : theme.text) : theme.disabled
            font: control.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    background: Rectangle {
        radius: width / 2
        color: !control.enabled ? theme.field : (control.prominent ? theme.text : (control.down ? theme.surfacePressed : (control.hovered ? theme.surfaceHover : theme.transparent)))
        border.width: control.activeFocus ? 1 : 0
        border.color: theme.focusRing
        Behavior on color { ColorAnimation { duration: theme.animFast } }
    }
}
