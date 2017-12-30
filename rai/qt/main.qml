import QtQuick 2.9
import QtQuick.Controls 2.3
import QtQuick.Layouts 1.3

import net.raiblocks 1.0

ApplicationWindow {
    id: root

    visible: true
    minimumWidth: 480
    minimumHeight: 480
    title: qsTr("RaiBlocks Wallet")

    palette.window: "#40B299"

    RowLayout {
        spacing: 0
        anchors.fill: parent

        Pane {
            Layout.fillWidth: false
            Layout.fillHeight: true

            background: Rectangle {
                color: "white"
            }

            ColumnLayout {
                anchors.fill: parent

                Label {
                    Layout.fillWidth: true
                    text: "RaiBlocks"
                    Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
                }

                Label {
                    Layout.fillWidth: true
                    text: "Total Balance: " + rai_accounts.totalBalance + " XRB"
                }

                Label {
                    Layout.fillWidth: true
                    text: "Total Pending: " + rai_accounts.totalPending + " XRB"
                    visible: rai_accounts.totalPending !== ""
                }

                Button {
                    Layout.fillWidth: true
                    id: btnCreateAccount
                    property string lastMsg: ""
                    property int lastCount: 0
                    text: qsTr("Create account")
                    onClicked: {
                        lastCount = rai_accounts.model.length
                        rai_accounts.createAccount()
                    }
                    state: "normal"
                    states: [
                        State {
                            name: "normal"
                        },
                        State {
                            name: "processing"
                            PropertyChanges {
                                target: timerCreateAccount
                                interval: 500
                                running: true
                                onTriggered: {
                                    rai_accounts.refresh()
                                    if (rai_accounts.model.length > btnCreateAccount.lastCount) {
                                        btnCreateAccount.state = "success"
                                    }
                                }
                            }
                            PropertyChanges {
                                target: btnCreateAccount
                                text: qsTr("Processing...")
                                enabled: false
                            }
                        },
                        State {
                            name: "success"
                            PropertyChanges {
                                target: timerCreateAccount
                                interval: 2000
                                running: true
                                onTriggered: btnCreateAccount.state = "normal"
                            }
                            PropertyChanges {
                                target: btnCreateAccount
                                text: qsTr("New account was created")
                                enabled: false
                            }
                        },
                        State {
                            name: "failure"
                            PropertyChanges {
                                target: timerCreateAccount
                                interval: 2000
                                running: true
                                onTriggered: btnCreateAccount.state = "normal"
                            }
                            PropertyChanges {
                                target: btnCreateAccount
                                text: lastMsg
                                enabled: false
                            }
                        }
                    ]
                    Timer {
                        id: timerCreateAccount
                    }
                    Connections {
                        target: rai_accounts
                        onCreateAccountSuccess: {
                            // FIXME: wait until new account appears (workaround)
                            btnCreateAccount.state = "processing"
                        }
                        onCreateAccountFailure: {
                            btnCreateAccount.lastMsg = msg
                            btnCreateAccount.state = "failure"
                        }
                    }
                }

                Button {
                    Layout.fillWidth: true
                    text: qsTr("Refresh")
                    onClicked: rai_accounts.refresh()
                    visible: false
                }

                ScrollView {
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    ListView {
                        model: rai_accounts.model
                        delegate: ItemDelegate {
                            text: "Account " + index
                            onClicked: rai_accounts.useAccount(model.modelData.account)
                        }
                    }
                }

                Button {
                    Layout.fillWidth: true
                    text: "Settings"
                }
            }
        }

        Item {
            Layout.fillHeight: true
            Layout.fillWidth: true

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

                Item {
                    id: tabBalance

                    anchors.fill: parent

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

                Pane {
                    id: tabReceive

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

                Pane {
                    id: tabSend

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
            }

            Pane {
                id: footer

                anchors {
                    bottom: parent.bottom
                    left: parent.left
                    right: parent.right
                }

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
        }
    }
}
