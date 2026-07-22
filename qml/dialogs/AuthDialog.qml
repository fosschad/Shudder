import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components" as Components

Drawer {
    id: drawer

    Components.Theme { id: theme }

    edge: Qt.RightEdge
    width: Math.min(520, parent ? parent.width * 0.92 : 520)
    height: parent ? parent.height : 760
    modal: true
    dim: true
    interactive: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle {
        color: theme.panel
        Rectangle { anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 1; color: theme.borderStrong }
    }

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
                Label { Layout.fillWidth: true; text: "Connect Twitch"; font.pixelSize: theme.fontHeading; font.weight: Font.DemiBold; color: theme.text }
                Components.GlassButton { text: "Close"; quiet: true; onClicked: drawer.close() }
            }

            Rectangle {
                Layout.fillWidth: true
                radius: theme.radiusLg
                color: theme.panelRaised
                border.color: theme.border
                height: accountCard.implicitHeight + 32

                RowLayout {
                    id: accountCard
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 16

                    Components.AvatarImage {
                        Layout.preferredWidth: 60
                        Layout.preferredHeight: 60
                        source: authService.avatarUrl
                        fallbackText: authService.signedIn ? authService.displayName : "Twitch"
                        fill: theme.field
                        borderColor: authService.signedIn ? theme.textSoft : theme.borderStrong
                        borderWidth: 2
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Label {
                            Layout.fillWidth: true
                            text: authService.signedIn ? "Twitch connected" : "Sign in with Twitch"
                            color: theme.text
                            font.pixelSize: 24
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Label {
                            Layout.fillWidth: true
                            text: authService.signedIn ? "Signed in as " + authService.displayName + "." : "Approve Shudder through Twitch Device Code sign-in. Your password never touches Shudder."
                            color: theme.textMuted
                            wrapMode: Text.WordWrap
                            font.pixelSize: theme.fontBody
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                visible: !authService.canSignIn
                radius: theme.radiusLg
                color: theme.field
                border.color: theme.border
                height: missingClientLabel.implicitHeight + 28
                Label {
                    id: missingClientLabel
                    anchors.fill: parent
                    anchors.margins: 14
                    text: "This build is missing Shudder's public Twitch Client ID."
                    color: theme.textSoft
                    wrapMode: Text.WordWrap
                    font.pixelSize: theme.fontMeta
                }
            }

            Rectangle {
                Layout.fillWidth: true
                visible: authService.deviceUserCode.length > 0
                radius: theme.radiusLg
                color: theme.field
                border.color: theme.borderStrong
                height: codeColumn.implicitHeight + 32

                ColumnLayout {
                    id: codeColumn
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 10

                    Label { text: "Verification code"; color: theme.textMuted; font.pixelSize: theme.fontMeta; font.weight: Font.DemiBold }
                    Label {
                        Layout.fillWidth: true
                        text: authService.deviceUserCode
                        color: theme.text
                        font.pixelSize: 38
                        font.bold: true
                        font.letterSpacing: 2
                        horizontalAlignment: Text.AlignHCenter
                    }
                    Label {
                        Layout.fillWidth: true
                        text: "Enter this code on Twitch, then keep Shudder open while authorization completes."
                        color: theme.textMuted
                        wrapMode: Text.WordWrap
                        font.pixelSize: theme.fontMeta
                    }
                    Components.GlassButton {
                        Layout.fillWidth: true
                        text: "Open Twitch Verification"
                        prominent: true
                        onClicked: appController.openExternalUrl(authService.deviceVerificationUri.toString())
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                radius: theme.radiusLg
                color: theme.field
                border.color: theme.border
                height: statusColumn.implicitHeight + 28

                ColumnLayout {
                    id: statusColumn
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 6
                    Label { text: "Status"; color: theme.text; font.weight: Font.DemiBold }
                    Label {
                        Layout.fillWidth: true
                        text: authService.status.length > 0 ? authService.status : (authService.signedIn ? "Your Twitch account is ready." : "Ready to connect.")
                        color: authService.status.indexOf("failed") >= 0 || authService.status.indexOf("unavailable") >= 0 ? theme.textSoft : theme.textMuted
                        wrapMode: Text.WordWrap
                        font.pixelSize: theme.fontMeta
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Components.GlassButton {
                    Layout.fillWidth: true
                    text: authService.deviceUserCode.length > 0 ? "Cancel Sign-In" : "Close"
                    onClicked: {
                        authService.cancelDeviceAuthorization()
                        drawer.close()
                    }
                }
                Components.GlassButton {
                    Layout.fillWidth: true
                    text: authService.signedIn ? "Sign Out" : (authService.busy ? "Starting..." : "Start Sign-In")
                    prominent: !authService.signedIn
                    enabled: authService.signedIn || (!authService.busy && authService.canSignIn)
                    onClicked: authService.signedIn ? authService.signOut() : authService.beginDeviceAuthorization()
                }
            }

            Item { Layout.fillWidth: true; Layout.preferredHeight: 8 }
        }
    }
}
