import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: row
    signal openRequested()

    property string title: ""
    property string subtitle: ""
    property string image: ""
    property int viewerCount: 0
    property bool live: false
    property bool selected: false

    Theme { id: theme }

    function compactViewerCountText(count) {
        const viewers = Math.round(Number(count))
        if (viewers < 1000) return viewers.toLocaleString(Qt.locale(), "f", 0)
        const thousands = viewers / 1000
        return thousands.toLocaleString(Qt.locale(), "f", thousands >= 10 ? 0 : 1) + "K"
    }

    height: theme.rowHeight
    Accessible.role: Accessible.Button
    Accessible.name: title

    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        anchors.topMargin: 3
        anchors.bottomMargin: 3
        radius: theme.radiusMd
        color: row.selected ? theme.surfacePressed : (mouse.containsMouse ? theme.surfaceHover : "transparent")
        border.color: row.selected ? theme.borderStrong : "transparent"
        border.width: 1
        Behavior on color { ColorAnimation { duration: theme.animFast } }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.leftMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        width: 2
        height: row.selected ? 30 : 0
        radius: 1
        color: theme.text
        Behavior on height { NumberAnimation { duration: theme.animFast; easing.type: Easing.OutCubic } }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        onClicked: row.openRequested()
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 10

        AvatarImage {
            Layout.preferredWidth: 40
            Layout.preferredHeight: 40
            source: row.image
            fallbackText: row.title
            fill: theme.surface
            borderColor: mouse.containsMouse || row.selected ? theme.borderStrong : theme.border
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1
            Label {
                Layout.fillWidth: true
                text: row.title
                color: theme.text
                font.pixelSize: theme.fontBody
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Label {
                Layout.fillWidth: true
                text: row.subtitle
                color: theme.textMuted
                font.pixelSize: theme.fontMeta
                elide: Text.ElideRight
            }
        }

        RowLayout {
            visible: row.live && row.viewerCount > 0
            spacing: 5
            Rectangle {
                Layout.preferredWidth: 5
                Layout.preferredHeight: 5
                radius: 2.5
                color: "#ff3b30"
            }
            Label {
                text: row.compactViewerCountText(row.viewerCount)
                color: theme.textSoft
                font.pixelSize: theme.fontCaption
            }
        }
    }
}
