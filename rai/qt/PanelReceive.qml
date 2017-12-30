import QtQuick 2.9
import QtQuick.Controls 2.3
import QtQuick.Layouts 1.3

import net.raiblocks 1.0

Pane {
    id: root

    ColumnLayout {
        anchors.fill: parent

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Receive")
        }
        RowLayout {
            Label {
                Layout.fillWidth: true
                text: rai_self_pane.account
                wrapMode: Text.WrapAnywhere
            }
            Button {
                text: qsTr("Copy")
                onClicked: clipboard.sendText(rai_self_pane.account)

                ClipboardProxy {
                    id: clipboard
                }
            }
        }
        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.minimumHeight: 128
            Layout.minimumWidth: 128
            color: "black"
        }
    }
}
