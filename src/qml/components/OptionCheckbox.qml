import QtQuick 2.5
import "../styles.js" as Styles

Item {
    property string text
    property bool checked: true

    signal clicked()

    id: root
    height: dp(60)

    Text {
        color: Styles.textColor
        font.pixelSize: Styles.titleFont.bigger
        text: root.text
        anchors {
            left: parent.left
            top: parent.top
            bottom: parent.bottom
            leftMargin: dp(10)
        }
        verticalAlignment: Text.AlignVCenter
        horizontalAlignment: Text.AlignHCenter
        //renderType: Text.NativeRendering
    }

    Rectangle {
        anchors {
            right: parent.right
            top: parent.top
            bottom: parent.bottom
            margins: dp(10)
        }

        width: dp(36)
        radius: dp(2)
        color: Styles.sidebarBg

        MouseArea{
            anchors.fill: parent

            onClicked: {
                root.checked = !root.checked
                root.clicked()
            }

            Icon {
                icon: "check"
                anchors.fill: parent
                visible: root.checked
            }
        }
    }
}
