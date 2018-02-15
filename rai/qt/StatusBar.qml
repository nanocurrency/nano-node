import QtQuick 2.5
import QtQuick.Controls 1.4
import QtQuick.Layouts 1.2

import net.raiblocks 1.0

Rectangle {
    id: root

    color: "pink"
    height: row.height
    width: row.width

    Row {
        id: row

        Label {
            Layout.alignment: Qt.AlignLeft
//            anchors.left: parent.left
            text: rai_status.text
            color: rai_status.color
//            ToolTip {
//                text: qsTr("Wallet status, block count (blocks downloaded)")
//            }
        }
        Label {
            Layout.alignment: Qt.AlignRight
//            anchors.right: parent.right
            text: qsTr("Version ") + RAIBLOCKS_VERSION_MAJOR + "." + RAIBLOCKS_VERSION_MINOR
        }
    }
}
