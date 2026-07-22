import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components" as Components

Frame {
    id: panel
    signal connectRequested()
    Components.Theme { id: theme }
    property string hoverText: ""
    property string hoverImage: ""
    property int hoverPreviewX: 0
    property int hoverPreviewY: 0
    property string copyText: ""
    property string replyTargetId: ""
    property string replyTargetUser: ""
    property string replyTargetBody: ""
    property string menuTargetId: ""
    property string menuTargetUser: ""
    property string menuTargetBody: ""

    function clearReplyTarget() {
        replyTargetId = ""
        replyTargetUser = ""
        replyTargetBody = ""
    }

    function setReplyTarget(messageId, user, bodyText) {
        replyTargetId = messageId
        replyTargetUser = user
        replyTargetBody = bodyText
        composer.forceActiveFocus()
    }

    function showHoverPreview(text, image, item, x, y) {
        const point = item.mapToItem(panel, x, y)
        hoverText = text
        hoverImage = image || ""
        hoverPreviewX = Math.min(panel.width - hoverPreview.width - 8, Math.max(8, point.x + 16))
        hoverPreviewY = Math.min(panel.height - hoverPreview.height - 8, Math.max(8, point.y + 16))
    }

    function hideHoverPreview(text) {
        if (hoverText === text) {
            hoverText = ""
            hoverImage = ""
        }
    }

    padding: 0
    background: Rectangle { color: theme.panel; border.color: theme.border }

    TextEdit {
        id: copyBuffer
        x: -10000
        y: -10000
        width: 1
        height: 1
        visible: false
        readOnly: true
        text: panel.copyText
    }

    Menu {
        id: messageMenu
        MenuItem {
            text: "Reply"
            enabled: panel.menuTargetId.length > 0 && composer.enabled
            onTriggered: {
                panel.setReplyTarget(panel.menuTargetId, panel.menuTargetUser, panel.menuTargetBody)
            }
        }
        MenuItem {
            text: "Copy Message"
            onTriggered: {
                copyBuffer.forceActiveFocus()
                copyBuffer.selectAll()
                copyBuffer.copy()
                copyBuffer.deselect()
            }
        }
    }

    Item {
        x: -10000
        y: -10000
        width: 1
        height: 1
        opacity: 0
        Repeater {
            model: chatModel.preloadEmoteImageUrls
            delegate: Image {
                width: 1
                height: 1
                source: modelData
                asynchronous: true
                cache: true
                sourceSize.width: 64
                sourceSize.height: 64
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 0
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            Layout.leftMargin: 16
            Layout.rightMargin: 12
            Label { text: "Stream Chat"; font.weight: Font.DemiBold; font.pixelSize: theme.fontBody; color: theme.text }
            Item { Layout.fillWidth: true }
            Label { text: chatModel.channel.length > 0 ? "#" + chatModel.channel : ""; color: theme.textMuted; font.pixelSize: theme.fontCaption; elide: Text.ElideRight }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.border }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: list
                objectName: "chatMessageList"
                property bool followTail: true
                property bool pendingTailJump: false
                property int pendingMessageCount: 0
                property string jumpChannel: ""
                property int previousCount: 0

                anchors.fill: parent
                model: chatModel
                clip: true
                spacing: 0
                cacheBuffer: 0
                reuseItems: true
                boundsBehavior: Flickable.StopAtBounds

                function bottomY() {
                    return Math.max(originY, originY + contentHeight - height)
                }

                function finite(value) {
                    return Number.isFinite(Number(value))
                }

                function clampedContentY(value) {
                    if (!finite(value)) return originY
                    const bottom = bottomY()
                    if (!finite(bottom)) return originY
                    return Math.min(bottom, Math.max(originY, value))
                }

                function nearEnd() {
                    return contentHeight <= height || contentY >= bottomY() - 24
                }

                function cancelPendingTailJump() {
                    pendingTailJump = false
                    tailJumpTimer.stop()
                }

                function jumpToPresent() {
                    jumpChannel = chatModel.channel
                    followTail = true
                    if (pendingTailJump) return
                    pendingTailJump = true
                    tailJumpTimer.restart()
                }

                function applyPendingTailJump() {
                    if (!pendingTailJump) return
                    if (jumpChannel !== chatModel.channel) {
                        cancelPendingTailJump()
                        return
                    }
                    if (count <= 0) {
                        pendingTailJump = false
                        pendingMessageCount = 0
                        return
                    }
                    if (width <= 0 || height <= 0) {
                        pendingTailJump = false
                        return
                    }
                    if (moving || flicking) cancelFlick()
                    const clampedY = clampedContentY(bottomY())
                    if (finite(clampedY) && Math.abs(contentY - clampedY) > 1) contentY = clampedY
                    followTail = true
                    pendingMessageCount = 0
                    pendingTailJump = false
                }

                Timer {
                    id: tailJumpTimer
                    interval: 0
                    repeat: false
                    onTriggered: list.applyPendingTailJump()
                }

                delegate: Item {
                    id: messageDelegate
                    readonly property string authorColor: model.color
                    readonly property bool highlightedMention: Boolean(model.mentioned || false)
                    readonly property bool replyActionShown: !notice && messageId.length > 0 && (messageHover.hovered || inlineReplyButton.hovered || inlineReplyButton.activeFocus || inlineReplyButton.down)
                    width: list.width
                    height: messageColumn.implicitHeight + 10
                    HoverHandler {
                        id: messageHover
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    }
                    Rectangle {
                        anchors.fill: parent
                        color: messageDelegate.highlightedMention ? Qt.rgba(0.46, 0.33, 0.1, 0.34) : theme.transparent
                        border.color: messageDelegate.highlightedMention ? Qt.rgba(0.96, 0.72, 0.22, 0.45) : theme.transparent
                        border.width: messageDelegate.highlightedMention ? 1 : 0
                    }
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: {
                            panel.menuTargetId = messageId
                            panel.menuTargetUser = displayName.length > 0 ? displayName : author
                            panel.menuTargetBody = deleted ? "<message deleted>" : body
                            panel.copyText = plainText
                            messageMenu.popup()
                        }
                    }
                    Button {
                        id: inlineReplyButton
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.topMargin: 3
                        anchors.rightMargin: 7
                        width: 28
                        height: 26
                        z: 5
                        visible: !notice && messageId.length > 0
                        opacity: messageDelegate.replyActionShown ? 1 : 0
                        enabled: opacity > 0.05
                        padding: 0
                        Accessible.name: "Reply"
                        ToolTip.visible: hovered
                        ToolTip.text: "Reply"
                        Behavior on opacity { NumberAnimation { duration: theme.animFast; easing.type: Easing.OutCubic } }
                        onClicked: panel.setReplyTarget(messageId, displayName.length > 0 ? displayName : author, deleted ? "<message deleted>" : body)
                        contentItem: Canvas {
                            id: inlineReplyIcon
                            anchors.fill: parent
                            Connections {
                                target: inlineReplyButton
                                function onHoveredChanged() { inlineReplyIcon.requestPaint() }
                                function onDownChanged() { inlineReplyIcon.requestPaint() }
                                function onEnabledChanged() { inlineReplyIcon.requestPaint() }
                            }
                            onPaint: {
                                const ctx = getContext("2d")
                                ctx.reset()
                                ctx.strokeStyle = inlineReplyButton.enabled ? (inlineReplyButton.hovered ? theme.text : theme.textMuted) : theme.disabled
                                ctx.lineWidth = 1.8
                                ctx.lineCap = "round"
                                ctx.lineJoin = "round"
                                const cx = width / 2
                                const cy = height / 2
                                ctx.beginPath()
                                ctx.moveTo(cx - 2.6, cy - 5.2)
                                ctx.lineTo(cx - 7.1, cy - 0.6)
                                ctx.lineTo(cx - 2.6, cy + 4.0)
                                ctx.moveTo(cx - 6.2, cy - 0.6)
                                ctx.lineTo(cx + 0.3, cy - 0.6)
                                ctx.bezierCurveTo(cx + 5.4, cy - 0.6, cx + 7.4, cy + 2.1, cx + 7.4, cy + 6.1)
                                ctx.stroke()
                            }
                        }
                        background: Rectangle {
                            radius: 8
                            color: inlineReplyButton.down ? Qt.rgba(1, 1, 1, 0.18) : (inlineReplyButton.hovered ? Qt.rgba(1, 1, 1, 0.13) : Qt.rgba(1, 1, 1, 0.055))
                            border.width: inlineReplyButton.activeFocus ? 1 : 0
                            border.color: theme.focusRing
                            Behavior on color { ColorAnimation { duration: theme.animFast } }
                        }
                    }
                    Column {
                        id: messageColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.leftMargin: 10
                        anchors.rightMargin: 16
                        anchors.topMargin: 7
                        spacing: 3
                        Text { visible: replyParentId.length > 0; text: "Replying to " + replyParentUser + ": " + replyParentBody; color: theme.textFaint; font.pixelSize: Math.max(10, preferences.get("chatFontSize") - 3); elide: Text.ElideRight; width: parent.width }
                        Flow {
                            id: messageFlow
                            width: parent.width
                            spacing: 3

                            Text { text: timestamp; color: theme.textFaint; font.pixelSize: Math.max(10, preferences.get("chatFontSize") - 3) }

                            Repeater {
                                model: notice ? [] : badgeAssets
                                delegate: Item {
                                    objectName: "chatBadgeCell"
                                    property string badgeImageUrl: String(modelData.imageUrl || "")
                                    property bool badgeImageActive: false
                                    property bool badgeImageReady: false
                                    readonly property string badgeHoverText: "Badge: " + String(modelData.title || modelData.key)
                                    width: badgeImageUrl.length > 0 ? 18 : 0
                                    height: 18
                                    visible: badgeImageUrl.length > 0
                                    onBadgeImageUrlChanged: {
                                        badgeImageActive = false
                                        badgeImageReady = false
                                        if (badgeImageUrl.length > 0) badgeImageReset.restart()
                                    }
                                    Component.onCompleted: if (badgeImageUrl.length > 0) badgeImageReset.restart()
                                    Timer {
                                        id: badgeImageReset
                                        interval: 0
                                        repeat: false
                                        onTriggered: badgeImageActive = badgeImageUrl.length > 0
                                    }
                                    MouseArea {
                                        id: badgeMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        acceptedButtons: Qt.NoButton
                                        onEntered: panel.showHoverPreview(badgeHoverText, badgeImageUrl, parent, mouseX, mouseY)
                                        onExited: panel.hideHoverPreview(badgeHoverText)
                                    }
                                    Rectangle {
                                        anchors.fill: parent
                                        radius: 3
                                        visible: badgeMouse.containsMouse && badgeImageReady
                                        color: theme.surfaceHover
                                        border.color: theme.borderStrong
                                    }
                                    Loader {
                                        objectName: "chatBadgeImageLoader"
                                        anchors.centerIn: parent
                                        width: 18
                                        height: 18
                                        active: badgeImageActive
                                        sourceComponent: Image {
                                            width: 18
                                            height: 18
                                            fillMode: Image.PreserveAspectFit
                                            source: badgeImageUrl
                                            asynchronous: true
                                            cache: true
                                            mipmap: true
                                            sourceSize.width: 36
                                            sourceSize.height: 36
                                            visible: badgeImageReady
                                            onStatusChanged: badgeImageReady = status === Image.Ready && source.toString() === badgeImageUrl
                                            Component.onCompleted: badgeImageReady = status === Image.Ready && source.toString() === badgeImageUrl
                                        }
                                    }
                                }
                            }

                            Text {
                                visible: !notice
                                text: displayName + (action ? " " : ":")
                                color: deleted ? theme.textFaint : messageDelegate.authorColor
                                font.pixelSize: preferences.get("chatFontSize")
                                font.bold: true
                            }

                            Repeater {
                                model: messageParts
                                delegate: Item {
                                    readonly property bool emotePart: modelData.type === "emote"
                                    readonly property string emoteHoverText: String(modelData.provider || "Emote") + ": " + String(modelData.name || "")
                                    width: emotePart ? preferences.get("chatEmoteSize") : Math.min(partText.implicitWidth + 1, Math.max(80, messageFlow.width - 16))
                                    height: emotePart ? preferences.get("chatEmoteSize") : partText.implicitHeight
                                    MouseArea {
                                        id: emoteMouse
                                        anchors.fill: parent
                                        enabled: parent.emotePart
                                        hoverEnabled: true
                                        acceptedButtons: Qt.NoButton
                                        onEntered: panel.showHoverPreview(parent.emoteHoverText, "", parent, mouseX, mouseY)
                                        onExited: panel.hideHoverPreview(parent.emoteHoverText)
                                    }
                                    Rectangle {
                                        anchors.fill: parent
                                        radius: 4
                                        visible: parent.emotePart && emoteMouse.containsMouse
                                        color: theme.surfaceHover
                                        border.color: theme.borderStrong
                                    }
                                    Components.StableImage {
                                        id: inlineEmoteImage
                                        anchors.centerIn: parent
                                        visible: parent.emotePart
                                        source: parent.emotePart ? String(modelData.imageUrl || "") : ""
                                        retainWhileLoading: true
                                        width: preferences.get("chatEmoteSize")
                                        height: preferences.get("chatEmoteSize")
                                        fillMode: Image.PreserveAspectFit
                                        fill: theme.transparent
                                        preferredSourceWidth: Math.max(32, preferences.get("chatEmoteSize") * 2)
                                        preferredSourceHeight: Math.max(32, preferences.get("chatEmoteSize") * 2)
                                    }
                                    Text {
                                        anchors.centerIn: parent
                                        visible: parent.emotePart && !inlineEmoteImage.ready
                                        text: String(modelData.name || "?").charAt(0).toUpperCase()
                                        color: theme.textMuted
                                        font.pixelSize: Math.max(10, preferences.get("chatEmoteSize") * 0.38)
                                        font.weight: Font.DemiBold
                                    }
                                    Text {
                                        id: partText
                                        visible: !parent.emotePart
                                        width: parent.width
                                        text: String(modelData.text || "")
                                        textFormat: Text.PlainText
                                        wrapMode: Text.Wrap
                                        color: deleted ? theme.textFaint : (notice ? theme.textSoft : (action ? messageDelegate.authorColor : theme.text))
                                        font.pixelSize: preferences.get("chatFontSize")
                                    }
                                }
                            }
                        }
                    }
                }

                ScrollBar.vertical: ScrollBar {
                    id: chatScrollBar
                    policy: ScrollBar.AsNeeded
                    active: hovered || pressed || list.moving || list.flicking
                    contentItem: Rectangle {
                        implicitWidth: 7
                        radius: 4
                        color: chatScrollBar.pressed ? theme.textSoft : (chatScrollBar.hovered ? theme.textMuted : theme.borderStrong)
                    }
                    background: Rectangle { color: theme.transparent }
                }

                Component.onCompleted: jumpToPresent()
                Component.onDestruction: cancelPendingTailJump()
                onMovementStarted: {
                    followTail = false
                    cancelPendingTailJump()
                }
                onMovementEnded: {
                    followTail = nearEnd()
                    if (followTail) {
                        pendingMessageCount = 0
                        jumpToPresent()
                    }
                }
                onCountChanged: {
                    const delta = count - previousCount
                    if (followTail) jumpToPresent()
                    else if (delta > 0) pendingMessageCount += delta
                    if (count <= 0) {
                        followTail = true
                        pendingMessageCount = 0
                        cancelPendingTailJump()
                    }
                    previousCount = count
                }
                Connections {
                    target: chatModel
                    function onModelAboutToBeReset() {
                        list.followTail = true
                        list.pendingMessageCount = 0
                        list.cancelPendingTailJump()
                    }
                    function onRowsAboutToBeRemoved() {
                        list.cancelPendingTailJump()
                    }
                    function onChannelChanged() {
                        list.followTail = true
                        list.pendingMessageCount = 0
                        list.cancelPendingTailJump()
                        list.jumpToPresent()
                    }
                }
            }

            Components.GlassButton {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 12
                z: 2
                text: "Jump to present"
                prominent: true
                visible: (!list.followTail && !list.nearEnd()) || list.pendingMessageCount > 0
                onClicked: list.jumpToPresent()
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.border }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.topMargin: 10
            Layout.bottomMargin: 10
            spacing: 8

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: replyRow.implicitHeight + 12
                visible: panel.replyTargetId.length > 0
                radius: theme.radiusSm
                color: theme.surface
                border.color: theme.border
                RowLayout {
                    id: replyRow
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 6
                    spacing: 8
                    Label { text: "Replying to"; color: theme.textFaint; font.pixelSize: theme.fontCaption }
                    Label { text: panel.replyTargetUser; color: theme.text; font.pixelSize: theme.fontCaption; font.weight: Font.DemiBold; elide: Text.ElideRight }
                    Label { Layout.fillWidth: true; text: panel.replyTargetBody; color: theme.textMuted; font.pixelSize: theme.fontCaption; elide: Text.ElideRight }
                    Components.GlassButton { text: "Cancel"; onClicked: panel.clearReplyTarget() }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
            TextField {
                id: composer
                property int mentionStart: -1
                property string mentionQuery: ""
                property string emoteQuery: ""

                Layout.fillWidth: true
                Layout.preferredHeight: 40
                placeholderText: authService.signedIn ? (chatModel.channel.length > 0 ? "Send a message" : "Open a channel to chat") : "Connect Twitch to chat"
                enabled: authService.signedIn && chatModel.channel.length > 0
                color: theme.text
                placeholderTextColor: theme.textFaint
                leftPadding: 12
                rightPadding: 12
                background: Rectangle { radius: theme.radiusSm; color: theme.field; border.color: composer.activeFocus ? theme.borderStrong : theme.border }
                function mentionMatches() {
                    if (mentionStart < 0) return []
                    const query = mentionQuery.toLowerCase()
                    const users = chatModel.knownUsers
                    const matches = []
                    for (let i = 0; i < users.length; ++i) {
                        const user = users[i]
                        const login = String(user.login)
                        const displayName = String(user.displayName)
                        if (query.length === 0 || login.toLowerCase().indexOf(query) === 0 || displayName.toLowerCase().indexOf(query) === 0) {
                            matches.push(user)
                        }
                        if (matches.length >= 6) break
                    }
                    return matches
                }
                function updateMentionState() {
                    const prefix = text.slice(0, cursorPosition)
                    const match = /(^|\s)@([A-Za-z0-9_]*)$/.exec(prefix)
                    if (!match) {
                        mentionStart = -1
                        mentionQuery = ""
                        mentionPopup.close()
                        return
                    }
                    mentionQuery = match[2]
                    mentionStart = prefix.length - mentionQuery.length - 1
                    if (mentionMatches().length > 0) mentionPopup.open()
                    else mentionPopup.close()
                }
                function insertMention(user) {
                    const before = text.slice(0, mentionStart)
                    const after = text.slice(cursorPosition)
                    const mention = "@" + user.displayName + " "
                    text = before + mention + after
                    cursorPosition = (before + mention).length
                    mentionPopup.close()
                    forceActiveFocus()
                }
                function acceptMentionSuggestion() {
                    if (mentionStart < 0) return false
                    const matches = mentionMatches()
                    if (matches.length === 0) return false
                    insertMention(matches[0])
                    return true
                }
                function openEmotePicker() {
                    emoteQuery = ""
                    chatModel.refreshEmotePicker()
                    emotePopup.open()
                    forceActiveFocus()
                }
                function filteredEmoteGroups() {
                    const query = emoteQuery.toLowerCase()
                    const groups = []
                    const indexByKey = ({})
                    const source = chatModel.emotePickerEmotes
                    for (let i = 0; i < source.length; ++i) {
                        const emote = source[i]
                        const provider = String(emote.provider || "Twitch")
                        const owner = String(emote.owner || "Global")
                        const name = String(emote.name)
                        if (query.length > 0 && name.toLowerCase().indexOf(query) < 0 && provider.toLowerCase().indexOf(query) < 0 && owner.toLowerCase().indexOf(query) < 0) continue
                        const key = provider + "|" + owner
                        let groupIndex = indexByKey[key]
                        if (groupIndex === undefined) {
                            groupIndex = groups.length
                            indexByKey[key] = groupIndex
                            groups.push({ provider: provider, owner: owner, title: provider + " " + owner, emotes: [] })
                        }
                        groups[groupIndex].emotes.push(emote)
                    }
                    return groups
                }
                function filteredEmoteCount() {
                    const groups = filteredEmoteGroups()
                    let count = 0
                    for (let i = 0; i < groups.length; ++i) count += groups[i].emotes.length
                    return count
                }
                function insertEmote(emote) {
                    const before = text.slice(0, cursorPosition)
                    const after = text.slice(cursorPosition)
                    const prefix = before.length === 0 || /\s$/.test(before) ? "" : " "
                    const suffix = after.length === 0 || /^\s/.test(after) ? " " : " "
                    text = before + prefix + emote.name + suffix + after
                    cursorPosition = (before + prefix + emote.name + suffix).length
                    emotePopup.close()
                    forceActiveFocus()
                }
                function submit() {
                    const body = text.trim()
                    const sent = panel.replyTargetId.length > 0 ? chatModel.sendReply(body, panel.replyTargetId) : chatModel.sendMessage(body)
                    if (body.length > 0 && sent) {
                        text = ""
                    }
                }
                onTextChanged: updateMentionState()
                onCursorPositionChanged: updateMentionState()
                onAccepted: if (!acceptMentionSuggestion()) submit()

                Connections {
                    target: chatModel
                    ignoreUnknownSignals: true
                    function onKnownUsersChanged() { composer.updateMentionState() }
                    function onSendSucceeded() { panel.clearReplyTarget() }
                    function onChannelChanged() { panel.clearReplyTarget() }
                }

                Popup {
                    id: mentionPopup
                    x: 0
                    y: -height - 8
                    width: composer.width
                    height: Math.min(220, mentionList.contentHeight + 10)
                    padding: 5
                    modal: false
                    focus: false
                    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
                    background: Rectangle { radius: theme.radiusMd; color: theme.panelRaised; border.color: theme.borderStrong }
                    ListView {
                        id: mentionList
                        anchors.fill: parent
                        clip: true
                        model: composer.mentionMatches()
                        delegate: Rectangle {
                            width: mentionList.width
                            height: 34
                            radius: theme.radiusSm
                            color: mentionMouse.containsMouse ? theme.surfaceHover : "transparent"
                            MouseArea { id: mentionMouse; anchors.fill: parent; hoverEnabled: true; onClicked: composer.insertMention(modelData) }
                            Label {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                text: "@" + modelData.displayName
                                color: theme.text
                                font.pixelSize: theme.fontMeta
                                elide: Text.ElideRight
                            }
                        }
                    }
                }

                Popup {
                    id: emotePopup
                    x: Math.min(0, composer.width - width)
                    y: -height - 8
                    width: Math.min(preferences.get("emotePickerWidth"), Math.max(280, panel.width - 20))
                    height: Math.min(preferences.get("emotePickerHeight"), 440)
                    padding: 10
                    modal: false
                    focus: false
                    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
                    background: Rectangle { radius: theme.radiusMd; color: theme.panelRaised; border.color: theme.borderStrong }

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true
                            Label { Layout.fillWidth: true; text: "Emotes"; color: theme.text; font.pixelSize: theme.fontBody; font.weight: Font.DemiBold }
                            Label { text: String(composer.filteredEmoteCount()) + " / " + String(chatModel.emotePickerEmotes.length); color: theme.textFaint; font.pixelSize: theme.fontCaption }
                        }

                        TextField {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 34
                            placeholderText: "Search emotes"
                            text: composer.emoteQuery
                            color: theme.text
                            placeholderTextColor: theme.textFaint
                            selectByMouse: true
                            onTextChanged: composer.emoteQuery = text
                            background: Rectangle { radius: theme.radiusSm; color: theme.field; border.color: parent.activeFocus ? theme.borderStrong : theme.border }
                        }

                        ScrollView {
                            id: emoteScroll
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true

                            Column {
                                id: emoteGroupColumn
                                width: emoteScroll.availableWidth
                                spacing: 12

                                Repeater {
                                    model: composer.filteredEmoteGroups()
                                    delegate: Column {
                                        width: emoteGroupColumn.width
                                        spacing: 6

                                        RowLayout {
                                            width: parent.width
                                            Label { Layout.fillWidth: true; text: modelData.title; color: theme.textSoft; font.pixelSize: theme.fontMeta; font.weight: Font.DemiBold; elide: Text.ElideRight }
                                            Label { text: String(modelData.emotes.length); color: theme.textFaint; font.pixelSize: theme.fontCaption }
                                        }

                                        Flow {
                                            id: emoteFlow
                                            width: parent.width
                                            spacing: 6
                                            property int columnCount: Math.max(3, Math.floor((width + spacing) / 68))
                                            property real tileWidth: Math.floor((width - spacing * (columnCount - 1)) / columnCount)
                                            Repeater {
                                                model: modelData.emotes
                                                delegate: Rectangle {
                                                    width: emoteFlow.tileWidth
                                                    height: 64
                                                    radius: theme.radiusSm
                                                    color: emoteMouse.containsMouse ? theme.surfaceHover : theme.surface
                                                    border.color: emoteMouse.containsMouse ? theme.borderStrong : theme.border
                                                    MouseArea { id: emoteMouse; anchors.fill: parent; hoverEnabled: true; onClicked: composer.insertEmote(modelData) }
                                                    Column {
                                                        anchors.fill: parent
                                                        anchors.margins: 6
                                                        spacing: 3
                                                        Item {
                                                            width: parent.width
                                                            height: 32
                                                            Components.StableImage { id: pickerEmoteImage; anchors.centerIn: parent; width: 30; height: 30; source: modelData.imageUrl; fillMode: Image.PreserveAspectFit; fill: theme.transparent; preferredSourceWidth: 60; preferredSourceHeight: 60 }
                                                            Text { anchors.centerIn: parent; visible: !pickerEmoteImage.ready; text: String(modelData.name || "?").charAt(0).toUpperCase(); color: theme.textMuted; font.pixelSize: 12; font.weight: Font.DemiBold }
                                                        }
                                                        Label { width: parent.width; text: modelData.name; color: theme.textSoft; font.pixelSize: 9; elide: Text.ElideRight; horizontalAlignment: Text.AlignHCenter }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: composer.filteredEmoteCount() === 0
                            text: chatModel.emotePickerEmotes.length === 0 ? "No emotes loaded yet. Try again after chat connects." : "No matching emotes."
                            color: theme.textMuted
                            wrapMode: Text.WordWrap
                            font.pixelSize: theme.fontMeta
                        }
                    }
                }
            }
            Components.GlassButton { text: ":)"; enabled: composer.enabled; onClicked: composer.openEmotePicker() }
            Components.GlassButton { text: authService.signedIn ? "Send" : "Connect"; onClicked: authService.signedIn ? composer.submit() : panel.connectRequested() }
            }
        }
    }

    Rectangle {
        id: hoverPreview
        x: panel.hoverPreviewX
        y: panel.hoverPreviewY
        z: 100
        visible: panel.hoverText.length > 0
        width: Math.min(260, hoverRow.implicitWidth + 18)
        height: hoverRow.implicitHeight + 14
        radius: theme.radiusMd
        color: theme.panelRaised
        border.color: theme.borderStrong
        RowLayout {
            id: hoverRow
            anchors.fill: parent
            anchors.margins: 7
            spacing: 8
            Image {
                visible: panel.hoverImage.length > 0 && status === Image.Ready
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                source: panel.hoverImage
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: true
                mipmap: true
                sourceSize.width: 64
                sourceSize.height: 64
            }
            Label {
                Layout.maximumWidth: 190
                text: panel.hoverText
                color: theme.text
                font.pixelSize: theme.fontMeta
                elide: Text.ElideRight
            }
        }
    }
}
