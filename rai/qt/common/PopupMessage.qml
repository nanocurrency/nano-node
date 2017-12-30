import QtQuick 2.9
import QtQuick.Controls 2.3

Item {
    id: root

    property alias text: popupLabel.text
    property alias color: popupLabel.color
    property alias interval: popupTimer.interval

    signal triggered()

    function open() {
        popupImpl.open()

    }
    function close() {
        popupImpl.close()
    }

    Popup {
        id: popupImpl
        parent: Overlay.overlay
        width: parent.width
        height: parent.height
        modal: true
        visible: root.visible

        background: Item {}

        Pane {
            anchors.centerIn: parent
            Label {
                id: popupLabel
                anchors.fill: parent
            }
        }
    }
    Timer {
        id: popupTimer
        repeat: true
        running: root.visible
        onTriggered: root.triggered()
    }
}
