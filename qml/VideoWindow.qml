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

    // Opens at the size of the videos (1282x720) but never larger than the
    // output it lands on, so its title bar stays reachable on a smaller screen.
    readonly property int screenWidth: videoWindow.screen ? videoWindow.screen.width : 1920
    readonly property int screenHeight: videoWindow.screen ? videoWindow.screen.height : 1080

    width: Math.min(1282, Math.round(screenWidth * 0.92))
    height: Math.min(720 + titleHeight, Math.round(screenHeight * 0.92))
    minimumWidth: Math.min(482, width)
    minimumHeight: Math.min(272 + titleHeight, height)
    visible: false
    // The same colour as the search window, so the two look like one program.
    color: "#0b0b0c"
    title: videoTitle.length > 0 ? videoTitle : qsTr("YtGst Player")
    // The frame is ours; asking for platform decorations would only be answered
    // by a compositor that draws them.
    flags: Qt.FramelessWindowHint
    // The panel is the one that is a transient child (it is smaller and has to
    // stay above the picture); this window is the parent, so it must not take
    // the panel as its own transient parent just because it is declared inside
    // it.
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
        // The window cannot be hidden and shown again: hiding it destroys its
        // Wayland surface, and the board's Vulkan driver crashes when a
        // window's surface is recreated. Closing it is therefore handled by
        // the player, which stops the pipeline and ends the process.
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
        color: "#0b0b0c"

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

            // Close. The window itself is never hidden (see onClosing above), so
            // this ends the player: the picture and the panel go away together.
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

        // The frame is drawn in the same colour as the window, so a line under
        // the title bar is what separates it from the picture.
        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: "#26262b"
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

    // A one-pixel outline: the window has the same colour as the search window,
    // so without it the edges would blend into the desktop.
    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.color: "#26262b"
        border.width: 1
        z: 2
    }
}
