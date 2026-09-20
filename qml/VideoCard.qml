import QtQuick
import QtQuick.Layouts

Item {
    id: card
    required property var model

    signal clicked(var videoData)

    width: ListView.view.width
    height: 112

    TapHandler {
        onTapped: card.clicked(model)
    }

    readonly property bool thumbInvalid: model.thumbnail.length === 0 ||
                                         thumbImage.status === Image.Error ||
                                         thumbImage.status === Image.Null

    readonly property bool showLikes: model.hasLikes && model.likesText.length > 0

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        anchors.topMargin: 6
        anchors.bottomMargin: 6
        spacing: 14

        Item {
            Layout.preferredWidth: 178
            Layout.fillHeight: true

            Image {
                id: thumbImage
                anchors.fill: parent
                asynchronous: true
                source: model.thumbnail
                fillMode: Image.PreserveAspectCrop
            }

            Rectangle {
                visible: model.isLive || model.isUpcoming
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.topMargin: 5
                anchors.leftMargin: 5
                height: 17
                radius: 4
                color: model.isLive ? "#e7070c" : "#34343b"
                width: badgeText.implicitWidth + 12

                Text {
                    id: badgeText
                    anchors.centerIn: parent
                    text: model.isLive ? "LIVE" : (model.isUpcoming ? "UPCOMING" : "PREMIERE")
                    color: "#ffffff"
                    font.pixelSize: 10
                    font.bold: true
                }
            }

            Rectangle {
                visible: !model.isLive && model.duration.length > 0
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                anchors.bottomMargin: 5
                anchors.rightMargin: 5
                height: 17
                radius: 4
                color: "#cc040406"

                Text {
                    anchors.centerIn: parent
                    text: model.duration
                    color: "#ffffff"
                    font.pixelSize: 10
                    font.weight: Font.Medium
                }
            }
        }

        Column {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 4

            Text {
                width: parent.width
                text: model.title
                color: "#ececec"
                font.pixelSize: 15
                font.weight: Font.Medium
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                lineHeight: 1.15
            }

            Text {
                width: parent.width
                text: model.uploader
                color: "#9a9aa2"
                font.pixelSize: 12
                elide: Text.ElideRight
            }

            RowLayout {
                width: parent.width
                spacing: 6

                Text {
                    Layout.fillWidth: true
                    text: model.viewsText ?? ""
                    color: "#8a8a92"
                    font.pixelSize: 12
                }

                Text {
                    visible: showLikes
                    text: model.likesText ?? ""
                    color: "#8a8a92"
                    font.pixelSize: 12
                }
            }
        }
    }
}
