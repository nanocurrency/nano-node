import QtQuick 2.9
import QtQuick.Controls 2.3
import QtQuick.Layouts 1.3

import net.raiblocks 1.0

Item {
    TabBar {
        id: bar
        width: parent.width

        TabButton {
            text: qsTr("Balance")
        }
        TabButton {
            text: qsTr("Receive")
        }
        TabButton {
            text: qsTr("Send")
        }
    }

    StackLayout {
        currentIndex: bar.currentIndex
        width: parent.width
        anchors {
            top: bar.bottom
            bottom: footer.top
        }

        PanelBalance {
            id: tabBalance
            anchors.fill: parent
        }

        PanelReceive {
            id: tabReceive
            anchors.fill: parent
        }

        PanelSend {
            id: tabSend
            anchors.fill: parent
        }
    }

    StatusBar {
        id: footer

        anchors {
            bottom: parent.bottom
            left: parent.left
            right: parent.right
        }
    }
}
