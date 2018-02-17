import QtQuick 2.5
import QtQuick.Controls 1.4
import QtQuick.Layouts 1.2

import net.raiblocks 1.0

Rectangle {
    id: root

    ColumnLayout {
        anchors.fill: parent

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Send")
        }
        Label {
            id: lblSendMsg
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: parent.width
            wrapMode: Text.Wrap
            text: qsTr("Sending to the wrong address will result in the loss of your funds, please ensure to check you have the corrent address")
        }
        TextField {
            id: tfAmount
            Layout.fillWidth: true
            placeholderText: qsTr("Amount")
            // FIXME: implement amount validation!
        }
        TextField {
            id: tfAddress
            Layout.fillWidth: true
            // original placeholder: rai::zero_key.pub.to_account().c_str()
            placeholderText: qsTr("Address")
            validator: RegExpValidator {
                regExp: /^xrb_[a-z0-9]{60}$/
            }
        }
        Button {
            id: btnSend
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Send")
            onClicked: {
                rai_wallet.send(tfAmount.text, tfAddress.text)
            }
            enabled: !rai_wallet.processingSend && tfAddress.acceptableInput && tfAmount.acceptableInput
        }
        Item {
            id: tabSendStateSelector
            property string msg: ""
            property bool redAmount: false
            property bool redAddress: false
            Timer {
                id: tabSendStateSelectorTimer
            }
            state: "normal"
            states: [
                State {
                    name: "normal"
                    PropertyChanges {
                        target: tabSendStateSelector
                        restoreEntryValues: false
                        msg: ""
                        redAddress: false
                        redAmount: false
                    }
                },
                State {
                    name: "warning"
                    PropertyChanges {
                        target: tabSendStateSelectorTimer
                        interval: 5000
                        running: true
                        onTriggered: tabSendStateSelector.state = "normal"
                    }
                    PropertyChanges {
                        target: tfAmount
                        color: tabSendStateSelector.redAmount ? "red" : color
                    }
                    PropertyChanges {
                        target: tfAddress
                        color: tabSendStateSelector.redAddress ? "red" : color
                    }
                    PropertyChanges {
                        target: lblSendMsg
                        color: "red"
                        text: tabSendStateSelector.msg
                    }
                    PropertyChanges {
                        target: btnSend
                        enabled: false
                    }
                },
                State {
                    name: "success"
                    PropertyChanges {
                        target: tabSendStateSelectorTimer
                        interval: 2000
                        running: true
                        onTriggered: tabSendStateSelector.state = "normal"
                    }
                    PropertyChanges {
                        target: lblSendMsg
                        color: "green"
                        text: qsTr("Funds sent successfully")
                    }
                    PropertyChanges {
                        target: btnSend
                        enabled: false
                    }
                }
            ]
        }

        Connections {
            id: walletConnector
            target: rai_wallet
            onSendFinished: {
                switch (result) {
                case SendResult.AmountTooBig:
                    tabSendStateSelector.msg = qsTr("Amount too big")
                    tabSendStateSelector.redAmount = true
                    tabSendStateSelector.state = "warning"
                    break
                case SendResult.BadAmountNumber:
                    tabSendStateSelector.msg = qsTr("Bad amount number")
                    tabSendStateSelector.redAmount = true
                    tabSendStateSelector.state = "warning"
                    break
                case SendResult.BadDestinationAccount:
                    tabSendStateSelector.msg = qsTr("Bad destination account")
                    tabSendStateSelector.redAddress = true
                    tabSendStateSelector.state = "warning"
                    break
                case SendResult.BlockSendFailed:
                    tabSendStateSelector.msg = qsTr("Block send failed")
                    tabSendStateSelector.redAmount = true
                    tabSendStateSelector.redAddress = true
                    tabSendStateSelector.state = "warning"
                    break
                case SendResult.NotEnoughBalance:
                    tabSendStateSelector.msg = qsTr("Not enough balance")
                    tabSendStateSelector.redAmount = true
                    tabSendStateSelector.state = "warning"
                    break
                case SendResult.Success:
                    tfAmount.clear()
                    tfAddress.clear()
                    tabSendStateSelector.state = "success"
                    break
                case SendResult.WalletIsLocked:
                    tabSendStateSelector.msg = qsTr("Wallet is locked, unlock it to send")
                    tabSendStateSelector.state = "warning"
                    break
                }
            }
        }
    }
}
