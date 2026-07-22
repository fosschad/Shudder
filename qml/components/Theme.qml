import QtQuick

QtObject {
    readonly property color app: "#090909"
    readonly property color panel: "#0f0f10"
    readonly property color panelRaised: "#151517"
    readonly property color surface: "#171719"
    readonly property color surfaceHover: "#202024"
    readonly property color surfacePressed: "#29292d"
    readonly property color field: "#141416"
    readonly property color border: "#2d2d31"
    readonly property color borderStrong: "#424248"
    readonly property color text: "#f5f5f5"
    readonly property color textSoft: "#b1b1b5"
    readonly property color textMuted: "#77777d"
    readonly property color textFaint: "#505054"
    readonly property color disabled: "#505054"
    readonly property color black: "#000000"
    readonly property color white: "#ffffff"
    readonly property color scrim: Qt.rgba(0, 0, 0, 0.62)
    readonly property color scrimMedium: Qt.rgba(0, 0, 0, 0.66)
    readonly property color scrimStrong: Qt.rgba(0, 0, 0, 0.80)
    readonly property color scrimDeeper: Qt.rgba(0, 0, 0, 0.88)
    readonly property color scrimClear: Qt.rgba(0, 0, 0, 0)
    readonly property color overlayBorder: Qt.rgba(1, 1, 1, 0.10)
    readonly property color overlayBorderSoft: Qt.rgba(1, 1, 1, 0.08)
    readonly property color focusRing: Qt.rgba(1, 1, 1, 0.62)
    readonly property color transparent: "transparent"

    readonly property int radiusXs: 6
    readonly property int radiusSm: 9
    readonly property int radiusMd: 11
    readonly property int radiusLg: 14
    readonly property int radiusXl: 18

    readonly property int spaceXs: 4
    readonly property int spaceSm: 8
    readonly property int spaceMd: 12
    readonly property int spaceLg: 18
    readonly property int spaceXl: 24

    readonly property int topBarHeight: 66
    readonly property int controlHeight: 36
    readonly property int compactControlHeight: 26
    readonly property int rowHeight: 56

    readonly property int fontCaption: 11
    readonly property int fontMeta: 12
    readonly property int fontBody: 14
    readonly property int fontTitle: 18
    readonly property int fontHeading: 26

    readonly property int animFast: 140
    readonly property int animNormal: 190
    readonly property int overlayFadeMs: 180
}
