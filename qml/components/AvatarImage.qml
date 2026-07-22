import QtQuick
import QtQuick.Controls
import QtQuick.Effects

Item {
    id: avatar

    property url source
    property string fallbackText: ""
    property color fill: theme.field
    property color borderColor: theme.borderStrong
    property color textColor: theme.text
    property int borderWidth: 1
    readonly property bool hasSource: image.source.toString().length > 0
    readonly property bool imageReady: hasSource && image.status !== Image.Error

    implicitWidth: 36
    implicitHeight: 36

    Theme { id: theme }

    function initials(name) {
        const value = String(name).trim()
        if (value.length === 0) return "?"
        const parts = value.split(/\s+/)
        if (parts.length > 1) return (parts[0].charAt(0) + parts[1].charAt(0)).toUpperCase()
        return value.charAt(0).toUpperCase()
    }

    Rectangle {
        anchors.fill: parent
        radius: Math.min(width, height) / 2
        color: avatar.fill
        antialiasing: true
    }

    Item {
        anchors.fill: parent
        anchors.margins: Math.max(1, avatar.borderWidth)
        visible: avatar.imageReady
        layer.enabled: true
        layer.smooth: true
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: avatarMask
        }

        Image {
            id: image
            anchors.fill: parent
            source: avatar.source
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            mipmap: true
            retainWhileLoading: true
            smooth: true
            sourceSize.width: Math.max(96, avatar.width * 4)
            sourceSize.height: Math.max(96, avatar.height * 4)
        }
    }

    Item {
        id: avatarMask
        anchors.fill: parent
        anchors.margins: Math.max(1, avatar.borderWidth)
        visible: false
        layer.enabled: true

        Rectangle {
            anchors.fill: parent
            radius: Math.min(width, height) / 2
            color: "white"
            antialiasing: true
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: Math.min(width, height) / 2
        color: theme.transparent
        border.color: avatar.borderColor
        border.width: avatar.borderWidth
        antialiasing: true
    }

    Label {
        anchors.centerIn: parent
        visible: !avatar.hasSource || image.status === Image.Error
        text: avatar.initials(avatar.fallbackText)
        color: avatar.textColor
        font.pixelSize: Math.max(11, avatar.width * 0.38)
        font.weight: Font.DemiBold
    }

}
