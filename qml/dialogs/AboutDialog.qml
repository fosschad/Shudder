import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components" as Components

Dialog {
    id: dialog
    Components.Theme { id: theme }

    title: "About Shudder"
    modal: true
    standardButtons: Dialog.Close
    width: Math.min(560, parent ? parent.width * 0.9 : 560)
    background: Rectangle { radius: theme.radiusLg; color: theme.panel; border.color: theme.borderStrong }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12
        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 86
            Layout.preferredHeight: 86
            radius: theme.radiusLg
            color: theme.text
            Label { anchors.centerIn: parent; text: "S"; color: theme.black; font.pixelSize: 52; font.bold: true }
        }
        Label { Layout.fillWidth: true; text: "Shudder"; horizontalAlignment: Text.AlignHCenter; font.pixelSize: 28; font.weight: Font.DemiBold; color: theme.text }
        Label { Layout.fillWidth: true; text: "Shudder version: " + appController.version; color: theme.textSoft }
        Label { Layout.fillWidth: true; text: "Application ID: " + appController.applicationId; color: theme.textMuted; wrapMode: Text.WrapAnywhere }
        Label {
            Layout.fillWidth: true
            text: "Shudder is an independent GPL-3.0-or-later native Linux Twitch desktop client. It is not affiliated with Twitch."
            color: theme.textSoft
            wrapMode: Text.WordWrap
        }
    }
}
