import QtQuick 2.5
import QtQuick.Controls 1.4

Item {
    id: root

    property alias text: popupLabel.text
    property alias color: popupLabel.color
    property alias interval: popupTimer.interval

    signal triggered()

    function open() {
        root.visible = true
    }
    function close() {
        root.visible = false
    }

    Rectangle {
        id: popupImpl
// FIXME: popup is not placed well, because Qt 5.9 doesn't provide Overlay type
//        parent: Overlay.overlay
        anchors.centerIn: parent
        width: parent.width
        height: parent.height
        visible: root.visible

        Label {
            id: popupLabel
            anchors.fill: parent
        }
    }

    Timer {
        id: popupTimer
        repeat: true
        running: root.visible
        onTriggered: root.triggered()
    }
}
