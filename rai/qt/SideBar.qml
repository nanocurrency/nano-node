import QtQuick 2.9
import QtQuick.Controls 2.3
import QtQuick.Layouts 1.3

import net.raiblocks 1.0

import "common" as Common

Pane {
    id: root

    property alias btnSettingsEnabled: btnSettings.enabled
    signal settingsClicked()

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


        RowLayout {
            Layout.fillWidth: true
            Label {
                text: rai_settings.locked ? qsTr("Locked") : qsTr("Unlocked")
            }
            Button {
                text: rai_settings.locked ? qsTr("Unlock") : qsTr("Lock")
                onClicked: {
                    if (rai_settings.locked) {
                        popupPassword.open()
                    } else {
                        rai_settings.lock()
                    }
                }

                Popup {
                    id: popupPassword
                    parent: Overlay.overlay
                    width: parent.width
                    height: parent.height
                    modal: true

                    background: Item {}

                    onClosed: tfPassword.clear()

                    Pane {
                        anchors.centerIn: parent
                        ColumnLayout {
                            Label {
                                id: lblPassword
                                text: qsTr("Type your password to unlock")
                                Timer {
                                    id: lblPasswordTimer

                                }
                                state: "normal"
                                states: [
                                    State {
                                        name: "normal"
                                    },
                                    State {
                                        name: "warning"
                                        PropertyChanges {
                                            target: lblPassword
                                            text: qsTr("Invalid password. Try again.")
                                            color: "red"
                                        }
                                        PropertyChanges {
                                            target: lblPasswordTimer
                                            interval: 5000
                                            running: true
                                            onTriggered: {
                                                lblPassword.state = "normal"
                                            }
                                        }
                                    }
                                ]
                            }
                            TextField {
                                id: tfPassword
                                placeholderText: qsTr("wallet password")
                                echoMode: TextInput.Password
                            }
                            RowLayout {
                                Button {
                                    text: qsTr("Cancel")
                                    onClicked: popupPassword.close()
                                }
                                Button {
                                    text: qsTr("Accept")
                                    onClicked: rai_settings.unlock(tfPassword.text)
                                }
                            }
                        }
                    }
                }

                Connections {
                    target: rai_settings
                    onUnlockSuccess: popupPassword.close()
                    onUnlockFailure: {
                        lblPassword.state = "warning"
                        tfPassword.clear()
                    }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            text: "Total Balance: " + rai_accounts.totalBalance
        }

        Label {
            Layout.fillWidth: true
            text: "Total Pending: " + rai_accounts.totalPending
            visible: rai_accounts.totalPending !== ""
        }

        Button {
            Layout.fillWidth: true
            id: btnCreateAccount
            property int lastCount: 0
            text: qsTr("Create account")
            onClicked: {
                lastCount = rai_accounts.model.length
                rai_accounts.createAccount()
            }
            Common.PopupMessage {
                id: popup
                property string errorMsg: "unknown error"
                state: "hidden"
                states: [
                    State {
                        name: "hidden"
                        PropertyChanges {
                            target: popup
                            visible: false
                        }
                    },
                    State {
                        name: "processing"
                        PropertyChanges {
                            target: popup
                            text: qsTr("Processing...")
                            interval: 500
                            visible: true
                            onTriggered: {
                                rai_accounts.refresh()
                                if (rai_accounts.model.length > btnCreateAccount.lastCount) {
                                    popup.state = "success"
                                }
                            }
                        }
                    },
                    State {
                        name: "success"
                        PropertyChanges {
                            target: popup
                            text: qsTr("New account was created!")
                            color: "green"
                            interval: 2000
                            visible: true
                            onTriggered: popup.state = "hidden"
                        }
                    },
                    State {
                        name: "failure"
                        PropertyChanges {
                            target: popup
                            text: errorMsg
                            color: "red"
                            interval: 2000
                            visible: true
                            onTriggered: popup.state = "hidden"
                        }
                    }
                ]
            }
            Connections {
                target: rai_accounts
                onCreateAccountSuccess: {
                    // FIXME: wait until new account appears (workaround)
                    popup.state = "processing"
                }
                onCreateAccountFailure: {
                    popup.errorMsg = msg
                    popup.state = "failure"
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
            id: btnSettings
            Layout.fillWidth: true
            text: "Settings"
            onClicked: settingsClicked()
        }
    }
}
