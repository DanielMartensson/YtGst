import QtQuick
import QtQuick.Controls

Rectangle {
    id: root

    property string placeholder: qsTr("Search...")
    property color accent: "#8b7cf6"

    signal submitted(string query)

    implicitHeight: 44
    radius: height / 2
    color: "#16161a"
    border.width: 1
    border.color: field.activeFocus ? accent : (field.hovered ? "#3a3a42" : "#26262b")

    TextField {
        id: field
        anchors.fill: parent
        anchors.leftMargin: 20
        anchors.rightMargin: 48
        verticalAlignment: Text.AlignVCenter
        color: "#e8e8e8"
        selectionColor: root.accent
        selectedTextColor: "#0b0b0c"
        placeholderText: root.placeholder
        placeholderTextColor: "#7a7a80"
        font.pixelSize: 15
        selectByMouse: true
        background: Item {}

        onAccepted: root.submitted(text)
    }

    Item {
        width: 18
        height: 18
        anchors.right: parent.right
        anchors.rightMargin: 16
        anchors.verticalCenter: parent.verticalCenter

        Rectangle {
            width: 12
            height: 12
            radius: width / 2
            color: "transparent"
            border.width: 1.5
            border.color: "#8a8a90"
        }

        Rectangle {
            width: 7
            height: 1.5
            x: 8
            y: 11
            rotation: 45
            color: "#8a8a90"
        }
    }
}