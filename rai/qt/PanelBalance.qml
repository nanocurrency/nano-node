import QtQuick 2.9
import QtQuick.Controls 2.3
import QtQuick.Layouts 1.3

import net.raiblocks 1.0

Item {
    id: root

    Pane {
        id: balanceHeader

        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
        }

        ColumnLayout {
            anchors.fill: parent

            Label {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Balance")
            }
            Label {
                Layout.alignment: Qt.AlignHCenter
                text: rai_self_pane.balance
            }
            Label {
                Layout.alignment: Qt.AlignHCenter
                text: "(not pocketed " + rai_self_pane.pending + ")"
                visible: rai_self_pane.pending !== ""
            }
        }
    }
    ScrollView {
        anchors {
            top: balanceHeader.bottom
            bottom: parent.bottom
            left: parent.left
            right: parent.right
            margins: 5
        }

        clip: true

        ListView {
            model: rai_history.model

            spacing: 5

            delegate: Pane {
                width: parent.width
                GridLayout {
                    anchors.fill: parent
                    Label {
                        Layout.minimumWidth: 50
                        text: model.modelData.type
                    }
                    Label {
                        text: model.modelData.amount
                    }
                    Button {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Details")
                        onClicked: {
                            lblHash.visible = !lblHash.visible
                            lblAccount.visible = !lblAccount.visible
                        }
                    }
                    Label {
                        id: lblAccount
                        Layout.row: 1
                        Layout.columnSpan: 3
                        text: "Account: " + model.modelData.account
                        visible: false
                    }
                    Label {
                        id: lblHash
                        Layout.row: 2
                        Layout.columnSpan: 3
                        text: "Hash: " + model.modelData.hash
                        visible: false
                    }
                }
            }
        }
    }
    Label {
        anchors.centerIn: parent
        text: qsTr("This account doesn't have transactions yet")
        visible: rai_history.model.length === 0
    }
}
