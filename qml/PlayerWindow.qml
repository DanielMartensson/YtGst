import QtQuick
import QtQuick.Controls
import Ytgst
import org.freedesktop.gstreamer.Qt6GLVideoItem 1.0

Window {
    id: playerWindow

    width: 960
    height: 540
    minimumWidth: 640
    minimumHeight: 360
    visible: false
    color: "#0b0b0c"
    title: videoTitle.length > 0 ? videoTitle : qsTr("YtGst Player")

    property string videoId: ""
    property string videoTitle: ""
    property bool standalone: false

    // Styrningen visas vid musrörelse och tonas ut efter 5 s utan rörelse.
    property bool controlsVisible: true
    readonly property bool controlsShown: controlsVisible || !player.playing

    function wakeControls() {
        controlsVisible = true;
        hideTimer.restart();
    }

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

    readonly property bool fullscreen: visibility === Window.FullScreen

    function toggleFullscreen() {
        visibility = fullscreen ? Window.Windowed : Window.FullScreen;
    }

    GstGLQt6VideoItem {
        id: videoSurface
        anchors.fill: parent
        visible: true
    }

    // Fångar mushover över hela fönstret utan att stjäla klick.
    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        onEntered: playerWindow.wakeControls()
        onPositionChanged: playerWindow.wakeControls()
        z: 1
    }

    // Släcker styrningen efter 5 s utan musrörelse.
    Timer {
        id: hideTimer
        interval: 5000
        onTriggered: playerWindow.controlsVisible = false
    }

    Text {
        id: errorLabel
        anchors.centerIn: parent
        visible: text.length > 0
        color: "#e8716d"
        font.pixelSize: 14
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
        width: parent.width - 80
        z: 2
    }

    Player {
        id: player
        videoItem: videoSurface
        onErrorOccurred: (message) => {
            errorLabel.text = message;
        }
        onPlayingChanged: {
            if (playing)
                playerWindow.wakeControls();
            else
                playerWindow.controlsVisible = true;
        }
    }

    // Progressbar i YouTube-stil, i full bredd och lyft ~5 mm från nederkanten.
    Item {
        id: progressBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        anchors.bottomMargin: 18
        height: 26
        z: 3

        readonly property bool active: hoverArea.containsMouse || scrubbing
        property bool scrubbing: false
        property real scrubValue: 0
        readonly property real shownPosition: scrubbing ? scrubValue : player.position
        readonly property real progress: player.duration > 0
            ? Math.max(0, Math.min(1, shownPosition / player.duration)) : 0

        opacity: progressBar.scrubbing ? 1.0
                 : (playerWindow.controlsShown ? (active ? 1.0 : 0.45) : 0.0)
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: 400; easing.type: Easing.OutCubic } }

        Rectangle {
            id: track
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
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
            anchors.bottom: track.top
            anchors.bottomMargin: 6

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

    // Play/paus-knapp (visas vid hover eller när videon är pausad).
    Rectangle {
        id: playButton
        width: 40
        height: 40
        radius: 20
        color: playButtonMouse.pressed ? "#e6000000" : "#b3000000"
        border.color: "#33ffffff"
        border.width: 1
        z: 3
        anchors.left: parent.left
        anchors.bottom: progressBar.top
        anchors.leftMargin: 12
        anchors.bottomMargin: 10
        opacity: playerWindow.controlsShown ? 1.0 : 0.0
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: 400; easing.type: Easing.OutCubic } }

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
        }

        Connections {
            target: player
            function onPlayingChanged() { playIcon.requestPaint(); }
        }

        MouseArea {
            id: playButtonMouse
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: player.togglePlayPause()
        }
    }

    // Hastighetsknapp med meny (0.25x–2x).
    Rectangle {
        id: speedButton
        width: 52
        height: 40
        radius: 20
        color: speedButtonMouse.pressed ? "#e6000000" : "#b3000000"
        border.color: "#33ffffff"
        border.width: 1
        z: 3
        anchors.left: playButton.right
        anchors.leftMargin: 8
        anchors.verticalCenter: playButton.verticalCenter
        opacity: playerWindow.controlsShown ? 1.0 : 0.0
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: 400; easing.type: Easing.OutCubic } }

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

    // Upplösningsknapp med meny (Auto + tillgängliga HLS-upplösningar).
    Rectangle {
        id: resolutionButton
        width: Math.max(52, resolutionText.implicitWidth + 22)
        height: 40
        radius: 20
        color: resolutionButtonMouse.pressed ? "#e6000000" : "#b3000000"
        border.color: "#33ffffff"
        border.width: 1
        z: 3
        anchors.left: speedButton.right
        anchors.leftMargin: 8
        anchors.verticalCenter: playButton.verticalCenter
        opacity: playerWindow.controlsShown ? 1.0 : 0.0
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: 400; easing.type: Easing.OutCubic } }

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
                text: qsTr("Auto")
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

    // Undertext-knapp (CC) med meny. Visas bara om videon har spår.
    Rectangle {
        id: subtitleButton
        width: 46
        height: 40
        radius: 20
        color: subtitleButtonMouse.pressed ? "#e6000000" : "#b3000000"
        border.color: player.subtitleLanguage.length > 0 ? "#ffffff" : "#33ffffff"
        border.width: player.subtitleLanguage.length > 0 ? 2 : 1
        z: 3
        anchors.left: resolutionButton.right
        anchors.leftMargin: 8
        anchors.verticalCenter: playButton.verticalCenter
        opacity: (player.subtitleTracks.length > 0 && playerWindow.controlsShown) ? 1.0 : 0.0
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: 400; easing.type: Easing.OutCubic } }

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
                text: qsTr("Av")
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

    // Fullskärmsknapp. Esc lämnar fullskärm (och stänger annars fönstret).
    Rectangle {
        id: fullscreenButton
        width: 40
        height: 40
        radius: 20
        color: fullscreenButtonMouse.pressed ? "#e6000000" : "#b3000000"
        border.color: "#33ffffff"
        border.width: 1
        z: 3
        anchors.left: subtitleButton.right
        anchors.leftMargin: 8
        anchors.verticalCenter: playButton.verticalCenter
        opacity: playerWindow.controlsShown ? 1.0 : 0.0
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: 400; easing.type: Easing.OutCubic } }

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
                if (!playerWindow.fullscreen) {
                    ctx.beginPath(); ctx.moveTo(m, m + s); ctx.lineTo(m, m); ctx.lineTo(m + s, m); ctx.stroke();
                    ctx.beginPath(); ctx.moveTo(w - m - s, m); ctx.lineTo(w - m, m); ctx.lineTo(w - m, m + s); ctx.stroke();
                    ctx.beginPath(); ctx.moveTo(m, h - m - s); ctx.lineTo(m, h - m); ctx.lineTo(m + s, h - m); ctx.stroke();
                    ctx.beginPath(); ctx.moveTo(w - m - s, h - m); ctx.lineTo(w - m, h - m); ctx.lineTo(w - m, h - m - s); ctx.stroke();
                } else {
                    ctx.beginPath(); ctx.moveTo(m, m + s); ctx.lineTo(m + s, m + s); ctx.lineTo(m + s, m); ctx.stroke();
                    ctx.beginPath(); ctx.moveTo(w - m, m + s); ctx.lineTo(w - m - s, m + s); ctx.lineTo(w - m - s, m); ctx.stroke();
                    ctx.beginPath(); ctx.moveTo(m, h - m - s); ctx.lineTo(m + s, h - m - s); ctx.lineTo(m + s, h - m); ctx.stroke();
                    ctx.beginPath(); ctx.moveTo(w - m, h - m - s); ctx.lineTo(w - m - s, h - m - s); ctx.lineTo(w - m - s, h - m); ctx.stroke();
                }
            }

            Connections {
                target: playerWindow
                function onFullscreenChanged() { fullscreenIcon.requestPaint(); }
            }
        }

        MouseArea {
            id: fullscreenButtonMouse
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: playerWindow.toggleFullscreen()
        }
    }

    // Nedladdningsknapp. Klick startar nedladdning, klick igen avbryter.
    Rectangle {
        id: downloadButton
        width: 40
        height: 40
        radius: 20
        color: downloadButtonMouse.pressed ? "#e6000000" : "#b3000000"
        border.color: player.downloading ? "#ffffff" : "#33ffffff"
        border.width: player.downloading ? 2 : 1
        z: 3
        anchors.left: fullscreenButton.right
        anchors.leftMargin: 8
        anchors.verticalCenter: playButton.verticalCenter
        opacity: playerWindow.controlsShown ? 1.0 : 0.0
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: 400; easing.type: Easing.OutCubic } }

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
                    ctx.beginPath();
                    ctx.moveTo(9, 2); ctx.lineTo(9, 12); ctx.stroke();
                    ctx.beginPath();
                    ctx.moveTo(5, 8); ctx.lineTo(9, 12); ctx.lineTo(13, 8); ctx.stroke();
                    ctx.beginPath();
                    ctx.moveTo(3, 15); ctx.lineTo(15, 15); ctx.stroke();
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

    // Statusruta för nedladdning, ovanför nedladdningsknappen.
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

    // Volymkontroll. Klick på ikonen visar ett vertikalt reglage ovanför.
    Item {
        id: volumeControl
        property bool expanded: false

        width: 40
        height: 40
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.verticalCenter: playButton.verticalCenter
        opacity: playerWindow.controlsShown ? 1.0 : 0.0
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: 400; easing.type: Easing.OutCubic } }

        // Panel med vertikalt reglage, ovanför knappen.
        Rectangle {
            id: volumePanel
            visible: volumeControl.expanded
            width: 44
            height: 140
            radius: 22
            color: "#cc000000"
            border.color: "#33ffffff"
            border.width: 1
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: volumeButton.top
            anchors.bottomMargin: 8

            Slider {
                id: volumeSlider
                orientation: Qt.Vertical
                anchors.centerIn: parent
                width: 24
                height: parent.height - 28
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
                    x: volumeSlider.leftPadding + volumeSlider.availableWidth / 2 - width / 2
                    y: volumeSlider.topPadding
                    width: 4
                    height: volumeSlider.availableHeight
                    radius: 2
                    color: "#55ffffff"

                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: volumeSlider.position * parent.height
                        radius: 2
                        color: "#ff0000"
                    }
                }

                handle: Rectangle {
                    x: volumeSlider.leftPadding + volumeSlider.availableWidth / 2 - width / 2
                    y: volumeSlider.topPadding + volumeSlider.visualPosition * (volumeSlider.availableHeight - height)
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

    // Undertextöverlägg ovanför kontrollerna.
    Rectangle {
        id: subtitleBox
        visible: player.subtitleText.length > 0
        color: "#cc000000"
        radius: 4
        z: 4
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 104
        width: subtitleText.width + 24
        height: subtitleText.height + 12

        Text {
            id: subtitleText
            anchors.centerIn: parent
            color: "#ffffff"
            font.pixelSize: 18
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            width: Math.min(implicitWidth, playerWindow.width * 0.8)
            text: player.subtitleText
        }
    }

    onVideoIdChanged: {
        errorLabel.text = "";
        if (videoId.length > 0)
            playTimer.restart();
    }

    Timer {
        id: playTimer
        interval: 300
        onTriggered: player.play(videoId)
    }

    onClosing: {
        player.stop();
        videoId = "";
        if (standalone)
            Qt.quit();
        else
            visible = false;
    }

    Shortcut {
        sequence: "M"
        onActivated: player.toggleMute()
    }

    Shortcut {
        sequence: "Up"
        onActivated: player.setVolume(Math.min(1.0, player.volume + 0.05))
    }

    Shortcut {
        sequence: "Down"
        onActivated: player.setVolume(Math.max(0.0, player.volume - 0.05))
    }

    Shortcut {
        sequence: "D"
        onActivated: player.download()
    }

    Shortcut {
        sequence: "F"
        onActivated: playerWindow.toggleFullscreen()
    }

    Shortcut {
        sequence: "F11"
        onActivated: playerWindow.toggleFullscreen()
    }

    Shortcut {
        sequence: "Esc"
        onActivated: {
            if (playerWindow.fullscreen)
                playerWindow.visibility = Window.Windowed;
            else
                playerWindow.close();
        }
    }
}
