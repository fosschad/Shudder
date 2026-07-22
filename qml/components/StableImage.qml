import QtQuick

Item {
    id: control

    property url source
    property int fillMode: Image.PreserveAspectCrop
    property bool asynchronous: true
    property bool cache: true
    property int preferredSourceWidth: 0
    property int preferredSourceHeight: 0
    property color fill: theme.surface
    property color borderColor: theme.transparent
    property int borderWidth: 0
    property real radius: 0
    property bool retainWhileLoading: true
    property int retryDelayMs: 650
    property int maxRetries: 2
    property int retryCount: 0
    property url effectiveSource: source
    readonly property bool animatedSource: effectiveSource.toString().toLowerCase().indexOf(".gif") !== -1
    readonly property bool ready: animatedSource
                                   ? animatedImage.status === AnimatedImage.Ready && animatedImage.source.toString().length > 0
                                   : image.status === Image.Ready && image.source.toString().length > 0

    onSourceChanged: {
        retryTimer.stop()
        retryCount = 0
        effectiveSource = source
    }

    function scheduleRetry() {
        if (effectiveSource.toString().length === 0 || retryCount >= maxRetries) return
        retryCount += 1
        retryTimer.restart()
    }

    Timer {
        id: retryTimer
        interval: control.retryDelayMs
        repeat: false
        onTriggered: {
            const nextSource = control.source
            control.effectiveSource = ""
            control.effectiveSource = nextSource
        }
    }

    Theme { id: theme }

    Rectangle {
        anchors.fill: parent
        radius: control.radius
        color: control.fill
        border.color: control.borderColor
        border.width: control.borderWidth
    }

    Image {
        id: image
        anchors.fill: parent
        source: control.animatedSource ? "" : control.effectiveSource
        fillMode: control.fillMode
        asynchronous: control.asynchronous
        cache: control.cache
        mipmap: true
        retainWhileLoading: control.retainWhileLoading
        sourceSize.width: control.preferredSourceWidth > 0 ? control.preferredSourceWidth : 0
        sourceSize.height: control.preferredSourceHeight > 0 ? control.preferredSourceHeight : 0
        visible: !control.animatedSource && source.toString().length > 0 && (status === Image.Ready || (control.retainWhileLoading && status === Image.Loading))
        onStatusChanged: if (status === Image.Error) control.scheduleRetry()
    }

    AnimatedImage {
        id: animatedImage
        anchors.fill: parent
        source: control.animatedSource ? control.effectiveSource : ""
        fillMode: control.fillMode
        asynchronous: control.asynchronous
        cache: control.cache
        mipmap: true
        sourceSize.width: control.preferredSourceWidth > 0 ? control.preferredSourceWidth : 0
        sourceSize.height: control.preferredSourceHeight > 0 ? control.preferredSourceHeight : 0
        visible: control.animatedSource && status === AnimatedImage.Ready
        onStatusChanged: if (status === AnimatedImage.Error) control.scheduleRetry()
    }
}
