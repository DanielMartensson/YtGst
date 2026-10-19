import QtQuick
import QtQuick.Controls
import Ytgst

Window {
    id: root
    width: 900
    height: 600
    visible: true
    title: qsTr("YtGst")
    color: "#0b0b0c"
    // Weston draws no window frames at all, so the window's own frame is drawn
    // here - the same shape the picture's window has: a title bar that the
    // window is dragged by, and a close button.
    flags: Qt.FramelessWindowHint

    readonly property int titleHeight: 34

    property string errorMessage: ""

    VideoListModel {
        id: videoModel
    }

    function openVideo(video) {
        launcher.play(video.id, video.title);
    }

    Youtube {
        id: youtube
        model: videoModel
        onSearchFailed: (message) => {
            root.errorMessage = message;
            errorTimer.restart();
        }
    }

    SearchBar {
        id: searchBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 20 + root.titleHeight
        anchors.leftMargin: 20
        anchors.rightMargin: 20

        onSubmitted: (query) => {
            root.errorMessage = "";
            youtube.search(query);
            listView.positionViewAtBeginning();
        }
    }

    Text {
        id: errorLabel
        anchors.top: searchBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 20
        visible: root.errorMessage.length > 0
        text: root.errorMessage
        color: "#e8716d"
        font.pixelSize: 12
        wrapMode: Text.Wrap
    }

    Timer {
        id: errorTimer
        interval: 6000
        onTriggered: root.errorMessage = ""
    }

    ListView {
        id: listView
        anchors.top: errorLabel.visible ? errorLabel.bottom : searchBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.topMargin: 8
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        anchors.bottomMargin: 8
        clip: true
        spacing: 4

        model: videoModel
        delegate: VideoCard {
            onClicked: (video) => root.openVideo(video)
        }

        function loadMoreIfNeeded() {
            if (youtube.busy || !atYEnd)
                return;
            youtube.loadNextPage();
        }

        onAtYEndChanged: loadMoreIfNeeded()

        Connections {
            target: youtube
            function onBusyChanged() {
                listView.loadMoreIfNeeded();
            }
        }

        ScrollBar.vertical: ScrollBar {
            width: 8
            anchors.right: parent.right
            anchors.rightMargin: 2
            policy: ScrollBar.AsNeeded

            contentItem: Rectangle {
                radius: width / 2
                color: parent.active ? "#5a5a64" : "#35353c"
            }

            background: Rectangle {
                color: "transparent"
            }
        }

        footer: Item {
            width: listView.width
            height: 52

            Spinner {
                anchors.centerIn: parent
                running: youtube.busy
                visible: youtube.busy
            }
        }
    }

    Text {
        anchors.centerIn: listView
        visible: videoModel.count === 0 && !youtube.busy && root.errorMessage.length === 0
        text: qsTr("Search for anything\u2026")
        color: "#6a6a72"
        font.pixelSize: 14
    }

    // ------------------------------------------------------------------
    // Frame
    // ------------------------------------------------------------------
    Rectangle {
        id: titleBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: root.titleHeight
        color: "#0b0b0c"
        z: 10

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            onPressed: root.startSystemMove()
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.right: closeButton.left
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: root.title
            color: "#e8e8ea"
            font.pixelSize: 13
            elide: Text.ElideRight
        }

        Rectangle {
            id: closeButton
            anchors.right: parent.right
            anchors.rightMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            width: 32
            height: root.titleHeight - 6
            radius: 4
            color: closeButtonMouse.pressed
                ? "#a33b3b"
                : (closeButtonMouse.containsMouse ? "#7a2a2a" : "transparent")

            Canvas {
                anchors.centerIn: parent
                width: 14
                height: 14
                onPaint: {
                    var ctx = getContext("2d");
                    ctx.reset();
                    ctx.strokeStyle = "#ffffff";
                    ctx.lineWidth = 1.8;
                    ctx.beginPath();
                    ctx.moveTo(3, 3); ctx.lineTo(11, 11);
                    ctx.moveTo(11, 3); ctx.lineTo(3, 11);
                    ctx.stroke();
                }
            }

            MouseArea {
                id: closeButtonMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                // Closing this window closes the players it started as well
                // (see Launcher::stopPlayers()).
                onClicked: root.close()
            }
        }

        // The frame has the window's own colour, so a line under the title bar
        // is what separates it from the content.
        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: "#26262b"
        }
    }

    // A one-pixel outline, so the window's edge is visible against the desktop.
    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.color: "#26262b"
        border.width: 1
        z: 11
    }
}