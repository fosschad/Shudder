import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Frame {
    id: card
    objectName: "streamCard"
    signal openRequested()
    signal categoryRequested()
    property bool cardHoverEnabled: true

    Theme { id: theme }

    function wholeViewerCountText(count) {
        return Math.round(Number(count)).toLocaleString(Qt.locale(), "f", 0)
    }

    padding: 0
    background: Rectangle {
        radius: theme.radiusLg
        color: card.cardHoverEnabled && mouse.containsMouse ? theme.surfaceHover : (kind === "category" ? theme.transparent : theme.panelRaised)
        border.color: card.cardHoverEnabled && mouse.containsMouse ? theme.borderStrong : (kind === "category" ? theme.transparent : theme.border)
        border.width: 1
        Behavior on color { ColorAnimation { duration: theme.animNormal } }
        Behavior on border.color { ColorAnimation { duration: theme.animNormal } }
    }
    Accessible.role: Accessible.Button
    Accessible.name: title.length > 0 ? title : displayName

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: card.cardHoverEnabled
        onClicked: card.openRequested()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: kind === "category" ? 6 : 10
        spacing: kind === "category" ? 8 : 10

        Rectangle {
            objectName: "thumbnailSurface"
            Layout.fillWidth: true
            Layout.preferredHeight: kind === "category" ? Math.max(128, (card.width - 12) * 1.32) : 148
            radius: theme.radiusMd
            color: theme.surface
            border.color: kind === "category" ? theme.border : theme.transparent
            border.width: kind === "category" ? 1 : 0
            clip: true
            Label {
                anchors.centerIn: parent
                width: parent.width - 24
                text: kind === "category" ? category : displayName
                color: theme.textMuted
                font.pixelSize: kind === "category" ? theme.fontBody : theme.fontMeta
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }
            StableImage {
                id: cardImage
                anchors.fill: parent
                source: thumbnail
                fillMode: Image.PreserveAspectCrop
                fill: theme.transparent
                preferredSourceWidth: kind === "category" ? 342 : 440
                preferredSourceHeight: kind === "category" ? 456 : 248
            }
            Rectangle {
                anchors.fill: parent
                visible: kind !== "category" && cardImage.ready
                gradient: Gradient {
                    GradientStop { position: 0; color: theme.scrimClear }
                    GradientStop { position: 1; color: theme.scrimMedium }
                }
            }
            Rectangle {
                visible: kind !== "category" && viewerCount > 0
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 10
                radius: theme.radiusXs
                color: theme.scrimDeeper
                border.color: theme.overlayBorder
                border.width: 1
                width: viewerLabel.width + 18
                height: viewerLabel.height + 10
                Label { id: viewerLabel; anchors.centerIn: parent; text: card.wholeViewerCountText(viewerCount) + " watching"; color: theme.text; font.pixelSize: theme.fontCaption }
            }
        }

        Label { Layout.fillWidth: true; text: kind === "category" ? category : displayName; color: theme.text; font.weight: Font.DemiBold; font.pixelSize: kind === "category" ? 16 : 15; elide: Text.ElideRight }
        Label { Layout.fillWidth: true; visible: kind === "category"; text: viewerCount > 0 ? card.wholeViewerCountText(viewerCount) + " watching" : " "; color: theme.textMuted; font.pixelSize: theme.fontCaption; elide: Text.ElideRight }
        Label { Layout.fillWidth: true; text: title; visible: kind !== "category" && title.length > 0; color: theme.textSoft; elide: Text.ElideRight; font.pixelSize: theme.fontMeta }
        RowLayout {
            Layout.fillWidth: true
            visible: kind !== "category"
            spacing: 6
            Label { text: uptime; color: theme.textMuted; font.pixelSize: theme.fontCaption }
            Label { text: language.length > 0 ? language.toUpperCase() : ""; color: theme.textFaint; font.pixelSize: theme.fontCaption }
            Item { Layout.fillWidth: true }
            GlassButton {
                visible: category.length > 0 && kind !== "category"
                text: category
                compact: true
                quiet: true
                onClicked: card.categoryRequested()
            }
        }
        Flow {
            Layout.fillWidth: true
            visible: kind !== "category"
            spacing: 5
            Repeater {
                model: tags
                delegate: Rectangle {
                    visible: index < 3
                    radius: theme.radiusXs
                    color: theme.surface
                    height: 22
                    width: tagLabel.width + 12
                    Label { id: tagLabel; anchors.centerIn: parent; text: modelData; color: theme.textMuted; font.pixelSize: 10 }
                }
            }
        }
    }
}
