import QtQuick 2.5
import QtQuick.Controls 1.4
import QtQuick.Dialogs 1.2
import QtQuick.Layouts 1.2

import net.raiblocks 1.0

import "common" as Common

Rectangle {
  signal goBack()
  ScrollView {
//    ScrollBar.vertical.policy: ScrollBar.AlwaysOn
    anchors {
        top: parent.top
        left: parent.left
        right: parent.right
        bottom: btnGoBack.top
    }
    clip: true

    ColumnLayout {
        anchors.fill: parent

        GroupBox {
            Layout.alignment: Qt.AlignHCenter
            ColumnLayout {
                TextField {
                    id: tfNewPassword1
                    placeholderText: qsTr("New password")
                    echoMode: TextInput.Password
                }

                TextField {
                    id: tfNewPassword2
                    placeholderText: qsTr("Re-type password")
                    echoMode: TextInput.Password
                }

                Button {
                    text: qsTr("Change password")
                    onClicked: rai_settings.changePassword(tfNewPassword1.text)
                    enabled: tfNewPassword1.text.length > 0 && tfNewPassword1.text === tfNewPassword2.text
                }

                Connections {
                    target: rai_settings
                    onChangePasswordSuccess: {
                        tfNewPassword1.clear()
                        tfNewPassword2.clear()
                        popupChangePassword.state = "success"
                    }
                    onChangePasswordFailure: {
                        tfNewPassword1.clear()
                        tfNewPassword2.clear()
                        popupChangePassword.errorMsg = errorMsg
                        popupChangePassword.state = "failure"
                    }
                }

                Common.PopupMessage {
                    id: popupChangePassword
                    property string errorMsg: "unknown error"
                    state: "hidden"
                    states: [
                        State {
                            name: "hidden"
                            PropertyChanges {
                                target: popupChangePassword
                                visible: false
                            }
                        },
                        State {
                            name: "success"
                            PropertyChanges {
                                target: popupChangePassword
                                text: qsTr("Password was changed")
                                color: "green"
                                interval: 2000
                                visible: true
                                onTriggered: popupChangePassword.state = "hidden"
                            }
                        },
                        State {
                            name: "failure"
                            PropertyChanges {
                                target: popupChangePassword
                                text: errorMsg
                                color: "red"
                                interval: 2000
                                visible: true
                                onTriggered: popupChangePassword.state = "hidden"
                            }
                        }
                    ]
                }

            }
        }

        GroupBox {
            Layout.alignment: Qt.AlignHCenter
            Button {
                id: btnBackupSeed
                text: qsTr("Copy wallet seed")
                onClicked: rai_accounts.backupSeed()
                Connections {
                    target: rai_accounts
                    onBackupSeedSuccess: {
                        popupBackupSeed.state = "success"
                    }
                    onBackupSeedFailure: {
                        popupBackupSeed.errorMsg = msg
                        popupBackupSeed.state = "failure"
                    }
                }
                Common.PopupMessage {
                    id: popupBackupSeed
                    property string errorMsg: "unknown error"
                    state: "hidden"
                    states: [
                        State {
                            name: "hidden"
                            PropertyChanges {
                                target: popupBackupSeed
                                visible: false
                            }
                        },
                        State {
                            name: "success"
                            PropertyChanges {
                                target: popupBackupSeed
                                text: qsTr("Seed was copied to clipboard")
                                color: "green"
                                interval: 2000
                                visible: true
                                onTriggered: popupBackupSeed.state = "hidden"
                            }
                        },
                        State {
                            name: "failure"
                            PropertyChanges {
                                target: popupBackupSeed
                                text: errorMsg
                                color: "red"
                                interval: 2000
                                visible: true
                                onTriggered: popupBackupSeed.state = "hidden"
                            }
                        }
                    ]
                }
            }
        }

        GroupBox {
            Layout.alignment: Qt.AlignHCenter

            ColumnLayout {
                TextField {
                    id: tfImportSeed
                    placeholderText: qsTr("Wallet seed")
                    validator: RegExpValidator {
                        regExp: /^[a-fA-F0-9]{64}$/
                    }
                }

                Label {
                    text: qsTr("Modifying seed clears existing keys.\nType 'clear keys' below to confirm:")
                }

                TextField {
                    id: tfImportConfirm
                    placeholderText: qsTr("clear keys")
                }

                Button {
                    text: qsTr("Restore wallet from seed")
                    enabled: tfImportSeed.acceptableInput && tfImportConfirm.text == tfImportConfirm.placeholderText
                    onClicked: {
                        rai_import.importSeed(tfImportSeed.text)
                        tfImportSeed.clear()
                        tfImportConfirm.clear()
                    }

                    Connections {
                        target: rai_import
                        onImportSeedSuccess: {
                            popupImportSeed.state = "success"
                        }
                        onImportSeedFailure: {
                            popupImportSeed.errorMsg = errorMsg
                            popupImportSeed.state = "failure"
                        }
                    }

                    Common.PopupMessage {
                        id: popupImportSeed
                        property string errorMsg: "unknown error"
                        state: "hidden"
                        states: [
                            State {
                                name: "hidden"
                                PropertyChanges {
                                    target: popupImportSeed
                                    visible: false
                                }
                            },
                            State {
                                name: "success"
                                PropertyChanges {
                                    target: popupImportSeed
                                    text: qsTr("Wallet was imported")
                                    color: "green"
                                    interval: 2000
                                    visible: true
                                    onTriggered: popupImportSeed.state = "hidden"
                                }
                            },
                            State {
                                name: "failure"
                                PropertyChanges {
                                    target: popupImportSeed
                                    text: errorMsg
                                    color: "red"
                                    interval: 2000
                                    visible: true
                                    onTriggered: popupImportSeed.state = "hidden"
                                }
                            }
                        ]
                    }
                }
            }
        }

        GroupBox {
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: parent.width

            ColumnLayout {
                anchors {
                    left: parent.left
                    right: parent.right
                }

                Label {
                    Layout.maximumWidth: parent.width
                    text: qsTr("Warning: accounts recovered from a file are not associated to your wallet seed, which means they cannot be recovered later with a seed backup. Move those funds to new accounts after recovering them.")
                    color: "red"
                    wrapMode: Text.WordWrap
                }

                TextField {
                    id: tfImportFromFilePath
                    placeholderText: qsTr("Wallet filepath")
                }

                Button {
                    text: qsTr("Load wallet backup file")
                    onClicked: fileDialog.open()
                    FileDialog {
                        id: fileDialog
                        title: qsTr("Please choose a wallet backup file")
                        onAccepted: {
                            tfImportFromFilePath.text = rai_import.convertUrlToNativeFilePath(fileUrl)
                            close()
                        }
                        onRejected: {
                            close()
                        }
                    }
                }

                TextField {
                    id: tfImportFromFilePassword
                    placeholderText: qsTr("Password")
                    echoMode: TextInput.Password
                }

                Button {
                    text: qsTr("Restore wallet from file")
                    enabled: tfImportFromFilePath.text.length > 0 && tfImportFromFilePassword.text.length > 0
                    onClicked: {
                        rai_import.importFromFile(tfImportFromFilePath.text, tfImportFromFilePassword.text)
                        tfImportFromFilePath.clear()
                        tfImportFromFilePassword.clear()
                    }

                    Connections {
                        target: rai_import
                        onImportFromFileSuccess: {
                            popupImportFromFile.state = "success"
                        }
                        onImportFromFileFailure: {
                            popupImportFromFile.errorMsg = errorMsg
                            popupImportFromFile.state = "failure"
                        }
                    }

                    Common.PopupMessage {
                        id: popupImportFromFile
                        property string errorMsg: "unknown error"
                        state: "hidden"
                        states: [
                            State {
                                name: "hidden"
                                PropertyChanges {
                                    target: popupImportFromFile
                                    visible: false
                                }
                            },
                            State {
                                name: "success"
                                PropertyChanges {
                                    target: popupImportFromFile
                                    text: qsTr("Wallet was imported")
                                    color: "green"
                                    interval: 2000
                                    visible: true
                                    onTriggered: popupImportFromFile.state = "hidden"
                                }
                            },
                            State {
                                name: "failure"
                                PropertyChanges {
                                    target: popupImportFromFile
                                    text: errorMsg
                                    color: "red"
                                    interval: 2000
                                    visible: true
                                    onTriggered: popupImportFromFile.state = "hidden"
                                }
                            }
                        ]
                    }
                }
            }
        }

        GroupBox {
            id: gbAdhocKey
            Layout.alignment: Qt.AlignHCenter

            ColumnLayout {
                anchors.fill: parent

                TextField {
                    id: tfAdhocKey
                    placeholderText: qsTr("Adhoc Key")
                    validator: RegExpValidator {
                        regExp: /^[a-fA-F0-9]{64}$/
                    }
                }
                Button {
                    id: btnAdhocKey
                    text: qsTr("Import adhoc key")
                    onClicked: rai_accounts.insertAdhocKey(tfAdhocKey.text)
                    enabled: tfAdhocKey.acceptableInput
                }
            }
            Connections {
                target: rai_accounts
                onInsertAdhocKeyFinished: {
                    if (success) {
                        tfAdhocKey.clear()
                    }
                    gbAdhocKey.state = success ? "success" : "warning"
                }
            }
            Timer {
                id: timerAdhocKey
                interval: 2000
                onTriggered: gbAdhocKey.state = "normal"
            }
            state: "normal"
            states: [
                State {
                    name: "normal"
                },
                State {
                    name: "warning"
                    PropertyChanges {
                        target: tfAdhocKey
                        text: qsTr("Invalid key")
                        color: "red"
                    }
                    PropertyChanges {
                        target: btnAdhocKey
                        enabled: false
                    }
                    PropertyChanges {
                        target: timerAdhocKey
                        running: true
                    }
                },
                State {
                    name: "success"
                    PropertyChanges {
                        target: tfAdhocKey
                        text: qsTr("Key was imported")
                        color: "green"
                    }
                    PropertyChanges {
                        target: btnAdhocKey
                        enabled: false
                    }
                    PropertyChanges {
                        target: timerAdhocKey
                        running: true
                    }
                }
            ]
        }

        GroupBox {
            Layout.alignment: Qt.AlignHCenter
            ColumnLayout {
                Label {
                    text: qsTr("Preferred unit:")
                }
                RadioButton {
                    checked: rai_wallet.renderingRatio === RenderingRatio.XRB
                    text: "XRB = 10^30 raw"
                    onClicked: rai_wallet.renderingRatio = RenderingRatio.XRB
                }
                RadioButton {
                    checked: rai_wallet.renderingRatio === RenderingRatio.MilliXRB
                    text: "mXRB = 10^27 raw"
                    onClicked: rai_wallet.renderingRatio = RenderingRatio.MilliXRB
                }
                RadioButton {
                    checked: rai_wallet.renderingRatio === RenderingRatio.MicroXRB
                    text: "uXRB = 10^24 raw"
                    onClicked: rai_wallet.renderingRatio = RenderingRatio.MicroXRB
                }
            }
        }

        GroupBox {
            Layout.alignment: Qt.AlignHCenter
            ColumnLayout {
                Label {
                    text: qsTr("Account representative:")
//                    ToolTip {
//                        text: qsTr("In the infrequent case where the network needs to make a global decision,\nyour wallet software performs a balance-weighted vote to determine\nthe outcome. Since not everyone can remain online and perform this duty,\nyour wallet names a representative that can vote with, but cannot spend,\nyour balance.")
//                    }
                }
                Label {
                    Layout.maximumWidth: parent.width
                    text: rai_settings.representative
                    wrapMode: Text.WrapAnywhere
                }
                TextField {
                    id: tfRepresentative
                    placeholderText: qsTr("New address")
                    validator: RegExpValidator {
                        regExp: /^xrb_[a-z0-9]{60}$/
                    }
                }
                Button {
                    id: btnRepresentative
                    text: qsTr("Change representative")
                    onClicked: {
                        btnRepresentative.enabled = false
                        rai_settings.changeRepresentative(tfRepresentative)
                    }
                    Connections {
                        target: rai_settings
                        onChangeRepresentativeSuccess: {
                            tfRepresentative.clear()
                            popupChangeRepresentative.state = "success"
                            btnRepresentative.enabled = true
                        }
                        onChangeRepresentativeFailure: {
                            tfRepresentative.clear()
                            popupChangeRepresentative.errorMsg = errorMsg
                            popupChangeRepresentative.state = "failure"
                            btnRepresentative.enabled = true
                        }
                    }
                    Common.PopupMessage {
                        id: popupChangeRepresentative
                        property string errorMsg: "unknown error"
                        state: "hidden"
                        states: [
                            State {
                                name: "hidden"
                                PropertyChanges {
                                    target: popupChangeRepresentative
                                    visible: false
                                }
                            },
                            State {
                                name: "success"
                                PropertyChanges {
                                    target: popupChangeRepresentative
                                    text: qsTr("Representative was changed")
                                    color: "green"
                                    interval: 2000
                                    visible: true
                                    onTriggered: popupChangeRepresentative.state = "hidden"
                                }
                            },
                            State {
                                name: "failure"
                                PropertyChanges {
                                    target: popupChangeRepresentative
                                    text: errorMsg
                                    color: "red"
                                    interval: 2000
                                    visible: true
                                    onTriggered: popupChangeRepresentative.state = "hidden"
                                }
                            }
                        ]
                    }
                }
            }
        }

        GroupBox {
            Layout.alignment: Qt.AlignHCenter
            Button {
// FIXME: Qt 5.9 doesn't provide palette to customize color, find replacement
//                palette.buttonText: "red"
                text: qsTr("Advanced tools")
                onClicked: rai_advanced.show()
            }
        }
    }
  }

  Button {
      id: btnGoBack
      anchors {
          left: parent.left
          right: parent.right
          bottom: parent.bottom
      }
      text: qsTr("Back")
      onClicked: goBack()
  }
}
