import QtQuick
import QtQuick.Controls
import Ytgst

Window {
    id: root
    width: 900
    height: 600
    visible: true
    title: qsTr("Ytgst")
    color: "#0b0b0c"

    property string errorMessage: ""

    VideoListModel {
        id: videoModel
    }

    function openVideo(video) {
        launcher.play(video.id);
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
        anchors.topMargin: 20
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
}