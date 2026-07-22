import QtQuick
import QtWebEngine
import "../components" as Components

Item {
    id: root
    property bool fullscreenActive: false
    signal fullscreenRequested()

    Components.Theme { id: theme }

    Rectangle { anchors.fill: parent; color: theme.black }
    WebEngineView {
        id: web
        anchors.fill: parent
        url: playerController.standardUrl
        settings.javascriptCanOpenWindows: false
        settings.localStorageEnabled: false
        settings.localContentCanAccessFileUrls: false
        settings.localContentCanAccessRemoteUrls: false
        onNewWindowRequested: function(request) { request.action = WebEngineNewWindowRequest.IgnoreRequest }
        onCertificateError: function(error) { error.rejectCertificate() }
        onLoadingChanged: function(loadRequest) {
            if (loadRequest.status === WebEngineLoadingInfo.LoadFailedStatus) console.warn("Standard player load failed", loadRequest.errorString)
        }
    }
    PlayerControls {
        showTransportControls: false
        showQualitySelection: false
        fullscreenActive: root.fullscreenActive
        statsSource: "Twitch embedded player"
        onFullscreenRequested: root.fullscreenRequested()
    }
}
