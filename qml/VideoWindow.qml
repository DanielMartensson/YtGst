import QtQuick

// The picture's own window.
//
// Weston 13's desktop-shell draws no window frames at all - it implements no
// xdg-decoration and it has no frame code, so every window on this board is a
// bare rectangle. The frame is therefore drawn here: a title bar with the title,
// a fullscreen and a close button, a drag area, and resize areas along the
// edges. The picture itself is drawn by waylandsink into a Wayland sub-surface
// of this window, filling everything inside the frame (see
// Player::embedVideoIntoWindow() and Player::setVideoRect()), which is what
// makes the picture's window movable and resizable like any other window.
//
// The sub-surface sits *on top of* the window's own content, so anything drawn
// here would be hidden below the picture. Only the frame - the title bar and the
// 4 px border - stays visible, and that is where the controls live. Pointer
// events do reach the whole window: the sink hands the picture area an empty
// input region when it is embedded, so clicks pass straight through to this
// window.
Window {
    id: videoWindow

    // The panel's Player object (see PlayerWindow.qml). It is not called
    // "player": a property of that name would shadow the panel's Player id in
    // the binding that sets it.
    required property var playback

    property string videoTitle: ""
    property bool fullscreen: false

    readonly property int titleHeight: 34
    readonly property int frameWidth: 4

    signal closeRequested()

    width: 1282
    height: 720 + titleHeight
    minimumWidth: 482
    minimumHeight: 272 + titleHeight
    visible: false
    color: "#26262b"
    title: videoTitle.length > 0 ? videoTitle : qsTr("YtGst Player")
    // The frame is ours; asking for platform decorations would only be answered
    // by a compositor that draws them.
    flags: Qt.FramelessWindowHint
    // Two independent windows on purpose: the panel has to be able to sit on
    // top of the picture. A window declared inside another one is a transient
    // child of it, and a compositor may then keep the child above (or clamp it
    // inside) its parent - exactly what must not happen here.
    transientParent: null

    // ------------------------------------------------------------------
    // Picture area
    // ------------------------------------------------------------------
    // QML owns the frame, so QML is what knows where the picture goes: inside
    // the border, below the title bar, and in fullscreen the whole window.
    function syncVideoRect() {
        var top = fullscreen ? 0 : titleHeight;
        var inset = fullscreen ? 0 : frameWidth;
        playback.setVideoRect(inset, top, Math.max(0, width - 2 * inset),
                            Math.max(0, height - top - inset));
    }

    // Fullscreen is a state of this window. Qt only applies it while the window
    // is actually shown, so a request that arrives while it is hidden is applied
    // by the next visible change (see the panel's start-of-playback).
    function syncVisibility() {
        if (!visible)
            return;
        if (fullscreen && visibility !== Window.FullScreen)
            showFullScreen();
        else if (!fullscreen && visibility !== Window.Windowed)
            showNormal();
    }

    onWidthChanged: syncVideoRect()
    onHeightChanged: syncVideoRect()
    onVisibleChanged: syncVisibility()
    onFullscreenChanged: {
        syncVisibility();
        syncVideoRect();
    }
    Component.onCompleted: {
        // The player needs the window itself: it draws the picture into a
        // sub-surface of this window's Wayland surface.
        playback.setVideoWindow(videoWindow);
        syncVideoRect();
    }

    onClosing: (close) => {
        // Closing the picture's window is the panel's decision: it stops the
        // playback (and with it the pipeline that draws into this window) and
        // then hides it. Keeping the window itself alive keeps its Wayland
        // surface - the surface the sink draws into - valid.
        close.accepted = false;
        videoWindow.closeRequested();
    }

    // ------------------------------------------------------------------
    // Title bar: drag to move, double click for fullscreen
    // ------------------------------------------------------------------
    Rectangle {
        id: titleBar
        visible: !videoWindow.fullscreen
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: videoWindow.titleHeight
        color: "#1b1b1e"

        MouseArea {
            id: titleDragArea
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            onPressed: videoWindow.startSystemMove()
            onDoubleClicked: videoWindow.playback.toggleFullscreen()
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 10
            anchors.right: buttonsRow.left
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: videoWindow.title
            elide: Text.ElideRight
            color: "#e8e8ea"
            font.pixelSize: 13
        }

        Row {
            id: buttonsRow
            anchors.right: parent.right
            anchors.rightMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2

            // Fullscreen (the same icon as the panel's button)
            Rectangle {
                width: 32
                height: videoWindow.titleHeight - 6
                radius: 4
                color: fullscreenButtonMouse.pressed
                    ? "#3a3a40"
                    : (fullscreenButtonMouse.containsMouse ? "#2c2c32" : "transparent")

                Canvas {
                    id: fullscreenIcon
                    anchors.centerIn: parent
                    width: 16
                    height: 16
                    onPaint: {
                        var ctx = getContext("2d");
                        ctx.reset();
                        ctx.strokeStyle = "#ffffff";
                        ctx.lineWidth = 1.6;
                        var m = 1;
                        var s = 4;
                        var w = width;
                        var h = height;
                        if (videoWindow.fullscreen) {
                            // Corners point inwards: "leave fullscreen".
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
                        target: videoWindow
                        function onFullscreenChanged() { fullscreenIcon.requestPaint(); }
                    }
                }

                MouseArea {
                    id: fullscreenButtonMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: videoWindow.playback.toggleFullscreen()
                }
            }

            // Close
            Rectangle {
                width: 32
                height: videoWindow.titleHeight - 6
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
                    onClicked: videoWindow.closeRequested()
                }
            }
        }
    }

    // ------------------------------------------------------------------
    // Resize areas: lives along the frame, so they are where a window border
    // usually is. The picture area passes events through to this window, so
    // the areas can reach a little way inside as well.
    // ------------------------------------------------------------------
    MouseArea {
        anchors.right: parent.right
        anchors.top: titleBar.bottom
        anchors.bottom: parent.bottom
        width: 6
        cursorShape: Qt.SizeHorCursor
        onPressed: videoWindow.startSystemResize(Qt.RightEdge)
    }

    MouseArea {
        anchors.left: parent.left
        anchors.top: titleBar.bottom
        anchors.bottom: parent.bottom
        width: 6
        cursorShape: Qt.SizeHorCursor
        onPressed: videoWindow.startSystemResize(Qt.LeftEdge)
    }

    MouseArea {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 6
        cursorShape: Qt.SizeVerCursor
        onPressed: videoWindow.startSystemResize(Qt.BottomEdge)
    }

    MouseArea {
        // The bottom-right corner: both edges at once, the classic grip.
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: 24
        height: 24
        cursorShape: Qt.SizeFDiagCursor
        onPressed: videoWindow.startSystemResize(Qt.RightEdge | Qt.BottomEdge)

        Canvas {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            width: 12
            height: 12
            onPaint: {
                var ctx = getContext("2d");
                ctx.reset();
                ctx.strokeStyle = "#88ffffff";
                ctx.lineWidth = 1;
                for (var i = 0; i < 2; i++) {
                    var o = 3 + i * 4;
                    ctx.beginPath();
                    ctx.moveTo(11, 11 - o);
                    ctx.lineTo(11 - o, 11);
                    ctx.stroke();
                }
            }
        }
    }
}
