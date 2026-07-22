import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtWebEngine
import "../components" as Components

Drawer {
    id: drawer
    signal connectRequested()
    Components.Theme { id: theme }

    edge: Qt.RightEdge
    width: Math.min(560, parent ? parent.width * 0.92 : 560)
    height: parent ? parent.height : 760
    modal: false
    interactive: true
    background: Rectangle { color: theme.panel; border.color: theme.border }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        ColumnLayout {
            x: 24
            y: 20
            width: drawer.width - 48
            spacing: 16

            RowLayout {
                Layout.fillWidth: true
                Label { Layout.fillWidth: true; text: "Settings"; font.pixelSize: theme.fontHeading; font.weight: Font.DemiBold; color: theme.text }
                Components.GlassButton { text: "Close"; quiet: true; onClicked: drawer.close() }
            }

            Rectangle {
                Layout.fillWidth: true
                radius: theme.radiusLg
                color: theme.panelRaised
                border.color: theme.border
                height: accountColumn.implicitHeight + 28
                ColumnLayout {
                    id: accountColumn
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 10
                    Label { text: "Twitch Account"; color: theme.text; font.weight: Font.DemiBold }
                    Label { Layout.fillWidth: true; text: authService.signedIn ? "Signed in as " + authService.displayName : "Not signed in. Connect Twitch for follows, chat, emotes, and authenticated browsing."; color: theme.textMuted; wrapMode: Text.WordWrap }
                    Components.GlassButton { text: authService.signedIn ? "Manage Account" : "Connect Twitch"; prominent: !authService.signedIn; onClicked: drawer.connectRequested() }
                }
            }

            Rectangle {
                objectName: "websiteSessionCard"
                Layout.fillWidth: true
                Layout.preferredHeight: websiteSessionColumn.implicitHeight + theme.spaceLg * 2
                implicitHeight: websiteSessionColumn.implicitHeight + theme.spaceLg * 2
                radius: theme.radiusLg
                color: theme.panelRaised
                border.color: theme.border
                ColumnLayout {
                    id: websiteSessionColumn
                    anchors.fill: parent
                    anchors.margins: theme.spaceLg
                    spacing: 10

                    Label { text: "Twitch Website Session"; color: theme.text; font.weight: Font.DemiBold }
                    Label {
                        objectName: "secretServiceStatusLabel"
                        Layout.fillWidth: true
                        text: websiteSession.linked ? "Linked as " + websiteSession.login + ". Native playback will use this Twitch website session for Streamlink." : "Optional. Link a Twitch website session to unlock the same native qualities Streamlink can access in a browser session."
                        color: theme.textMuted
                        wrapMode: Text.WordWrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Components.GlassButton {
                            text: "Link Session"
                            enabled: !websiteSession.linking
                            prominent: !websiteSession.linked
                            onClicked: websiteSession.beginLink()
                        }
                        Components.GlassButton {
                            text: "Remove"
                            quiet: true
                            enabled: websiteSession.linked
                            onClicked: websiteSession.clear()
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: websiteSession.status.length > 0 ? websiteSession.status : (secrets.available ? "Stored in your system Secret Service." : "Secret Service is unavailable in this build; the session cannot be stored securely.")
                        color: secrets.available ? theme.textFaint : theme.textMuted
                        wrapMode: Text.WordWrap
                        font.pixelSize: theme.fontMeta
                    }
                }
            }

            Label { Layout.topMargin: 10; text: "Playback"; color: theme.textMuted; font.bold: true; font.letterSpacing: 1.1 }
            ComboBox {
                Layout.fillWidth: true
                Layout.preferredHeight: 38
                model: ["native", "standard"]
                currentIndex: model.indexOf(preferences.get("playerMode"))
                onActivated: preferences.set("playerMode", currentText)
                Accessible.name: "Playback mode"
                contentItem: Text { text: parent.displayText; color: theme.text; verticalAlignment: Text.AlignVCenter; leftPadding: 12 }
                background: Rectangle { radius: theme.radiusSm; color: theme.field; border.color: parent.activeFocus ? theme.borderStrong : theme.border }
            }
            CheckBox { text: "Dynamic audio compression"; checked: preferences.get("audioCompression"); onToggled: preferences.set("audioCompression", checked) }

            Label { text: "Chat font size: " + Math.round(chatFontSlider.value) + " px"; color: theme.textSoft }
            Components.MonoSlider { id: chatFontSlider; Layout.fillWidth: true; from: 14; to: 25; stepSize: 1; value: preferences.get("chatFontSize"); onMoved: preferences.set("chatFontSize", Math.round(value)) }
            Label { text: "Emote size: " + Math.round(emoteSizeSlider.value) + " px"; color: theme.textSoft }
            Components.MonoSlider { id: emoteSizeSlider; Layout.fillWidth: true; from: 18; to: 48; stepSize: 1; value: preferences.get("chatEmoteSize"); onMoved: preferences.set("chatEmoteSize", Math.round(value)) }

        }
    }

    Dialog {
        id: websiteSessionDialog
        parent: Overlay.overlay
        x: parent ? Math.round((parent.width - width) / 2) : 0
        y: parent ? Math.round((parent.height - height) / 2) : 0
        width: Math.min(1040, parent ? parent.width * 0.92 : 1040)
        height: Math.min(760, parent ? parent.height * 0.88 : 760)
        modal: true
        visible: websiteSession.linking
        closePolicy: Popup.CloseOnEscape
        padding: 0
        onClosed: if (websiteSession.linking) websiteSession.cancelLink()
        background: Rectangle { radius: theme.radiusXl; color: theme.panel; border.color: theme.border }

        contentItem: ColumnLayout {
            spacing: 0
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 58
                radius: theme.radiusXl
                color: theme.panelRaised
                border.color: theme.border
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 18
                    anchors.rightMargin: 12
                    spacing: 12
                    Label { Layout.fillWidth: true; text: "Link Twitch Website Session"; color: theme.text; font.pixelSize: theme.fontTitle; font.weight: Font.DemiBold }
                    Components.GlassButton { text: "Close"; quiet: true; onClicked: websiteSession.cancelLink() }
                }
            }
            Label {
                Layout.fillWidth: true
                Layout.margins: 14
                text: websiteSession.status
                color: theme.textMuted
                wrapMode: Text.WordWrap
                font.pixelSize: theme.fontMeta
            }
            WebEngineView {
                id: websiteSessionWebView
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: 14
                Layout.rightMargin: 14
                Layout.bottomMargin: 14
                url: websiteSession.linking ? websiteSession.linkUrl : "about:blank"
                settings.javascriptCanOpenWindows: false
                settings.localContentCanAccessFileUrls: false
                settings.localContentCanAccessRemoteUrls: false
                onNewWindowRequested: function(request) { request.action = WebEngineNewWindowRequest.IgnoreRequest }
                onCertificateError: function(error) { error.rejectCertificate() }
            }
        }
    }
}
