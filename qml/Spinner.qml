import QtQuick

Item {
    id: root

    implicitWidth: 22
    implicitHeight: 22

    property bool running: true
    property color color: "#8b7cf6"

    Canvas {
        id: canvas
        anchors.fill: parent

        property real angle: 0

        onPaint: {
            const ctx = getContext("2d");
            const size = Math.min(width, height);
            const radius = size / 2 - 2;
            ctx.clearRect(0, 0, width, height);
            ctx.strokeStyle = root.color;
            ctx.lineWidth = 2.5;
            ctx.lineCap = "round";
            ctx.beginPath();
            ctx.arc(width / 2, height / 2, radius, angle * Math.PI / 180,
                    Math.max(angle * Math.PI / 180, (angle + 300) * Math.PI / 180));
            ctx.stroke();
        }

        NumberAnimation on angle {
            from: 0
            to: 360
            duration: 900
            loops: Animation.Infinite
            running: root.running
        }

        onAngleChanged: {
            if (root.running)
                requestPaint();
        }
    }
}