import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Shudder 1.0
import "components" as Components
import "chat" as Chat
import "dialogs" as Dialogs
import "player" as Player

ApplicationWindow {
    id: root
    width: 1500
    height: 900
    minimumWidth: 1040
    minimumHeight: 660
    visible: true
    title: playerController.channel.length > 0 ? playerController.channel + " - Shudder" : "Shudder"
    color: theme.app

    Components.Theme { id: theme }

    property string section: "home"
    property bool chatVisible: preferences.get("chatVisible")
    property bool streamActive: playerController.channel.length > 0
    property bool playerFullscreen: false
    property bool currentFollowed: false
    property bool suppressGridHover: false
    readonly property var activeGridModel: section === "home" ? homeModel : directoryModel
    readonly property bool categoryGrid: section === "browse" && directoryModel.pageTitle === "Popular Categories"
    readonly property bool categoryStreams: section === "browse" && authService.signedIn && !categoryGrid && directoryModel.pageTitle.length > 0 && directoryModel.pageTitle !== "Live Channels" && directoryModel.pageTitle.indexOf("Search:") !== 0

    function compactViewerCountText(count) {
        const viewers = Math.round(Number(count))
        if (viewers <= 0) return "--"
        if (viewers < 1000) return viewers.toLocaleString(Qt.locale(), "f", 0)
        const thousands = viewers / 1000
        return thousands.toLocaleString(Qt.locale(), "f", thousands >= 10 ? 0 : 1) + "K"
    }

    function setPlayerFullscreen(fullscreen) {
        if (fullscreen) {
            fullscreenRestoreTimer.stop()
            if (!playerFullscreen) playerFullscreen = true
            if (root.visibility !== Window.FullScreen) root.showFullScreen()
            return
        }
        if (root.visibility === Window.FullScreen) root.showNormal()
        fullscreenRestoreTimer.restart()
    }

    function togglePlayerFullscreen() {
        setPlayerFullscreen(!playerFullscreen)
    }

    function suspendGridHover() {
        suppressGridHover = true
        gridHoverResumeTimer.restart()
    }

    function setChatVisible(visible) {
        if (chatVisible !== visible) chatVisible = visible
        if (preferences.get("chatVisible") !== visible) preferences.set("chatVisible", visible)
    }

    function openAuthDialog() {
        if (settingsSheet.opened) settingsSheet.close()
        authDialog.open()
    }

    function refreshLiveData() {
        if (!authService.signedIn) return
        followedModel.refresh()
        liveModel.refresh()
        if (section === "home") homeModel.refresh()
        else if (section === "browse" && !categoryGrid) directoryModel.refresh()
    }

    function openHome() {
        root.suspendGridHover()
        section = "home"
        if (authService.signedIn) homeModel.loadLive()
        else root.openAuthDialog()
        if (root.streamActive) Qt.callLater(function() { playerController.stop() })
    }

    function openBrowse() {
        root.suspendGridHover()
        section = "browse"
        if (!authService.signedIn) {
            root.openAuthDialog()
            return
        }
        directoryModel.loadCategories()
        if (liveModel.count === 0) liveModel.loadLive()
        if (root.streamActive) Qt.callLater(function() { playerController.stop() })
    }

    function openGridItem(row) {
        const item = activeGridModel.itemAt(row)
        if (item.kind === "category") {
            section = "browse"
            directoryModel.loadCategoryStreams(item.categoryId, item.category)
            return
        }
        appController.openItem(item)
        root.refreshCurrentFollowState()
    }

    function openPlayerCategory() {
        if (playerController.categoryId.length === 0) return
        section = "browse"
        directoryModel.loadCategoryStreams(playerController.categoryId, playerController.category)
        if (root.streamActive) Qt.callLater(function() { playerController.stop() })
    }

    function syncCurrentStreamMetadata() {
        if (!root.streamActive) return
        playerController.updateFromItem(followedModel.itemForLogin(playerController.channel))
        playerController.updateFromItem(liveModel.itemForLogin(playerController.channel))
    }

    function refreshCurrentFollowState() {
        currentFollowed = playerController.broadcasterId.length > 0 && authService.isFollowing(playerController.broadcasterId)
        if (authService.signedIn && playerController.broadcasterId.length > 0) authService.refreshFollowState(playerController.broadcasterId)
    }

    function openCurrentChannelOnTwitch() {
        const login = playerController.channel.trim()
        if (login.length === 0) return
        appController.openExternalUrl("https://www.twitch.tv/" + encodeURIComponent(login))
        if (authService.signedIn && playerController.broadcasterId.length > 0) followStateRefreshTimer.restart()
    }

    function emptyStateTitle() {
        if (root.categoryStreams) return "No live streams in " + directoryModel.pageTitle
        if (section === "search") return "No search results"
        if (section === "home") return "No live channels"
        return "No results"
    }

    function emptyStateBody() {
        if (root.categoryStreams) return "This category has no live Twitch streams right now. Try again later or browse another category."
        if (section === "search") {
            const query = searchField.text.trim()
            return query.length > 0 ? "No matches for \"" + query + "\". Try a different channel or category." : "Enter a channel or category to search Twitch."
        }
        return "Twitch did not return any live channels for this view."
    }

    Timer {
        id: fullscreenRestoreTimer
        interval: 120
        repeat: false
        onTriggered: if (root.playerFullscreen && root.visibility !== Window.FullScreen) root.playerFullscreen = false
    }

    Timer {
        id: liveRefreshTimer
        interval: 60000
        repeat: true
        running: authService.signedIn
        onTriggered: root.refreshLiveData()
    }

    Timer {
        id: gridHoverResumeTimer
        interval: 500
        repeat: false
        onTriggered: root.suppressGridHover = false
    }

    Timer {
        id: followStateRefreshTimer
        interval: 7000
        repeat: false
        onTriggered: root.refreshCurrentFollowState()
    }

    Timer {
        id: browsePrefetchTimer
        interval: 250
        repeat: false
        onTriggered: if (authService.signedIn && directoryModel.count === 0) directoryModel.loadCategories()
    }

    palette.window: theme.app
    palette.windowText: theme.text
    palette.base: theme.panel
    palette.text: theme.text
    palette.button: theme.surface
    palette.buttonText: theme.text
    palette.highlight: theme.text
    palette.highlightedText: theme.black

    Component.onCompleted: {
        if (authService.signedIn) {
            homeModel.loadLive()
            liveModel.loadLive()
            browsePrefetchTimer.restart()
        }
        else Qt.callLater(function() { root.openAuthDialog() })
    }
    onActiveChanged: if (active && authService.signedIn) root.refreshLiveData()
    onClosing: preferences.saveNow()

    Connections {
        target: authService
        function onSignedInChanged() {
            if (authService.signedIn) {
                homeModel.loadLive()
                liveModel.loadLive()
                browsePrefetchTimer.restart()
                root.refreshCurrentFollowState()
            }
        }
        function onFollowStateChanged(broadcasterId) {
            if (broadcasterId === playerController.broadcasterId) root.currentFollowed = authService.isFollowing(broadcasterId)
        }
    }

    Connections {
        target: playerController
        function onBroadcasterIdChanged() { root.refreshCurrentFollowState() }
        function onChannelChanged() { root.syncCurrentStreamMetadata() }
    }

    Connections {
        target: liveModel
        function onItemsChanged() { root.syncCurrentStreamMetadata() }
    }

    Connections {
        target: followedModel
        function onItemsChanged() { root.syncCurrentStreamMetadata() }
    }

    Connections {
        target: preferences
        function onValuesChanged() {
            root.chatVisible = preferences.get("chatVisible")
        }
    }

    onVisibilityChanged: if (visibility !== Window.FullScreen && playerFullscreen) fullscreenRestoreTimer.restart()

    Shortcut { sequence: "F11"; onActivated: root.setPlayerFullscreen(!(root.playerFullscreen || root.visibility === Window.FullScreen)) }
    Shortcut { sequence: "Escape"; onActivated: if (root.playerFullscreen) root.setPlayerFullscreen(false); else if (root.visibility === Window.FullScreen) root.showNormal() }
    Shortcut { sequence: "M"; onActivated: playerController.muted = !playerController.muted }
    Shortcut { sequence: "Space"; onActivated: playerController.paused = !playerController.paused }

    header: ToolBar {
        id: toolbar
        height: root.playerFullscreen ? 0 : theme.topBarHeight
        visible: !root.playerFullscreen
        padding: 0
        background: Rectangle {
            gradient: Gradient {
                GradientStop { position: 0; color: "#121214" }
                GradientStop { position: 1; color: theme.panel }
            }
            Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: theme.border }
        }

        Item {
            anchors.fill: parent

            RowLayout {
                id: leftActions
                anchors.left: parent.left
                anchors.leftMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                spacing: 12

                Image {
                    Layout.preferredWidth: 46
                    Layout.preferredHeight: 46
                    source: "qrc:/shudder/Shudder/icons/shudder.svg"
                    fillMode: Image.PreserveAspectFit
                    mipmap: true
                }

                Label { text: "Shudder"; font.pixelSize: theme.fontTitle; font.weight: Font.DemiBold; color: theme.text }
                Item { Layout.preferredWidth: 4 }

                Rectangle {
                    Layout.preferredHeight: 40
                    Layout.preferredWidth: 168
                    radius: theme.radiusMd
                    color: theme.field
                    border.color: theme.border
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 3
                        spacing: 3
                        Components.GlassButton { Layout.fillWidth: true; text: "Home"; quiet: root.streamActive || section !== "home"; prominent: !root.streamActive && section === "home"; onClicked: root.openHome() }
                        Components.GlassButton { Layout.fillWidth: true; text: "Browse"; quiet: root.streamActive || section !== "browse"; prominent: !root.streamActive && section === "browse"; onClicked: root.openBrowse() }
                    }
                }
            }

            Item {
                id: searchSlot
                anchors.left: leftActions.right
                anchors.right: rightActions.left
                anchors.leftMargin: 24
                anchors.rightMargin: 24
                anchors.verticalCenter: parent.verticalCenter
                height: 40
            }

            TextField {
                id: searchField
                anchors.centerIn: searchSlot
                width: Math.min(620, Math.max(220, searchSlot.width))
                height: 40
                placeholderText: "Search channels, categories, or exact channel"
                color: theme.text
                placeholderTextColor: theme.textFaint
                selectByMouse: true
                leftPadding: 16
                rightPadding: 16
                background: Rectangle {
                    radius: 13
                    color: theme.field
                    border.color: searchField.activeFocus ? theme.focusRing : (searchField.hovered ? theme.borderStrong : theme.border)
                    Behavior on border.color { ColorAnimation { duration: theme.animFast } }
                    Behavior on color { ColorAnimation { duration: theme.animFast } }
                }
                function runSearch() {
                    const query = text.trim()
                    if (!authService.signedIn) {
                        root.openAuthDialog()
                        return
                    }
                    section = query.length > 0 ? "search" : "home"
                    if (query.length > 0) directoryModel.search(query)
                    else homeModel.loadLive()
                    if (root.streamActive) Qt.callLater(function() { playerController.stop() })
                }
                onAccepted: runSearch()
                Accessible.name: placeholderText
            }

            RowLayout {
                id: rightActions
                anchors.right: parent.right
                anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                spacing: 12

                Components.GlassButton { Layout.preferredWidth: 94; text: "Settings"; onClicked: settingsSheet.open() }
                Button {
                    id: accountButton
                    property bool hasAvatar: authService.signedIn && String(authService.avatarUrl).length > 0

                    Layout.preferredWidth: hasAvatar ? 46 : (authService.signedIn ? 96 : 102)
                    Layout.preferredHeight: 40
                    leftPadding: 0
                    rightPadding: 0
                    topPadding: 0
                    bottomPadding: 0
                    text: authService.signedIn ? authService.displayName : "Sign in"
                    onClicked: root.openAuthDialog()

                    contentItem: Item {
                        Components.AvatarImage {
                            visible: accountButton.hasAvatar
                            anchors.centerIn: parent
                            width: 36
                            height: 36
                            source: authService.avatarUrl
                            fallbackText: authService.displayName
                            fill: theme.field
                            borderColor: accountButton.hovered ? theme.textSoft : theme.borderStrong
                            borderWidth: 2
                        }
                        Label {
                            visible: !accountButton.hasAvatar
                            anchors.centerIn: parent
                            width: parent.width - 18
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            text: accountButton.text
                            color: authService.signedIn ? theme.text : theme.black
                            font.pixelSize: theme.fontMeta
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                    }

                    background: Rectangle {
                        radius: accountButton.hasAvatar ? 20 : theme.radiusMd
                        color: accountButton.hasAvatar ? (accountButton.hovered || accountButton.down ? theme.surfaceHover : theme.transparent) : (!authService.signedIn ? (accountButton.down ? theme.textSoft : theme.text) : (accountButton.down ? theme.surfacePressed : (accountButton.hovered ? theme.surfaceHover : theme.field)))
                        border.width: accountButton.hasAvatar ? 0 : 1
                        border.color: accountButton.activeFocus ? theme.focusRing : (authService.signedIn ? theme.borderStrong : theme.text)
                    }
                }
            }
        }
    }

    SplitView {
        anchors.fill: parent
        spacing: 0
        orientation: Qt.Horizontal

        Rectangle {
            id: leftRail
            SplitView.preferredWidth: root.playerFullscreen ? 0 : 268
            SplitView.minimumWidth: root.playerFullscreen ? 0 : 224
            SplitView.maximumWidth: root.playerFullscreen ? 0 : 310
            visible: !root.playerFullscreen
            color: theme.panel
            property real followedRailPreferredHeight: 0
            property bool dividerDragging: false
            Behavior on followedRailPreferredHeight { enabled: !leftRail.dividerDragging; NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
            readonly property real railDividerHeight: 9
            readonly property real signedOutFooterHeight: authService.signedIn ? 0 : 112
            function defaultFollowedRailHeight() {
                return followedRail.count > 0 ? Math.min(followedRail.count * theme.rowHeight, leftRail.height * 0.38) : 66
            }
            function maxFollowedRailHeight() {
                return Math.max(66, leftRail.height - 48 - 48 - signedOutFooterHeight - railDividerHeight - 116)
            }
            function followedRailHeight() {
                const preferred = followedRailPreferredHeight > 0 ? followedRailPreferredHeight : defaultFollowedRailHeight()
                return Math.max(66, Math.min(preferred, maxFollowedRailHeight()))
            }
            Rectangle { anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 1; color: theme.border }

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 48
                    Layout.leftMargin: 16
                    Layout.rightMargin: 14
                    Label { text: "FOLLOWED"; color: theme.textMuted; font.pixelSize: theme.fontCaption; font.bold: true; font.letterSpacing: 1.1 }
                    Item { Layout.fillWidth: true }
                    Label { text: followedModel.busy ? "..." : String(followedRail.count); color: theme.textFaint; font.pixelSize: theme.fontCaption }
                }

                ListView {
                    id: followedRail
                    Layout.fillWidth: true
                    Layout.preferredHeight: leftRail.followedRailHeight()
                    interactive: contentHeight > height
                    boundsBehavior: Flickable.StopAtBounds
                    clip: true
                    cacheBuffer: 420
                    reuseItems: true
                    model: followedModel
                    delegate: Components.SideChannelRow {
                        width: followedRail.width
                        title: displayName
                        subtitle: category
                        image: avatar
                        viewerCount: model.viewerCount
                        live: true
                        selected: playerController.channel === login
                        onOpenRequested: appController.openItem(followedModel.itemAt(index))
                    }

                    Label {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16
                        visible: followedRail.count === 0
                        verticalAlignment: Text.AlignVCenter
                        text: authService.signedIn ? (followedModel.busy ? "Loading followed channels..." : "No followed channels are live.") : "Sign in to sync followed channels."
                        color: theme.textFaint
                        font.pixelSize: theme.fontMeta
                        wrapMode: Text.WordWrap
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: leftRail.railDividerHeight
                    color: dividerMouse.containsMouse || dividerMouse.pressed ? theme.surfaceHover : theme.panel
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16
                        height: 1
                        color: dividerMouse.containsMouse || dividerMouse.pressed ? theme.borderStrong : theme.border
                    }
                    MouseArea {
                        id: dividerMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.SplitVCursor
                        property real startY: 0
                        property real startHeight: 0
                        onPressed: function(mouse) {
                            leftRail.dividerDragging = true
                            startY = mapToItem(leftRail, mouse.x, mouse.y).y
                            startHeight = followedRail.height
                        }
                        onReleased: leftRail.dividerDragging = false
                        onCanceled: leftRail.dividerDragging = false
                        onPositionChanged: function(mouse) {
                            if (!pressed) return
                            const nextHeight = startHeight + mapToItem(leftRail, mouse.x, mouse.y).y - startY
                            leftRail.followedRailPreferredHeight = Math.max(66, Math.min(nextHeight, leftRail.maxFollowedRailHeight()))
                        }
                        onDoubleClicked: leftRail.followedRailPreferredHeight = 0
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 48
                    Layout.leftMargin: 16
                    Layout.rightMargin: 14
                    Label { text: "LIVE CHANNELS"; color: theme.textMuted; font.pixelSize: theme.fontCaption; font.bold: true; font.letterSpacing: 1.1 }
                    Item { Layout.fillWidth: true }
                    Label { text: liveModel.busy ? "..." : String(channelRail.count); color: theme.textFaint; font.pixelSize: theme.fontCaption }
                }

                ListView {
                    id: channelRail
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    cacheBuffer: 720
                    reuseItems: true
                    model: liveModel
                    delegate: Components.SideChannelRow {
                        width: channelRail.width
                        visible: authService.signedIn
                        title: displayName
                        subtitle: category
                        image: avatar
                        viewerCount: model.viewerCount
                        live: model.live
                        selected: playerController.channel === login
                        onOpenRequested: appController.openItem(liveModel.itemAt(index))
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: authService.signedIn ? 0 : 112
                    visible: !authService.signedIn
                    color: theme.panel
                    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; height: 1; color: theme.border }
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 8
                        Label { Layout.fillWidth: true; text: "Sign in to load Twitch channels."; color: theme.textMuted; wrapMode: Text.WordWrap; font.pixelSize: theme.fontMeta }
                        Components.GlassButton { visible: !authService.signedIn; Layout.fillWidth: true; text: "Sign in with Twitch"; prominent: true; onClicked: root.openAuthDialog() }
                    }
                }
            }
        }

        Rectangle {
            id: centerPane
            SplitView.fillWidth: true
            SplitView.fillHeight: true
            color: theme.app

            Loader {
                anchors.fill: parent
                sourceComponent: playerController.channel.length > 0 ? watchComponent : browseComponent
            }
        }

        Chat.ChatPanel {
            SplitView.preferredWidth: root.streamActive && root.chatVisible && !root.playerFullscreen ? preferences.get("sideChatWidth") : 0
            SplitView.minimumWidth: root.streamActive && root.chatVisible && !root.playerFullscreen ? 320 : 0
            SplitView.maximumWidth: root.streamActive && root.chatVisible && !root.playerFullscreen ? 420 : 0
            visible: root.streamActive && root.chatVisible && !root.playerFullscreen
            onConnectRequested: root.openAuthDialog()
        }
    }

    Component {
        id: watchComponent
        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: root.playerFullscreen ? 0 : 66
                visible: !root.playerFullscreen
                color: theme.app
                Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: theme.border }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 20
                    anchors.rightMargin: 20
                    spacing: 14
                    Components.GlassButton { text: "Back"; quiet: true; onClicked: Qt.callLater(function() { playerController.stop() }) }
                    Button {
                        id: followButton
                        property bool followed: root.currentFollowed
                        Layout.preferredWidth: 42
                        Layout.preferredHeight: 42
                        enabled: playerController.channel.length > 0
                        Accessible.name: root.currentFollowed ? "Open Twitch channel to manage follow" : "Open Twitch channel to follow"
                        scale: 1.0
                        onClicked: root.openCurrentChannelOnTwitch()
                        contentItem: Item {
                            Canvas {
                                id: followIcon
                                anchors.centerIn: parent
                                width: 22
                                height: 22
                                Connections {
                                    target: followButton
                                    function onHoveredChanged() { followIcon.requestPaint() }
                                    function onDownChanged() { followIcon.requestPaint() }
                                    function onEnabledChanged() { followIcon.requestPaint() }
                                }
                                Connections {
                                    target: root
                                    function onCurrentFollowedChanged() { followIcon.requestPaint() }
                                }
                                onPaint: {
                                    const ctx = getContext("2d")
                                    ctx.reset()
                                    const ink = !followButton.enabled ? theme.disabled : (root.currentFollowed ? "#ff5c8a" : (followButton.hovered ? theme.text : theme.textSoft))
                                    ctx.strokeStyle = ink
                                    ctx.fillStyle = ink
                                    ctx.lineWidth = 1.9
                                    ctx.lineCap = "round"
                                    ctx.lineJoin = "round"
                                    ctx.beginPath()
                                    ctx.moveTo(11, 18.5)
                                    ctx.bezierCurveTo(5.3, 14.1, 3, 11.3, 3, 7.9)
                                    ctx.bezierCurveTo(3, 5.1, 5.1, 3.3, 7.5, 3.3)
                                    ctx.bezierCurveTo(9.1, 3.3, 10.3, 4.1, 11, 5.3)
                                    ctx.bezierCurveTo(11.7, 4.1, 12.9, 3.3, 14.5, 3.3)
                                    ctx.bezierCurveTo(16.9, 3.3, 19, 5.1, 19, 7.9)
                                    ctx.bezierCurveTo(19, 11.3, 16.7, 14.1, 11, 18.5)
                                    ctx.closePath()
                                    if (root.currentFollowed) ctx.fill()
                                    else ctx.stroke()
                                }
                            }
                        }
                        background: Rectangle {
                            radius: 21
                            color: followButton.down ? theme.surfacePressed : (followButton.hovered ? theme.surfaceHover : theme.surface)
                            border.width: 1
                            border.color: followButton.activeFocus ? theme.focusRing : (root.currentFollowed ? "#ff5c8a" : (followButton.hovered ? theme.borderStrong : theme.border))
                            Behavior on border.color { ColorAnimation { duration: theme.animNormal } }
                            Behavior on color { ColorAnimation { duration: theme.animFast } }
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        Label { Layout.fillWidth: true; text: playerController.title; color: theme.text; font.pixelSize: 16; font.weight: Font.DemiBold; elide: Text.ElideRight }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Label { text: playerController.channel; color: theme.textMuted; font.pixelSize: theme.fontMeta; elide: Text.ElideRight }
                            Label { text: "•"; visible: playerController.category.length > 0; color: theme.textFaint; font.pixelSize: theme.fontCaption }
                            Button {
                                visible: playerController.category.length > 0
                                text: playerController.category
                                flat: true
                                padding: 0
                                onClicked: root.openPlayerCategory()
                                contentItem: Text { text: parent.text; color: parent.hovered ? theme.text : theme.textMuted; font.pixelSize: theme.fontMeta; elide: Text.ElideRight }
                                background: Rectangle { color: theme.transparent }
                            }
                            Label { text: "•"; color: theme.textFaint; font.pixelSize: theme.fontCaption }
                            Label { text: root.compactViewerCountText(playerController.viewerCount) + " watching"; color: theme.textMuted; font.pixelSize: theme.fontMeta }
                            Label { text: "•"; visible: playerController.liveDuration.length > 0; color: theme.textFaint; font.pixelSize: theme.fontCaption }
                            Label { text: playerController.liveDuration; visible: playerController.liveDuration.length > 0; color: theme.textMuted; font.pixelSize: theme.fontMeta }
                            Item { Layout.fillWidth: true }
                        }
                    }
                    Components.GlassButton {
                        Layout.preferredWidth: root.chatVisible ? 92 : 104
                        text: root.chatVisible ? "Hide Chat" : "Show Chat"
                        quiet: !root.chatVisible
                        prominent: root.chatVisible
                        onClicked: root.setChatVisible(!root.chatVisible)
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: theme.black
                clip: true
                Loader { anchors.fill: parent; sourceComponent: playerController.mode === "native" ? nativePlayerComponent : standardPlayerComponent }
                Component { id: standardPlayerComponent; Player.StandardPlayer { anchors.fill: parent; fullscreenActive: root.playerFullscreen; onFullscreenRequested: root.togglePlayerFullscreen() } }
                Component { id: nativePlayerComponent; Player.NativePlayer { anchors.fill: parent; fullscreenActive: root.playerFullscreen; onFullscreenRequested: root.togglePlayerFullscreen() } }
            }
        }
    }

    Component {
        id: browseComponent
        ColumnLayout {
            anchors.fill: parent
            anchors.leftMargin: 28
            anchors.rightMargin: 28
            anchors.topMargin: 24
            anchors.bottomMargin: 0
            spacing: 16

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Components.GlassButton { visible: root.categoryStreams; text: "Back"; quiet: true; onClicked: directoryModel.loadCategories() }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3
                    Label { text: section === "home" ? homeModel.pageTitle : directoryModel.pageTitle; font.pixelSize: theme.fontHeading; font.weight: Font.DemiBold; color: theme.text }
                    Label { visible: !authService.signedIn; text: "Twitch sign-in is ready. No Client ID entry required."; color: theme.textMuted; font.pixelSize: theme.fontBody }
                }
                Label { text: root.activeGridModel.busy ? "Loading..." : ""; color: theme.textMuted; font.pixelSize: theme.fontMeta }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 150
                visible: !authService.signedIn
                radius: theme.radiusLg
                color: theme.panelRaised
                border.color: theme.border
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 22
                    spacing: 20
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Label { text: "Sign in with Twitch"; color: theme.text; font.pixelSize: 24; font.weight: Font.DemiBold }
                        Label { Layout.fillWidth: true; text: "Shudder now includes its public Twitch Client ID. Start Device Code sign-in, approve it in Twitch, and channels/chat assets will load automatically."; color: theme.textMuted; wrapMode: Text.WordWrap; font.pixelSize: theme.fontBody }
                        Label { Layout.fillWidth: true; text: authService.status; visible: authService.status.length > 0; color: theme.textSoft; wrapMode: Text.WordWrap; font.pixelSize: theme.fontMeta }
                    }
                    Components.GlassButton { Layout.preferredWidth: 170; text: "Start sign-in"; prominent: true; onClicked: root.openAuthDialog() }
                }
            }

            Label {
                Layout.fillWidth: true
                visible: root.activeGridModel.error.length > 0 && authService.signedIn
                text: root.activeGridModel.error
                color: theme.textSoft
                wrapMode: Text.WordWrap
            }

            Item {
                x: -10000
                y: -10000
                width: 1
                height: 1
                opacity: 0
                visible: authService.signedIn && section === "home" && directoryModel.count > 0
                Repeater {
                    model: Math.min(directoryModel.count, 18)
                    delegate: Image {
                        readonly property var prefetchItem: directoryModel.itemAt(index)
                        width: 1
                        height: 1
                        source: prefetchItem.thumbnail || ""
                        asynchronous: true
                        cache: true
                        sourceSize.width: prefetchItem.kind === "category" ? 342 : 440
                        sourceSize.height: prefetchItem.kind === "category" ? 456 : 248
                    }
                }
            }

            ScrollView {
                id: searchResults
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: authService.signedIn && section === "search"
                contentWidth: availableWidth
                clip: true

                Column {
                    id: searchColumn
                    width: searchResults.availableWidth
                    spacing: 20
                    property int channelColumns: Math.max(1, Math.floor((width + 14) / 310))
                    property real channelCardWidth: (width - 14 * (channelColumns - 1)) / channelColumns

                    Column {
                        width: parent.width
                        spacing: 10
                        visible: directoryModel.searchCategoryItems.length > 0
                        RowLayout {
                            width: parent.width
                            Label { Layout.fillWidth: true; text: "Categories"; color: theme.text; font.pixelSize: theme.fontTitle; font.weight: Font.DemiBold }
                            Label { text: String(directoryModel.searchCategoryItems.length); color: theme.textFaint; font.pixelSize: theme.fontCaption }
                        }
                        ListView {
                            width: parent.width
                            height: 244
                            orientation: ListView.Horizontal
                            spacing: 14
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            model: directoryModel.searchCategoryItems
                            delegate: Item {
                                width: 152
                                height: 244
                                Rectangle {
                                    anchors.fill: parent
                                    radius: theme.radiusLg
                                    color: categoryMouse.containsMouse ? theme.surfaceHover : theme.transparent
                                    border.width: 1
                                    border.color: categoryMouse.containsMouse ? theme.borderStrong : theme.transparent
                                    Behavior on color { ColorAnimation { duration: theme.animFast } }
                                    Behavior on border.color { ColorAnimation { duration: theme.animFast } }
                                }
                                MouseArea {
                                    id: categoryMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: {
                                        section = "browse"
                                        directoryModel.loadCategoryStreams(modelData.categoryId, modelData.category)
                                    }
                                }
                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 4
                                    spacing: 8
                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 202
                                        radius: theme.radiusMd
                                        color: theme.surface
                                        border.color: theme.border
                                        border.width: 1
                                        clip: true
                                        Components.StableImage { id: searchCategoryImage; anchors.fill: parent; source: modelData.thumbnail; fillMode: Image.PreserveAspectCrop; fill: theme.surface; preferredSourceWidth: 342; preferredSourceHeight: 456 }
                                        Label { anchors.centerIn: parent; width: parent.width - 24; visible: !searchCategoryImage.ready; text: modelData.category; color: theme.textMuted; font.pixelSize: theme.fontBody; font.weight: Font.DemiBold; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight }
                                    }
                                    Label { Layout.fillWidth: true; text: modelData.category; color: theme.text; font.pixelSize: theme.fontMeta; font.weight: Font.DemiBold; elide: Text.ElideRight }
                                }
                            }
                        }
                    }

                    Column {
                        width: parent.width
                        spacing: 10
                        visible: directoryModel.searchChannelItems.length > 0
                        RowLayout {
                            width: parent.width
                            Label { Layout.fillWidth: true; text: "Channels"; color: theme.text; font.pixelSize: theme.fontTitle; font.weight: Font.DemiBold }
                            Label { text: String(directoryModel.searchChannelItems.length); color: theme.textFaint; font.pixelSize: theme.fontCaption }
                        }
                        Flow {
                            id: searchChannelFlow
                            width: parent.width
                            height: childrenRect.height
                            spacing: 14
                            Repeater {
                                model: directoryModel.searchChannelItems
                                delegate: Rectangle {
                                    width: searchColumn.channelCardWidth
                                    height: 256
                                    radius: theme.radiusLg
                                    color: channelMouse.containsMouse ? theme.surfaceHover : theme.panelRaised
                                    border.color: channelMouse.containsMouse ? theme.borderStrong : theme.border
                                    border.width: 1
                                    Behavior on color { ColorAnimation { duration: theme.animFast } }
                                    Behavior on border.color { ColorAnimation { duration: theme.animFast } }
                                    MouseArea {
                                        id: channelMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onClicked: appController.openItem(modelData)
                                    }
                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 10
                                        spacing: 9
                                        Rectangle {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 138
                                            radius: theme.radiusMd
                                            color: theme.surface
                                            clip: true
                                            Components.StableImage { anchors.fill: parent; visible: modelData.kind !== "channel"; source: modelData.thumbnail; fillMode: Image.PreserveAspectCrop; fill: theme.surface; preferredSourceWidth: 440; preferredSourceHeight: 248 }
                                            Components.AvatarImage {
                                                visible: modelData.kind === "channel"
                                                anchors.centerIn: parent
                                                width: 86
                                                height: 86
                                                source: modelData.avatar.length > 0 ? modelData.avatar : modelData.thumbnail
                                                fallbackText: modelData.displayName.length > 0 ? modelData.displayName : modelData.login
                                                fill: theme.surface
                                                borderColor: theme.borderStrong
                                            }
                                            Rectangle {
                                                visible: modelData.kind !== "channel" && modelData.viewerCount > 0
                                                anchors.right: parent.right
                                                anchors.bottom: parent.bottom
                                                anchors.margins: 9
                                                radius: theme.radiusXs
                                                color: theme.scrimDeeper
                                                border.color: theme.overlayBorder
                                                border.width: 1
                                                width: viewerLabel.width + 18
                                                height: viewerLabel.height + 10
                                                Label { id: viewerLabel; anchors.centerIn: parent; text: root.compactViewerCountText(modelData.viewerCount) + " watching"; color: theme.text; font.pixelSize: theme.fontCaption }
                                            }
                                        }
                                        Label { Layout.fillWidth: true; text: modelData.displayName.length > 0 ? modelData.displayName : modelData.login; color: theme.text; font.pixelSize: 15; font.weight: Font.DemiBold; elide: Text.ElideRight }
                                        Label { Layout.fillWidth: true; text: modelData.title; visible: modelData.title.length > 0; color: theme.textSoft; font.pixelSize: theme.fontMeta; elide: Text.ElideRight }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 6
                                            Label { text: modelData.live ? (modelData.uptime.length > 0 ? modelData.uptime : "Live") : "Channel"; color: theme.textMuted; font.pixelSize: theme.fontCaption }
                                            Label { Layout.fillWidth: true; text: modelData.category; color: theme.textSoft; font.pixelSize: theme.fontCaption; elide: Text.ElideRight; horizontalAlignment: Text.AlignRight }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            GridView {
                id: grid
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: authService.signedIn && section !== "search" && (root.activeGridModel.busy || count > 0)
                readonly property int categoryColumns: Math.max(1, Math.floor(width / 154))
                cellWidth: root.categoryGrid ? width / categoryColumns : Math.max(260, width / Math.max(1, Math.floor(width / 286)))
                cellHeight: root.categoryGrid ? Math.round((cellWidth - 12) * 1.32 + 58) : 286
                clip: true
                model: root.activeGridModel
                function maybeLoadMore() {
                    if (!visible || !authService.signedIn || !root.activeGridModel.hasMore || root.activeGridModel.busy) return
                    if (contentHeight <= height || contentY + height >= contentHeight - cellHeight * 2) root.activeGridModel.loadMore()
                }
                Timer {
                    interval: 120
                    repeat: true
                    running: grid.visible && root.activeGridModel.hasMore && !root.activeGridModel.busy && grid.contentHeight <= grid.height
                    onTriggered: grid.maybeLoadMore()
                }
                onContentYChanged: maybeLoadMore()
                onContentHeightChanged: maybeLoadMore()
                onMovementEnded: maybeLoadMore()
                onCountChanged: Qt.callLater(maybeLoadMore)
                onVisibleChanged: if (visible) Qt.callLater(maybeLoadMore)
                onHeightChanged: Qt.callLater(maybeLoadMore)
                Connections {
                    target: root.activeGridModel
                    function onHasMoreChanged() { Qt.callLater(grid.maybeLoadMore) }
                    function onBusyChanged() {
                        if (root.activeGridModel.busy) root.suspendGridHover()
                        else Qt.callLater(grid.maybeLoadMore)
                    }
                }
                delegate: Components.StreamCard {
                    width: root.categoryGrid ? grid.cellWidth - 14 : grid.cellWidth - 12
                    height: root.categoryGrid ? grid.cellHeight - 10 : grid.cellHeight - 12
                    cardHoverEnabled: !root.suppressGridHover
                    onOpenRequested: root.openGridItem(index)
                    onCategoryRequested: { section = "browse"; directoryModel.loadCategoryStreams(categoryId, category) }
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: authService.signedIn && !root.activeGridModel.busy && root.activeGridModel.error.length === 0 && ((section === "search" && directoryModel.searchCategoryItems.length === 0 && directoryModel.searchChannelItems.length === 0) || (section !== "search" && grid.count === 0))
                ColumnLayout {
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 64, 420)
                    spacing: 14

                    Rectangle {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: 58
                        Layout.preferredHeight: 58
                        radius: 18
                        color: theme.surface
                        border.color: theme.border
                        Canvas {
                            anchors.fill: parent
                            onPaint: {
                                const ctx = getContext("2d")
                                ctx.reset()
                                ctx.strokeStyle = theme.textMuted
                                ctx.lineWidth = 2
                                ctx.lineCap = "round"
                                ctx.beginPath()
                                ctx.arc(width / 2 - 4, height / 2 - 4, 10, 0, Math.PI * 2)
                                ctx.stroke()
                                ctx.beginPath()
                                ctx.moveTo(width / 2 + 5, height / 2 + 5)
                                ctx.lineTo(width / 2 + 15, height / 2 + 15)
                                ctx.stroke()
                            }
                        }
                    }
                    Label { Layout.fillWidth: true; text: root.emptyStateTitle(); color: theme.text; font.pixelSize: theme.fontTitle; font.weight: Font.DemiBold; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap }
                    Label { Layout.fillWidth: true; text: root.emptyStateBody(); color: theme.textMuted; font.pixelSize: theme.fontBody; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap }
                    Components.GlassButton {
                        visible: root.categoryStreams || section === "search"
                        Layout.alignment: Qt.AlignHCenter
                        text: root.categoryStreams ? "Browse Categories" : "Browse"
                        prominent: true
                        onClicked: root.openBrowse()
                    }
                }
            }

        }
    }

    Dialogs.SettingsSheet { id: settingsSheet; onConnectRequested: { settingsSheet.close(); Qt.callLater(function() { root.openAuthDialog() }) } }
    Dialogs.AboutDialog { id: aboutDialog }
    Dialogs.AuthDialog { id: authDialog }
}
