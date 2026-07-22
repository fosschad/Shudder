import QtQuick
import QtQuick.Controls
import Shudder.Playback 1.0
import "../components" as Components

Item {
    id: root
    property bool fullscreenActive: false
    signal fullscreenRequested()

    Components.Theme { id: theme }

    Rectangle { anchors.fill: parent; color: theme.black }
    MpvVideoItem {
        id: mpv
        anchors.fill: parent
        source: playerController.nativeSource
        paused: playerController.paused
        muted: playerController.muted
        volume: playerController.volume
    }
    Label {
        anchors.centerIn: parent
        visible: playerController.nativeSource.length === 0
        text: playerController.status.length > 0 ? playerController.status : "Preparing stream..."
        color: theme.text
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        width: parent.width * 0.7
    }
    PlayerControls {
        fullscreenActive: root.fullscreenActive
        statsAvailable: playerController.nativeSource.length > 0
        statsVideoWidth: mpv.videoWidth
        statsVideoHeight: mpv.videoHeight
        statsVideoFps: mpv.videoFps
        statsDisplayFps: mpv.displayFps
        statsDroppedFrames: mpv.droppedFrames
        statsDecoderDroppedFrames: mpv.decoderDroppedFrames
        statsOutputDroppedFrames: mpv.outputDroppedFrames
        statsMistimedFrames: mpv.mistimedFrames
        statsDelayedFrames: mpv.delayedFrames
        statsAvSync: mpv.avSync
        statsVideoBitrate: mpv.videoBitrate
        statsAudioBitrate: mpv.audioBitrate
        statsCacheSeconds: mpv.cacheSeconds
        statsCacheEndSeconds: mpv.cacheEndSeconds
        statsCacheIdle: mpv.cacheIdle
        statsVideoCodec: mpv.videoCodec
        statsAudioCodec: mpv.audioCodec
        statsPixelFormat: mpv.pixelFormat
        statsHardwareDecoder: mpv.hardwareDecoder
        statsEstimatedFrameNumber: mpv.estimatedFrameNumber
        statsEstimatedFrameCount: mpv.estimatedFrameCount
        statsSource: playerController.nativeSource.length > 0 ? "Streamlink / mpv" : "Resolving stream"
        onFullscreenRequested: root.fullscreenRequested()
    }
}
