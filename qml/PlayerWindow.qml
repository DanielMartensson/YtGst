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
    title: videoTitle.length > 0 ? videoTitle : qsTr("Ytgst Player")

    property string videoId: ""
    property string videoTitle: ""
    property bool standalone: false

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
        z: 1
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

        opacity: active ? 1.0 : 0.45
        Behavior on opacity { NumberAnimation { duration: 150 } }

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
        sequence: "Esc"
        onActivated: playerWindow.close()
    }
}
