import QtQuick 2.9
import QtQuick.Controls 2.3
import QtQuick.Layouts 1.3

import net.raiblocks 1.0

Pane {
    id: root

    GridLayout {
        anchors.fill: parent

        rows: 1
        columns: 2

        Label {
            Layout.alignment: Qt.AlignLeft
            anchors.left: parent.left
            text: rai_status.text
            color: rai_status.color
            ToolTip {
                text: qsTr("Wallet status, block count (blocks downloaded)")
            }
        }
        Label {
            Layout.alignment: Qt.AlignRight
            anchors.right: parent.right
            text: qsTr("Version ") + RAIBLOCKS_VERSION_MAJOR + "." + RAIBLOCKS_VERSION_MINOR
        }
    }
}
