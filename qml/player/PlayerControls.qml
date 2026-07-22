import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components" as Components

Item {
    id: root

    property bool showTransportControls: true
    property bool showQualitySelection: showTransportControls
    property bool showStatsButton: true
    property bool showFullscreenButton: true
    property bool fullscreenActive: false
    property bool statsAvailable: false
    property int statsVideoWidth: 0
    property int statsVideoHeight: 0
    property real statsVideoFps: 0
    property real statsDisplayFps: 0
    property int statsDroppedFrames: 0
    property int statsDecoderDroppedFrames: 0
    property int statsOutputDroppedFrames: 0
    property int statsMistimedFrames: 0
    property int statsDelayedFrames: 0
    property real statsAvSync: 0
    property real statsVideoBitrate: 0
    property real statsAudioBitrate: 0
    property real statsCacheSeconds: 0
    property real statsCacheEndSeconds: 0
    property bool statsCacheIdle: false
    property string statsVideoCodec: ""
    property string statsAudioCodec: ""
    property string statsPixelFormat: ""
    property string statsHardwareDecoder: ""
    property int statsEstimatedFrameNumber: 0
    property int statsEstimatedFrameCount: 0
    property string statsSource: playerController.mode === "native" ? "Native player" : "Twitch embedded player"

    Components.Theme { id: theme }

    property bool qualityMenuOpen: false
    property bool statsVisible: false
    property bool controlsVisible: false
    property bool controlsShown: controlsVisible || qualityMenuOpen || statsVisible
    readonly property bool compactLayout: width < 760

    signal fullscreenRequested()

    function reveal() {
        controlsVisible = true
        hideTimer.restart()
    }

    function statsText(value, suffix, decimals) {
        if (value <= 0) return "--"
        return Number(value).toLocaleString(Qt.locale(), "f", decimals) + suffix
    }

    function statValue(value) {
        const text = String(value || "").trim()
        return text.length > 0 ? text : "--"
    }

    function integerText(value) {
        return value > 0 ? Number(value).toLocaleString(Qt.locale(), "f", 0) : "--"
    }

    function bitrateText(value) {
        if (value <= 0) return "--"
        if (value >= 1000000) return (value / 1000000).toLocaleString(Qt.locale(), "f", 2) + " Mbps"
        if (value >= 1000) return (value / 1000).toLocaleString(Qt.locale(), "f", 0) + " Kbps"
        return value.toLocaleString(Qt.locale(), "f", 0) + " bps"
    }

    function qualityFpsFallback() {
        const match = /p(\d+)$/.exec(String(playerController.quality || ""))
        return match ? Number(match[1]) : 0
    }

    function playbackFpsText() {
        if (root.statsVideoFps > 0) return root.statsText(root.statsVideoFps, "", 2)
        const fallback = qualityFpsFallback()
        return fallback > 0 ? root.statsText(fallback, "", 0) + " inferred" : "--"
    }

    function syncText() {
        if (!root.statsAvailable || Math.abs(root.statsAvSync) < 0.0005) return "0.0 ms"
        return (root.statsAvSync * 1000).toLocaleString(Qt.locale(), "f", 1) + " ms"
    }

    function qualityLabel(value) {
        const quality = String(value || "")
        if (quality.length === 0) return "Highest"
        if (quality === "audio_only") return "Audio Only"
        return quality
    }

    anchors.fill: parent
    z: 20

    Timer {
        id: hideTimer
        interval: Math.max(1000, preferences.get("nativeControlAutoHideMs"))
        repeat: false
        onTriggered: if (!root.qualityMenuOpen && !root.statsVisible) root.controlsVisible = false
    }

    HoverHandler {
        id: playerHover
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
    }

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        cursorShape: root.controlsShown ? Qt.ArrowCursor : Qt.BlankCursor
        onPositionChanged: root.reveal()
        onEntered: root.reveal()
        onExited: {
            hideTimer.stop()
            if (!root.qualityMenuOpen && !root.statsVisible) root.controlsVisible = false
        }
    }

    Rectangle {
        id: scrim
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Math.min(parent.height * 0.28, 150)
        opacity: root.controlsShown ? 1 : 0
        gradient: Gradient {
            GradientStop { position: 0; color: theme.scrimClear }
            GradientStop { position: 1; color: theme.scrimStrong }
        }
        Behavior on opacity { NumberAnimation { duration: theme.overlayFadeMs; easing.type: Easing.OutCubic } }
    }

    Rectangle {
        id: statsPanel
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 18
        width: Math.min(root.compactLayout ? 360 : 500, parent.width - 36)
        height: Math.min(parent.height - 36, statsColumn.implicitHeight + 28)
        radius: 18
        color: Qt.rgba(0.045, 0.045, 0.05, 0.92)
        border.color: Qt.rgba(1, 1, 1, 0.12)
        border.width: 1
        opacity: root.statsVisible ? 1 : 0
        visible: root.statsVisible || opacity > 0
        z: 30
        Behavior on opacity { NumberAnimation { duration: theme.overlayFadeMs; easing.type: Easing.OutCubic } }

        ColumnLayout {
            id: statsColumn
            anchors.fill: parent
            anchors.margins: 14
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                Label { Layout.fillWidth: true; text: "Stream Statistics"; color: theme.text; font.pixelSize: theme.fontBody; font.weight: Font.DemiBold }
                Label { text: root.statsAvailable ? "live" : "waiting"; color: root.statsAvailable ? "#86efac" : theme.textMuted; font.pixelSize: theme.fontCaption; font.weight: Font.DemiBold }
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.overlayBorderSoft }
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 18
                rowSpacing: 6

                Label { text: "Channel"; color: theme.textMuted; font.pixelSize: theme.fontCaption }
                Label { Layout.fillWidth: true; text: playerController.channel.length > 0 ? playerController.channel : "--"; color: theme.text; font.pixelSize: theme.fontCaption; elide: Text.ElideRight }
                Label { text: "Player"; color: theme.textMuted; font.pixelSize: theme.fontCaption }
                Label { Layout.fillWidth: true; text: playerController.mode === "native" ? "Native" : "Standard"; color: theme.text; font.pixelSize: theme.fontCaption; elide: Text.ElideRight }
                Label { text: "Backend"; color: theme.textMuted; font.pixelSize: theme.fontCaption }
                Label { Layout.fillWidth: true; text: root.statValue(root.statsSource); color: theme.text; font.pixelSize: theme.fontCaption; elide: Text.ElideRight }
                Label { text: "Status"; color: theme.textMuted; font.pixelSize: theme.fontCaption }
                Label { Layout.fillWidth: true; text: root.statValue(playerController.status); color: theme.text; font.pixelSize: theme.fontCaption; elide: Text.ElideRight }
                Label { text: "Quality"; color: theme.textMuted; font.pixelSize: theme.fontCaption }
                Label { Layout.fillWidth: true; text: playerController.mode === "native" ? root.qualityLabel(playerController.quality) : "Managed by Twitch"; color: theme.text; font.pixelSize: theme.fontCaption; elide: Text.ElideRight }
                Label { text: "Video"; color: theme.textMuted; font.pixelSize: theme.fontCaption }
                Label { Layout.fillWidth: true; text: root.statsAvailable && root.statsVideoWidth > 0 && root.statsVideoHeight > 0 ? root.statsVideoWidth + "x" + root.statsVideoHeight : "--"; color: theme.text; font.pixelSize: theme.fontCaption; elide: Text.ElideRight }
                Label { text: "FPS"; color: theme.textMuted; font.pixelSize: theme.fontCaption }
                Label { Layout.fillWidth: true; text: root.playbackFpsText(); color: theme.text; font.pixelSize: theme.fontCaption; elide: Text.ElideRight }
                Label { text: "Display FPS"; color: theme.textMuted; font.pixelSize: theme.fontCaption }
                Label { Layout.fillWidth: true; text: root.statsText(root.statsDisplayFps, "", 2); color: theme.text; font.pixelSize: theme.fontCaption; elide: Text.ElideRight }
                Label { text: "Video Codec"; color: theme.textMuted; font.pixelSize: theme.fontCaption }
                Label { Layout.fillWidth: true; text: root.statValue(root.statsVideoCodec); color: theme.text; font.pixelSize: theme.fontCaption; elide: Text.ElideRight }
                Label { text: "Audio Codec"; color: theme.textMuted; font.pixelSize: theme.fontCaption }
                Label { Layout.fillWidth: true; text: root.statValue(root.statsAudioCodec); color: theme.text; font.pixelSize: theme.fontCaption; elide: Text.ElideRight }
                Label { text: "Pixel Format"; color: theme.textMuted; font.pixelSize: theme.fontCaption }
                Label { Layout.fillWidth: true; text: root.statValue(root.statsPixelFormat); color: theme.text; font.pixelSize: theme.fontCaption; elide: Text.ElideRight }
                Label { text: "Hardware Dec"; color: theme.textMuted; font.pixelSize: theme.fontCaption }
                Label { Layout.fillWidth: true; text: root.statValue(root.statsHardwareDecoder); color: theme.text; font.pixelSize: theme.fontCaption; elide: Text.ElideRight }
                Label { text: "Video Bitrate"; color: theme.textMuted; font.pixelSize: theme.fontCaption }
                Label { Layout.fillWidth: true; text: root.bitrateText(root.statsVideoBitrate); color: theme.text; font.pixelSize: theme.fontCaption; elide: Text.ElideRight }
                Label { text: "Audio Bitrate"; color: theme.textMuted; font.pixelSize: theme.fontCaption }
                Label { Layout.fillWidth: true; text: root.bitrateText(root.statsAudioBitrate); color: theme.text; font.pixelSize: theme.fontCaption; elide: Text.ElideRight }
                Label { text: "Dropped"; color: theme.textMuted; font.pixelSize: theme.fontCaption }
                Label { Layout.fillWidth: true; text: root.statsAvailable ? root.statsDroppedFrames.toLocaleString(Qt.locale(), "f", 0) : "--"; color: theme.text; font.pixelSize: theme.fontCaption; elide: Text.ElideRight }
                Label { text: "Decoder Drop"; color: theme.textMuted; font.pixelSize: theme.fontCaption }
                Label { Layout.fillWidth: true; text: root.statsAvailable ? root.statsDecoderDroppedFrames.toLocaleString(Qt.locale(), "f", 0) : "--"; color: theme.text; font.pixelSize: theme.fontCaption; elide: Text.ElideRight }
                Label { text: "Output Drop"; color: theme.textMuted; font.pixelSize: theme.fontCaption }
                Label { Layout.fillWidth: true; text: root.statsAvailable ? root.statsOutputDroppedFrames.toLocaleString(Qt.locale(), "f", 0) : "--"; color: theme.text; font.pixelSize: theme.fontCaption; elide: Text.ElideRight }
                Label { text: "Mistimed"; color: theme.textMuted; font.pixelSize: theme.fontCaption }
                Label { Layout.fillWidth: true; text: root.statsAvailable ? root.statsMistimedFrames.toLocaleString(Qt.locale(), "f", 0) : "--"; color: theme.text; font.pixelSize: theme.fontCaption; elide: Text.ElideRight }
                Label { text: "Delayed"; color: theme.textMuted; font.pixelSize: theme.fontCaption }
                Label { Layout.fillWidth: true; text: root.statsAvailable ? root.statsDelayedFrames.toLocaleString(Qt.locale(), "f", 0) : "--"; color: theme.text; font.pixelSize: theme.fontCaption; elide: Text.ElideRight }
                Label { text: "A/V Sync"; color: theme.textMuted; font.pixelSize: theme.fontCaption }
                Label { Layout.fillWidth: true; text: root.syncText(); color: theme.text; font.pixelSize: theme.fontCaption; elide: Text.ElideRight }
                Label { text: "Buffer"; color: theme.textMuted; font.pixelSize: theme.fontCaption }
                Label { Layout.fillWidth: true; text: root.statsText(root.statsCacheSeconds, " s", 1); color: theme.text; font.pixelSize: theme.fontCaption; elide: Text.ElideRight }
                Label { text: "Cache End"; color: theme.textMuted; font.pixelSize: theme.fontCaption }
                Label { Layout.fillWidth: true; text: root.statsText(root.statsCacheEndSeconds, " s", 1); color: theme.text; font.pixelSize: theme.fontCaption; elide: Text.ElideRight }
                Label { text: "Cache State"; color: theme.textMuted; font.pixelSize: theme.fontCaption }
                Label { Layout.fillWidth: true; text: root.statsAvailable ? (root.statsCacheIdle ? "idle" : "filling") : "--"; color: theme.text; font.pixelSize: theme.fontCaption; elide: Text.ElideRight }
                Label { text: "Frame"; color: theme.textMuted; font.pixelSize: theme.fontCaption }
                Label { Layout.fillWidth: true; text: root.statsEstimatedFrameNumber > 0 || root.statsEstimatedFrameCount > 0 ? root.integerText(root.statsEstimatedFrameNumber) + " / " + root.integerText(root.statsEstimatedFrameCount) : "--"; color: theme.text; font.pixelSize: theme.fontCaption; elide: Text.ElideRight }
                Label { text: "State"; color: theme.textMuted; font.pixelSize: theme.fontCaption }
                Label { Layout.fillWidth: true; text: playerController.paused ? "Paused" : "Playing"; color: theme.text; font.pixelSize: theme.fontCaption; elide: Text.ElideRight }
                Label { text: "Volume"; color: theme.textMuted; font.pixelSize: theme.fontCaption }
                Label { Layout.fillWidth: true; text: playerController.muted ? "Muted" : playerController.volume + "%"; color: theme.text; font.pixelSize: theme.fontCaption; elide: Text.ElideRight }
            }
        }
    }

    Rectangle {
        id: controlSurface
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 78
        color: "transparent"
        opacity: root.controlsShown ? 1 : 0
        visible: root.controlsShown || opacity > 0
        Behavior on opacity { NumberAnimation { duration: theme.overlayFadeMs; easing.type: Easing.OutCubic } }

        MouseArea {
            id: controlsMouse
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.NoButton
            onPositionChanged: root.reveal()
            onEntered: root.reveal()
        }

        Rectangle {
            id: controlDock
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: root.compactLayout ? 10 : 16
            width: Math.min(parent.width - 20, controlRow.implicitWidth + 22)
            height: root.compactLayout ? 48 : 50
            radius: height / 2
            color: Qt.rgba(0.03, 0.03, 0.035, 0.74)
            border.color: Qt.rgba(1, 1, 1, 0.13)
            border.width: 1

        RowLayout {
            id: controlRow
            anchors.fill: parent
            anchors.leftMargin: 11
            anchors.rightMargin: 11
            spacing: 8

            Components.IconButton {
                id: playButton
                visible: root.showTransportControls
                Layout.preferredWidth: 36
                Layout.preferredHeight: 36
                Layout.alignment: Qt.AlignVCenter
                iconName: playerController.paused ? "play" : "pause"
                prominent: playerController.paused
                onClicked: { playerController.paused = !playerController.paused; root.reveal() }
            }

            Components.IconButton {
                id: muteButton
                visible: root.showTransportControls
                property bool effectiveMuted: playerController.muted || playerController.volume <= 0
                Layout.preferredWidth: 36
                Layout.preferredHeight: 36
                Layout.alignment: Qt.AlignVCenter
                iconName: effectiveMuted ? "muted" : "volume"
                onClicked: {
                    if (effectiveMuted) {
                        if (playerController.volume <= 0) playerController.volume = 35
                        playerController.muted = false
                    } else {
                        playerController.muted = true
                    }
                    root.reveal()
                }
            }

            Components.MonoSlider {
                visible: root.showTransportControls && !root.compactLayout
                Layout.preferredWidth: Math.max(120, Math.min(220, root.width * 0.20))
                Layout.alignment: Qt.AlignVCenter
                from: 0
                to: 200
                value: playerController.volume
                onMoved: {
                    playerController.volume = value
                    playerController.muted = value <= 0 ? true : false
                    root.reveal()
                }
            }

            Item { Layout.fillWidth: true }

            ComboBox {
                id: qualityBox
                visible: root.showQualitySelection && playerController.qualityOptions.length > 0
                Layout.minimumWidth: root.compactLayout ? 108 : 124
                Layout.preferredWidth: root.compactLayout ? 108 : 132
                Layout.preferredHeight: 32
                Layout.alignment: Qt.AlignVCenter
                model: playerController.qualityOptions
                font.pixelSize: theme.fontMeta
                leftPadding: 12
                rightPadding: 30

                function optionIndex(value) {
                    for (let i = 0; i < playerController.qualityOptions.length; ++i) {
                        if (playerController.qualityOptions[i] === value) return i
                    }
                    return 0
                }

                function syncQuality() {
                    currentIndex = optionIndex(playerController.quality)
                }

                Component.onCompleted: syncQuality()
                onDownChanged: {
                    root.qualityMenuOpen = down
                    if (down) root.reveal()
                }
                Connections {
                    target: playerController
                    function onQualityChanged() { qualityBox.syncQuality() }
                    function onQualityOptionsChanged() { qualityBox.syncQuality() }
                }
                onActivated: function(index) {
                    const nextQuality = playerController.qualityOptions[index]
                    playerController.quality = nextQuality
                    preferences.set("nativeQuality", nextQuality)
                    root.qualityMenuOpen = false
                    root.reveal()
                }
                contentItem: Text {
                    text: root.qualityLabel(qualityBox.currentText)
                    color: qualityBox.enabled ? theme.text : theme.disabled
                    font: qualityBox.font
                    leftPadding: qualityBox.leftPadding
                    rightPadding: qualityBox.rightPadding
                    horizontalAlignment: Text.AlignLeft
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideNone
                }
                indicator: Canvas {
                    x: qualityBox.width - width - 11
                    y: qualityBox.topPadding + qualityBox.availableHeight / 2 - height / 2
                    width: 9
                    height: 6
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset()
                        ctx.strokeStyle = qualityBox.enabled ? theme.textMuted : theme.disabled
                        ctx.lineWidth = 1.6
                        ctx.lineCap = "round"
                        ctx.lineJoin = "round"
                        ctx.beginPath()
                        ctx.moveTo(1, 1)
                        ctx.lineTo(width / 2, height - 1)
                        ctx.lineTo(width - 1, 1)
                        ctx.stroke()
                    }
                }
                background: Rectangle {
                    radius: height / 2
                    color: qualityBox.down ? Qt.rgba(1, 1, 1, 0.16) : (qualityBox.hovered ? Qt.rgba(1, 1, 1, 0.11) : Qt.rgba(1, 1, 1, 0.07))
                    border.color: qualityBox.activeFocus ? theme.focusRing : Qt.rgba(1, 1, 1, 0.12)
                    Behavior on color { ColorAnimation { duration: theme.animFast } }
                    Behavior on border.color { ColorAnimation { duration: theme.animFast } }
                }
                delegate: ItemDelegate {
                    id: qualityOption
                    width: qualityBox.popup.width - 12
                    height: 34
                    highlighted: qualityBox.highlightedIndex === index
                    padding: 0
                    contentItem: RowLayout {
                        spacing: 10
                        Rectangle {
                            Layout.preferredWidth: 5
                            Layout.preferredHeight: 5
                            radius: 2.5
                            visible: qualityBox.currentIndex === index
                            color: theme.text
                        }
                        Item { visible: qualityBox.currentIndex !== index; Layout.preferredWidth: 5; Layout.preferredHeight: 5 }
                        Text {
                            Layout.fillWidth: true
                            text: root.qualityLabel(modelData)
                            color: qualityOption.enabled ? theme.text : theme.disabled
                            font.pixelSize: theme.fontMeta
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                    }
                    background: Rectangle {
                        radius: theme.radiusSm
                        color: qualityOption.down ? theme.surfacePressed : (qualityOption.highlighted ? theme.surfaceHover : theme.transparent)
                    }
                }
                popup: Popup {
                    y: -height - 8
                    width: Math.max(qualityBox.width, 168)
                    implicitHeight: Math.min(contentItem.implicitHeight + topPadding + bottomPadding, 348)
                    padding: 6
                    modal: false
                    focus: true
                    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
                    background: Rectangle {
                        radius: theme.radiusMd
                        color: theme.panelRaised
                        border.color: theme.borderStrong
                        border.width: 1
                    }
                    contentItem: ListView {
                        clip: true
                        implicitHeight: contentHeight
                        model: qualityBox.popup.visible ? qualityBox.delegateModel : null
                        currentIndex: qualityBox.highlightedIndex
                        boundsBehavior: Flickable.StopAtBounds
                    }
                }
            }

            Components.IconButton {
                visible: root.showStatsButton
                Layout.preferredWidth: 36
                Layout.preferredHeight: 36
                Layout.alignment: Qt.AlignVCenter
                iconName: "stats"
                prominent: root.statsVisible
                onClicked: { root.statsVisible = !root.statsVisible; root.reveal() }
            }

            Components.IconButton {
                visible: root.showFullscreenButton
                Layout.preferredWidth: 36
                Layout.preferredHeight: 36
                Layout.alignment: Qt.AlignVCenter
                iconName: "fullscreen"
                prominent: root.fullscreenActive
                onClicked: { root.fullscreenRequested(); root.reveal() }
            }

        }
        }
    }
}
