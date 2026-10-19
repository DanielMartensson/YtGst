import QtQuick
import QtQuick.Controls
import Ytgst

Window {
    id: playerWindow

    // The panel is laid out once at 720x140 and then scaled as a whole, so
    // every margin, icon and font grows by exactly the same factor. The window
    // grows with it (panelRoot below does the scaling), and the controls are
    // rendered at the larger size through a supersampled layer, so text stays
    // sharp instead of being magnified.
    width: Math.round(720 * panelScale)
    height: Math.round(140 * panelScale)
    minimumWidth: Math.round(560 * panelScale)
    minimumHeight: Math.round(140 * panelScale)
    visible: false
    // The picture has a window of its own (VideoWindow.qml, filled by
    // waylandsink), so the panel is only the controls: a frameless, see-through
    // strip that can sit on top of the picture and is moved by dragging its
    // empty areas.
    color: "transparent"
    flags: Qt.FramelessWindowHint
    title: videoTitle.length > 0 ? videoTitle : qsTr("YtGst Player")

    property string videoId: ""
    property string videoTitle: ""
    property bool standalone: false

    // After this many milliseconds without the pointer over the panel the
    // controls fade away; they come back the moment the pointer is over the
    // panel again. The window itself stays mapped so the pointer still lands on
    // it while the controls are invisible.
    property int autoHideInterval: 5000
    property bool chromeVisible: true

    // Panel size: S is the original 720x140 strip, the others scale the whole
    // panel proportionally. XL stops at 2.5x because the panel is centred on a
    // 1920 pixel wide output (720 * 2.5 = 1800) - at 3x it would be wider than
    // the screen.
    property int panelSizeIndex: 0
    readonly property var panelScales: [1.0, 1.5, 2.0, 2.5]
    readonly property var panelSizeNames: ["S", "M", "L", "XL"]
    readonly property real panelScale: panelScales[panelSizeIndex]
    readonly property string panelSizeName: panelSizeNames[panelSizeIndex]

    function setPanelSize(index) {
        if (index < 0 || index >= panelScales.length || index === panelSizeIndex)
            return;
        panelSizeIndex = index;
    }

    function cyclePanelSize() {
        setPanelSize((panelSizeIndex + 1) % panelScales.length);
    }

    // ------------------------------------------------------------------
    // Helpers
    // ------------------------------------------------------------------
    function formatTime(ms) {
        if (!isFinite(ms) || ms < 0)
            ms = 0;
        const total = Math.floor(ms / 1000);
        const h = Math.floor(total / 3600);
        const m = Math.floor((total % 3600) / 60);
        const s = total % 60;
        const pad = (n) => (n < 10 ? "0" + n : "" + n);
        return h > 0 ? (h + ":" + pad(m) + ":" + pad(s)) : (m + ":" + pad(s));
    }

    function formatRate(rate) {
        const rounded = Math.round(rate * 100) / 100;
        return rounded + "x";
    }

    function formatResolution(height) {
        return height > 0 ? (height + "p") : qsTr("Auto");
    }

    // ------------------------------------------------------------------
    // Stay over the video window.
    //
    // The panel is the video window's Wayland child (transientParent). Weston
    // stacks a child immediately in front of its parent, activates the child
    // instead of the parent and keeps the two in the same layer, so the
    // controls stay over the picture - also in fullscreen - without ever
    // hiding and re-showing a window. That matters on this board: hiding a Qt
    // window destroys its Wayland surface, and the VeriSilicon Vulkan driver
    // segfaults when a window's surface is recreated.
    // ------------------------------------------------------------------
    transientParent: videoWindow

    // The video window has to be mapped before the panel: a child can only
    // attach to a parent that already has a Wayland surface.
    onStandaloneChanged: {
        if (!standalone)
            return;
        videoWindow.visible = true;
        videoWindow.syncVisibility();
        panelShowTimer.start();
    }

    Timer {
        id: panelShowTimer
        interval: 0
        onTriggered: playerWindow.visible = true
    }

    // ------------------------------------------------------------------
    // Fade the controls out when the pointer has been away for a while.
    // The animation drives a container the controls are moved into (see
    // Component.onCompleted below), so the panel background and every control
    // fade - but the subtitles, which live outside the container, do not.
    // ------------------------------------------------------------------
    NumberAnimation {
        id: chromeFade
        target: chrome
        property: "opacity"
        duration: 220
        easing.type: Easing.InOutQuad
    }

    Timer {
        id: chromeHideTimer
        interval: playerWindow.autoHideInterval
        onTriggered: playerWindow.hideChrome()
    }

    function revealChrome() {
        chromeHideTimer.stop();
        if (!chromeVisible) {
            chromeVisible = true;
            chromeFade.to = 1.0;
            chromeFade.start();
        }
    }

    function hideChrome() {
        if (!chromeVisible)
            return;
        chromeVisible = false;
        chromeFade.to = 0.0;
        chromeFade.start();
    }

    function scheduleHide() {
        if (chromeVisible)
            chromeHideTimer.restart();
    }

    // ------------------------------------------------------------------
    // Player
    // ------------------------------------------------------------------
    Player {
        id: player
        onErrorOccurred: (message) => {
            errorLabel.text = message;
            errorClearTimer.restart();
        }
        onPlayingChanged: {
            if (!playing)
                playerWindow.revealChrome();  // show the play button again
        }
    }

    // ------------------------------------------------------------------
    // The picture's own window
    //
    // It has to exist - and be mapped, so that it has a Wayland surface -
    // before playback starts: the sink draws the picture into a sub-surface of
    // that surface (Player::embedVideoIntoWindow()). The frame around the
    // picture is this window's, which is what makes the picture movable.
    // ------------------------------------------------------------------
    VideoWindow {
        id: videoWindow
        playback: player
        videoTitle: playerWindow.videoTitle
        fullscreen: player.fullscreen

        onCloseRequested: playerWindow.closePlayer()
    }

    // ------------------------------------------------------------------
    // Panel background, drag handle and hover tracker.
    //
    // dragArea sits below the controls (z 1), so dragging the empty parts of the
    // panel asks the compositor to move the window; hoverArea sits above
    // everything (z 20) and accepts hover only, so clicks pass straight through
    // to the controls while the fade timer still sees the pointer.
    // ------------------------------------------------------------------
    Rectangle {
        id: panelBackground
        anchors.fill: parent
        anchors.margins: 2
        radius: 12
        color: "#a60b0b0c"
        border.color: "#2effffff"
        border.width: 1
        z: 0
    }

    MouseArea {
        id: dragArea
        anchors.fill: parent
        z: 1
        acceptedButtons: Qt.LeftButton
        onPressed: playerWindow.startSystemMove()
    }

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        z: 20
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        onContainsMouseChanged: {
            if (containsMouse)
                playerWindow.revealChrome();
            else
                playerWindow.scheduleHide();
        }
        onPositionChanged: playerWindow.revealChrome()
    }

    // ------------------------------------------------------------------
    // Error label (shown briefly when playback has a problem)
    // ------------------------------------------------------------------
    Text {
        id: errorLabel
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 12
        color: "#e8716d"
        font.pixelSize: 13
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
        visible: text.length > 0
        z: 5
    }
    Timer {
        id: errorClearTimer
        interval: 8000
        onTriggered: errorLabel.text = ""
    }

    // ------------------------------------------------------------------
    // Progress bar (always visible, seekable)
    // ------------------------------------------------------------------
    Item {
        id: progressBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 8
        height: 36
        z: 3

        readonly property bool active: mouseArea.containsMouse || scrubbing
        property bool scrubbing: false
        property real scrubValue: 0
        readonly property real shownPosition: scrubbing ? scrubValue : player.position
        readonly property real progress: player.duration > 0
            ? Math.max(0, Math.min(1, shownPosition / player.duration)) : 0

        Rectangle {
            id: track
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            height: progressBar.active ? 5 : 3
            radius: height / 2
            color: "#55ffffff"
            Behavior on height { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }

            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: Math.max(0, Math.min(parent.width, parent.width * progressBar.progress))
                radius: height / 2
                color: "#ff0000"
            }
        }

        Rectangle {
            id: handle
            visible: progressBar.active
            width: 14
            height: 14
            radius: 7
            color: "#ff0000"
            x: Math.max(0, Math.min(track.width - width, track.width * progressBar.progress - width / 2))
            anchors.verticalCenter: track.verticalCenter
        }

        Rectangle {
            id: timeBubble
            visible: progressBar.active && player.duration > 0
            color: "#cc000000"
            radius: 4
            width: bubbleText.implicitWidth + 14
            height: bubbleText.implicitHeight + 8
            x: Math.max(2, Math.min(progressBar.width - width - 2, mouseArea.mouseX - width / 2))
            anchors.top: track.top
            anchors.topMargin: -16

            Text {
                id: bubbleText
                anchors.centerIn: parent
                color: "#ffffff"
                font.pixelSize: 12
                text: playerWindow.formatTime(progressBar.shownPosition)
            }
        }

        MouseArea {
            id: mouseArea
            anchors.fill: parent
            hoverEnabled: true
            enabled: player.duration > 0
            cursorShape: Qt.PointingHandCursor

            function msAt(px) {
                return Math.round(Math.max(0, Math.min(1, px / width)) * player.duration);
            }

            onPressed: (mouse) => {
                progressBar.scrubbing = true;
                progressBar.scrubValue = msAt(mouse.x);
            }
            onPositionChanged: (mouse) => {
                if (progressBar.scrubbing)
                    progressBar.scrubValue = msAt(mouse.x);
            }
            onReleased: (mouse) => {
                if (progressBar.scrubbing) {
                    player.seek(msAt(mouse.x));
                    progressBar.scrubbing = false;
                }
            }
        }
    }

    // ------------------------------------------------------------------
    // Buttons row (fades together with the rest of the panel)
    // ------------------------------------------------------------------
    // Play/pause
    Rectangle {
        id: playButton
        width: 40
        height: 40
        radius: 20
        color: playButtonMouse.pressed ? "#e6000000" : "#b3000000"
        border.color: "#33ffffff"
        border.width: 1
        z: 3
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: 52
        anchors.leftMargin: 12

        Canvas {
            id: playIcon
            anchors.centerIn: parent
            width: 18
            height: 18
            onPaint: {
                var ctx = getContext("2d");
                ctx.reset();
                ctx.fillStyle = "#ffffff";
                if (player.playing) {
                    ctx.fillRect(3, 1, 5, 16);
                    ctx.fillRect(10, 1, 5, 16);
                } else {
                    ctx.beginPath();
                    ctx.moveTo(4, 1);
                    ctx.lineTo(17, 9);
                    ctx.lineTo(4, 17);
                    ctx.closePath();
                    ctx.fill();
                }
            }
            Connections {
                target: player
                function onPlayingChanged() { playIcon.requestPaint(); }
            }
        }

        MouseArea {
            id: playButtonMouse
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: player.togglePlayPause()
        }
    }

    // Speed (0.25x-2x)
    Rectangle {
        id: speedButton
        width: 52
        height: 40
        radius: 20
        color: speedButtonMouse.pressed ? "#e6000000" : "#b3000000"
        border.color: "#33ffffff"
        border.width: 1
        z: 3
        anchors.top: parent.top
        anchors.left: playButton.right
        anchors.leftMargin: 8
        anchors.topMargin: 52

        Text {
            anchors.centerIn: parent
            color: "#ffffff"
            font.pixelSize: 13
            font.bold: true
            text: playerWindow.formatRate(player.rate)
        }

        MouseArea {
            id: speedButtonMouse
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: speedMenu.open()
        }

        Menu {
            id: speedMenu
            width: 140
            y: -height - 6
            x: (speedButton.width - width) / 2
            MenuItem { text: "0.25x"; font.bold: Math.abs(player.rate - 0.25) < 0.001; onTriggered: player.setRate(0.25) }
            MenuItem { text: "0.5x"; font.bold: Math.abs(player.rate - 0.5) < 0.001; onTriggered: player.setRate(0.5) }
            MenuItem { text: "0.75x"; font.bold: Math.abs(player.rate - 0.75) < 0.001; onTriggered: player.setRate(0.75) }
            MenuItem { text: "1x"; font.bold: Math.abs(player.rate - 1.0) < 0.001; onTriggered: player.setRate(1.0) }
            MenuItem { text: "1.25x"; font.bold: Math.abs(player.rate - 1.25) < 0.001; onTriggered: player.setRate(1.25) }
            MenuItem { text: "1.5x"; font.bold: Math.abs(player.rate - 1.5) < 0.001; onTriggered: player.setRate(1.5) }
            MenuItem { text: "1.75x"; font.bold: Math.abs(player.rate - 1.75) < 0.001; onTriggered: player.setRate(1.75) }
            MenuItem { text: "2x"; font.bold: Math.abs(player.rate - 2.0) < 0.001; onTriggered: player.setRate(2.0) }
        }
    }

    // Resolution (Auto + available)
    Rectangle {
        id: resolutionButton
        width: Math.max(52, resolutionText.implicitWidth + 22)
        height: 40
        radius: 20
        color: resolutionButtonMouse.pressed ? "#e6000000" : "#b3000000"
        border.color: "#33ffffff"
        border.width: 1
        z: 3
        anchors.top: parent.top
        anchors.left: speedButton.right
        anchors.leftMargin: 8
        anchors.topMargin: 52

        Text {
            id: resolutionText
            anchors.centerIn: parent
            color: "#ffffff"
            font.pixelSize: 13
            font.bold: true
            text: playerWindow.formatResolution(player.resolution)
        }

        MouseArea {
            id: resolutionButtonMouse
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: resolutionMenu.open()
        }

        Menu {
            id: resolutionMenu
            width: 140
            y: -height - 6
            x: (resolutionButton.width - width) / 2
            MenuItem {
                // yt-dlp needs ~20 s on the STM32MP25 before the format list is
                // in, so say that the list is coming while it is still empty.
                text: player.resolutions.length === 0
                    ? qsTr("Auto (loading resolutions…)") : qsTr("Auto")
                font.bold: player.resolution >= resolutionButton.maxAvailableHeight
                onTriggered: player.setResolution(0)
            }
            Repeater {
                model: player.resolutions
                MenuItem {
                    text: modelData + "p"
                    font.bold: player.resolution === modelData
                    onTriggered: player.setResolution(modelData)
                }
            }
        }

        readonly property int maxAvailableHeight: player.resolutions.length > 0
            ? player.resolutions[0] : 0
    }

    // Subtitle (CC) - shown only if the video has tracks
    Rectangle {
        id: subtitleButton
        width: 46
        height: 40
        radius: 20
        color: subtitleButtonMouse.pressed ? "#e6000000" : "#b3000000"
        border.color: player.subtitleLanguage.length > 0 ? "#ffffff" : "#33ffffff"
        border.width: player.subtitleLanguage.length > 0 ? 2 : 1
        z: 3
        anchors.top: parent.top
        anchors.left: resolutionButton.right
        anchors.leftMargin: 8
        anchors.topMargin: 52

        Text {
            anchors.centerIn: parent
            color: "#ffffff"
            font.pixelSize: 13
            font.bold: true
            text: "CC"
        }

        MouseArea {
            id: subtitleButtonMouse
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: subtitleMenu.open()
        }

        Menu {
            id: subtitleMenu
            width: 220
            y: -height - 6
            x: (subtitleButton.width - width) / 2
            MenuItem {
                text: qsTr("Off")
                font.bold: player.subtitleLanguage.length === 0
                onTriggered: player.setSubtitleLanguage("")
            }
            Repeater {
                model: player.subtitleTracks
                MenuItem {
                    text: modelData.name
                    font.bold: player.subtitleLanguage === modelData.code
                    onTriggered: player.setSubtitleLanguage(modelData.code)
                }
            }
        }
    }

    // Fullscreen. The picture's window is the application's own
    // (VideoWindow.qml), so this only flips the player's fullscreen state; the
    // window itself changes its visibility state in reaction.
    Rectangle {
        id: fullscreenButton
        width: 40
        height: 40
        radius: 20
        color: fullscreenButtonMouse.pressed ? "#e6000000" : "#b3000000"
        border.color: player.fullscreen ? "#ffffff" : "#33ffffff"
        border.width: player.fullscreen ? 2 : 1
        z: 3
        anchors.top: parent.top
        anchors.left: subtitleButton.right
        anchors.leftMargin: 8
        anchors.topMargin: 52

        Canvas {
            id: fullscreenIcon
            anchors.centerIn: parent
            width: 18
            height: 18
            onPaint: {
                var ctx = getContext("2d");
                ctx.reset();
                ctx.strokeStyle = "#ffffff";
                ctx.lineWidth = 2;
                var m = 1;
                var s = 5;
                var w = width;
                var h = height;
                if (player.fullscreen) {
                    // Point the corners inwards for "leave fullscreen".
                    ctx.beginPath(); ctx.moveTo(m, m + s); ctx.lineTo(m + s, m + s); ctx.lineTo(m + s, m); ctx.stroke();
                    ctx.beginPath(); ctx.moveTo(w - m - s, m); ctx.lineTo(w - m - s, m + s); ctx.lineTo(w - m, m + s); ctx.stroke();
                    ctx.beginPath(); ctx.moveTo(m, h - m - s); ctx.lineTo(m + s, h - m - s); ctx.lineTo(m + s, h - m); ctx.stroke();
                    ctx.beginPath(); ctx.moveTo(w - m - s, h - m); ctx.lineTo(w - m - s, h - m - s); ctx.lineTo(w - m, h - m - s); ctx.stroke();
                } else {
                    ctx.beginPath(); ctx.moveTo(m, m + s); ctx.lineTo(m, m); ctx.lineTo(m + s, m); ctx.stroke();
                    ctx.beginPath(); ctx.moveTo(w - m - s, m); ctx.lineTo(w - m, m); ctx.lineTo(w - m, m + s); ctx.stroke();
                    ctx.beginPath(); ctx.moveTo(m, h - m - s); ctx.lineTo(m, h - m); ctx.lineTo(m + s, h - m); ctx.stroke();
                    ctx.beginPath(); ctx.moveTo(w - m - s, h - m); ctx.lineTo(w - m, h - m); ctx.lineTo(w - m, h - m - s); ctx.stroke();
                }
            }
            Connections {
                target: player
                function onFullscreenChanged() { fullscreenIcon.requestPaint(); }
            }
        }

        MouseArea {
            id: fullscreenButtonMouse
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: player.toggleFullscreen()
        }
    }

    // Download (click starts, click again cancels)
    Rectangle {
        id: downloadButton
        width: 40
        height: 40
        radius: 20
        color: downloadButtonMouse.pressed ? "#e6000000" : "#b3000000"
        border.color: player.downloading ? "#ffffff" : "#33ffffff"
        border.width: player.downloading ? 2 : 1
        z: 3
        anchors.top: parent.top
        anchors.left: fullscreenButton.right
        anchors.leftMargin: 8
        anchors.topMargin: 52

        Canvas {
            id: downloadIcon
            anchors.centerIn: parent
            width: 18
            height: 18
            onPaint: {
                var ctx = getContext("2d");
                ctx.reset();
                ctx.strokeStyle = "#ffffff";
                ctx.lineWidth = 2;
                if (player.downloading) {
                    var start = -Math.PI / 2;
                    ctx.beginPath();
                    ctx.arc(9, 9, 7, start, start + 2 * Math.PI * Math.max(0.02, player.downloadProgress));
                    ctx.stroke();
                } else {
                    ctx.beginPath(); ctx.moveTo(9, 2); ctx.lineTo(9, 12); ctx.stroke();
                    ctx.beginPath(); ctx.moveTo(5, 8); ctx.lineTo(9, 12); ctx.lineTo(13, 8); ctx.stroke();
                    ctx.beginPath(); ctx.moveTo(3, 15); ctx.lineTo(15, 15); ctx.stroke();
                }
            }
            Connections {
                target: player
                function onDownloadingChanged() { downloadIcon.requestPaint(); }
                function onDownloadProgressChanged() { downloadIcon.requestPaint(); }
            }
        }

        MouseArea {
            id: downloadButtonMouse
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: player.download()
        }
    }

    // Panel size (S/M/L/XL). Scales the whole strip proportionally: S is the
    // panel as it has always looked, M is 1.5x, L 2x and XL 2.5x.
    Rectangle {
        id: sizeButton
        width: 46
        height: 40
        radius: 20
        color: sizeButtonMouse.pressed ? "#e6000000" : "#b3000000"
        border.color: "#33ffffff"
        border.width: 1
        z: 3
        anchors.top: parent.top
        anchors.left: downloadButton.right
        anchors.leftMargin: 8
        anchors.topMargin: 52

        Text {
            anchors.centerIn: parent
            color: "#ffffff"
            font.pixelSize: 13
            font.bold: true
            text: playerWindow.panelSizeName
        }

        MouseArea {
            id: sizeButtonMouse
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: sizeMenu.open()
        }

        Menu {
            id: sizeMenu
            width: 140
            y: -height - 6
            x: (sizeButton.width - width) / 2
            MenuItem {
                text: qsTr("Small")
                font.bold: playerWindow.panelSizeIndex === 0
                onTriggered: playerWindow.setPanelSize(0)
            }
            MenuItem {
                text: qsTr("Medium")
                font.bold: playerWindow.panelSizeIndex === 1
                onTriggered: playerWindow.setPanelSize(1)
            }
            MenuItem {
                text: qsTr("Large")
                font.bold: playerWindow.panelSizeIndex === 2
                onTriggered: playerWindow.setPanelSize(2)
            }
            MenuItem {
                text: qsTr("Extra large")
                font.bold: playerWindow.panelSizeIndex === 3
                onTriggered: playerWindow.setPanelSize(3)
            }
        }
    }

    // Close. The panel is the process's only visible control, so closing it also
    // takes the video window down (close -> stop -> quit). The video window can
    // still be closed on its own from its title bar, which leaves the panel
    // running so the play button can bring the picture back.
    Rectangle {
        id: closeButton
        width: 40
        height: 40
        radius: 20
        color: closeButtonMouse.pressed ? "#e6000000" : "#b3000000"
        border.color: "#33ffffff"
        border.width: 1
        z: 3
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: 52
        anchors.rightMargin: 12

        Canvas {
            anchors.centerIn: parent
            width: 16
            height: 16
            onPaint: {
                var ctx = getContext("2d");
                ctx.reset();
                ctx.strokeStyle = "#ffffff";
                ctx.lineWidth = 2;
                ctx.beginPath(); ctx.moveTo(2, 2); ctx.lineTo(14, 14); ctx.stroke();
                ctx.beginPath(); ctx.moveTo(14, 2); ctx.lineTo(2, 14); ctx.stroke();
            }
        }

        MouseArea {
            id: closeButtonMouse
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: playerWindow.closePlayer()
        }
    }

    // Volume (clicking the icon shows a horizontal slider reaching leftwards,
    // side by side with the icon, quiet on the left)
    Item {
        id: volumeControl
        property bool expanded: false

        width: 40
        height: 40
        z: 3
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: 52
        anchors.rightMargin: 60

        Rectangle {
            id: volumePanel
            visible: volumeControl.expanded
            width: 150
            height: 44
            radius: 22
            color: "#cc000000"
            border.color: "#33ffffff"
            border.width: 1
            anchors.verticalCenter: volumeButton.verticalCenter
            anchors.right: volumeButton.left
            anchors.rightMargin: 8

            Slider {
                id: volumeSlider
                orientation: Qt.Horizontal
                anchors.centerIn: parent
                width: parent.width - 28
                height: 24
                from: 0.0
                to: 1.0
                value: player.volume
                onMoved: player.setVolume(value)

                Connections {
                    target: player
                    function onVolumeChanged() {
                        if (!volumeSlider.pressed)
                            volumeSlider.value = player.volume;
                    }
                }

                background: Rectangle {
                    x: volumeSlider.leftPadding
                    y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
                    width: volumeSlider.availableWidth
                    height: 4
                    radius: 2
                    color: "#55ffffff"

                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: volumeSlider.position * parent.width
                        radius: 2
                        color: "#ff0000"
                    }
                }

                handle: Rectangle {
                    x: volumeSlider.leftPadding + volumeSlider.visualPosition * (volumeSlider.availableWidth - width)
                    y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
                    width: 16
                    height: 16
                    radius: 8
                    color: "#ffffff"
                }
            }
        }

        Rectangle {
            id: volumeButton
            anchors.left: parent.left
            width: 40
            height: 40
            radius: 20
            color: volumeButtonMouse.pressed ? "#e6000000" : "#b3000000"
            border.color: "#33ffffff"
            border.width: 1

            Canvas {
                id: volumeIcon
                anchors.centerIn: parent
                width: 20
                height: 20
                onPaint: {
                    var ctx = getContext("2d");
                    ctx.reset();
                    ctx.fillStyle = "#ffffff";
                    ctx.strokeStyle = "#ffffff";
                    ctx.lineWidth = 2;
                    ctx.beginPath();
                    ctx.moveTo(3, 7);
                    ctx.lineTo(7, 7);
                    ctx.lineTo(12, 2);
                    ctx.lineTo(12, 18);
                    ctx.lineTo(7, 13);
                    ctx.lineTo(3, 13);
                    ctx.closePath();
                    ctx.fill();
                    if (player.muted || player.volume <= 0.001) {
                        ctx.beginPath();
                        ctx.moveTo(14, 6);
                        ctx.lineTo(19, 14);
                        ctx.moveTo(19, 6);
                        ctx.lineTo(14, 14);
                        ctx.stroke();
                    } else {
                        ctx.beginPath();
                        ctx.arc(12, 10, 5, -Math.PI / 3, Math.PI / 3);
                        ctx.stroke();
                        if (player.volume > 0.5) {
                            ctx.beginPath();
                            ctx.arc(12, 10, 8, -Math.PI / 3, Math.PI / 3);
                            ctx.stroke();
                        }
                    }
                }
                Connections {
                    target: player
                    function onVolumeChanged() { volumeIcon.requestPaint(); }
                    function onMutedChanged() { volumeIcon.requestPaint(); }
                }
            }

            MouseArea {
                id: volumeButtonMouse
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: volumeControl.expanded = !volumeControl.expanded
            }
        }
    }

    // Download status toast, above the download button
    Rectangle {
        id: downloadToast
        visible: player.downloadStatus.length > 0
        color: "#cc000000"
        radius: 6
        border.color: "#33ffffff"
        border.width: 1
        z: 4
        width: downloadToastText.implicitWidth + 20
        height: downloadToastText.implicitHeight + 10
        anchors.horizontalCenter: downloadButton.horizontalCenter
        anchors.bottom: downloadButton.top
        anchors.bottomMargin: 8

        Text {
            id: downloadToastText
            anchors.centerIn: parent
            color: "#ffffff"
            font.pixelSize: 12
            text: player.downloadStatus
        }
    }

    // ------------------------------------------------------------------
    // Everything on the panel lives inside this item, which is laid out at the
    // original 720x140 and scaled as a whole. The layer renders the contents at
    // the scaled resolution, so the scale-up is a 1:1 mapping of already-sharp
    // pixels instead of a magnified small rendering.
    // ------------------------------------------------------------------
    Item {
        id: panelRoot
        width: 720
        height: 140
        transformOrigin: Item.TopLeft
        scale: playerWindow.panelScale
        layer.enabled: true
        layer.smooth: true
        layer.textureSize: Qt.size(Math.ceil(width * playerWindow.panelScale),
                                   Math.ceil(height * playerWindow.panelScale))
    }

    // ------------------------------------------------------------------
    // Subtitles: white on a black plate, and they stay on screen when the
    // controls fade away. Every other item of the panel is moved into the
    // "chrome" container below, which is what the fade animates.
    // ------------------------------------------------------------------
    Item {
        id: chrome
        anchors.fill: parent
        z: 0
    }

    Item {
        id: subtitleBox
        visible: player.subtitleText.length > 0
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 8
        width: Math.min(subtitleText.implicitWidth, panelRoot.width - 40) + 24
        height: subtitleText.implicitHeight + 12
        z: 4

        Rectangle {
            anchors.fill: parent
            radius: 4
            color: "#cc000000"
            border.color: "#33ffffff"
            border.width: 1
        }

        Text {
            id: subtitleText
            anchors.centerIn: parent
            width: parent.width - 24
            color: "#ffffff"
            font.pixelSize: 16
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: player.subtitleText
        }
    }

    // The controls are collected here once the window's items exist, so the
    // fade above can exclude the subtitles. The container covers the scaled
    // root, so every item keeps its anchors and grows with the panel.
    Component.onCompleted: {
        const items = playerWindow.contentItem.children.slice();
        for (let i = 0; i < items.length; ++i) {
            const item = items[i];
            if (item === chrome || item === subtitleBox || item === panelRoot)
                continue;
            item.parent = chrome;
        }
        chrome.parent = panelRoot;
        subtitleBox.parent = panelRoot;
    }

    // ------------------------------------------------------------------
    // Start playback
    // ------------------------------------------------------------------
    onVideoIdChanged: {
        errorLabel.text = "";
        if (videoId.length > 0)
            playTimer.restart();
    }

    Timer {
        id: playTimer
        interval: 300
        onTriggered: {
            // The picture's window is mapped for the whole session (one that is
            // shown again gets a new Wayland surface, which the board's Vulkan
            // driver does not survive), so this only makes sure it is visible
            // before the sink draws into it.
            videoWindow.visible = true;
            videoWindow.syncVisibility();
            player.play(videoId);
        }
    }

    // Closing the panel ends the player, and with it the picture's window. The
    // picture's window cannot be hidden on its own - hiding destroys its
    // Wayland surface, which the board's Vulkan driver does not survive - so
    // closing the picture ends the process that owns it.
    function closePlayer() {
        console.log("YtGst: closing the player");
        player.stop();
        videoId = "";
        Qt.quit();
    }

    onClosing: (close) => {
        // Refuse the close: the window must not be hidden (its Wayland surface
        // would be destroyed, and the board's Vulkan driver does not survive
        // that), so the process ends instead - and the windows with it.
        close.accepted = false;
        playerWindow.closePlayer();
    }

    // ------------------------------------------------------------------
    // Shortcuts
    //
    // Application-wide on purpose: the picture has a window of its own now, and
    // a key pressed while it is focused has to reach the player too.
    // ------------------------------------------------------------------
    Shortcut {
        sequence: "M"
        context: Qt.ApplicationShortcut
        onActivated: player.toggleMute()
    }
    Shortcut {
        sequence: "Up"
        context: Qt.ApplicationShortcut
        onActivated: player.setVolume(Math.min(1.0, player.volume + 0.05))
    }
    Shortcut {
        sequence: "Down"
        context: Qt.ApplicationShortcut
        onActivated: player.setVolume(Math.max(0.0, player.volume - 0.05))
    }
    Shortcut {
        sequence: "D"
        context: Qt.ApplicationShortcut
        onActivated: player.download()
    }
    Shortcut {
        sequence: "F"
        context: Qt.ApplicationShortcut
        onActivated: player.toggleFullscreen()
    }
    Shortcut {
        sequence: "F11"
        context: Qt.ApplicationShortcut
        onActivated: player.toggleFullscreen()
    }
    Shortcut {
        sequence: "Esc"
        context: Qt.ApplicationShortcut
        onActivated: playerWindow.close()
    }
}
