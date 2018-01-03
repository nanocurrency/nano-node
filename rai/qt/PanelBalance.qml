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
                font.pixelSize: 28
                color: "#E1F784"
            }
            Label {
                Layout.alignment: Qt.AlignHCenter
                text: "(pending " + rai_self_pane.pending + ")"
                color: "#E1F784"
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
            id: listView
            model: rai_history.model

            spacing: 5

            delegate: Pane {
                width: listView.width
                background: Rectangle {
                    color: Qt.rgba(255, 255, 255, 0.25)
                }

                GridLayout {
                    anchors.fill: parent
                    columns: 3
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
                            details.visible = !details.visible
                        }
                    }
                    GridLayout {
                        id: details
                        Layout.columnSpan: 3
                        Layout.row: 1
                        Layout.maximumWidth: parent.width
                        visible: false
                        columns: 2
                        Label {
                            id: lblAccount
                            text: qsTr("Account: ")
                        }
                        TextField {
                            Layout.fillWidth: true
                            readOnly: true
                            selectByMouse: true
                            text: model.modelData.account
                            wrapMode: Text.WrapAnywhere
                        }
                        Label {
                            id: lblHash
                            text: qsTr("Hash: ")
                        }
                        TextField {
                            Layout.fillWidth: true
                            readOnly: true
                            selectByMouse: true
                            text: model.modelData.hash
                            wrapMode: Text.WrapAnywhere
                        }
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
