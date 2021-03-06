import QtQuick 2.5
import QtQuick.Window 2.0
import QtQuick.Controls 1.4
import "styles.js" as Styles
import "style"

ApplicationWindow {
    id: root
    visible: true

    title: "Orion"
    //flags: Qt.FramelessWindowHint | Qt.Window
    visibility: g_fullscreen ? "FullScreen" : "Windowed"

    property variant g_rootWindow: root
    property variant g_tooltip
    property variant g_toolBox: sidebar
    property bool g_contextMenuVisible: false
    property bool g_fullscreen: false

    function dp(number){
        return Dpi.scale(number)
    }

    function fitToAspectRatio() {
        height = view.width * 0.5625
    }

    onClosing: {
        if (!g_cman.isCloseToTray()) {
            Qt.quit()
        }
    }

    Connections {
        target: g_tray
        onShowTriggered: {
            if (root.visible)
                root.hide()
            else
                root.show()
        }
    }

    Connections {
        target: g_guard
        onAnotherProcessTriggered: {
            root.show()
            root.raise()
        }
    }

    Item {
        anchors.fill: parent

        SideBar {
            id: sidebar
            hidden: g_fullscreen
            anchors {
                left: parent.left
                top: parent.top
                bottom: parent.bottom
            }

            Component.onCompleted: toggle()
        }

        Item {
            anchors {
                left: sidebar.right
                top: parent.top
                right: parent.right
                bottom: parent.bottom
            }

            Views {
                id: view
                selection: sidebar.selectedView
                anchors.fill: parent
                onRequestSelectionChange: {
                    g_toolBox.setView(index)
                }
            }
        }
    }

    Component.onCompleted: {
        height=Screen.height * 0.7
        width=height * 1.2

        setX(Screen.width / 2 - width / 2);
        setY(Screen.height / 2 - height / 2);

        var component = Qt.createComponent("components/Tooltip.qml")
        g_tooltip = component.createObject(root)

        g_cman.checkFavourites()
        pollTimer.start()

        //Initial view
        g_toolBox.setView(2)

        console.log("Pixel density", Screen.pixelDensity)
        console.log("Pixel ratio", Screen.devicePixelRatio)
        console.log("Logical pixel density", Screen.logicalPixelDensity)
        console.log("Orientation", Screen.orientation)
    }

    Timer {
        id: pollTimer
        interval: 30000
        running: false
        repeat: true
        onTriggered: {
            g_cman.checkFavourites()
        }
    }
}

