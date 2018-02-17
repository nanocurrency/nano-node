import QtQuick 2.5
import QtQuick.Controls 1.4
import QtQuick.Layouts 1.2

import net.raiblocks 1.0

Item {
    TabView {
        id: bar
        width: parent.width
        anchors {
            top: parent.top
            bottom: footer.top
        }

        Tab {
            title: qsTr("Balance")
            PanelBalance {
                id: tabBalance
                anchors.fill: parent
            }
        }
        Tab {
            title: qsTr("Receive")
            PanelReceive {
                id: tabReceive
                anchors.fill: parent
            }
        }
        Tab {
            title: qsTr("Send")
            PanelSend {
                id: tabSend
                anchors.fill: parent
            }
        }
    }

    StatusBar {
        id: footer
        width: parent.width

        anchors {
            bottom: parent.bottom
            left: parent.left
            right: parent.right
        }
    }
}
